#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline tests for dp-lib.sh's CSRF 403 retry (curl_dp_send).

Sources the production dp-lib.sh (symlinked, not copied) with a stubbed curl on
PATH that simulates the Device Portal's CSRF behaviour. The stub is driven by
FAKE_SCENARIO:

  ok      every request returns 200
  stale   GET returns 200; POST with the seeded (stale) jar returns 403;
          a cookie fetch (-c) writes a FRESH token into the jar, after which
          POST returns 200 — the recoverable staleness case
  stuck   POST returns 403 no matter what — auth is failing for real

No network, no console, no secrets (DP_PASS is a dummy).
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
DP = TOOLS.parent / 'dp'

STUB_CURL = r'''#!/bin/bash
# Simulated Device Portal. Scenario from $FAKE_SCENARIO:
#   ok    every request 200
#   stale GET 200; POST with a non-FRESH jar token 403 (recoverable staleness)
#   stuck POST always 403 (auth failing for real)
# Like real curl, the -w format output is printed after the body.
mutating=no; jar=""; prev=""
for a in "$@"; do
  case "$prev" in
    -c) jar="$a" ;;
    -b) jar="$a" ;;
    -w) w="$a" ;;
  esac
  case "$a" in
    -X|-F) mutating=yes ;;
    -c|-b|-w) prev="$a" ;;
    *) prev="" ;;
  esac
done
sc="${FAKE_SCENARIO:-ok}"
if [ -n "$jar" ] && [ "$mutating" = no ] && [[ "$*" == *"/api/os/info"* ]]; then
  printf '.xboxlive.com\tTRUE\t/\tTRUE\t0\tCSRF-Token\tFRESH\n' > "$jar"
  exit 0
fi
fresh=$(awk '$6=="CSRF-Token"{print $7}' "$jar" 2>/dev/null | tail -1)
if [ "$sc" = stuck ]; then
  printf 'HTTP/1.1 403 Forbidden\n'; [ -n "$w" ] && printf '403\n'; exit 0
fi
if [ "$sc" = ok ] || [ "$fresh" = FRESH ]; then
  printf 'HTTP/1.1 200 OK\n'; [ -n "$w" ] && printf '200\n'; exit 0
fi
if [ "$mutating" = yes ]; then
  printf 'HTTP/1.1 403 Forbidden\n'; [ -n "$w" ] && printf '403\n'; exit 0
fi
printf 'HTTP/1.1 200 OK\n'; [ -n "$w" ] && printf '200\n'; exit 0
'''

STALE_JAR = '.xboxlive.com\tTRUE\t/\tTRUE\t0\tCSRF-Token\tSTALE\n'


class DpLibCsrfRetryTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='dp-lib-csrf-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        (self.root / 'bin').mkdir()
        (self.root / 'bin' / 'curl').write_text(STUB_CURL)
        (self.root / 'bin' / 'curl').chmod(0o755)
        (self.root / 'dp-lib.sh').symlink_to(DP / 'dp-lib.sh')
        (self.root / 'env.sh').write_text(
            'DP_HOST=invalid.invalid\nDP_PASS=dummy-not-a-secret\n'
            'DP_LOCK=%s/lock\nDP_JAR=%s/jar\n' % (self.root, self.root))
        self.jar = self.root / 'jar'

    def run_lib(self, script, scenario='stale', seed_jar=True):
        """Seed a stale jar (the post-rotation state), then source dp-lib.sh and run `script`."""
        if seed_jar:
            self.jar.write_text(STALE_JAR)
        env = {k: v for k, v in os.environ.items() if not k.startswith('DP_')}
        env['FAKE_ROOT'] = str(self.root)
        env['FAKE_SCENARIO'] = scenario
        p = subprocess.run(
            ['bash', '-c',
             'set -a; . "%s/env.sh"; set +a; PATH="%s/bin:$PATH"; '
             '. "%s/dp-lib.sh"; %s; echo "rc=$?"' % (self.root, self.root, self.root, script)],
            env=env, capture_output=True, text=True, timeout=60)
        return p.returncode, p.stdout + p.stderr

    def test_get_with_stale_jar_succeeds_without_retry(self):
        rc, out = self.run_lib('curl_dp /api/resourcemanager/processes >/dev/null')[0:2]
        self.assertEqual(rc, 0, out)
        self.assertNotIn('403', out)
        self.assertNotIn('refetching', out)

    def test_stale_csrf_post_recovers_after_one_retry(self):
        rc, out = self.run_lib('curl_dp_send /api/taskmanager/app -X POST -H "Content-Length: 0" >/dev/null')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('refetching cookie and retrying once', out)
        self.assertNotIn('still 403', out)
        # after the refetch the jar holds a FRESH token and the retry succeeded
        self.assertEqual(awk_fresh(self.jar), 'FRESH')

    def test_persistent_403_reports_clearly_and_retries_only_once(self):
        # stuck: POST 403s even with a FRESH token — the wrapper must say so and
        # stop at exactly one retry (two requests total, no loop).
        rc, out = self.run_lib('curl_dp_send /api/taskmanager/app -X POST >/dev/null', scenario='stuck')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('still 403', out)
        self.assertIn('CSRF auth is failing for real', out)
        self.assertEqual(out.count('refetching cookie and retrying once'), 1)

    def test_ok_scenario_never_retries(self):
        rc, out = self.run_lib('curl_dp_send /api/taskmanager/app -X POST >/dev/null', scenario='ok')[:2]
        self.assertEqual(rc, 0, out)
        self.assertNotIn('403', out)
        self.assertNotIn('refetching', out)

    def test_body_and_status_survive_the_wrapper(self):
        rc, out = self.run_lib('curl_dp_send /api/app/packagemanager/state -X PUT', scenario='ok')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('HTTP/1.1 200 OK', out)

    def test_dp_retry_uses_the_send_path(self):
        rc, out = self.run_lib('dp_retry POST /api/taskmanager/app >/dev/null')[:2]
        self.assertEqual(rc, 0, out)
        # first attempt 403s, curl_dp_send refetches and succeeds -> dp_retry's
        # first iteration returns 0, so only one dp_retry iteration ran.
        self.assertEqual(awk_fresh(self.jar), 'FRESH')


def awk_fresh(jar):
    return subprocess.run(['awk', '$6=="CSRF-Token"{print $7}', str(jar)],
                          capture_output=True, text=True).stdout.strip()


if __name__ == '__main__':
    unittest.main()
