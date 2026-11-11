#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline tests for the `xn` entry point: CI-step runner, console helpers, and the
`xn console` flow against a fake Device Portal (no network, no console, no secrets)."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
XBOX_TEST = TOOLS.parent
ROOT = XBOX_TEST.parents[1]
sys.path.insert(0, str(TOOLS))
import ci_local  # noqa: E402
import xn_console  # noqa: E402

WORKFLOW = '''name: t
on:
  pull_request:
jobs:
  good:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      # a comment between steps
      - name: inline
        run: echo one
      - name: "quoted: name"
        run: |
          set -e
          true
          echo two
      - uses: actions/setup-node@v4
        with:
          node-version: '24'
  needs-wine:
    runs-on: ubuntu-latest
    steps:
      - name: install
        run: sudo apt-get update && sudo apt-get install -y wine64 gcc-mingw-w64-x86-64
      - name: uses wine
        run: echo should-not-run
  bad:
    runs-on: ubuntu-latest
    steps:
      - name: fails
        run: |
          echo about to fail
          false
          echo not reached
'''


class CiLocalTest(unittest.TestCase):
    def test_parses_the_real_workflow_completely(self):
        text = (ROOT / '.github/workflows/ci.yml').read_text()
        jobs = ci_local.parse_workflow(text)
        runs = sum(1 for j in jobs for s in j['steps'] if s['run'])
        self.assertEqual(runs, len([l for l in text.splitlines() if l.strip().startswith(('run:', '- run:'))]))
        self.assertIn('offline-checks', [j['name'] for j in jobs])
        offline = [j for j in jobs if j['name'] == 'offline-checks'][0]
        self.assertTrue(any('session-restore.sh' in (s['run'] or '') for s in offline['steps']))
        self.assertTrue(all(s['name'] for j in jobs for s in j['steps']))

    def test_parse_shapes(self):
        jobs = {j['name']: j for j in ci_local.parse_workflow(WORKFLOW)}
        steps = jobs['good']['steps']
        self.assertEqual([s['name'] for s in steps], ['actions/checkout@v4', 'inline', 'quoted: name', 'actions/setup-node@v4'])
        self.assertEqual(steps[1]['run'], 'echo one')
        self.assertEqual(steps[2]['run'], 'set -e\ntrue\necho two')
        self.assertIn('wine64', ci_local.job_needs(jobs['needs-wine']))
        self.assertFalse(ci_local.runnable(jobs['needs-wine']['steps'][0]))

    def test_unsupported_yaml_is_an_error_not_a_skip(self):
        with self.assertRaises(ci_local.ParseError):
            ci_local.parse_workflow('jobs:\n  a:\n    steps:\n      - name: x\n        shell: pwsh\n        run: y\n')
        with self.assertRaises(ci_local.ParseError):
            ci_local.parse_workflow('jobs:\n  a:\n    strategy:\n      matrix: {}\n')

    def run_runner(self, *argv, path=None):
        tmp = Path(tempfile.mkdtemp(prefix='xn-ci-'))
        self.addCleanup(shutil.rmtree, tmp, True)
        wf = tmp / 'ci.yml'
        wf.write_text(WORKFLOW)
        env = dict(os.environ)
        if path is not None:
            env['PATH'] = path
        proc = subprocess.run([sys.executable, str(TOOLS / 'ci_local.py'), '--workflow', str(wf), '--root', str(tmp),
                               '--log-dir', str(tmp / 'logs'), *argv], env=env, text=True, capture_output=True)
        return proc.returncode, proc.stdout

    def test_pass_fail_and_exit_codes(self):
        rc, out = self.run_runner('--job', 'good')
        self.assertEqual(rc, 0, out)
        self.assertIn('PASS: inline', out)
        rc, out = self.run_runner('--job', 'bad')
        self.assertEqual(rc, 1, out)
        self.assertIn('FAIL: fails', out)
        self.assertIn('about to fail', out)
        self.assertNotIn('not reached', out)

    def test_skip_is_not_a_pass(self):
        # A PATH with only the base tools: the wine job must be skipped, and a skip must not exit 0.
        bindir = Path(tempfile.mkdtemp(prefix='xn-bin-'))
        self.addCleanup(shutil.rmtree, bindir, True)
        for tool in ('bash', 'git', 'python3', 'node', 'c++'):
            found = shutil.which(tool)
            if found:
                (bindir / tool).symlink_to(found)
        rc, out = self.run_runner('--job', 'needs-wine', path=str(bindir))
        self.assertEqual(rc, 3, out)
        self.assertIn('SKIP: uses wine (missing:', out)
        self.assertNotIn('PASS', out.replace('NOT PASSING', ''))
        self.assertNotIn('should-not-run', out)
        rc, out = self.run_runner('--job', 'needs-wine', '--allow-skip', path=str(bindir))
        self.assertEqual(rc, 0, out)
        self.assertIn('1 skipped', out)


