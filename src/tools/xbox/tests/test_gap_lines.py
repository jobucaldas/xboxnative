"""Exact launch GAP lines for the repository's x64 and PE32 test images."""
import importlib.util
import json
import os
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('wine_gap', Path(__file__).parents[1] / 'wine_gap.py')
gap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gap)

FIXTURES = Path(os.environ.get('GAP_FIXTURE_DIR', ''))
MAP = json.loads((Path(__file__).parents[1] / 'import_map.json').read_text())['dlls']


@unittest.skipUnless(os.environ.get('GAP_FIXTURE_DIR'), 'GAP_FIXTURE_DIR not set')
class LaunchGapLineTests(unittest.TestCase):
    def lines_for(self, name):
        pairs = gap.scan([FIXTURES / name])
        assets = {f.name.lower() for f in FIXTURES.iterdir()
                  if f.is_file() and f.suffix.lower() in ('.exe', '.dll')}
        rows = []
        for dll, symbol in pairs:
            route = 'game' if dll in assets else MAP.get(dll, {}).get('class', 'unclassified')
            rows.append({'dll': dll, 'symbol': symbol, 'console_route': route})
        return gap.gap_lines(rows)

    def test_x64_fixture_exact_lines(self):
        self.assertEqual(self.lines_for('play-d3d11.exe'), [
            'GAP: dll=d3d11.dll system=1 donor=0 stub=0 missing=0',
            'GAP: dll=kernel32.dll system=3 donor=0 stub=0 missing=0',
            'GAP: dll=user32.dll system=5 donor=0 stub=0 missing=0',
        ])

    def test_pe32_fixture_exact_lines(self):
        self.assertEqual(self.lines_for('gap.exe'), [
            'GAP: dll=kernel32.dll system=2 donor=0 stub=0 missing=0',
        ])

    def test_stub_and_missing_names_are_bounded(self):
        rows = [
            {'dll': 'a-stub.dll', 'symbol': 'Stubbed', 'console_route': 'stub'},
            {'dll': 'b-unknown.dll', 'symbol': 'Absent', 'console_route': 'unclassified'},
            {'dll': 'c-donor.dll', 'symbol': 'Donated', 'console_route': 'wine-pe'},
            {'dll': 'd-host.dll', 'symbol': 'Hosted', 'console_route': 'uwph'},
        ]
        self.assertEqual(gap.gap_lines(rows, limit=1), [
            'GAP: dll=a-stub.dll system=0 donor=0 stub=1 missing=0',
            'GAP: dll=b-unknown.dll system=0 donor=0 stub=0 missing=1',
            'GAP: dll=c-donor.dll system=0 donor=1 stub=0 missing=0',
            'GAP: dll=d-host.dll system=1 donor=0 stub=0 missing=0',
            'GAP: stub a-stub.dll!Stubbed',
            'GAP: missing-or-stubbed truncated after 1 names total=2',
        ])


if __name__ == '__main__':
    unittest.main()
