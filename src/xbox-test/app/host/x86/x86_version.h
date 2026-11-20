// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Version-info parsing for the kernel32 GetFileVersionInfo*/VerQueryValue* routes.
// The VS_VERSION_INFO block comes from the image bytes of a file (PE32 or PE32+),
// and VerQueryValue walks the block the guest holds, following the documented tree.
namespace x86_version {
inline bool u16at(const std::vector<uint8_t>& b, size_t at, uint32_t& v) {
    if (at > b.size() || b.size() - at < 2) return false;
    v = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8;
    return true;
}
inline bool u32at(const std::vector<uint8_t>& b, size_t at, uint32_t& v) {
    if (at > b.size() || b.size() - at < 4) return false;
    v = uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
    return true;
}
inline size_t align4(size_t v) { return (v + 3) & ~size_t(3); }

// Locates the first RT_VERSION resource of an image. Errors are Win32 codes:
// 193 ERROR_BAD_EXE_FORMAT for a file that is not a PE image, 1813 ERROR_RESOURCE_TYPE_NOT_FOUND
// when the image carries no version resource.
inline bool find_version_resource(const std::vector<uint8_t>& image, uint32_t& offset, uint32_t& size, uint32_t& error) {
    constexpr uint32_t kBadFormat = 193, kTypeNotFound = 1813, kRtVersion = 16;
    uint32_t lfanew = 0, sig = 0, sections = 0, optional_size = 0, magic = 0, rva = 0, rsrc_size = 0;
    error = kBadFormat;
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z' || !u32at(image, 0x3c, lfanew)) return false;
    if (!u32at(image, lfanew, sig) || sig != 0x00004550u) return false;
    if (!u16at(image, size_t(lfanew) + 6, sections) || !u16at(image, size_t(lfanew) + 20, optional_size)) return false;
    const size_t opt = size_t(lfanew) + 24;
    if (!u16at(image, opt, magic)) return false;
    const size_t dirs = magic == 0x10bu ? 96 : magic == 0x20bu ? 112 : 0;  // data directories follow the optional header fields
    if (!dirs) return false;
    // Data directory 2 is the resource table: RVA then size.
    if (!u32at(image, opt + dirs + 16, rva) || !u32at(image, opt + dirs + 20, rsrc_size)) return false;
    const size_t table = opt + optional_size;
    auto rva_to_offset = [&](uint32_t target, uint32_t& at) {
        for (uint32_t i = 0; i < sections; ++i) {
            const size_t s = table + size_t(i) * 40;
            uint32_t vsize = 0, va = 0, raw_size = 0, raw = 0;
            if (!u32at(image, s + 8, vsize) || !u32at(image, s + 12, va) || !u32at(image, s + 16, raw_size) || !u32at(image, s + 20, raw)) return false;
            const uint32_t span = std::max(vsize, raw_size);
            if (target >= va && target - va < span) { at = raw + (target - va); return true; }
        }
        return false;
    };
    uint32_t base = 0;  // file offset of the resource table
    if (!rva_to_offset(rva, base)) { error = kTypeNotFound; return false; }
    // One directory level: the entry for `want` (an ID entry), or the first entry when `any` is set.
    // Subdirectory entries carry the high bit of the offset.
    auto entry = [&](uint32_t dir, uint32_t want, bool any, uint32_t& data, bool& subdir) {
        uint32_t named = 0, ids = 0;
        if (!u16at(image, size_t(base) + dir + 12, named) || !u16at(image, size_t(base) + dir + 14, ids)) return false;
        for (uint32_t i = 0; i < named + ids; ++i) {
            const size_t e = size_t(base) + dir + 16 + size_t(i) * 8;
            uint32_t name = 0, off = 0;
            if (!u32at(image, e, name) || !u32at(image, e + 4, off)) return false;
            if (any || (i >= named && name == want)) {
                data = off & 0x7fffffffu;
                subdir = (off & 0x80000000u) != 0;
                return true;
            }
        }
        return false;
    };
    uint32_t dir = 0, leaf = 0;
    bool sub = false;
    if (!entry(0, kRtVersion, false, dir, sub) || !sub) { error = kTypeNotFound; return false; }
    if (!entry(dir, 0, true, dir, sub) || !sub) { error = kTypeNotFound; return false; }
    if (!entry(dir, 0, true, leaf, sub) || sub) { error = kTypeNotFound; return false; }
    uint32_t data_rva = 0, data_size = 0, at = 0;
    if (!u32at(image, size_t(base) + leaf, data_rva) || !u32at(image, size_t(base) + leaf + 4, data_size)) { error = kTypeNotFound; return false; }
    if (!rva_to_offset(data_rva, at) || at > image.size() || image.size() - at < data_size) { error = kTypeNotFound; return false; }
    offset = at;
    size = data_size;
    error = 0;
    return true;
}

// Reads one VS_VERSIONINFO node: its end, key, value offset, and value length
// (bytes, or UTF-16 units when the node is text).
inline bool read_node(const std::vector<uint8_t>& b, size_t off, size_t& end, std::u16string& key, size_t& value, uint32_t& value_len, bool& text) {
    uint32_t len = 0, vlen = 0, type = 0;
    if (!u16at(b, off, len) || !u16at(b, off + 2, vlen) || !u16at(b, off + 4, type) || len < 6 || off + len > b.size()) return false;
    end = off + len;
    size_t p = off + 6;
    key.clear();
    for (;;) {
        uint32_t c = 0;
        if (p >= end || !u16at(b, p, c)) return false;
        p += 2;
        if (!c) break;
        key.push_back(char16_t(c));
    }
    value = align4(p);
    value_len = vlen;
    text = type == 1;
    return true;
}
inline char16_t fold(char16_t c) { return c >= u'A' && c <= u'Z' ? char16_t(c + (u'a' - u'A')) : c; }
inline bool same_key(const std::u16string& a, const std::u16string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (fold(a[i]) != fold(b[i])) return false;
    return true;
}

// Resolves a VerQueryValue sub-block ("\\", "\\VarFileInfo\\Translation",
// "\\StringFileInfo\\040904b0\\ProductName"). Key matching is case-insensitive.
// On success the value's offset in the block and its length (bytes, or UTF-16 units for text) are returned.
inline bool query(const std::vector<uint8_t>& b, const std::u16string& sub, size_t& value, uint32_t& value_len, bool& text) {
    size_t end = 0, val = 0;
    std::u16string key;
    uint32_t vlen = 0;
    bool txt = false;
    if (!read_node(b, 0, end, key, val, vlen, txt)) return false;
    size_t bound = end;
    std::vector<std::u16string> parts;
    for (size_t start = 0; start <= sub.size();) {
        size_t next = sub.find(u'\\', start);
        if (next == std::u16string::npos) next = sub.size();
        if (next > start) parts.push_back(sub.substr(start, next - start));
        start = next + 1;
    }
    for (const auto& part : parts) {
        size_t child = align4(val + (txt ? size_t(vlen) * 2 : size_t(vlen)));
        bool found = false;
        while (child + 6 <= bound) {
            size_t cend = 0, cval = 0;
            std::u16string k;
            uint32_t cvlen = 0;
            bool ctxt = false;
            if (!read_node(b, child, cend, k, cval, cvlen, ctxt) || cend > bound) return false;
            if (same_key(k, part)) {
                end = cend;
                val = cval;
                vlen = cvlen;
                txt = ctxt;
                found = true;
                break;
            }
            child = align4(cend);
        }
        if (!found) return false;
        bound = end;
    }
    if (val > b.size() || b.size() - val < (txt ? size_t(vlen) * 2 : size_t(vlen))) return false;
    value = val;
    value_len = vlen;
    text = txt;
    return true;
}
}