def packages(*versions):
    return json.dumps({'InstalledPackages': [
        {'PackageFullName': 'Xboxnative.Launcher_%s_x64__0bdeywx3x8szp' % v} for v in versions] + [
        {'PackageFullName': 'Other.App_9.9.9.9_x64__abc'}]})


class ConsoleHelperTest(unittest.TestCase):
    def test_latest_package_and_next_version(self):
        self.assertEqual(xn_console.latest_package(packages('0.5.9.0', '0.5.10.0', '0.5.2.0')),
                         'Xboxnative.Launcher_0.5.10.0_x64__0bdeywx3x8szp')
        self.assertEqual(xn_console.latest_package(packages()), '')
        self.assertEqual(xn_console.next_version('0.5.225.0'), '0.5.226.0')
        self.assertEqual(xn_console.next_version('0.5.225.0', '0.5.300.0'), '0.5.301.0')
        self.assertEqual(xn_console.next_version('0.5.400.0', '0.5.300.0'), '0.5.401.0')

    def test_manifest_version_from_msix_and_xml(self):
        tmp = Path(tempfile.mkdtemp(prefix='xn-msix-'))
        self.addCleanup(shutil.rmtree, tmp, True)
        manifest = '<Package><Identity Name="Xboxnative.Launcher" Publisher="CN=x" Version="1.2.3.4" /></Package>'
        (tmp / 'AppxManifest.xml').write_text(manifest)
        with zipfile.ZipFile(tmp / 'p.msix', 'w') as z:
            z.writestr('AppxManifest.xml', manifest)
        self.assertEqual(xn_console.manifest_version(tmp / 'p.msix'), '1.2.3.4')
        self.assertEqual(xn_console.manifest_version(tmp / 'AppxManifest.xml'), '1.2.3.4')
        self.assertEqual(xn_console.manifest_version(XBOX_TEST / 'app/AppxManifest.xml').count('.'), 3)

    LOG_OK = ('SELFTEST BEGIN request=2 modes\nSELFTEST RESULT x86 PASS\n'
              'SELFTEST RESULT d3d9 PASS frame-presented\nSELFTEST SUMMARY pass=2 fail=0 blocked=0 skip=0\n')

    def test_verdict_pass(self):
        code, report = xn_console.selftest_verdict(self.LOG_OK)
        self.assertEqual(code, 0, report)
        self.assertIn('SELFTEST VERDICT: PASS', report)

    def test_verdict_not_pass_for_fail_blocked_skip(self):
        for line, tally in (('SELFTEST RESULT d3d10 FAIL exit-17', 'pass=1 fail=1 blocked=0 skip=0'),
                            ('SELFTEST RESULT opengl BLOCKED pixel-readback', 'pass=1 fail=0 blocked=1 skip=0'),
                            ('SELFTEST RESULT vulkan SKIP not-packaged', 'pass=1 fail=0 blocked=0 skip=1')):
            text = 'SELFTEST BEGIN request=2 modes\nSELFTEST RESULT x86 PASS\n%s\nSELFTEST SUMMARY %s\n' % (line, tally)
            code, report = xn_console.selftest_verdict(text)
            self.assertEqual(code, 1, report)
            self.assertIn('SELFTEST VERDICT: FAIL', report)

    def test_verdict_needs_a_fresh_summary(self):
        # An earlier run's summary must not satisfy this run.
        self.assertEqual(xn_console.selftest_verdict(self.LOG_OK, skip_lines=4)[0], 4)
        self.assertEqual(xn_console.selftest_verdict('')[0], 4)
        newer = self.LOG_OK + 'SELFTEST BEGIN request=1 modes\nSELFTEST RESULT x86 FAIL exit-1\nSELFTEST SUMMARY pass=0 fail=1 blocked=0 skip=0\n'
        self.assertEqual(xn_console.selftest_verdict(newer, skip_lines=4)[0], 1)

    def test_verdict_rejects_inconsistent_or_empty(self):
        bad = 'SELFTEST BEGIN request=1 modes\nSELFTEST RESULT x86 PASS\nSELFTEST SUMMARY pass=3 fail=0 blocked=0 skip=0\n'
        code, report = xn_console.selftest_verdict(bad)
        self.assertEqual(code, 1, report)
        self.assertIn('disagrees', report)
        self.assertEqual(xn_console.selftest_verdict('SELFTEST SUMMARY pass=0 fail=0 blocked=0 skip=0\n')[0], 1)


