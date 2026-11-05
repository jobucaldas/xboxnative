# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with: python3 -m unittest discover -s src/xbox-test/kit -p 'test_*.py'."""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

import kitlib

HERE = Path(__file__).resolve().parent


def manifest():
    return {'packages': [{'name': 'tls', 'identity': 'Test.Tls', 'version': '1.2.3.4',
                          'file': 'tls.msix', 'sha256': 'a' * 64}],
            'extras': [{'name': 'tls-test.exe', 'file': 'tls-test.exe', 'sha256': 'b' * 64}]}


class KitMetadataTests(unittest.TestCase):
    def test_versions_are_deterministic_and_artifact_local(self):
        doc = manifest()
        self.assertEqual(kitlib.render_versions(doc), kitlib.render_versions(copy.deepcopy(doc)))
        self.assertIn('| `tls` | `Test.Tls` | 1.2.3.4 |', kitlib.render_versions(doc))
        readme = (HERE / 'README.md').read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'manifest.json'
            path.write_text(json.dumps(doc))
            def run(*args):
                return subprocess.run(['bash', str(HERE / 'sync-readme-versions.sh'), *args],
                                      capture_output=True, text=True)
            result = run(str(path))
            self.assertEqual(result.returncode, 0, result.stderr)
            output = path.with_name('VERSIONS.md')
            self.assertEqual(output.read_text(), kitlib.render_versions(doc))
            self.assertEqual(run('--check', str(path)).returncode, 0)
            output.write_text('stale\n')
            self.assertNotEqual(run('--check', str(path)).returncode, 0)
            self.assertEqual(output.read_text(), 'stale\n')
            self.assertNotEqual(run(str(path), str(path)).returncode, 0)
            self.assertEqual(json.loads(path.read_text()), doc)
            output.unlink()
            output.symlink_to(path)
            self.assertNotEqual(run(str(path)).returncode, 0)
            self.assertEqual(json.loads(path.read_text()), doc)
        self.assertEqual((HERE / 'README.md').read_bytes(), readme)

    def test_unsafe_or_ambiguous_entries_are_rejected(self):
        mutations = [
            ('name', '../tls'), ('name', 'tls\nrow'), ('name', '`tls|'),
            ('file', '/tmp/tls.msix'), ('file', '../tls.msix'),
            ('file', 'sub/tls.msix'), ('file', 'sub\\tls.msix'),
            ('identity', 'Bad|Identity'), ('identity', None),
            ('version', '1.2.3'), ('version', '1.2.3.65536'),
            ('version', '1.2.3.4\nrow'), ('sha256', 'not-a-hash'),
        ]
        for key, value in mutations:
            with self.subTest(key=key, value=value):
                doc = manifest()
                doc['packages'][0][key] = value
                with self.assertRaises(ValueError):
                    kitlib.render_versions(doc)
        for key in ('name', 'file', 'identity'):
            doc = manifest()
            other = dict(doc['packages'][0], name='other', file='other.msix', identity='Test.Other')
            other[key] = doc['packages'][0][key].upper()
            doc['packages'].append(other)
            with self.assertRaises(ValueError):
                kitlib.validate_manifest(doc)
        for suffix in ('.pfx', '.key', '.pem', '.cer', '.json'):
            doc = manifest()
            doc['extras'][0].update(name='secret' + suffix, file='secret' + suffix)
            with self.assertRaises(ValueError):
                kitlib.validate_manifest(doc)
        with self.assertRaises(ValueError):
            kitlib.validate_manifest({'packages': [], 'extras': []})

    def test_invalid_manifest_does_not_clobber_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            path, output = Path(tmp) / 'manifest.json', Path(tmp) / 'VERSIONS.md'
            doc = manifest()
            doc['packages'][0]['file'] = '../../outside.msix'
            path.write_text(json.dumps(doc))
            output.write_text('preserve me')
            result = subprocess.run(['bash', str(HERE / 'sync-readme-versions.sh'), str(path)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_text(), 'preserve me')
            # Both verification gates reject the bad path before using it.
            for script in ('verify-kit.sh', 'verify-kit-offline.sh'):
                result = subprocess.run(['bash', str(HERE / script), tmp],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('safe basenames', result.stderr)

    def test_identity_is_read_from_built_msix(self):
        xml = b'<Package xmlns="urn:test"><Identity Name="Test.Built" Version="2.3.4.5"/></Package>'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'built.msix'
            with zipfile.ZipFile(path, 'w') as package:
                package.writestr('AppxManifest.xml', xml)
                package.writestr('../not-extracted', b'not a host file')
            self.assertEqual(kitlib.package_identity(path), ('Test.Built', '2.3.4.5'))
            with zipfile.ZipFile(path, 'w') as package:
                package.writestr('AppxManifest.xml', b'<Package/>')
            with self.assertRaises(ValueError):
                kitlib.package_identity(path)

    def test_failed_required_builder_keeps_previous_latest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            builder = root / 'builders/src/xbox-test/uwph/host/build.sh'
            builder.parent.mkdir(parents=True)
            builder.write_text('#!/bin/bash\nexit 23\n')
            output = root / 'kit'
            (output / 'previous').mkdir(parents=True)
            (output / 'latest').symlink_to('previous')
            env = dict(os.environ, KIT_BUILD_ROOT=str(root / 'builders'))
            result = subprocess.run(['bash', str(HERE / 'build-kit.sh'), str(output)],
                                    cwd=tmp, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)
            self.assertEqual(os.readlink(output / 'latest'), 'previous')

    def test_write_manifest_rejects_unknown_kinds(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'manifest.json'
            result = subprocess.run(['python3', str(HERE / 'kitlib.py'), 'write-manifest',
                                     str(output), 'date', 'commit', 'branch'],
                                    input='tls\tTest.Tls\t1.2.3.4\ttls.msix\t' + 'a' * 64 + '\tunknown\n',
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
