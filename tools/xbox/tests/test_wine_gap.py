"""Focused regressions: import completeness, deduplication, and honest routing."""
import csv
import io
import json
import importlib.util
from pathlib import Path
import unittest
import sys
import re
import shutil
import subprocess

SPEC = importlib.util.spec_from_file_location('wine_gap', Path(__file__).parents[1] / 'wine_gap.py')
gap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gap)


class GapTests(unittest.TestCase):
    def test_summary_counts_imports_not_syscalls(self):
        rows = [dict(console_route=r, n_win32u=n) for r, n in
                [('system', 9), ('game', 0), ('stub', 3), ('uwph', 0), ('unclassified', 0)]]
        self.assertEqual(gap.summary(rows), 'GAP: total=5 missing=3 win32u=2')

    def test_real_pe_imports_are_deduplicated(self):
        path = Path('/workspace/megabonk-from-console/baselib.dll')
        if not path.exists():
            self.skipTest('local game fixture absent')
        self.assertEqual(gap.scan([path]), gap.scan([path, path]))
        self.assertIn(('kernel32.dll', 'GetProcAddress'), gap.scan([path]))
        # pefile fills a convenience name for known Winsock ordinals. Preserve
        # the actual PE encoding so offline and console catalogs use one key.
        self.assertIn(('ws2_32.dll', '#21'), gap.scan([path]))
        self.assertNotIn(('ws2_32.dll', 'setsockopt'), gap.scan([path]))

    def test_import_pairs_against_independent_llvm_reader(self):
        reader = shutil.which('llvm-readobj-19') or shutil.which('llvm-readobj')
        root = Path('/workspace/megabonk-from-console')
        if not reader or not root.exists():
            self.skipTest('local LLVM/game fixture absent')
        paths = sorted(p for p in root.iterdir() if p.suffix.lower() in ('.exe', '.dll'))
        expected = set()
        for path in paths:
            output = subprocess.check_output([reader, '--coff-imports', str(path)], text=True)
            dll = None
            for line in output.splitlines():
                name = re.match(r'\s*Name: (\S+)', line)
                if name:
                    dll = name[1].lower()
                symbol = re.match(r'\s*Symbol:\s*(.*?)\s*\((\d+)\)\s*$', line)
                if symbol:
                    expected.add((dll, symbol[1].strip() or '#' + symbol[2]))
        self.assertEqual(set(gap.scan(paths)), expected)

    def test_historical_log_dropped_real_imports(self):
        root = Path('/workspace/megabonk-from-console')
        baseline = Path('/workspace/x64-run/research/wine-coverage/megabonk-imports-vs-wine.v1-391rows.csv')
        if not root.exists() or not baseline.exists():
            self.skipTest('local research fixture absent')
        with baseline.open() as f:
            old = {(r['dll'], r['symbol']) for r in csv.DictReader(f)}
        actual = set(gap.scan([root / n for n in ['baselib.dll', 'UnityPlayer.dll', 'Megabonk.exe']]))
        self.assertTrue(old <= actual)
        self.assertGreater(len(actual), len(old))


class WineSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not Path('/workspace/research/wine/dlls/opengl32/wgl.c').exists():
            raise unittest.SkipTest('local Wine source absent')
        sys.path.insert(0, str(Path(__file__).parents[1]))
        import wine_graph
        cls.graph = wine_graph

    def test_corrected_oracle_exact_bytes(self):
        root = Path('/workspace/megabonk-from-console')
        baseline = Path('/workspace/x64-run/research/wine-coverage/megabonk-imports-vs-wine.csv')
        if not root.exists() or not baseline.exists():
            self.skipTest('local research fixture absent')
        paths = sorted(p for p in root.iterdir() if p.suffix.lower() in ('.exe', '.dll'))
        mapping = json.loads((Path(__file__).parents[1] / 'import_map.json').read_text())['dlls']
        rows = gap.classify(gap.scan(paths), mapping, {p.name.lower() for p in paths}, self.graph)
        output = io.StringIO(newline='')
        writer = csv.DictWriter(output, gap.FIELDS)
        writer.writeheader()
        writer.writerows(rows)
        self.assertEqual(output.getvalue().encode(), baseline.read_bytes())

    def test_opengl_unix_wrapper_is_not_pure(self):
        rows = gap.classify([('opengl32.dll', 'wglGetProcAddress')], {}, set(), self.graph)
        self.assertEqual(rows[0]['verdict'], 'PE_NEEDS_UNIXLIB')
        self.assertEqual(rows[0]['unixlib_call'], 1)

    def test_winsock_ordinal_resolves_to_named_export(self):
        self.assertEqual(self.graph.resolve('ws2_32.dll', '#115'),
                         self.graph.resolve('ws2_32.dll', 'WSAStartup'))


if __name__ == '__main__':
    unittest.main()