FAKE_DP = r'''
import json, os, sys, zipfile
from pathlib import Path
root = Path(os.environ['FAKE_ROOT']); sp = root / 'state.json'
st = json.loads(sp.read_text())
def save(): sp.write_text(json.dumps(st))
op, a = sys.argv[1], sys.argv[2:]
st.setdefault('calls', []).append(op)
def version_of(msix):
    import re
    return re.search(r'Version="([0-9.]+)"', zipfile.ZipFile(msix).read('AppxManifest.xml').decode()).group(1)
if op == 'curl':          # curl_dp PATH -o FILE -w FMT
    path = a[0]; rest = a[1:]; out = rest[rest.index('-o') + 1]
    if path == '/api/os/info':
        Path(out).write_text('{}'); print(st.get('os_code', '200'), end=''); save(); sys.exit(0)
    if path == '/api/app/packagemanager/packages':
        Path(out).write_text(json.dumps({'InstalledPackages': [{'PackageFullName': p} for p in st['packages']]}))
        print('200', end=''); save(); sys.exit(0)
    print('404', end=''); save(); sys.exit(0)
if op == 'kill':
    st['killed'] = a[0]; save(); print('kill HTTP 200'); sys.exit(0)
if op == 'install':
    ver = version_of(a[0]); st['installed_version'] = ver
    if st.get('install') == 'ok':
        st['packages'].append('Xboxnative.Launcher_%s_x64__0bdeywx3x8szp' % ver)
        save(); print('INSTALL OK'); sys.exit(0)
    if st.get('install') == 'silent':
        save(); print('INSTALL OK'); sys.exit(0)
    save(); print('INSTALL FAILED'); sys.exit(1)
if op == 'launch':
    st['launched'] = a[1]; save(); print('launch HTTP 200'); sys.exit(0)
if op == 'waitlog':
    Path(a[4]).write_text('boot: Initialize\nRun: entering\n'); print('MATCH in ' + a[4]); sys.exit(0)
if op == 'put':
    f = Path(a[2])
    if f.name == 'selftest.txt':
        modes = [m for m in f.read_text().split() if m]
        st['requested'] = modes
        scenario = st['selftest']
        lines = ['SELFTEST BEGIN request=%d modes' % len(modes)]
        verdicts = {'pass': 'PASS', 'blocked': 'BLOCKED pixel-readback', 'skip': 'SKIP not-packaged', 'fail': 'FAIL exit-17'}
        if scenario != 'nosummary':
            first = verdicts[scenario] if scenario != 'pass' else 'PASS'
            lines += ['SELFTEST RESULT x86 PASS', 'SELFTEST RESULT d3d9 %s' % first]
            p = 1 + (scenario == 'pass')
            lines.append('SELFTEST SUMMARY pass=%d fail=%d blocked=%d skip=%d' % (
                p, scenario == 'fail', scenario == 'blocked', scenario == 'skip'))
        st['log'] += '\n'.join(lines) + '\n'
    save(); print('put HTTP 200'); sys.exit(0)
if op == 'get':
    fname, out = a[2], a[3]
    if fname == 'selftest.log':
        if st['log'] == '' and not st.get('log_exists', True):
            Path(out).write_text('{"ErrorCode":404}'); print('get HTTP 404'); sys.exit(1)
        Path(out).write_text(st['log']); print('get HTTP 200'); save(); sys.exit(0)
    Path(out).write_text('some log for %s\n' % fname); print('get HTTP 200'); save(); sys.exit(0)
if op == 'ls':
    names = ['boot.log'] + (['steam_session.bin'] if (st['wipe'] is False or a[0] == st['packages'][0]) else [])
    for n in names: print('- 10 ' + n)
    save(); sys.exit(0)
if op == 'shot':
    Path(a[0]).write_bytes(b'png'); print('shot HTTP 200'); sys.exit(0)
sys.exit(98)
'''

