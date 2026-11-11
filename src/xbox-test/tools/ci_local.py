#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the `run:` steps of .github/workflows/ci.yml locally, so `xn check` cannot drift from CI.

ci_local.py [--workflow FILE] [--job NAME ...|--all] [--allow-skip] [--list] [--log-dir DIR]

Only the subset of YAML that ci.yml uses is understood (jobs -> steps -> name/run, inline or
`|` block scalars). Anything else in the steps section is a parse error, never silently skipped.
Setup steps (`uses:`, `sudo apt-get install`) are not run; the apt package list of a job is
turned into the commands the job needs, and a job whose commands are missing is reported SKIP
(never PASS). Exit: 0 all passed; 1 a step failed; 3 nothing failed but something was skipped.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE_NEEDS = ['bash', 'git', 'python3']
JOB_EXTRA_NEEDS = {'offline-checks': ['node', 'c++']}
APT_COMMANDS = {
    'gcc-mingw-w64-x86-64': 'x86_64-w64-mingw32-gcc',
    'g++-mingw-w64-x86-64': 'x86_64-w64-mingw32-g++',
    'binutils-mingw-w64-x86-64': 'x86_64-w64-mingw32-objdump',
    'gcc-mingw-w64-i686': 'i686-w64-mingw32-gcc',
    'wine64': 'wine64',
    'wine': 'wine',
}


class ParseError(Exception):
    pass


def _unquote(value):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
        return value[1:-1]
    return value


def parse_workflow(text):
    """-> [{'name': job, 'steps': [{'name': str, 'run': str|None}]}] in file order."""
    lines = text.splitlines()
    jobs, i, n = [], 0, len(lines)
    in_jobs = False
    job = step = None
    while i < n:
        raw = lines[i]
        i += 1
        stripped = raw.strip()
        if not stripped or stripped.startswith('#'):
            continue
        indent = len(raw) - len(raw.lstrip(' '))
        if indent == 0:
            in_jobs = stripped == 'jobs:'
            job = step = None
            continue
        if not in_jobs:
            continue
        if indent == 2:
            m = re.fullmatch(r'([A-Za-z0-9_-]+):', stripped)
            if not m:
                raise ParseError('line %d: expected a job name, got %r' % (i, stripped))
            job = {'name': m.group(1), 'steps': []}
            jobs.append(job)
            step = None
            continue
        if job is None:
            raise ParseError('line %d: content outside a job' % i)
        if indent == 4:
            if stripped != 'steps:' and not re.match(r'(runs-on|needs|if|timeout-minutes|env):', stripped):
                raise ParseError('line %d: unsupported job key %r' % (i, stripped))
            continue
        item = re.match(r'^      - (\S.*)$', raw)
        if indent == 6 and item:
            step = {'name': None, 'run': None, 'uses': None}
            job['steps'].append(step)
            key_line = item.group(1)
        elif indent == 8 and step is not None:
            key_line = stripped
        else:
            raise ParseError('line %d: unsupported structure %r' % (i, stripped))
        m = re.match(r'([A-Za-z_-]+):\s*(.*)$', key_line)
        if not m:
            raise ParseError('line %d: expected key: value, got %r' % (i, key_line))
        key, value = m.group(1), m.group(2)
        if key == 'run' and value in ('|', '|-', '>', '>-'):
            if value[0] == '>':
                raise ParseError('line %d: folded scalars are not supported' % i)
            block = []
            while i < n:
                nxt = lines[i]
                if nxt.strip() == '':
                    block.append('')
                    i += 1
                    continue
                if len(nxt) - len(nxt.lstrip(' ')) < 10:
                    break
                block.append(nxt[10:])
                i += 1
            while block and block[-1] == '':
                block.pop()
            step['run'] = '\n'.join(block)
        elif key in ('name', 'run', 'uses'):
            step[key] = _unquote(value)
        elif key in ('with', 'env', 'if', 'shell', 'working-directory'):
            if key in ('shell', 'working-directory', 'env', 'if'):
                raise ParseError('line %d: step key %r changes how the step runs; not supported' % (i, key))
            # `with:` carries only inputs for `uses:` steps; skip its nested lines.
            while i < n and (lines[i].strip() == '' or len(lines[i]) - len(lines[i].lstrip(' ')) > 8):
                i += 1
        else:
            raise ParseError('line %d: unsupported step key %r' % (i, key))
    for j in jobs:
        for k, s in enumerate(j['steps']):
            if not s['name']:
                s['name'] = (s['uses'] or ('step %d' % (k + 1)))
    return jobs


