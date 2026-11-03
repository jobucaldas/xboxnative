# Load-config diagnosis

`load_config.py` explains, field by field, why the x64 mapper blocks a PE's load
configuration (data directory 10). It uses only the Python standard library and
reads each file as untrusted bytes. It never loads, executes or modifies the file.
Inputs must be regular files (including symlinks to regular files). The tool opens
once, checks that descriptor's type and size, and reads at most 512 MiB plus one
byte to enforce the limit even if a file grows. Devices and FIFOs are rejected;
opening a FIFO does not wait for a writer. Raw-backed ranges are memoryviews of
that bounded input, not additional record-sized copies. Virtual-section overlap
checks sort nonempty half-open ranges, so validation does not compare every
section pair; adjacent ranges and empty sections do not overlap.

```sh
python3 -I src/tools/xbox/load_config.py /path/to/game/*.exe /path/to/game/*.dll
python3 -I -m unittest discover -s src/tools/xbox/tests -p 'test_load_config.py'
```

For each image the report shows:

- the directory size, the record's declared size, and its raw and virtual bounds
- the security cookie's target section, and whether it still holds the MSVC
  default value
- `GuardFlags` and the `GUARD_CF` DLL characteristic; the function-table stride is
  four bytes for each RVA plus the extra bytes encoded in the high nibble
- every nonzero field, with each VA field's direct target section (a target can
  be metadata, a cookie or a function-pointer slot; it is not dereferenced)
- the exact count of base relocations overlapping the record (including repeated
  entries and straddling DIR64 writes), with at most the first 16 entries shown
  and an explicit omitted count; the entire table is still checked for malformed
  blocks; HIGHADJ's second WORD is consumed as an addend, not counted as a
  relocation, and a missing in-block addend is malformed

Delay-import (13) and CLR (14) directories get their own lines. Only unmapped
loads reject those two with the generic gate, so they are a separate case from
the load-config block on Play.

Exit status is 0 when no load configuration is present, 1 when one is reported as
BLOCKED, and 2 on a malformed or unreadable input. Malformed includes load-config
directories outside SizeOfImage, outside a single section, not fully raw-backed,
or too short for Size, and declared Size values below four or beyond the
directory. With multiple files, the highest status wins.

`inert Wine file-byte candidate: yes` describes only the record's file-byte shape,
DLL characteristics and section permissions, not donor mapper acceptance. The
tool does not validate loader writes: both mappers reject overlapping DIR64
writes (including boundary straddles), and imports, delay imports and TLS may
also write into an otherwise inert-looking record. Other mapper checks still
apply. Play still uses `LoadConfig::reject`; every well-formed present record is
BLOCKED. The report does not certify a record, and it does not claim that an
image can run. Supporting a real MSVC record means
implementing what the record asks for (cookie initialization, CFG/XFG, volatile
metadata), not ignoring or stripping it. The report prints only file base names.
Keep reports from proprietary inputs private unless they have been reviewed.
