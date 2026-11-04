#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify source-built Wine donors and generate their direct/delay import table."""
import argparse
import hashlib
import json
from pathlib import Path
import re

DONORS = ("hid.dll", "setupapi.dll", "version.dll", "shcore.dll", "avrt.dll")


def checksums(directory: Path, source_url: str, source_sha256: str, patch_sha256=None) -> dict:
    source = (directory / "SOURCE.txt").read_text()
    if f"sha256 {source_sha256}" not in source or source_url not in source:
        raise ValueError("donors were not built from the selected Wine source pin")
    if patch_sha256 is not None:
        patch = directory / "0001-pe-rodata.patch"
        if (f"Patch: 0001-pe-rodata.patch sha256 {patch_sha256}" not in source
                or not patch.is_file()
                or hashlib.sha256(patch.read_bytes()).hexdigest() != patch_sha256):
            raise ValueError("donor source patch does not match the selected recipe")
    expected = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64}) [ *]([a-z0-9]+\.dll)", line)
        if not match or match[2] in expected:
            raise ValueError("invalid or duplicate donor checksum")
        expected[match[2]] = match[1]
    if set(expected) != set(DONORS):
        raise ValueError("checksum manifest must contain exactly the five Wine donors")
    for name, digest in expected.items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"{name}: SHA256 mismatch")
    return expected


def validate_load_config(pe, name):
    config = pe.OPTIONAL_HEADER.DATA_DIRECTORY[10]
    if not (config.VirtualAddress or config.Size):
        return
    section = pe.get_section_by_rva(config.VirtualAddress)
    data = pe.get_data(config.VirtualAddress, config.Size)
    if (config.Size != 0x140 or len(data) != 0x140
            or int.from_bytes(data[:4], "little") != 0x140 or any(data[4:])
            or pe.OPTIONAL_HEADER.DllCharacteristics & 0x4000
            or section is None or section.Characteristics & 0xA0000000
            or config.VirtualAddress - section.VirtualAddress + config.Size > section.SizeOfRawData):
        raise ValueError(f"{name}: unsupported or non-read-only Wine load configuration")


def imports(directory: Path) -> str:
    import pefile

    rows = ["// Generated from source-built Wine donors; direct and delay imports."]
    for name in DONORS:
        with pefile.PE(str(directory / name)) as pe:
            if pe.FILE_HEADER.Machine != 0x8664:
                raise ValueError(f"{name}: expected x64")
            validate_load_config(pe, name)
            count = 0
            for key in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
                for descriptor in getattr(pe, key, []):
                    for symbol in descriptor.imports:
                        rows.append("{%s,%s,%s,%d}," % (
                            json.dumps(name), json.dumps(descriptor.dll.decode()),
                            json.dumps(symbol.name.decode()) if symbol.name else "nullptr",
                            symbol.ordinal or 0))
                        count += 1
            print(f"{name}: hash verified, direct+delay imports={count}")
    return "\n".join(rows) + "\n"


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor-dir", type=Path, default=root / "out/wine-donors")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pins = [line.split() for line in (root / "deps.pin").read_text().splitlines()
            if line.startswith("wine-src ")]
    if len(pins) != 1 or len(pins[0]) != 3:
        parser.error("exactly one wine-src pin is required")
    _, url, sha = pins[0]
    patch_sha = hashlib.sha256((root / "patches/wine-src/0001-pe-rodata.patch").read_bytes()).hexdigest()
    checksums(args.donor_dir, url, sha, patch_sha)
    text = imports(args.donor_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == "__main__":
    main()
