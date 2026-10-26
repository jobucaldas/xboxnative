#!/usr/bin/env python3
"""Offline regression checks; never contacts Device Portal."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('verify', HERE / 'verify-depot.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)


class VerifyTests(unittest.TestCase):
    def test_local_integrity_and_failures(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            f = root / 'sub folder' / 'a file.bin'
            f.parent.mkdir()
            f.write_bytes(b'abc')
            manifest = root / 'files.json'
            manifest.write_text(json.dumps({'files': [{'filename': 'sub folder\\a file.bin', 'size': 3,
                'sha_content': hashlib.sha1(b'abc').hexdigest()}]}))
            def run():
                return subprocess.run(['bash', str(HERE / 'verify-depot.sh'), '--local', d,
                                       '--manifest', str(manifest)], capture_output=True, text=True)
            self.assertEqual(run().returncode, 0)
            f.write_bytes(b'xyz')
            self.assertIn('sha1 mismatch', run().stdout)
            self.assertEqual(run().returncode, 1)
            f.unlink()
            self.assertIn('missing=1', run().stdout)
            (root / 'extra').touch()
            self.assertIn('extra=1', run().stdout)

    def test_console_recursion(self):
        old = v.helper
        calls = []
        def fake(script, package, folder):
            calls.append(folder)
            return 'd 0 sub folder\n- 3 root file\n' if folder == '\\LocalState\\app' else '- 4 nested file\n'
        v.helper = fake
        try:
            self.assertEqual(v.console_files('pkg', '\\LocalState\\app'),
                             {'root file': 3, 'sub folder/nested file': 4})
            self.assertEqual(len(calls), 2)
        finally:
            v.helper = old

    def test_reject_unsafe_and_duplicate(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'files.json'
            for names in [['../escape'], ['a', 'a']]:
                p.write_text(json.dumps([{'filename': n, 'size': 0} for n in names]))
                with self.assertRaises(ValueError):
                    v.load_manifest(p)


if __name__ == '__main__':
    unittest.main()
