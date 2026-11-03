# Load-config diagnosis

`load_config.py` explains, field by field, why the x64 mapper blocks a PE's load
configuration (data directory 10). It uses only the Python standard library and
reads each file as untrusted bytes. It never loads, executes or modifies the file.

```sh
python3 -I src/tools/xbox/load_config.py /path/to/game/*.exe /path/to/game/*.dll
python3 -I -m unittest discover -s src/tools/xbox/tests -p 'test_load_config.py'
```

For each image the report shows:

- the directory size, the record's declared size, and its raw and virtual bounds
- the security cookie's target section, and whether it still holds the MSVC
  default value
- `GuardFlags` and the `GUARD_CF` DLL characteristic
- every nonzero field, with each VA field's direct target section (a target can
  be metadata, a cookie or a function-pointer slot; it is not dereferenced)
- the base relocations overlapping the record, including straddling DIR64 writes

Delay-import (13) and CLR (14) directories get their own lines. Only unmapped
loads reject those two with the generic gate, so they are a separate case from
the load-config block on Play.

Exit status is 0 when no load configuration is present, 1 when one is reported as
BLOCKED, and 2 on a malformed or unreadable input. `inert Wine record: yes` means
only the donor loader's `LoadConfig::inert_wine` opt-in would accept the record.
Play still uses `LoadConfig::reject`. The report does not certify a record, and it
does not claim that an image can run. Supporting a real MSVC record means
implementing what the record asks for (cookie initialization, CFG/XFG, volatile
metadata), not ignoring or stripping it. The report prints only file base names.
Keep reports from proprietary inputs private unless they have been reviewed.