WRAPPERS = {'kill.sh': 'kill', 'install.sh': 'install', 'launch.sh': 'launch', 'wait-log.sh': 'waitlog',
            'put-file.sh': 'put', 'get-file.sh': 'get', 'ls-dir.sh': 'ls', 'screenshot.sh': 'shot'}


class ConsoleFlowTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='xn-console-'))
        self.addCleanup(shutil.rmtree, self.root, True)
        (self.root / 'fake_dp.py').write_text(FAKE_DP)
        dp = self.root / 'dp'
        dp.mkdir()
        (dp / 'dp-lib.sh').write_text('dp_lock() { "$@"; }\ncurl_dp() { python3 -I "$FAKE_ROOT/fake_dp.py" curl "$@"; }\n')
        for name, op in WRAPPERS.items():
            (dp / name).write_text('#!/bin/bash\nexec python3 -I "$FAKE_ROOT/fake_dp.py" %s "$@"\n' % op)
        bindir = self.root / 'bin'
        bindir.mkdir()
        (bindir / 'sleep').write_text('#!/bin/sh\nexit 0\n')
        (bindir / 'sleep').chmod(0o755)
        (self.root / '.env').write_text('# fake, no real secrets\nDP_HOST=https://console.invalid:11443\nDP_USER=tester\nDP_PASS=not-a-secret\n'
                                        'IGNORED_VAR=1\nbad line\n')
        self.msix = self.root / 'new.msix'
        self.set_msix('0.5.300.0')
        self.state = dict(packages=['Xboxnative.Launcher_0.5.225.0_x64__0bdeywx3x8szp'], install='ok', selftest='pass',
                          log='SELFTEST BEGIN request=1 modes\nSELFTEST RESULT x86 PASS\nSELFTEST SUMMARY pass=1 fail=0 blocked=0 skip=0\n',
                          wipe=False)

    def set_msix(self, version):
        with zipfile.ZipFile(self.msix, 'w') as z:
            z.writestr('AppxManifest.xml', '<Package><Identity Name="Xboxnative.Launcher" Version="%s" /></Package>' % version)

    def run_xn(self, *argv, env_extra=None, stdin=subprocess.DEVNULL):
        (self.root / 'state.json').write_text(json.dumps(self.state))
        env = {k: v for k, v in os.environ.items() if not k.startswith(('DP_', 'XN_'))}
        env.update(PATH='%s:%s' % (self.root / 'bin', env['PATH']), FAKE_ROOT=str(self.root), XN_DP_DIR=str(self.root / 'dp'),
                   XN_ENV=str(self.root / '.env'), DP_LOCK=str(self.root / 'console.lock'), HOME=str(self.root))
        env.update(env_extra or {})
        proc = subprocess.run(['bash', str(XBOX_TEST / 'xn.sh'), 'console', '--out', str(self.root / 'out'), *argv],
                              env=env, text=True, capture_output=True, stdin=stdin, timeout=120)
        return proc.returncode, proc.stdout + proc.stderr, json.loads((self.root / 'state.json').read_text())

    def test_pass_path_installs_launches_and_judges_selftest(self):
        rc, out, st = self.run_xn('--yes', '--msix', str(self.msix), '--modes', 'x86,d3d9')
        self.assertEqual(rc, 0, out)
        self.assertIn('xn console: PASS', out)
        self.assertEqual(st['requested'], ['x86', 'd3d9'])
        self.assertEqual(st['installed_version'], '0.5.300.0')
        self.assertEqual(st['killed'], 'Xboxnative.Launcher_0.5.225.0_x64__0bdeywx3x8szp')
        self.assertEqual(st['launched'], 'Xboxnative.Launcher_0.5.300.0_x64__0bdeywx3x8szp')
        self.assertTrue((self.root / 'out/selftest.log').is_file())
        self.assertTrue((self.root / 'out/selftest-report.txt').is_file())
        self.assertNotIn('not-a-secret', out)
        self.assertIn('IGNORED_VAR is not a variable xn uses', out)

    def test_blocked_and_skip_are_not_a_pass(self):
        for scenario in ('blocked', 'skip', 'fail'):
            self.state['selftest'] = scenario
            rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix))
            self.assertEqual(rc, 1, (scenario, out))
            self.assertIn('FAIL: console self-test', out)
            self.assertNotIn('xn console: PASS', out)

    def test_stale_summary_from_an_earlier_run_is_not_accepted(self):
        self.state['selftest'] = 'nosummary'   # the launcher never writes a new summary
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix), '--timeout', '1')
        self.assertEqual(rc, 1, out)
        self.assertIn('did not finish', out)

    def test_install_failures_are_failures(self):
        self.state['install'] = 'fail'
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix))
        self.assertEqual((rc, 'FAIL: install' in out), (1, True), out)
        self.state['install'] = 'silent'      # install.sh says OK but the version never shows up
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix))
        self.assertEqual(rc, 1, out)

    def test_refuses_an_old_or_equal_version(self):
        for version in ('0.5.225.0', '0.5.100.0'):
            self.set_msix(version)
            rc, out, st = self.run_xn('--yes', '--msix', str(self.msix))
            self.assertEqual(rc, 2, out)
            self.assertIn('silently ignored', out)
            self.assertNotIn('install', st['calls'])

    def test_env_and_confirmation_guards(self):
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix), env_extra={'XN_ENV': str(self.root / 'nope.env')})
        self.assertEqual(rc, 2, out)
        self.assertIn('DP_HOST DP_PASS', out)
        rc, out, st = self.run_xn('--msix', str(self.msix))          # no --yes and no terminal
        self.assertEqual(rc, 2, out)
        self.assertIn('--yes', out)
        self.assertNotIn('install', st.get('calls', []))

    def test_bad_credentials_and_unreachable_are_told_apart(self):
        self.state['os_code'] = '401'
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix))
        self.assertEqual(rc, 2)
        self.assertIn('refused the credentials', out)
        self.state['os_code'] = '000'
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix))
        self.assertEqual(rc, 2)
        self.assertIn('unreachable', out)

    def test_dry_run_touches_nothing(self):
        rc, out, st = self.run_xn('--dry-run', '--msix', str(self.msix))
        self.assertEqual(rc, 0, out)
        self.assertIn('dry run', out)
        self.assertNotIn('calls', st)
        self.assertNotIn('not-a-secret', out)

    def test_wiped_login_is_reported(self):
        self.state['wipe'] = True
        rc, out, _ = self.run_xn('--yes', '--msix', str(self.msix))
        self.assertIn('WARN: the Steam login file existed before the install and is gone', out)


