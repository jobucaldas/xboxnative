`wine_gap.py` reads PE32 and PE32+ imports (including delay loads), deduplicates
DLL/symbol pairs and writes Wine source classifications. It never executes PEs.
Requires Python 3 and `pefile` (`python3 -m pip install pefile`).

Example:

```sh
python3 tools/xbox/wine_gap.py /path/to/game --map tools/xbox/import_map.json \
  --wine-source /path/to/wine --csv /tmp/game-gap.csv
```

Default scope is every top-level `.exe` and `.dll`. To restrict scope, repeat
`--pe relative/path.exe`. Modules supplied in that list have route `game`.
Other routes come from the input map; absent entries are `unclassified`.

`GAP: total=N missing=M win32u=W` counts unique imports. `missing` counts planned
non-system routes, excluding supplied game modules. It is not the mapper's count
of failed GetProcAddress calls or evidence that a system export works on Xbox.
`win32u` counts imports whose approximate Wine graph reaches NtUser/NtGdi.
CSV includes the explicit Nt and win32u syscall lists.

`--catalog xbox-test/app/wine_gap_catalog.h` exports Wine metadata for the launcher.
Repeated runs merge catalogs by DLL/symbol when supporting multiple games. The launcher scans
actual installed top-level PEs on every Play, even when the mapper rejects PE32.
It reports `GAP-METADATA: unknown=N` for imports absent from that catalog. Unknown
imports contribute to total/missing; their win32u needs are unknown. Regenerate
metadata when game binaries or Wine change; do not interpret unknown=0 as proof
of complete semantic coverage. The checked-in catalog is Wine 11.0 source data.

The regex graph is a prioritization aid. It misses indirect calls, preprocessor
variants, generated functions, runtime DLL loads and dependencies outside its
search rules. `PE_PURE` means no syscall found by this walk, not proof of portability.
`GRAPH_UNRESOLVED` means no function body found. `PE_NEEDS_UNIXLIB` takes precedence
when a known Wine Unix-call wrapper is found. Needed Nt* and NtUser* sets remain
available even for that verdict. Wine export presence is distinct from behavior.

The historical 391-row Megabonk research CSV omits actual PE imports. The current
three launcher targets have 512 direct pairs; all top-level PEs plus delay loads
have 628. Do not remove real imports to force equality with the historical subset.

The corrected acceptance oracle is `research/wine-coverage/megabonk-imports-vs-wine.csv`
under `/workspace/x64-run`. Its inputs come from LLVM, independently cross-checked
with pefile. Historical 391 rows remain unchanged in the `.v1-391rows.csv` archive.
Both current CSVs sort by `(lowercase DLL, case-sensitive symbol)` with the same
FIELDS column order and CRLF CSV output. Acceptance requires byte-for-byte equality.
The report and LANE_NOTES record each of the 101 corrected historical verdicts.
