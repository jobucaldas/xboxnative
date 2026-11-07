// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include "steam_wire.h"

// Valve's own signed steamclient64.dll, for games whose Steam DRM wrapper
// verifies the client's signature (see play_runtime signedClientModule). It is
// never shipped: the launcher reads Valve's public Windows client manifest (the
// same unauthenticated one the Steam bootstrapper uses), downloads the package
// that carries the DLL, checks it against the SHA-256 Valve publishes, and
// keeps only steamclient64.dll after confirming Valve's signature block.
namespace steam_client_package {
constexpr const char* host = "media.steampowered.com";
constexpr const char* manifest_path = "/client/steam_client_win64";
constexpr const char* package = "bins_win64";
constexpr const char* member = "steamclient64.dll";

struct Entry { std::string file, sha256; };
inline bool hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
// The package's zip name and the SHA-256 of that zip ("sha2"; "sha2vz" is the
// LZMA variant, not used). The name must be a plain file beside the manifest.
inline bool manifest_entry(const std::string& text, Entry& out) {
    out = {};
    Vdf root;
    if (!vdf_parse(text, root)) return false;
    // The platform block ("win64"), beside Valve's manifest signature blocks.
    const Vdf* top = nullptr;
    for (auto& kid : root.kids) if (kid.obj && kid.get(package)) { top = &kid; break; }
    if (!top) return false;
    out.file = top->str({package, "file"});
    out.sha256 = top->str({package, "sha2"});
    if (out.file.empty() || out.file.find_first_of("/\\:") != std::string::npos || out.file[0] == '.') return false;
    return hex64(out.sha256);
}
inline uint32_t le32(const std::string& z, size_t at) { uint32_t v; memcpy(&v, z.data() + at, 4); return v; }
inline uint16_t le16(const std::string& z, size_t at) { uint16_t v; memcpy(&v, z.data() + at, 2); return v; }
// Raw-deflate inflater: (compressed, expected size, out) -> ok.
using Inflate = std::function<bool(const char*, size_t, size_t, std::string&)>;
// One member of a (non-zip64) archive, by exact name in the central directory.
inline bool zip_member(const std::string& z, const char* name, const Inflate& inflate, std::string& out) {
    out.clear();
    if (z.size() < 22) return false;
    size_t end = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > 0;) {
        if (le32(z, i) == 0x06054b50) { end = i; break; }
        if (z.size() - i > 70000) break;
    }
    if (end == std::string::npos) return false;
    size_t count = le16(z, end + 10), at = le32(z, end + 16);
    const size_t nameLength = strlen(name);
    for (size_t n = 0; n < count; ++n) {
        if (at + 46 > z.size() || le32(z, at) != 0x02014b50) return false;
        uint16_t method = le16(z, at + 10), nl = le16(z, at + 28), xl = le16(z, at + 30), cl = le16(z, at + 32);
        uint32_t csize = le32(z, at + 20), usize = le32(z, at + 24), local = le32(z, at + 42);
        if (at + 46 + nl > z.size()) return false;
        if (nl == nameLength && !memcmp(z.data() + at + 46, name, nl)) {
            if (csize == 0xffffffffu || usize == 0xffffffffu || local == 0xffffffffu) return false;   // zip64: not expected
            if (size_t(local) + 30 > z.size() || le32(z, local) != 0x04034b50) return false;
            size_t data = size_t(local) + 30 + le16(z, local + 26) + le16(z, local + 28);
            if (data > z.size() || csize > z.size() - data) return false;
            if (method == 0) { if (csize != usize) return false; out.assign(z.data() + data, csize); return true; }
            return method == 8 && inflate(z.data() + data, csize, usize, out) && out.size() == usize;
        }
        at += 46 + nl + xl + cl;
    }
    return false;
}
// Valve's signature block ("VLV\0", format 1, signed length within the file)
// at DOS-header offset 0x40: the block Steam's DRM verifies. This only checks
// that the block is present; the DRM itself verifies the signature.
inline bool valve_signature_block(const std::string& dll) {
    if (dll.size() < 0x200 || dll[0] != 'M' || dll[1] != 'Z') return false;
    if (memcmp(dll.data() + 0x40, "VLV\0", 4) || le32(dll, 0x44) != 1) return false;
    uint32_t signedLength = le32(dll, 0x48);
    return signedLength > 0x200 && signedLength <= dll.size();
}
}