class BuildStampTest(unittest.TestCase):
    """app/build.sh's PACKAGE_VERSION block stamps the packaged manifest copy only."""

    def block(self):
        text = (XBOX_TEST / 'app/build.sh').read_text()
        start = text.index('# PACKAGE_VERSION=')
        end = text.index('\nfi\n', start) + 4
        return text[start:end]

    def stamp(self, version):
        tmp = Path(tempfile.mkdtemp(prefix='xn-stamp-'))
        self.addCleanup(shutil.rmtree, tmp, True)
        (tmp / 'layout').mkdir()
        src = XBOX_TEST / 'app/AppxManifest.xml'
        shutil.copy(src, tmp / 'layout/AppxManifest.xml')
        env = dict(os.environ, PACKAGE_VERSION=version) if version is not None else {k: v for k, v in os.environ.items() if k != 'PACKAGE_VERSION'}
        proc = subprocess.run(['bash', '-c', 'out=%s\n%s' % (tmp, self.block())], env=env, text=True, capture_output=True)
        return proc, xn_console.manifest_version(tmp / 'layout/AppxManifest.xml'), src.read_text(), (tmp / 'layout/AppxManifest.xml').read_text()

    def test_stamps_only_the_identity_version(self):
        proc, version, original, stamped = self.stamp('7.8.9.0')
        self.assertEqual((proc.returncode, version), (0, '7.8.9.0'), proc.stderr)
        self.assertEqual(stamped.replace('7.8.9.0', xn_console.manifest_version(XBOX_TEST / 'app/AppxManifest.xml')), original)

    def test_unset_leaves_it_and_junk_is_refused(self):
        proc, version, original, _ = self.stamp(None)
        self.assertEqual((proc.returncode, version), (0, xn_console.manifest_version(XBOX_TEST / 'app/AppxManifest.xml')))
        proc, *_ = self.stamp('1.2.3')
        self.assertEqual(proc.returncode, 2)
        proc, *_ = self.stamp('1.2.3.4/../x')
        self.assertEqual(proc.returncode, 2)


