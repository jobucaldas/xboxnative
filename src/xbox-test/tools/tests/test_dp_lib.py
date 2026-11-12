#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline tests for dp-lib.sh's CSRF 403 retry (curl_dp).

Sources the production dp-lib.sh (symlinked, not copied) with a stubbed curl on
PATH that simulates the Device Portal's CSRF behaviour. The stub is driven by
FAKE_SCENARIO:

  ok    every request 200
  stale GET 200; POST with a non-FRESH jar token 403 (recoverable staleness)
  stuck POST always 403 (auth failing for real)

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
#   ok      every request 200
#   stale   GET 200; POST with a non-FRESH jar token 403 (recoverable staleness)
#   stuck   POST always 403 (auth failing for real)
#   expect  like stale, but requests carrying "Expect: 100-continue" also get a
#           "HTTP/1.1 100 Continue" line prepended to the -D dump and stdout
# Honours -D, -w and -o like real curl (-w output goes after the body).
mutating=no; jar=""; hdr=""; prev=""
for a in "$@"; do
  case "$prev" in
    -c) jar="$a" ;;
    -b) jar="$a" ;;
    -D) hdr="$a" ;;
  esac
  case "$a" in
    -X|-F) mutating=yes ;;
    -c|-b|-D) prev="$a" ;;
    *) prev="" ;;
  esac
done
w=""
prev=""
for a in "$@"; do
  case "$prev" in
    -w) w="$a" ;;
  esac
  case "$a" in
    -w) prev="$a" ;;
    *) prev="" ;;
  esac
done
o=""
prev=""
for a in "$@"; do
  case "$prev" in
    -o) o="$a" ;;
  esac
  case "$a" in
    -o) prev="$a" ;;
    *) prev="" ;;
  esac
done
sc="${FAKE_SCENARIO:-ok}"
if [ -n "$jar" ] && [ "$mutating" = no ] && [[ "$*" == *"/api/os/info"* ]]; then
  printf '.xboxlive.com\tTRUE\t/\tTRUE\t0\tCSRF-Token\tFRESH\n' > "$jar"
  exit 0
fi
fresh=$(awk '$6=="CSRF-Token"{print $7}' "$jar" 2>/dev/null | tail -1)
expect100=no
[[ "$*" == *"100-continue"* ]] && expect100=yes
emit() {
  # body to -o when given (like real curl), else stdout; -D dump and caller -w
  if [ -n "$o" ]; then
    printf 'HTTP/1.1 %s OK\n' "$code" > "$o"
  else
    if [ -n "$hdr" ]; then
      [ "$expect100" = yes ] && printf 'HTTP/1.1 100 Continue\n\n' >> "$hdr"
      printf 'HTTP/1.1 %s Forbidden\n' "$code" >> "$hdr"
    fi
    [ "$expect100" = yes ] && printf 'HTTP/1.1 100 Continue\n'
    printf 'HTTP/1.1 %s\n' "$code"
  fi
  [ -n "$w" ] && printf '%s\n' "$(printf %s "$w" | sed "s/%{http_code}/$code/g")"
}
if [ "$sc" = stuck ]; then
  code=403; emit; exit 0
fi
if [ "$sc" = ok ] || [ "$fresh" = FRESH ]; then
  code=200; emit; exit 0
fi
if [ "$mutating" = yes ]; then
  code=403; emit; exit 0
