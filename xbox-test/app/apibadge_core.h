// Graphics-API badge, portable core (std + stdio only; unit-tested off-device by tests/apibadge_test.cpp).
//  - pe_dlls(): DLL names from the import + delay-import tables of one PE32/PE32+ file. Seeks, never loads the whole file; never maps/executes anything.
//  - classify_dlls(): d3d11.dll => D3D11; d3d12.dll => D3D12 (not supported yet); dxgi.dll alone => D3D11; d3d9/d3d8/ddraw/d3d10*/opengl32 only => LEGACY; else NONE.
//  - classify_store_text(): Steam store pc_requirements text ("DirectX Version 11") => same kinds, or UNKNOWN when the text says nothing.
#pragma once
#include <cstdio>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
namespace apib {
enum Kind { K_UNKNOWN = 0, K_NONE = 1, K_D3D11 = 2, K_D3D12 = 3, K_LEGACY = 4 };
static inline const char* kind_name(int k) { return k == K_D3D11 ? "D3D11" : k == K_D3D12 ? "D3D12" : k == K_LEGACY ? "LEGACY" : k == K_NONE ? "NONE" : "UNKNOWN"; }
namespace detail {
static inline uint32_t u32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t u16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline bool rd(FILE* f, uint64_t off, void* buf, size_t n) {
    if (fseek(f, 0, SEEK_SET) != 0) return false;
#if defined(_WIN32)
    if (_fseeki64(f, (long long)off, SEEK_SET) != 0) return false;
#else
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return false;
#endif
    return fread(buf, 1, n, f) == n;
}
}
// Returns false when the file is not a readable PE (out untouched in that case beyond names already added).
static inline bool pe_dlls(FILE* f, std::set<std::string>& out) {
    using namespace detail;
    unsigned char h[4096]; if (fseek(f, 0, SEEK_SET)) return false; size_t n = fread(h, 1, sizeof h, f);
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return false;
    uint32_t nt = u32(h + 0x3c); if (nt > n || nt + 24 > n || h[nt] != 'P' || h[nt + 1] != 'E' || h[nt + 2] || h[nt + 3]) return false;
    unsigned nsec = u16(h + nt + 6), osz = u16(h + nt + 20); size_t opt = nt + 24; if (opt + osz > n || osz < 96) return false;
    unsigned magic = u16(h + opt); if (magic != 0x10b && magic != 0x20b) return false;
    bool wide = magic == 0x20b; size_t sec = opt + osz; if (nsec == 0 || nsec > 96 || sec + (size_t)nsec * 40 > n) return false;
    uint64_t base = wide ? ((uint64_t)u32(h + opt + 28) << 32 | u32(h + opt + 24)) : u32(h + opt + 28);
    size_t dirs = opt + (wide ? 112 : 96); uint32_t nd = u32(h + opt + (wide ? 108 : 92));
    auto r2o = [&](uint32_t rva, uint64_t& off) {
        for (unsigned i = 0; i < nsec; i++) { const unsigned char* s = h + sec + 40 * i; uint32_t vs = u32(s + 8), va = u32(s + 12), rs = u32(s + 16), rp = u32(s + 20);
            uint32_t span = vs > rs ? vs : rs; if (rva >= va && rva - va < span) { if (rva - va >= rs) return false; off = (uint64_t)rp + (rva - va); return true; } }
        uint32_t first = u32(h + sec + 12); if (rva < first) { off = rva; return true; } return false; };
    auto name_at = [&](uint32_t rva, std::string& s) {
        uint64_t off; if (!rva || !r2o(rva, off)) return false; char b[96] = {};
        if (fseek(f, 0, SEEK_SET)) return false;
#if defined(_WIN32)
        if (_fseeki64(f, (long long)off, SEEK_SET)) return false;
#else
        if (fseeko(f, (off_t)off, SEEK_SET)) return false;
#endif
        size_t got = fread(b, 1, sizeof b - 1, f); if (!got) return false; b[got] = 0; s.assign(b);
        if (s.empty() || s.size() > 64) return false;
        for (auto& c : s) { if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if ((unsigned char)c < 32 || (unsigned char)c > 126) return false; }
        return true; };
    for (uint32_t idx : {1u, 13u}) {
        if (nd <= idx || dirs + idx * 8 + 8 > sec) continue;
        uint32_t rva = u32(h + dirs + idx * 8), size = u32(h + dirs + idx * 8 + 4); if (!rva || !size) continue;
        bool delay = idx == 13; unsigned stride = delay ? 32 : 20; unsigned cnt = size / stride; if (cnt > 512) cnt = 512; if (!cnt) cnt = 512;
        for (unsigned i = 0; i < cnt; i++) {
            uint64_t off; if (!r2o(rva + i * stride, off)) break; unsigned char d[32] = {}; if (!rd(f, off, d, stride)) break;
            uint32_t nm = u32(d + (delay ? 4 : 12)); if (!nm) break;
            if (delay && !(u32(d) & 1)) nm = (uint32_t)(nm - base);
            std::string s; if (name_at(nm, s)) out.insert(s);
        }
    }
    return true;
}
static inline bool is_vendor_gfx_name(const std::string& lower) {   // a shipped d3d*.dll/dxgi.dll is a replacement implementation, not evidence of what the game imports
    static const char* n[] = {"d3d8.dll", "d3d9.dll", "d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "d3d11.dll", "d3d12.dll", "d3d12core.dll", "dxgi.dll", "ddraw.dll", "opengl32.dll", "dxcompiler.dll", "dxil.dll"};
    for (auto s : n) if (lower == s) return true; return false; }
static inline int classify_dlls(const std::set<std::string>& d) {
    auto has = [&](const char* s) { return d.count(s) > 0; };
    if (has("d3d11.dll")) return K_D3D11;
    if (has("d3d12.dll")) return K_D3D12;
    if (has("dxgi.dll")) return K_D3D11;
    if (has("d3d9.dll") || has("d3d8.dll") || has("ddraw.dll") || has("d3d10.dll") || has("d3d10_1.dll") || has("d3d10core.dll") || has("opengl32.dll")) return K_LEGACY;
    return K_NONE;
}
static inline std::string gfx_evidence(const std::set<std::string>& d) {
    std::string s; for (auto n : {"d3d11.dll", "d3d12.dll", "dxgi.dll", "d3d9.dll", "d3d8.dll", "ddraw.dll", "d3d10.dll", "d3d10_1.dll", "d3d10core.dll", "opengl32.dll"}) if (d.count(n)) { if (!s.empty()) s += ", "; s += n; } return s; }
// Steam store requirement text (HTML tags already irrelevant): every "DirectX ... <version>" mention. 11 => D3D11, >=12 => D3D12, 8..10 => LEGACY, none => UNKNOWN.
static inline int classify_store_text(const std::string& text, std::string* note = nullptr) {
    std::string l; for (char c : text) l += (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    bool v11 = false, v12 = false, vold = false; std::string seen;
    for (size_t p = l.find("directx"); p != std::string::npos; p = l.find("directx", p + 7)) {
        size_t i = p + 7; int skip = 0;   // allow "version", ":", spaces, tags, "(R)" within a short window
        while (i < l.size() && skip < 40 && !(l[i] >= '0' && l[i] <= '9')) { i++; skip++; }
        if (i >= l.size() || !(l[i] >= '0' && l[i] <= '9') || skip >= 40) continue;
        int v = 0; size_t j = i; while (j < l.size() && l[j] >= '0' && l[j] <= '9' && j < i + 3) v = v * 10 + (l[j++] - '0');
        if (v == 11) v11 = true; else if (v >= 12 && v <= 13) v12 = true; else if (v >= 7 && v <= 10) vold = true; else continue;
        if (!seen.empty()) seen += "/"; seen += std::to_string(v);
    }
    if (note) *note = seen.empty() ? "" : "DirectX " + seen;
    if (v11) return K_D3D11; if (v12) return K_D3D12; if (vold) return K_LEGACY; return K_UNKNOWN;
}
}
