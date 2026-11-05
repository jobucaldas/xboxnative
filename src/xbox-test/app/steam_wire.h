// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Portable Steam wire helpers shared by the launcher and its native tests:
// minimal protobuf encode/parse and the PICS appinfo VDF text parser.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// ---------------------------------------------------------------- minimal protobuf
inline void pb_varint(std::string& o, uint64_t v) { while (v >= 0x80) { o += (char)(v | 0x80); v >>= 7; } o += (char)v; }
inline void pb_u(std::string& o, int f, uint64_t v) { pb_varint(o, (uint64_t)f << 3); pb_varint(o, v); }
inline void pb_s(std::string& o, int f, const std::string& s) { pb_varint(o, ((uint64_t)f << 3) | 2); pb_varint(o, s.size()); o += s; }
inline void pb_fixed64(std::string& o, int f, uint64_t v) { pb_varint(o, ((uint64_t)f << 3) | 1); for (int i = 0; i < 8; i++) o += (char)(v >> (8 * i)); }
struct PBField { int f, wt; uint64_t u; std::string s; };
inline std::vector<PBField> pb_parse(const std::string& d) {
    std::vector<PBField> out; size_t i = 0;
    auto rv = [&](uint64_t& v) { v = 0; int sh = 0; while (i < d.size()) { uint8_t b = d[i++]; v |= (uint64_t)(b & 0x7f) << sh; if (!(b & 0x80)) return true; sh += 7; } return false; };
    while (i < d.size()) {
        uint64_t tag; if (!rv(tag)) break; PBField p{(int)(tag >> 3), (int)(tag & 7), 0, {}};
        if (p.wt == 0) { if (!rv(p.u)) break; }
        else if (p.wt == 2) { uint64_t n; if (!rv(n) || i + n > d.size()) break; p.s = d.substr(i, (size_t)n); i += (size_t)n; }
        else if (p.wt == 1) { if (i + 8 > d.size()) break; memcpy(&p.u, &d[i], 8); i += 8; }
        else if (p.wt == 5) { if (i + 4 > d.size()) break; uint32_t v; memcpy(&v, &d[i], 4); p.u = v; i += 4; }
        else break;
        out.push_back(std::move(p));
    }
    return out;
}
inline const PBField* pb_get(const std::vector<PBField>& v, int f) { for (auto& x : v) if (x.f == f) return &x; return nullptr; }

// ---------------------------------------------------------------- VDF text parser (PICS appinfo buffer)
struct Vdf {
    std::string val; bool obj = false; std::vector<std::string> keys; std::vector<Vdf> kids;
    const Vdf* get(const std::string& k) const { for (size_t i = 0; i < keys.size(); i++) if (keys[i] == k) return &kids[i]; return nullptr; }
    const Vdf* at(std::initializer_list<const char*> p) const { const Vdf* c = this; for (auto k : p) { if (!c) return nullptr; c = c->get(k); } return c; }
    std::string str(std::initializer_list<const char*> p, const std::string& d = "") const { const Vdf* c = at(p); return c && !c->obj ? c->val : d; }
};
inline bool vdf_tok(const std::string& s, size_t& i, std::string& tok, int& kind) {   // kind: 0 string, 1 '{', 2 '}'
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == 0)) i++;
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') i++; continue; }
        break;
    }
    if (i >= s.size()) return false;
    if (s[i] == '{') { kind = 1; i++; return true; }
    if (s[i] == '}') { kind = 2; i++; return true; }
    kind = 0; tok.clear();
    if (s[i] == '"') {
        i++;
        while (i < s.size() && s[i] != '"') { if (s[i] == '\\' && i + 1 < s.size()) { i++; char c = s[i]; tok += c == 'n' ? '\n' : c == 't' ? '\t' : c; } else tok += s[i]; i++; }
        i++; return true;
    }
    while (i < s.size() && !strchr(" \t\r\n{}\"", s[i])) tok += s[i++];
    return true;
}
inline bool vdf_block(const std::string& s, size_t& i, Vdf& out, int depth) {   // parses until '}' or EOF
    if (depth > 64) return false;
    out.obj = true;
    for (;;) {
        std::string k; int kind; if (!vdf_tok(s, i, k, kind)) return true;
        if (kind == 2) return true;
        if (kind == 1) return false;
        std::string v; if (!vdf_tok(s, i, v, kind)) return false;
        out.keys.push_back(k); out.kids.emplace_back();
        if (kind == 1) { if (!vdf_block(s, i, out.kids.back(), depth + 1)) return false; }
        else if (kind == 0) { out.kids.back().val = v; }
        else return false;
    }
}
inline bool vdf_parse(const std::string& s, Vdf& root) { size_t i = 0; return vdf_block(s, i, root, 0); }
