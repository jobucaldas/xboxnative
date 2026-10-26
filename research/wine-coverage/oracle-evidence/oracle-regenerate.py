import csv, hashlib, json, re, subprocess, sys
from pathlib import Path
import pefile
root=Path('/workspace/wt/wgap'); research=Path('/workspace/x64-run/research'); coverage=research/'wine-coverage'
sys.path.insert(0,str(root/'tools/xbox'))
import wine_gap as gap
import wine_graph as graph
source=Path('/workspace/megabonk-from-console')
paths=sorted(p for p in source.iterdir() if p.suffix.lower() in ('.exe','.dll'))
pairs=set(); second=set(); evidence=[]
for path in paths:
    out=subprocess.check_output(['llvm-readobj-19','--coff-imports',str(path)],text=True)
    evidence.append(out)
    dll=None
    for line in out.splitlines():
        m=re.match(r'\s*Name: (\S+)',line)
        if m: dll=m[1].lower()
        m=re.match(r'\s*Symbol:\s*(.*?)\s*\((\d+)\)\s*$',line)
        if m: pairs.add((dll,m[1].strip() or '#'+m[2]))
    pe=pefile.PE(str(path))
    for table in ('DIRECTORY_ENTRY_IMPORT','DIRECTORY_ENTRY_DELAY_IMPORT'):
        for entry in getattr(pe,table,[]):
            for imp in entry.imports:
                second.add((entry.dll.decode().lower(),'#'+str(imp.ordinal) if imp.import_by_ordinal else imp.name.decode()))
    pe.close()
assert pairs==second, f'PE reader mismatch: LLVM-only={pairs-second}; pefile-only={second-pairs}'
original=coverage/'megabonk-imports-vs-wine.csv'
archive=coverage/'megabonk-imports-vs-wine.v1-391rows.csv'
if not archive.exists(): archive.write_bytes(original.read_bytes())
old=list(csv.DictReader(archive.open())); assert len(old)==391
oldpairs={(r['dll'],r['symbol']) for r in old}
assert oldpairs<=pairs, f'Lost historical rows: {oldpairs-pairs}'
rows=gap.classify(sorted(pairs),json.loads((root/'tools/xbox/import_map.json').read_text())['dlls'],{p.name.lower() for p in paths},graph)
new={(r['dll'],r['symbol']):r for r in rows}
changes=[]
for r in old:
    n=new[(r['dll'],r['symbol'])]
    if r['verdict']==n['verdict']: continue
    if n['verdict']=='PE_NEEDS_UNIXLIB': reason='Wine Unix-call wrapper/body detected in expanded source graph'
    elif n['verdict']=='GRAPH_UNRESOLVED': reason='No parsed function body; old empty graph was incorrectly treated as PE_PURE'
    elif r['wine_class']!=n['wine_class']: reason='Corrected spec/ordinal/forwarder resolution'
    else: reason='Expanded Wine DLL graph resolves additional call dependencies: '+(n['nt_syscalls']+';'+n['win32u_syscalls']).strip(';')
    changes.append(f"| `{r['dll']}!{r['symbol']}` | {r['verdict']} | {n['verdict']} | {reason} |")
assert len(changes)==101, f'Expected 101 changed verdicts, got {len(changes)}'
with original.open('w',newline='') as f:
    w=csv.DictWriter(f,gap.FIELDS); w.writeheader(); w.writerows(rows)
run=Path('/workspace/x64-run/runs/wgap')
(run/'oracle-llvm.txt').write_text('\n'.join(evidence))
(run/'oracle-imports.json').write_text(json.dumps(sorted(pairs),indent=2)+'\n')
(run/'oracle-input-sha256.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p)+'\n' for p in paths))
note='''\n### wgap — authorised oracle correction, 2026-10-26\n\nWatcher decision authorises correction while retaining exact equality. Preserved\n`research/wine-coverage/megabonk-imports-vs-wine.v1-391rows.csv` byte-for-byte.\nRegenerated current oracle from `llvm-readobj-19 --coff-imports` on every top-level\nMegabonk EXE/DLL, including direct and delay imports; independent pefile traversal\n(not wine_gap.scan) agrees on all 628 pairs. All 391 historical pairs remain;\n237 real pairs were omitted by the old log-derived list. Wine classifications use\nthe corrected source graph and import_map policy. Sort: ascending Python tuple\n`(dll.lower(), symbol)`, case-sensitive symbol; csv.DictWriter FIELDS order, CRLF.\nNo route is claimed to be a measured Xbox export. Historical tables below/above\nremain historical. Exact byte equality with the corrected CSV is required.\nEvidence and reproducible generator: runs/wgap/oracle-{llvm.txt,imports.json,input-sha256.txt,regenerate.py}.\nThe 101 changed verdicts and individual reasons follow. Dependency lists are\nheuristic source findings, not proof of runtime closure.\n\n| Import | Historical verdict | Corrected verdict | Reason |\n|---|---|---|---|\n'''+ '\n'.join(changes)+'\n'
for p in [root/'LANE_NOTES.md',research/'WINE-BASE-REPORT.md']:
    with p.open('a') as f:f.write(note)
report=research/'WINE-BASE-REPORT.md'
s=report.read_text().replace('The original 391-row CSV is retained; exact equality acceptance remains failing.','The original 391-row CSV is archived unchanged as `megabonk-imports-vs-wine.v1-391rows.csv`. The current 628-row oracle is independently regenerated; see the correction ledger below.').replace('`research/wine-coverage/megabonk-imports-vs-wine.csv` (391 rows;', '`research/wine-coverage/megabonk-imports-vs-wine.v1-391rows.csv` (391 rows;')
report.write_text(s)
print(f'PASS LLVM == pefile: {len(pairs)}; historical retained: {len(oldpairs)}; changed verdicts: {len(changes)}')
