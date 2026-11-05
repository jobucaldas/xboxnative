# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import re
import unittest

from generate_routes import generate_routes


class GenerateRoutesTests(unittest.TestCase):
    def setUp(self):
        self.entries = {name: {'class': 'uwph'} for name in ('ntdll.dll', 'win32u.dll')}

    def test_explicit_class_mapping(self):
        for kind, identifier in [('system', 'system'), ('uwph', 'uwph'),
                                 ('stub', 'stub'), ('wine-pe', 'wine_pe')]:
            with self.subTest(kind=kind):
                self.entries['example.dll'] = {'class': kind}
                self.assertIn(f'{{L"example.dll", RouteKind::{identifier}}}',
                              generate_routes(self.entries))

    def test_unknown_class_is_rejected(self):
        self.entries['example.dll'] = {'class': 'future-native'}
        with self.assertRaisesRegex(ValueError, 'unsupported route class'):
            generate_routes(self.entries)

    def test_missing_wine_boundary_is_rejected(self):
        for name in self.entries:
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'missing Wine boundary'):
                generate_routes({k: v for k, v in self.entries.items() if k != name})

    def test_checked_map_uses_declared_enum_members(self):
        here = Path(__file__).resolve().parent
        entries = json.loads((here.parents[3] / 'src/tools/xbox/import_map.json').read_text())['dlls']
        output = generate_routes(entries)
        enum = re.search(r'enum class RouteKind \{([^}]+)\}', (here / 'dll_map.cpp').read_text())[1]
        members = {member.strip() for member in enum.split(',')}
        self.assertLessEqual(set(re.findall(r'RouteKind::(\w+)', output)), members)
        self.assertNotIn('RouteKind::wine-pe', output)
        for name, entry in entries.items():
            if entry['class'] == 'wine-pe':
                self.assertIn(f'{{L"{name}", RouteKind::wine_pe}}', output)


if __name__ == '__main__':
    unittest.main()