fi
code=200
emit
exit 0
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

    # ---- plain curl_dp: GETs never retry, mutating calls do ----
    def test_get_with_stale_jar_succeeds_without_retry(self):
        rc, out = self.run_lib('curl_dp /api/resourcemanager/processes >/dev/null')[:2]
        self.assertEqual(rc, 0, out)
        self.assertNotIn('403', out)
        self.assertNotIn('refetching', out)

    def test_stale_csrf_post_recovers_after_one_retry(self):
        rc, out = self.run_lib('curl_dp /api/taskmanager/app -X POST -H "Content-Length: 0" >/dev/null')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('refetching cookie and retrying once', out)
        self.assertNotIn('still 403', out)
        # after the refetch the jar holds a FRESH token and the retry succeeded
        self.assertEqual(self.jar_fresh(), 'FRESH')

    def test_multipart_upload_403_recovers(self):
        # the -F upload path (put-file.sh) needs the same recovery
        rc, out = self.run_lib('curl_dp /api/filesystem/apps/file -X POST -F "f=@/etc/hostname" >/dev/null')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('refetching cookie and retrying once', out)
        self.assertNotIn('still 403', out)

    def test_expect_100_continue_does_not_hide_the_403(self):
        # Bugbot: multipart uploads send Expect: 100-continue, so the -D dump's
        # first line is "HTTP/1.1 100 Continue"; the parse must take the LAST
        # HTTP status line or the 403 recovery never fires for install/put-file.
        rc, out = self.run_lib(
            'curl_dp /api/filesystem/apps/file -X POST -H "Expect: 100-continue" -F "f=@/etc/hostname" >/dev/null',
            scenario='expect')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('refetching cookie and retrying once', out)
        self.assertNotIn('still 403', out)
        self.assertEqual(self.jar_fresh(), 'FRESH')

    def test_recovered_403_leaks_no_failed_attempt_output(self):
        # Bugbot: the caller's -w ran for the failed attempt too, so stdout had
        # "HTTP 403" then "HTTP 200" and smoke.sh's HTTP [45]xx grep saw a fail.
        # Only the final attempt's output may reach the caller.
        rc, out = self.run_lib('curl_dp /api/taskmanager/app -X POST -w "launch HTTP %{http_code}\\n"')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('launch HTTP 200', out)
        self.assertNotIn('403', out.replace(
            'dp-lib: 403 on /api/taskmanager/app (stale CSRF cookie?) — refetching cookie and retrying once', ''))
        self.assertEqual(out.count('launch HTTP'), 1)

    def test_persistent_403_reports_clearly_and_retries_only_once(self):
        # stuck: POST 403s even with a FRESH token — the wrapper must say so and
        # stop at exactly one retry (two requests total, no loop).
        rc, out = self.run_lib('curl_dp /api/taskmanager/app -X POST >/dev/null', scenario='stuck')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('still 403', out)
        self.assertIn('CSRF auth is failing for real', out)
        self.assertEqual(out.count('refetching cookie and retrying once'), 1)

    def test_ok_scenario_never_retries(self):
        rc, out = self.run_lib('curl_dp /api/taskmanager/app -X POST >/dev/null', scenario='ok')[:2]
        self.assertEqual(rc, 0, out)
        self.assertNotIn('403', out)
        self.assertNotIn('refetching', out)

    # ---- caller flags survive: -w / -o / -f must keep working ----
    def test_caller_w_format_is_not_corrupted(self):
        # a caller's own -w (launch.sh, put-file.sh) must still print its text,
        # and must not hide the 403 detection from the wrapper
        rc, out = self.run_lib('curl_dp "/api/taskmanager/app" -X POST -w "launch HTTP %{http_code}\\n"')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('launch HTTP 200', out)
        self.assertIn('refetching cookie and retrying once', out)

    def test_caller_w_with_stuck_403_still_reports(self):
        # even when the caller's -w would win, the -D-based detection still fires
        rc, out = self.run_lib('curl_dp /api/taskmanager/app -X POST -w "kill HTTP %{http_code}\\n"',
                               scenario='stuck')[:2]
        self.assertIn('still 403', out)
        self.assertIn('kill HTTP 403', out)

    def test_caller_o_file_gets_body(self):
        # install.sh writes the body to a file with -o; that must survive
        rc, out = self.run_lib('curl_dp /api/app/packagemanager/package -X POST -o "%s/body" -w "upload HTTP %%{http_code}\\n"'
                               % self.root, scenario='ok')[:2]
        self.assertEqual(rc, 0, out)
        self.assertIn('upload HTTP 200', out)
        # the stub writes the body to -o only when it parses -o; assert the
        # header dump went to the wrapper's own -D, not the caller's -o file
        body = (self.root / 'body')
        self.assertTrue(body.exists(), out)
        self.assertIn('200 OK', body.read_text())

    def test_dp_retry_uses_the_send_path(self):
        rc, out = self.run_lib('dp_retry POST /api/taskmanager/app >/dev/null')[:2]
        self.assertEqual(rc, 0, out)
        # first attempt 403s, curl_dp refetches and succeeds -> dp_retry's
        # first iteration returns 0, so only one dp_retry iteration ran.
        self.assertEqual(self.jar_fresh(), 'FRESH')

    def jar_fresh(self):
        return subprocess.run(['awk', '$6=="CSRF-Token"{print $7}', str(self.jar)],
                              capture_output=True, text=True).stdout.strip()


if __name__ == '__main__':
    unittest.main()
