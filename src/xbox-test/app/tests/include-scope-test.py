#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Include-scope guard for the launcher's play_runtime namespace.

play_runtime.h includes some uwph headers inside `namespace play_runtime {`.
With #pragma once, the first include of a header decides which scope its
declarations land in, so a later global include of that header is a no-op.
launch.h and main.cpp are not compiled by CI, so this checks the include graph:
no header first pulled in inside the namespace may be reached at global scope
by a launcher translation unit after play_runtime.h has opened the namespace.
"""
import re
import tempfile
import unittest
from pathlib import Path

TREE = Path(__file__).resolve().parents[2]  # src/xbox-test
RUNTIME = Path('app/host/play_runtime.h')
NAMESPACE = re.compile(r'^\s*namespace\s+play_runtime\s*\{')
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')


def quoted_includes(path):
    """(line index, resolved header or None) for each quoted include in path."""
    found = []
    lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    for index, line in enumerate(lines):
        match = INCLUDE.match(line)
        if match:
            target = (path.parent / match.group(1)).resolve()
            found.append((index, target if target.is_file() else None))
    return found


def reachable(includes):
    """Every header reachable from a list of (line, target) includes."""
    seen, todo = set(), [target for _, target in includes]
    while todo:
        header = todo.pop()
        if header is None or header in seen:
            continue
        seen.add(header)
        todo.extend(target for _, target in quoted_includes(header))
    return seen


def walk_root(root, runtime, before, namespaced):
    """Global-scope walk of one translation unit, in include order."""
    problems, seen, at_runtime = [], {root}, None

    def walk(includes):
        nonlocal at_runtime
        for _, target in includes:
            if target is None:
                continue
            if target == runtime:
                if at_runtime is None:
                    walk(before)
                    at_runtime = frozenset(seen)
                continue
            if target in seen:
                continue
            seen.add(target)
            if at_runtime is not None and target in namespaced and target not in at_runtime:
                problems.append((root, target))
            walk(quoted_includes(target))

    walk(quoted_includes(root))
    return problems


def audit(tree):
    """(problems, roots). A problem is (translation unit, header) for a header
    that play_runtime's namespace pulls in first and that the unit then reaches
    at global scope, where the include is a no-op and its names are missing."""
    runtime = (tree / RUNTIME).resolve()
    lines = runtime.read_text(encoding='utf-8', errors='replace').splitlines()
    opened = next(i for i, line in enumerate(lines) if NAMESPACE.match(line))
    includes = quoted_includes(runtime)
    before = [inc for inc in includes if inc[0] < opened]
    inside = [inc for inc in includes if inc[0] > opened]
    namespaced = reachable(inside) - reachable(before)
    roots = sorted(p.resolve() for p in (tree / 'app').rglob('*')
                   if p.suffix in ('.h', '.cpp')
                   and any(target == runtime for _, target in quoted_includes(p)))
    problems = []
    for root in roots:
        problems += walk_root(root, runtime, before, namespaced)
    return problems, roots


def describe(problems, tree):
    return [f'{root.relative_to(tree)} reaches {header.relative_to(tree)} at global scope'
            for root, header in problems]


class IncludeScopeTest(unittest.TestCase):
    def fixture(self, pre=''):
        """Miniature tree with the same include shape as the launcher."""
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        tree = Path(tmp.name) / 'xbox-test'
        files = {
            'app/host/play_runtime.h': '#pragma once\n' + pre +
                'namespace play_runtime {\n#include "../../uwph/shell32/special_folder.h"\n}\n',
            'app/launch.h': '#pragma once\n#include "host/play_runtime.h"\n'
                '#include "../uwph/shell32/folder_chain.h"\n',
            'uwph/shell32/folder_chain.h': '#pragma once\nnamespace folder_chain { struct Package {}; }\n',
            'uwph/shell32/special_folder.h': '#pragma once\n#include "folder_chain.h"\n'
                'namespace shell_folder { inline int probe(folder_chain::Package*) { return 0; } }\n',
        }
        for rel, text in files.items():
            (tree / rel).parent.mkdir(parents=True, exist_ok=True)
            (tree / rel).write_text(text)
        return tree

    def test_launcher_units_do_not_reach_namespaced_headers_globally(self):
        problems, roots = audit(TREE)
        self.assertIn('launch.h', [r.name for r in roots])
        self.assertEqual(describe(problems, TREE), [])

    def test_fixture_reproduces_folder_chain_scope_leak(self):
        tree = self.fixture()
        problems, roots = audit(tree)
        self.assertIn('launch.h', [r.name for r in roots])
        self.assertEqual(describe(problems, tree), [
            'app/launch.h reaches uwph/shell32/folder_chain.h at global scope'])

    def test_fixture_with_global_include_before_namespace_passes(self):
        tree = self.fixture(pre='#include "../../uwph/shell32/folder_chain.h"\n')
        problems, roots = audit(tree)
        self.assertIn('launch.h', [r.name for r in roots])
        self.assertEqual(describe(problems, tree), [])


if __name__ == '__main__':
    unittest.main()
