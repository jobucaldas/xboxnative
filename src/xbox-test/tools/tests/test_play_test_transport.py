#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline Play-runner transport regressions.

Runs the production tools/play-test.sh (symlinked, not copied) against a fake
Device Portal transport. The fakes never reach a network: dp-lib's curl_dp and
any curl binary on PATH fail loudly, and the test asserts neither was invoked.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
APP = 3405340
STALE = '1000:1'

FAKE_DP = r'''
import json, os, shutil, sys
from pathlib import Path
root = Path(os.environ['FAKE_ROOT']); st = root / 'state'; cfg_path = root / 'fake.json'
cfg = json.loads(cfg_path.read_text())
def save(): cfg_path.write_text(json.dumps(cfg))
def calls(op): cfg.setdefault('calls', []).append(op)
def code(name): return cfg.get(name, '200')
def boot_append(line):
    with open(st / 'boot.log', 'a') as f: f.write(line + '\n')
def finish(att):
    ok = cfg['app'] == 'ok'
    boot_append('launch: RESULT attempt=%s ok=%d mapped=4/4 gameCodeExecuted=%s: %s' % (
        att, ok, 'true' if ok else 'false', 'EXE returned code=0' if ok else 'mapping failed'))
    (st / 'launch_result.json').write_text(json.dumps({
        'appid': cfg['appid'], 'attemptId': att, 'stage': 'guest-execution' if ok else 'loader',
        'state': 'completed' if ok else 'failed', 'terminal': True, 'ok': ok,
        'gameCodeExecuted': ok, 'exitCode': 0 if ok else None}))
op = sys.argv[1]; a = sys.argv[2:]; calls(op)
if op == 'kill':
    print('kill HTTP ' + code('kill'))
    if code('kill') == '200': cfg['running'] = False
    save(); sys.exit(7 if code('kill') == '000' else 0)
if op == 'launch':
    print('launch HTTP ' + code('launch'))
    if code('launch') == '200' and cfg['app'] != 'dead' and not cfg.get('running'):
        cfg.update(running=True, pid=cfg.get('pid', 1000) + 1000, gen=0)
        (st / 'boot.log').write_text('library: owned=3\n')
    save(); sys.exit(7 if code('launch') == '000' else 0)
if op == 'put':
    data = Path(a[2]).read_text()
    if code('put') != '200':
        print('put HTTP ' + code('put')); save(); sys.exit(0)
    if cfg.get('put_fail') and cfg['put_fail'] in data:
        print('put HTTP 000'); save(); sys.exit(7)
    shutil.copy(a[2], st / Path(a[2]).name); print('put HTTP 200')
    if cfg.get('running') and 'press' in data:
        cfg['gen'] += 1; att = '%d:%d' % (cfg['pid'], cfg['gen'])
        (st / 'launch_request.json').write_text(json.dumps({'appid': cfg['appid'], 'attemptId': att}))
        boot_append('launch: request written LocalState\\launch_request.json appid=%d attempt=%s dir=game via=test' % (cfg['appid'], att))
        boot_append('launch: RUNNING attempt=%s ok=0 mapped=0/4 gameCodeExecuted=false: mapping' % att)
        (st / 'launch_result.json').write_text(json.dumps({'appid': cfg['appid'], 'attemptId': att, 'state': 'running', 'terminal': False, 'ok': False}))
        (st / 'launch_probe.log').write_text('GAP: total=10 missing=0 win32u=0\n')
        cfg['pending'] = att; cfg['delay'] = cfg.get('result_delay', 2)
    save(); sys.exit(0)
if op == 'get':
    fname, out = a[2], a[3]
    if fname == cfg.get('get_fail') and cfg.get('pending'):   # link drops mid-attempt
        Path(out).write_text('{"Reason":"transport"}')   # curl without -f keeps error bodies
        print('get HTTP 000 bytes=0 file=' + out); save(); sys.exit(1)
    if fname == 'boot.log' and cfg.get('pending'):
        cfg['delay'] -= 1
        if cfg['delay'] <= 0: finish(cfg.pop('pending'))
    src = st / fname
    if not src.exists():
        Path(out).write_text('{"ErrorCode":404}'); print('get HTTP 404 bytes=0 file=' + out); save(); sys.exit(1)
    shutil.copy(src, out); print('get HTTP 200 bytes=%d file=%s' % (src.stat().st_size, out)); save(); sys.exit(0)
if op == 'shot':
    Path(a[0]).write_bytes(b''); print('shot HTTP 200 bytes=0'); save(); sys.exit(0)
save(); sys.exit(98)
'''

WRAPPERS = {'kill.sh': 'kill', 'launch.sh': 'launch', 'put-file.sh': 'put',
            'get-file.sh': 'get', 'screenshot.sh': 'shot'}


class PlayRunnerTransportTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='play-transport-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        (self.root / 'tools').mkdir()
        for name in ('play-test.sh', 'check-play-result.py'):
            (self.root / 'tools' / name).symlink_to(TOOLS / name)
        dp = self.root / 'dp'; dp.mkdir()
        (self.root / 'fake_dp.py').write_text(FAKE_DP)
        (dp / 'dp-lib.sh').write_text(
            'dp_lock() { "$@"; }\n'
            'curl_dp() { echo forbidden >> "$FAKE_ROOT/network"; return 97; }\n')
        for script, op in WRAPPERS.items():
            (dp / script).write_text('#!/bin/bash\nexec python3 -I "$FAKE_ROOT/fake_dp.py" %s "$@"\n' % op)
        bin_ = self.root / 'bin'; bin_.mkdir()
        (bin_ / 'curl').write_text('#!/bin/sh\necho curl >> "$FAKE_ROOT/network"; exit 97\n')
        (bin_ / 'sleep').write_text('#!/bin/sh\nexit 0\n')
        for f in bin_.iterdir():
            f.chmod(0o755)
        self.state = self.root / 'state'; self.state.mkdir()

    def seed_stale_success(self):
        """LocalState left by an earlier, genuinely completed attempt."""
        (self.state / 'boot.log').write_text(
            'library: owned=3\n'
            'launch: request written LocalState\\launch_request.json appid=%d attempt=%s dir=game via=test\n'
            'launch: RESULT attempt=%s ok=1 mapped=4/4 gameCodeExecuted=true: EXE returned code=0\n' % (APP, STALE, STALE))
        (self.state / 'launch_request.json').write_text(json.dumps({'appid': APP, 'attemptId': STALE}))
        (self.state / 'launch_result.json').write_text(json.dumps({
            'appid': APP, 'attemptId': STALE, 'stage': 'guest-execution', 'state': 'completed',
            'terminal': True, 'ok': True, 'gameCodeExecuted': True, 'exitCode': 0}))
        (self.state / 'launch_probe.log').write_text('GAP: total=10 missing=0 win32u=0\n')

    def run_play(self, timeout_s=3, **cfg):
        cfg.setdefault('app', 'ok'); cfg['appid'] = APP
        (self.root / 'fake.json').write_text(json.dumps(cfg))
        env = {k: v for k, v in os.environ.items() if not k.startswith(('DP_', 'PT_'))}
        env.update(FAKE_ROOT=str(self.root), PKG='Grok.XboxTest_0.0.0.0_x64__fake',
                   DP_HOST='invalid.invalid', DP_LOCK=str(self.root / 'lock'),
                   PATH=str(self.root / 'bin') + os.pathsep + env.get('PATH', ''))
        p = subprocess.run(['bash', str(self.root / 'tools/play-test.sh'), str(APP), str(timeout_s),
                            str(self.root / 'out')], env=env, capture_output=True, text=True, timeout=120)
        self.assertFalse((self.root / 'network').exists(), 'fake transport fell through to curl')
        return p.returncode, p.stdout + p.stderr

    def assert_not_accepted(self, rc, log):
        self.assertEqual(rc, 1, log)
        self.assertNotIn('GUEST COMPLETED', log)
        self.assertNotIn('LAUNCH RESULT PASS', log)

    # ---- transport errors fail honestly ----
    def test_transport_loss_does_not_accept_stale_success(self):
        self.seed_stale_success()
        rc, log = self.run_play(app='dead', kill='000', launch='000', put_fail='')
        self.assert_not_accepted(rc, log)
        self.assertIn('TRANSPORT FAIL', log)

    def test_unrelaunched_app_does_not_accept_stale_success(self):
        self.seed_stale_success()
        rc, log = self.run_play(app='dead')
        self.assert_not_accepted(rc, log)

    def test_command_upload_error_fails_fast(self):
        rc, log = self.run_play(timeout_s=600, put_fail='press')
        self.assert_not_accepted(rc, log)
        self.assertIn('TRANSPORT FAIL', log)

    def test_log_download_error_fails_fast(self):
        rc, log = self.run_play(timeout_s=600, get_fail='boot.log')
        self.assert_not_accepted(rc, log)
        self.assertIn('TRANSPORT FAIL', log)

    def test_launch_http_error_fails_even_when_helper_exits_zero(self):
        self.seed_stale_success()
        rc, log = self.run_play(launch='500')
        self.assert_not_accepted(rc, log)
        self.assertIn('TRANSPORT FAIL: launch app', log)

    def test_upload_http_error_fails_even_when_helper_exits_zero(self):
        self.seed_stale_success()
        rc, log = self.run_play(put='403')
        self.assert_not_accepted(rc, log)
        self.assertIn('TRANSPORT FAIL: clear cmd.txt', log)

    # ---- valid correlated terminal evidence keeps the existing contract ----
    def test_fresh_correlated_success_passes(self):
        self.seed_stale_success()
        rc, log = self.run_play(timeout_s=600)
        self.assertEqual(rc, 0, log)
        self.assertIn('LAUNCH RESULT PASS', log)
        self.assertIn('GUEST COMPLETED: launch: RESULT attempt=2000:1 ok=1', log)

    def test_running_session_waits_for_fresh_attempt(self):
        self.seed_stale_success()
        # The old session survives a refused stop; only the new attempt may be accepted.
        rc, log = self.run_play(timeout_s=600, kill='404', running=True, pid=1000, gen=1, result_delay=3)
        self.assertEqual(rc, 0, log)
        self.assertIn('GUEST COMPLETED: launch: RESULT attempt=1000:2 ok=1', log)

    def test_fresh_terminal_failure_is_reported(self):
        rc, log = self.run_play(timeout_s=600, app='fail')
        self.assert_not_accepted(rc, log)
        self.assertIn('LAUNCH RESULT FAIL: no successful terminal result', log)


if __name__ == '__main__':
    unittest.main()
