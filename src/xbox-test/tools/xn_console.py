#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small parsing helpers for `xn console` (src/xbox-test/xn.sh). No network, no secrets.

  xn_console.py manifest-version APPX_MANIFEST_OR_MSIX
  xn_console.py latest-package PACKAGES_JSON [NAME_PREFIX]   -> newest installed full name, or nothing
  xn_console.py next-version CURRENT [INSTALLED]            -> CURRENT/INSTALLED bumped one patch above the larger
  xn_console.py version-gt A B                              -> exit 0 when A > B
  xn_console.py selftest-verdict LOG [SKIP_LINES]           -> table + verdict; exit 0 PASS, 1 FAIL, 4 no summary yet
"""
import json
import re
import sys
import zipfile
from pathlib import Path

LAUNCHER = 'Xboxnative.Launcher'
FULL = re.compile(r'^(?P<name>[A-Za-z0-9.]+)_(?P<ver>\d+\.\d+\.\d+\.\d+)_(?P<arch>[a-z0-9]+)__(?P<hash>[a-z0-9]+)$')
IDENTITY = re.compile(r'<Identity\b[^>]*\bVersion="(\d+\.\d+\.\d+\.\d+)"')


def vtuple(version):
    parts = tuple(int(p) for p in version.split('.'))
    if len(parts) != 4:
        raise ValueError('version must be a.b.c.d: %r' % version)
    return parts


def manifest_version(path):
    path = Path(path)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            text = z.read('AppxManifest.xml').decode('utf-8', 'replace')
    else:
        text = path.read_text()
    m = IDENTITY.search(text)
    if not m:
        raise ValueError('no <Identity Version="a.b.c.d"> in %s' % path)
    return m.group(1)


def latest_package(packages_json, prefix=LAUNCHER):
    data = json.loads(packages_json)
    best = None
    for item in data.get('InstalledPackages', []):
        full = item.get('PackageFullName', '')
        m = FULL.match(full)
        if m and m.group('name') == prefix:
            key = vtuple(m.group('ver'))
            if best is None or key > best[0]:
                best = (key, full)
    return best[1] if best else ''


def package_version(full):
    m = FULL.match(full)
    if not m:
        raise ValueError('not a package full name: %r' % full)
    return m.group('ver')


def next_version(current, installed=''):
    base = max(vtuple(current), vtuple(installed)) if installed else vtuple(current)
    return '%d.%d.%d.%d' % (base[0], base[1], base[2] + 1, 0)


CLASSES = ('PASS', 'FAIL', 'BLOCKED', 'SKIP')


def selftest_verdict(text, skip_lines=0):
    """Judge the part of selftest.log written by this run (after skip_lines).

    PASS only when a SUMMARY line exists, the RESULT lines agree with it, at least one
    mode passed and nothing failed, was blocked or was skipped. SKIP is never PASS.
    Returns (code, report): 0 PASS, 1 FAIL, 4 the run has not written its SUMMARY yet.
    """
    lines = text.splitlines()[skip_lines:]
    summary_at = max((i for i, l in enumerate(lines) if l.startswith('SELFTEST SUMMARY')), default=None)
    if summary_at is None:
        return 4, 'no SELFTEST SUMMARY yet (%d new line(s))' % len(lines)
    window = lines[:summary_at + 1]
    begin = max((i for i, l in enumerate(window) if l.startswith('SELFTEST BEGIN')), default=0)
    window = window[begin:]
    results = []
    for l in window:
        m = re.match(r'SELFTEST RESULT (\S+) (\S+)(?: (.*))?$', l)
        if m:
            results.append((m.group(1), m.group(2), m.group(3) or ''))
    counts = {c: 0 for c in CLASSES}
    out = []
    for mode, verdict, detail in results:
        cls = verdict if verdict in CLASSES else 'FAIL'
        counts[cls] += 1
        out.append('%-5s %-26s %s' % (cls if cls == 'PASS' else cls + '  ', mode, (verdict + ' ' + detail).strip()))
    m = re.search(r'pass=(\d+) fail=(\d+) blocked=(\d+) skip=(\d+)', window[-1])
    problems = []
    if not m:
        problems.append('unreadable SUMMARY line')
    elif tuple(int(x) for x in m.groups()) != (counts['PASS'], counts['FAIL'], counts['BLOCKED'], counts['SKIP']):
        problems.append('SUMMARY (%s) disagrees with the RESULT lines (%s)' % (
            ' '.join(m.groups()), ' '.join(str(counts[c]) for c in CLASSES)))
    if not results:
        problems.append('no RESULT lines')
    notpass = counts['FAIL'] + counts['BLOCKED'] + counts['SKIP']
    out.append('totals: pass=%d fail=%d blocked=%d skip=%d' % (counts['PASS'], counts['FAIL'], counts['BLOCKED'], counts['SKIP']))
    if notpass:
        out.append('%d mode(s) did not pass; FAIL, BLOCKED and SKIP are all not-pass' % notpass)
    out.extend('problem: ' + p for p in problems)
    ok = counts['PASS'] > 0 and not notpass and not problems
    out.append('SELFTEST VERDICT: ' + ('PASS' if ok else 'FAIL'))
    return (0 if ok else 1), '\n'.join(out)


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, a = argv[1], argv[2:]
    try:
        if cmd == 'manifest-version' and len(a) == 1:
            print(manifest_version(a[0]))
        elif cmd == 'latest-package' and 1 <= len(a) <= 2:
            print(latest_package(Path(a[0]).read_text(), *a[1:]))
        elif cmd == 'package-version' and len(a) == 1:
            print(package_version(a[0]))
        elif cmd == 'next-version' and 1 <= len(a) <= 2:
            print(next_version(*a))
        elif cmd == 'version-gt' and len(a) == 2:
            return 0 if vtuple(a[0]) > vtuple(a[1]) else 1
        elif cmd == 'selftest-verdict' and 1 <= len(a) <= 2:
            code, report = selftest_verdict(Path(a[0]).read_text(errors='replace'), int(a[1]) if len(a) > 1 else 0)
            print(report)
            return code
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except (ValueError, OSError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as e:
        print('xn_console: %s' % e, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
