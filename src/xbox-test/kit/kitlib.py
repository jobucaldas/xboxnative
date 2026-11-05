"""Shared kit metadata helpers. SPDX-License-Identifier: GPL-3.0-or-later"""
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
import zipfile


def identity_from_xml(data):
    root = ET.fromstring(data)
    ident = next((el for el in root if el.tag.rsplit('}', 1)[-1] == 'Identity'), None)
    if ident is None:
        raise ValueError('manifest has no Identity')
    name, version = ident.get('Name'), ident.get('Version')
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9.-]{3,50}', name):
        raise ValueError('invalid package identity')
    validate_version(version)
    return name, version


def read_identity(manifest_path):
    return identity_from_xml(Path(manifest_path).read_bytes())


def package_identity(path):
    # No extraction: package-controlled paths must never become host paths.
    with zipfile.ZipFile(path) as package:
        if package.namelist().count('AppxManifest.xml') != 1:
            raise ValueError('MSIX must contain exactly one AppxManifest.xml')
        info = package.getinfo('AppxManifest.xml')
        if info.file_size > 1 << 20:
            raise ValueError('AppxManifest.xml is too large')
        return identity_from_xml(package.read(info))


def validate_version(version):
    if not isinstance(version, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+){3}', version):
        raise ValueError('version must have four numeric components')
    if any(len(v) > 5 or int(v) > 65535 for v in version.split('.')):
        raise ValueError('version component exceeds 65535')


def validate_manifest(doc):
    """Reject unsafe paths, ambiguous entries and Markdown/control injection."""
    if not isinstance(doc, dict) or not isinstance(doc.get('packages'), list) or not doc['packages']:
        raise ValueError('manifest must contain packages')
    if not isinstance(doc.get('extras'), list):
        raise ValueError('manifest must contain an extras list')
    names, files, identities = set(), set(), set()
    for kind in ('packages', 'extras'):
        for entry in doc[kind]:
            if not isinstance(entry, dict):
                raise ValueError('entry must be an object')
            name, file = entry.get('name'), entry.get('file')
            for value in (name, file):
                if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', value):
                    raise ValueError('entry names/files must be safe basenames')
            # Case-insensitive uniqueness also matters on the install host.
            if name.lower() in names or file.lower() in files:
                raise ValueError('duplicate entry name or file')
            names.add(name.lower())
            files.add(file.lower())
            if not isinstance(entry.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', entry['sha256']):
                raise ValueError('invalid sha256')
            if kind == 'packages':
                if file != name + '.msix':
                    raise ValueError('package file must match name.msix')
                identity = entry.get('identity')
                if not isinstance(identity, str) or not re.fullmatch(r'[A-Za-z0-9.-]{3,50}', identity):
                    raise ValueError('invalid package identity')
                if identity.lower() in identities:
                    raise ValueError('duplicate package identity')
                identities.add(identity.lower())
                validate_version(entry.get('version'))
            elif not file.lower().endswith(('.exe', '.dll')):
                raise ValueError('extras must be guest EXE/DLL files, never key material')
    # An extra must not overwrite a package certificate.
    certs = {e['name'].lower() + '.cer' for e in doc['packages']}
    if files & certs:
        raise ValueError('entry collides with a certificate')
    return doc


def load_manifest(path):
    return validate_manifest(json.loads(Path(path).read_text(encoding='utf-8')))


def render_versions(doc):
    validate_manifest(doc)
    rows = ['# Built kit versions', '',
            'Generated from this kit\'s `manifest.json`; not console acceptance evidence.', '',
            '| Package | Identity | Version |', '| --- | --- | --- |']
    for entry in doc['packages']:
        rows.append(f"| `{entry['name']}` | `{entry['identity']}` | {entry['version']} |")
    return '\n'.join(rows) + '\n'


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    cmd = sys.argv[1]
    if cmd in ('identity', 'package-identity'):
        fn = read_identity if cmd == 'identity' else package_identity
        print('\t'.join(fn(sys.argv[2])))
    elif cmd == 'sha256':
        print(sha256_file(sys.argv[2]))
    elif cmd == 'validate':
        load_manifest(sys.argv[2])
    elif cmd == 'versions':
        mode, manifest, output = sys.argv[2:5]
        rendered = render_versions(load_manifest(manifest))
        path = Path(output)
        if mode == 'check':
            if not path.is_file() or path.read_text(encoding='utf-8') != rendered:
                raise ValueError('version document differs from manifest')
            print('PASS: version document matches manifest')
        elif mode == 'write':
            if path.is_symlink():
                raise ValueError('output must not be a symlink')
            if path.resolve() == Path(manifest).resolve():
                raise ValueError('output must not overwrite manifest')
            path.write_text(rendered, encoding='utf-8')
        else:
            raise ValueError('unknown versions mode')
    elif cmd == 'write-manifest':
        out_path, generated_at, commit, branch = sys.argv[2:6]
        packages, extras = [], []
        for line in sys.stdin:
            if not line.strip():
                continue
            name, identity, version, filename, digest, kind = line.rstrip('\n').split('\t')
            entry = {'name': name, 'file': filename, 'sha256': digest}
            if kind == 'package':
                entry.update(identity=identity, version=version)
                packages.append(entry)
            elif kind == 'extra':
                extras.append(entry)
            else:
                raise ValueError('unknown manifest entry kind')
        doc = validate_manifest(dict(generated_at=generated_at, source_commit=commit,
                                     source_branch=branch, packages=packages, extras=extras))
        Path(out_path).write_text(json.dumps(doc, indent=2) + '\n', encoding='utf-8')
    else:
        raise ValueError(f'unknown command: {cmd}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, zipfile.BadZipFile, ET.ParseError) as exc:
        sys.exit(f'kitlib: {exc}')