class EntryPointTest(unittest.TestCase):
    def run_xn(self, *argv):
        return subprocess.run([str(ROOT / 'xn'), *argv], text=True, capture_output=True, cwd=tempfile.gettempdir())

    def test_help_and_unknown(self):
        p = self.run_xn('--help')
        self.assertEqual(p.returncode, 0)
        for word in ('doctor', 'check', 'build', 'local', 'console', '--dry-run'):
            self.assertIn(word, p.stdout)
        self.assertEqual(self.run_xn('nope').returncode, 2)
        self.assertEqual(self.run_xn('console', '--help').returncode, 0)

    def test_env_example_is_placeholders_and_not_ignored(self):
        example = (ROOT / '.env.example').read_text()
        for name in ('DP_HOST', 'DP_USER', 'DP_PASS'):
            self.assertRegex(example, r'(?m)^%s=' % name)
        self.assertNotRegex(example, r'7656119\d{10}|tskey-')
        ignored = subprocess.run(['git', '-C', str(ROOT), 'check-ignore', '-q', '.env'])
        self.assertEqual(ignored.returncode, 0, '.env must be git-ignored')
        tracked_ok = subprocess.run(['git', '-C', str(ROOT), 'check-ignore', '-q', '.env.example'])
        self.assertEqual(tracked_ok.returncode, 1, '.env.example must not be ignored')


if __name__ == '__main__':
    unittest.main()
