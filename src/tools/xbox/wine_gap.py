#!/usr/bin/env python3
"""Classify direct and delay PE imports against Wine source and a routing plan.

Requires pefile (pip install pefile). No guest code executes. Routes describe the
map's plan, never console export availability. Missing counts non-system imports
except modules present in the supplied PE list. Wine call graphs are approximate.
"""
import argparse
import csv
import json
import os
import re
from pathlib import Path
import sys

FIELDS = 'dll symbol console_route wine_class wine_impl_dll wine_c_name verdict n_nt n_win32u unixlib_call nt_syscalls win32u_syscalls'.split()


def scan(paths):
    import pefile
    pairs = set()
    for path in paths:
        try:
            pe = pefile.PE(str(path), fast_load=True)
        except pefile.PEFormatError as exc:
            raise ValueError(f'{path}: {exc}') from exc
        try:
            pe.parse_data_directories(directories=[1, 13])
            for table in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT'):
                for entry in getattr(pe, table, []):
                    for imp in entry.imports:
                        pairs.add((entry.dll.decode('ascii').lower(),
                                   '#' + str(imp.ordinal) if imp.import_by_ordinal else imp.name.decode('ascii')))
        except pefile.PEFormatError as exc:
            raise ValueError(f'{path}: {exc}') from exc
        finally:
            pe.close()
    return sorted(pairs)


def classify(pairs, mapping, assets, graph):
    rows = []
    for dll, sym in pairs:
        cls, idll, cname = graph.resolve(dll, sym)
        calls, unix = set(), False
        if cname:
            cn = graph.canon(cname)
            if cn in graph.ALLSYS:
                calls = {cn}
            else:
                calls, unix, _ = graph.reach((idll, cn))
        nt, win = sorted(calls & graph.NT), sorted(calls & graph.W32)
        verdict = cls
        if cls == 'IMPL':
            if unix:
                verdict = 'PE_NEEDS_UNIXLIB'
            elif win:
                verdict = 'PE_NEEDS_WIN32U'
            elif nt:
                verdict = 'PE_NEEDS_NT'
            elif (idll, graph.canon(cname)) not in graph.graph:
                verdict = 'GRAPH_UNRESOLVED'
            else:
                verdict = 'PE_PURE'
        route = 'game' if dll in assets else mapping.get(dll, {}).get('class', 'unclassified')
        rows.append(dict(zip(FIELDS, (dll, sym, route, cls, idll, cname or '', verdict,
            len(nt), len(win), int(unix), ';'.join(nt), ';'.join(win)))))
    return rows


def summary(rows):
    return 'GAP: total={} missing={} win32u={}'.format(len(rows),
        sum(r['console_route'] not in ('system', 'game') for r in rows),
        sum(int(r['n_win32u']) > 0 for r in rows))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--map', type=Path, default=Path(__file__).with_name('import_map.json'))
    p.add_argument('--wine-source', type=Path, default=Path('/workspace/research/wine'))
    p.add_argument('--pe', action='append', help='Relative PE path, repeatable; default all top-level EXE/DLL files')
    p.add_argument('--csv', required=True, type=Path)
    p.add_argument('--catalog', type=Path, help='Write a C++ Wine classification catalog for the launcher')
    a = p.parse_args()
    try:
        if not a.directory.is_dir():
            raise ValueError(f'not a directory: {a.directory}')
        paths = [a.directory / name for name in a.pe] if a.pe else sorted(
            f for f in a.directory.iterdir() if f.is_file() and f.suffix.lower() in ('.exe', '.dll'))
        if not paths:
            raise ValueError('no PE files selected')
        mapping = json.loads(a.map.read_text())['dlls']
        if not isinstance(mapping, dict):
            raise ValueError('map dlls must be an object')
        for name, entry in mapping.items():
            if not isinstance(entry, dict) or name != name.lower() or entry['class'] not in ('system', 'uwph', 'stub', 'wine-pe'):
                raise ValueError(f'invalid route: {name}')
        os.environ['WINE_SOURCE'] = str(a.wine_source)
        import wine_graph as graph
        rows = classify(scan(paths), mapping, {f.name.lower() for f in paths}, graph)
        with a.csv.open('w', newline='') as f:
            w = csv.DictWriter(f, FIELDS); w.writeheader(); w.writerows(rows)
        if a.catalog:
            # Only Wine metadata is cached. The launcher reads actual imports and
            # routing policy each run; unrecognized symbols are reported explicitly.
            existing = {}
            if a.catalog.exists():
                for dll, symbol, value in re.findall(r'\{("[^"]*"), ("[^"]*"), ([01])\}', a.catalog.read_text()):
                    existing[(json.loads(dll), json.loads(symbol))] = int(value)
            existing.update({(r['dll'], r['symbol']): int(r['n_win32u']) > 0 for r in rows})
            with a.catalog.open('w') as f:
                f.write('// Generated by wine_gap.py; approximate Wine source metadata.\n')
                for (dll, symbol), win in sorted(existing.items()):
                    f.write('{%s, %s, %d},\n' % (json.dumps(dll), json.dumps(symbol), win))
        print(summary(rows))
        return 0
    except (OSError, ValueError, KeyError, TypeError, ImportError) as exc:
        print(f'wine_gap: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