def job_needs(job):
    needs = list(BASE_NEEDS) + JOB_EXTRA_NEEDS.get(job['name'], [])
    for s in job['steps']:
        run = s['run'] or ''
        if 'apt-get install' in run:
            for pkg in re.findall(r'[A-Za-z0-9+.-]+', run.split('install', 1)[1]):
                if pkg in APT_COMMANDS:
                    needs.append(APT_COMMANDS[pkg])
    return list(dict.fromkeys(needs))


def runnable(step):
    run = step['run']
    return bool(run) and 'apt-get install' not in run


def run_step(step, root, env, log_path):
    with open(log_path, 'w') as log:
        start = time.time()
        rc = subprocess.run(['bash', '-e', '-c', step['run']], cwd=root, env=env,
                            stdout=log, stderr=subprocess.STDOUT).returncode
    return rc, time.time() - start


def tail(path, count=30):
    try:
        return Path(path).read_text(errors='replace').splitlines()[-count:]
    except OSError:
        return []


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--workflow', default=str(ROOT / '.github/workflows/ci.yml'))
    ap.add_argument('--root', default=str(ROOT), help='directory the steps run in (default: the repository)')
    ap.add_argument('--job', action='append', help='job to run (repeatable; default offline-checks)')
    ap.add_argument('--all', action='store_true', help='every job in the workflow')
    ap.add_argument('--allow-skip', action='store_true', help='exit 0 when steps were skipped for missing tools')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--log-dir')
    args = ap.parse_args(argv)
    try:
        jobs = parse_workflow(Path(args.workflow).read_text())
    except (ParseError, OSError) as e:
        print('ci_local: cannot read %s: %s' % (args.workflow, e), file=sys.stderr)
        return 2
    wanted = [j['name'] for j in jobs] if args.all else (args.job or ['offline-checks'])
    by_name = {j['name']: j for j in jobs}
    unknown = [w for w in wanted if w not in by_name]
    if unknown:
        print('ci_local: unknown job %s (have: %s)' % (', '.join(unknown), ', '.join(by_name)), file=sys.stderr)
        return 2
    if args.list:
        for name in wanted:
            print('%s (needs: %s)' % (name, ' '.join(job_needs(by_name[name]))))
            for s in by_name[name]['steps']:
                if runnable(s):
                    print('  - %s' % s['name'])
        return 0
    log_dir = Path(args.log_dir) if args.log_dir else Path(tempfile.mkdtemp(prefix='xn-check-'))
    log_dir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env['CI'] = 'true'
    env['RUNNER_TEMP'] = str(log_dir / 'runner-temp')
    Path(env['RUNNER_TEMP']).mkdir(exist_ok=True)
    passed = failed = skipped = 0
    for name in wanted:
        job = by_name[name]
        missing = [c for c in job_needs(job) if shutil.which(c) is None]
        print('== job %s ==' % name, flush=True)
        for idx, step in enumerate(job['steps']):
            if not runnable(step):
                continue
            if missing:
                print('SKIP: %s (missing: %s)' % (step['name'], ', '.join(missing)), flush=True)
                skipped += 1
                continue
            log = log_dir / ('%s-%02d.log' % (name, idx))
            rc, secs = run_step(step, args.root, env, log)
            if rc == 0:
                print('PASS: %s (%.0fs)' % (step['name'], secs), flush=True)
                passed += 1
            else:
                print('FAIL: %s (exit %d, %.0fs) log=%s' % (step['name'], rc, secs, log), flush=True)
                for line in tail(log):
                    print('    ' + line)
                failed += 1
    print('== SUMMARY: %d passed, %d failed, %d skipped (logs: %s) ==' % (passed, failed, skipped, log_dir))
    if failed:
        return 1
    if skipped and not args.allow_skip:
        print('NOT PASSING: %d step(s) skipped for missing tools; a skip is not a pass (--allow-skip to accept)' % skipped)
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
