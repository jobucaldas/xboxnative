"""Shared helpers for build-kit.sh and verify-kit.sh. Not a standalone tool."""
import hashlib
import json
import sys
import xml.etree.ElementTree as ET

NS = {"a": "http://schemas.microsoft.com/appx/manifest/foundation/windows10"}


def read_identity(manifest_path):
    """Return (Name, Version) from an AppxManifest.xml's Identity element."""
    root = ET.parse(manifest_path).getroot()
    ident = root.find("a:Identity", NS)
    if ident is None:
        # Namespace URI occasionally differs; fall back to a tag-name match.
        for el in root.iter():
            if el.tag.endswith("}Identity") or el.tag == "Identity":
                ident = el
                break
    if ident is None:
        raise ValueError(f"no Identity element in {manifest_path}")
    return ident.get("Name"), ident.get("Version")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


if __name__ == "__main__":
    # CLI glue so bash can call this file directly instead of importing it.
    cmd = sys.argv[1]
    if cmd == "identity":
        name, version = read_identity(sys.argv[2])
        print(f"{name}\t{version}")
    elif cmd == "sha256":
        print(sha256_file(sys.argv[2]))
    elif cmd == "write-manifest":
        # write-manifest OUT.json GENERATED_AT COMMIT BRANCH  <  TSV on stdin:
        # name\tidentity\tversion\tfile\tsha256\tkind  (kind: package|extra)
        out_path, generated_at, commit, branch = sys.argv[2:6]
        packages, extras = [], []
        for line in sys.stdin:
            line = line.rstrip("\n")
            if not line:
                continue
            name, identity, version, filename, digest, kind = line.split("\t")
            entry = {"name": name, "file": filename, "sha256": digest}
            if kind == "package":
                entry["identity"] = identity
                entry["version"] = version
                packages.append(entry)
            else:
                extras.append(entry)
        doc = {
            "generated_at": generated_at,
            "source_commit": commit,
            "source_branch": branch,
            "packages": packages,
            "extras": extras,
        }
        with open(out_path, "w") as f:
            json.dump(doc, f, indent=2, sort_keys=False)
            f.write("\n")
    else:
        sys.exit(f"unknown command: {cmd}")
