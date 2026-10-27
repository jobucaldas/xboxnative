#!/usr/bin/env python3
"""Cross-check real audio imports with LLVM, pefile, saved evidence and routes.

This checks static imports only; it never claims native export availability or
infers a selected FMOD backend from an import list.
"""
import argparse
import csv
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/xbox'))
from wine_gap import scan


def audio(dll):
    return (dll in {'winmm.dll', 'ole32.dll', 'mmdevapi.dll', 'dsound.dll',
                    'audioses.dll', 'avrt.dll'} or dll.startswith('xaudio2_'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    args = parser.parse_args()
    reader = next((p for name in ['llvm-readobj'] +
                   [f'llvm-readobj-{n}' for n in range(30, 13, -1)]
                   if (p := shutil.which(name))), None)
    if not reader:
        parser.error('llvm-readobj required')
    images = sorted(p for p in args.game.iterdir()
                    if p.suffix.lower() in {'.exe', '.dll'})
    if not images:
        parser.error('no game PEs found')
    actual = set()
    for image in images:
        pairs = {(dll, symbol) for dll, symbol in scan([image]) if audio(dll)}
        dump = subprocess.run([reader, '--coff-imports', str(image)],
                              capture_output=True, text=True, check=True).stdout
        independent = set()
        for block in re.findall(r'(?:Delay)?Import \{(.*?)\n\}', dump, re.S):
            dll = re.search(r'^\s*Name: (\S+)', block, re.M).group(1).lower()
            if audio(dll):
                for symbol, ordinal in re.findall(r'^\s*Symbol: (.*?) \((\d+)\)$', block, re.M):
                    independent.add((dll, symbol if symbol else '#' + ordinal))
        if independent != pairs:
            raise SystemExit(f'FAIL: LLVM/pefile mismatch for {image.name}')
        actual.update((image.name, dll, symbol) for dll, symbol in pairs)
    with (ROOT / 'runs/aud/audio-imports.csv').open() as stream:
        saved = {(r['image'], r['dll'], r['symbol']) for r in csv.DictReader(stream)}
    if actual != saved:
        raise SystemExit(f'FAIL: saved imports differ: added={sorted(actual-saved)} removed={sorted(saved-actual)}')
    source = (ROOT / 'xbox-test/audio/routes.h').read_text()
    for dll, array in [('winmm.dll', 'wave_symbols'), ('ole32.dll', 'com_symbols')]:
        body = re.search(rf'{array}\[\]=\{{(.*?)\}};', source, re.S).group(1)
        declared = set(re.findall(r'"([^"]+)"', body))
        observed = {symbol for _, module, symbol in actual if module == dll}
        if declared != observed:
            raise SystemExit(f'FAIL: {dll} route list differs from real imports')
        print(f'PASS: {dll} {len(observed)} unique symbols match route diagnostics')
    print(f'PASS: {len(images)} PEs; LLVM/pefile/saved audio imports agree; no execution claim')


if __name__ == '__main__':
    main()
