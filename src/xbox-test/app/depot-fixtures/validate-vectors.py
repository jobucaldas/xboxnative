#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate synthetic depot hash vector data only; no downloader is exercised."""
import hashlib
import json
import re
import sys
from pathlib import Path

SCHEMA = "xboxnative.synthetic-depot-hash-cases.v1"
EXPECTED = {"valid-matching-digest", "mutated-bytes", "missing-hash", "empty-hash"}


def validate(path: Path) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != SCHEMA:
        raise ValueError("unexpected schema")
    cases = data.get("cases")
    if not isinstance(cases, list) or {c.get("id") for c in cases} != EXPECTED:
        raise ValueError("case IDs do not match required vector set")
    if len(cases) != len(EXPECTED):
        raise ValueError("duplicate case ID")
    for case in cases:
        raw = case.get("bytes_hex")
        if not isinstance(raw, str) or len(raw) % 2 or not re.fullmatch(r"[0-9a-fA-F]*", raw):
            raise ValueError(f"{case['id']}: invalid hex bytes")
        digest = case.get("declared_sha1")
        if digest is not None and (not isinstance(digest, str) or (digest and not re.fullmatch(r"[0-9a-fA-F]{40}", digest))):
            raise ValueError(f"{case['id']}: malformed SHA1")
        actual = hashlib.sha1(bytes.fromhex(raw)).hexdigest()
        ident, expected = case["id"], case.get("expected")
        if ident == "valid-matching-digest" and (expected != "accept_file" or digest != actual):
            raise ValueError("valid vector must match computed digest and accept")
        if ident == "mutated-bytes" and (expected != "reject_file_no_marker_state_failed" or digest == actual):
            raise ValueError("mutated vector must reject and differ from computed digest")
        if ident in {"missing-hash", "empty-hash"} and (expected != "reject_file_no_marker_state_failed" or digest not in (None, "")):
            raise ValueError(f"{ident}: must specify desired fail-closed rejection")
    print(f"PASS validated {len(cases)} synthetic vectors (data validation only)")


if __name__ == "__main__":
    try:
        validate(Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).with_name("manifest-cases.json")))
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print(f"FAIL vector validation: {exc}", file=sys.stderr)
        sys.exit(1)
