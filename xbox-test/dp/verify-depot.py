#!/usr/bin/env python3
"""Compare depot files without changing console state; Python standard library only."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent


def relative(name):
    name = name.replace('\\', '/')
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '..' in p.parts or ':' in name or '\n' in name or '\r' in name:
        raise ValueError('unsafe manifest path')
    return str(p)


def load_manifest(path):
    data = path.read_text()
    result = {}
    if path.suffix.lower() == '.json':
        obj = json.loads(data)
        if isinstance(obj, dict):
            obj = obj.get('files', obj.get('manifest', {}).get('files'))
        if not isinstance(obj, list):
            raise ValueError('JSON must contain a files array')
        rows = []
        for f in obj:
            if int(f.get('flags', 0)) & 64:
                continue
            rows.append((f.get('filename', f.get('name')), {
                'size': int(f['size']), 'sha1': f.get('sha_content', f.get('sha1')),
                'sha256': f.get('sha256')}))
    else:
        rows = []
        for line in data.splitlines():
            if not line.strip():
                continue
            m = re.fullmatch(r'([0-9a-fA-F]{40}|[0-9a-fA-F]{64}) [ *](.+)', line)
            if m:
                rows.append((m[2], {'sha1' if len(m[1]) == 40 else 'sha256': m[1]}))
                continue
            m = re.fullmatch(r'-\s+(\d+)\s+(.+)', line)
            if not m:
                raise ValueError('expected JSON files, sha1sum/sha256sum, or ls-dir listing')
            rows.append((m[2], {'size': int(m[1])}))
    for name, f in rows:
        name = relative(name)
        if name in result:
            raise ValueError('duplicate manifest path: ' + name)
        if f.get('size', 0) < 0:
            raise ValueError('negative size')
        for alg, length in [('sha1', 40), ('sha256', 64)]:
            h = f.get(alg)
            if h is not None:
                if not isinstance(h, str) or not re.fullmatch('[0-9a-fA-F]{%d}' % length, h):
                    raise ValueError('invalid ' + alg + ' for ' + name)
                f[alg] = h.lower()
        result[name] = f
    if not result:
        raise ValueError('empty manifest')
    return result


def helper(script, *args):
    return subprocess.check_output(['bash', str(HERE / script), *map(str, args)], text=True)


def console_files(package, remote):
    found = {}
    def walk(folder, prefix=''):
        for line in helper('ls-dir.sh', package, folder).splitlines():
            m = re.fullmatch(r'([d-])\s+(?:(\d+)\s+)?(.+)', line)
            if not m:
                raise ValueError('invalid console listing')
            name = relative(m[3])
            if '/' in name:
                raise ValueError('invalid console basename')
            rel = prefix + name
            if m[1] == 'd':
                walk(folder + '\\' + name, rel + '/')
            else:
                if m[2] is None:
                    raise ValueError('console omitted file size')
                found[rel] = int(m[2])
    walk(remote)
    return found


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, help='JSON files array, checksum list, or saved ls-dir output')
    p.add_argument('--local', type=Path, metavar='DIR', help='verify local files, including all available hashes')
    p.add_argument('--package', help='console PackageFullName (required unless --local)')
    p.add_argument('--remote', default='\\LocalState\\spacewar', help='console app folder')
    p.add_argument('--sha', action='store_true', help='download console files temporarily and check manifest hashes')
    a = p.parse_args()
    if not a.local and not a.package:
        p.error('--package is required for console mode')
    manifest = a.manifest or (a.local / 'MANIFEST.sha256' if a.local else None)
    if manifest is None:
        p.error('--manifest is required for console mode')
    expected = load_manifest(manifest)
    if a.sha and any(not (f.get('sha1') or f.get('sha256')) for f in expected.values()):
        p.error('--sha requires a hash for every manifest file')
    if a.local:
        root = a.local.resolve(strict=True)
        actual = {}
        for f in root.rglob('*'):
            if f.is_file():
                if not f.resolve().is_relative_to(root):
                    raise ValueError('local symlink escapes root')
                actual[f.relative_to(root).as_posix()] = f.stat().st_size
    else:
        actual = console_files(a.package, a.remote.rstrip('\\'))
    ok = missing = bad = hashed = sized = present = 0
    with tempfile.TemporaryDirectory(prefix='verify-depot-') as tmp:
        for name, f in expected.items():
            if name not in actual:
                print('MISSING', name)
                missing += 1
                continue
            present += 1
            errors = []
            if 'size' in f:
                sized += 1
                if actual[name] != f['size']:
                    errors.append('size expected=%d actual=%d' % (f['size'], actual[name]))
            hashes = [alg for alg in ('sha1', 'sha256') if f.get(alg)]
            if hashes and (a.local or a.sha):
                if a.local:
                    target = root / name
                else:
                    folder, _, base = name.rpartition('/')
                    target = Path(tmp) / 'file'
                    helper('get-file.sh', a.package, a.remote.rstrip('\\') + ('\\' + folder.replace('/', '\\') if folder else ''), base, target)
                with target.open('rb') as stream:
                    hs = {alg: hashlib.new(alg) for alg in hashes}
                    for block in iter(lambda: stream.read(1024 * 1024), b''):
                        for h in hs.values():
                            h.update(block)
                hashed += 1
                for alg, h in hs.items():
                    # Steam uses zero SHA1 for empty files.
                    if alg == 'sha1' and f[alg] == '0' * 40 and actual[name] == 0:
                        continue
                    if h.hexdigest() != f[alg]:
                        errors.append(alg + ' mismatch')
            if errors:
                bad += 1
                print('BAD', name, '; '.join(errors))
            else:
                ok += 1
                print('OK', name)
    extras = set(actual) - set(expected)
    # Conventional checksum sidecars are metadata, not depot payload.
    extras -= {'MANIFEST.sha256', 'MANIFEST.sha1'}
    if a.local and manifest.resolve().is_relative_to(root):
        extras.discard(manifest.resolve().relative_to(root).as_posix())
    for name in sorted(extras):
        print('EXTRA', name)
    print(f'{ok}/{len(expected)} OK present={present} missing={missing} bad={bad} extra={len(extras)} size_checked={sized} hash_checked={hashed}')
    return int(bool(missing or bad or extras))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as e:
        print('ERROR:', e, file=sys.stderr)
        sys.exit(2)
