#!/usr/bin/env python3
"""Compare Megabonk import inventory with this worktree's explicit routes.

Native export availability and AppContainer behavior require separate console
proof. This inventory deliberately does not equate routing with implementation.
"""
import argparse
import csv
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[3]
OWNED = {
    'advapi32.dll', 'crypt32.dll', 'ole32.dll', 'oleaut32.dll', 'version.dll',
    'dbghelp.dll', 'shlwapi.dll', 'setupapi.dll', 'iphlpapi.dll', 'ws2_32.dll',
    'wininet.dll', 'winhttp.dll', 'winmm.dll', 'imm32.dll', 'hid.dll',
    'dwmapi.dll', 'baselib.dll', 'bcrypt.dll',
}
FIELDS = ['dll', 'symbol', 'console_route', 'wine_class', 'verdict',
          'tree_route', 'status', 'priority', 'evidence']


def audit(source):
    routes = dict(re.findall(r'\{"([^"]+)", "([^"]+)"\}',
        (ROOT / 'src/xbox-test/app/host/mlp_routes.h').read_text()))
    tail = set(re.findall(r'\{"([^"]+)","([^"]+)"\}',
        (ROOT / 'src/xbox-test/shared/pe_tail.h').read_text()))
    imm = set(re.findall(r'!strcmp\(name, "([^"]+)"\)',
        (ROOT / 'src/xbox-test/uwph/imm32/route.h').read_text()))
    native = set(re.findall(r'\{"([^"]+)", "([^"]+)"\}',
        (ROOT / 'src/xbox-test/uwph/sysx/native.h').read_text()))
    result = []
    for row in csv.DictReader(source):
        dll, symbol = row['dll'], row['symbol']
        if dll not in OWNED and not dll.startswith('api-ms-win-core-winrt-'):
            continue
        kind = routes.get(dll, 'unclassified')
        status, evidence = 'missing', 'No production builtin or native allowlist entry'
        if kind == 'system':
            status = 'native-route'
            evidence = 'System32/API-set lookup; AppContainer operation semantics not certified'
        if kind == 'uwph':
            status = 'game-asset'
            evidence = 'Unmodified game-supplied baselib; loader owned by prun'
        if (dll, symbol) in tail and dll != 'imm32.dll':
            kind = 'native-tail'
            status, evidence = 'native-tail', 'pe_tail explicit native allowlist; console semantics unverified'
        if (dll, symbol) in native:
            kind = 'native-allowlist'
            status = 'native-export-unverified'
            evidence = 'uwph/sysx/native.h; operation regression passes on Wine; Xbox export/API availability pending'
        if dll == 'imm32.dll' and symbol in imm:
            kind = 'record-builtin'
            status = 'no-context-builtin'
            evidence = 'uwph/imm32/route.h; Wine differential and guest-record host regression'
        if dll == 'dwmapi.dll' and symbol in ('DwmGetWindowAttribute', 'DwmSetWindowAttribute'):
            kind = 'record-builtin'
            status = 'record-builtin-partial'
            evidence = ('uwph/dwmapi/route.h; physical frame bounds use validated guest records; '
                        'DWM policy changes unsupported; host regression passes, Xbox pending')
        priority = '1' if dll in ('imm32.dll', 'dwmapi.dll') else '2' if dll == 'dbghelp.dll' else '3'
        if dll == 'winmm.dll' and not symbol.startswith('time'):
            priority = 'deferred-audio'
        result.append(dict(zip(FIELDS, [dll, symbol, row['console_route'],
            row['wine_class'], row['verdict'], kind, status, priority, evidence])))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=pathlib.Path,
        default=pathlib.Path(os.environ.get('RUNS_ROOT', pathlib.Path(__file__).resolve().parents[3] / 'out')) / 'research/u32m-wave-gap.csv')
    args = parser.parse_args()
    with args.source.open(newline='') as source:
        rows = audit(source)
    todo = [row for row in rows if row['status'] in ('missing', 'native-export-unverified', 'record-builtin-partial')]
    for name, selected in [('sysx-route-audit.csv', rows), ('sysx-todo.csv', todo)]:
        with (ROOT / 'research' / name).open('w', newline='') as output:
            writer = csv.DictWriter(output, FIELDS)
            writer.writeheader()
            writer.writerows(selected)
    print(f'{len(rows)} owned imports; {len(todo)} missing or Xbox-unverified; '
          f'{sum(r["priority"] == "deferred-audio" for r in todo)} deferred audio')


if __name__ == '__main__':
    main()
