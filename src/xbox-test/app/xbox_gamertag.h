// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The console's XBOX gamertag, and the parsing that pulls it out of what Xbox
// Live identity hands back.
//
// Windows.System.KnownUserProperties has no gamertag: DisplayName is the
// Microsoft account name ("Jobu"), which is not the name the console shows for
// the signed-in player ("jobucaldas"). The gamertag lives in Xbox Live
// identity, and a packaged app reaches it through XblIdpAuthManager
// (um/XblIdpAuthManager.h): GetTokenAndSignatureWithTokenResult returns an XSTS
// token for the console's own gamer account, and
// IXblIdpAuthTokenResult::GetGamertag names the player. That token is a JWT
// whose `gtg` claim carries the same name, so the claim is the fallback when
// the accessor answers with nothing.
//
// The xodus project reads the same value over REST
// (crates/xodus-cli/src/commands/profile.rs: profile.xboxlive.com with
// "XBL3.0 x=<uhs>;<token>"); the token it authorizes with is the one this
// header's `gtg` claim comes from. No token, user hash or gamertag is ever
// logged — callers log only which path answered.
//
// Portable (no Windows headers), so tests/xbox-gamertag.cpp runs it natively.
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xbox_gamertag {

// A JSON value, just enough of one to read a JWT payload and a profile
// response. Numbers keep their text: nothing here does arithmetic.
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    std::string text;                                     // String, Number
    std::vector<Json> items;                              // Array
    std::vector<std::pair<std::string, Json>> members;    // Object

    const Json* member(std::string_view key) const {
        if (type != Object) return nullptr;
        for (auto const& m : members) if (m.first == key) return &m.second;
        return nullptr;
    }
    const Json* at(std::size_t i) const {
        return type == Array && i < items.size() ? &items[i] : nullptr;
    }
    const std::string* as_string() const { return type == String ? &text : nullptr; }
};

namespace detail {

inline void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

// Recursive-descent over a bounded buffer. Depth is capped so a hostile or
// truncated payload cannot exhaust the stack.
struct Parser {
    const char* p;
    const char* end;
    int depth = 0;

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
    bool lit(std::string_view word) {
        if (std::size_t(end - p) < word.size()) return false;
        if (std::string_view(p, word.size()) != word) return false;
        p += word.size();
        return true;
    }
    static int hex(unsigned char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    bool hex4(unsigned& out) {
        if (end - p < 4) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            int v = hex((unsigned char)p[i]);
            if (v < 0) return false;
            out = (out << 4) | unsigned(v);
        }
        p += 4;
        return true;
    }
    bool string(std::string& out) {
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end) {
            unsigned char c = (unsigned char)*p++;
            if (c == '"') return true;
            if (c != '\\') { out += char(c); continue; }
            if (p >= end) return false;
            char e = *p++;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    const char* save = p;
                    p += 2;
                    unsigned lo = 0;
                    if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else
                        p = save;
                }
                put_utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(Json& out) {
        if (++depth > 32) return false;
        ws();
        if (p >= end) return false;
        bool ok = false;
        if (*p == '{') {
            ++p; out.type = Json::Object;
            ws();
            if (p < end && *p == '}') { ++p; ok = true; }
            else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!string(key)) return false;
                    ws();
                    if (p >= end || *p != ':') return false;
                    ++p;
                    Json v;
                    if (!value(v)) return false;
                    out.members.emplace_back(std::move(key), std::move(v));
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == '}') { ++p; ok = true; }
                    break;
                }
            }
        } else if (*p == '[') {
            ++p; out.type = Json::Array;
            ws();
            if (p < end && *p == ']') { ++p; ok = true; }
            else {
                for (;;) {
                    Json v;
                    if (!value(v)) return false;
                    out.items.push_back(std::move(v));
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == ']') { ++p; ok = true; }
                    break;
                }
            }
        } else if (*p == '"') {
            out.type = Json::String;
            ok = string(out.text);
        } else if (lit("true")) { out.type = Json::Bool; out.boolean = true; ok = true; }
        else if (lit("false")) { out.type = Json::Bool; ok = true; }
        else if (lit("null")) { out.type = Json::Null; ok = true; }
        else {
            const char* start = p;
            if (p < end && (*p == '-' || *p == '+')) ++p;
            while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) ++p;
            if (p == start) return false;
            out.type = Json::Number;
            out.text.assign(start, p);
            ok = true;
        }
        --depth;
        return ok;
    }
};

}  // namespace detail

// Parse one complete JSON value. Trailing whitespace is allowed; anything else
// after the value is an error, so a truncated or spliced payload is rejected
// rather than half-read.
inline bool parse_json(std::string_view text, Json& out) {
    detail::Parser ps{text.data(), text.data() + text.size()};
    out = Json{};
    if (!ps.value(out)) return false;
    ps.ws();
    return ps.p == ps.end;
}

// Base64url, with the standard alphabet accepted too and padding optional —
// JWT segments are unpadded base64url, but a decoded profile blob is not.
inline bool decode_base64url(std::string_view in, std::string& out) {
    out.clear();
    unsigned acc = 0;
    int bits = 0;
    for (char ch : in) {
        if (ch == '=') break;
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '-' || ch == '+') v = 62;
        else if (ch == '_' || ch == '/') v = 63;
        else return false;
        acc = (acc << 6) | unsigned(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out += char((acc >> bits) & 0xFF); }
    }
    return true;
}

// The decoded payload of a three-segment JWT. Empty on anything that is not one.
inline std::string jwt_payload(std::string_view jwt) {
    std::size_t first = jwt.find('.');
    if (first == std::string_view::npos) return {};
    std::size_t second = jwt.find('.', first + 1);
    if (second == std::string_view::npos) return {};
    std::string out;
    if (!decode_base64url(jwt.substr(first + 1, second - first - 1), out)) return {};
    return out;
}

// A string claim from the JWT payload. `xui` is an array of objects, so the
// caller picks the element and the field inside it.
inline std::string jwt_string_claim(std::string_view jwt, std::string_view claim) {
    Json doc;
    if (!parse_json(jwt_payload(jwt), doc)) return {};
    const Json* v = doc.member(claim);
    const std::string* s = v ? v->as_string() : nullptr;
    return s ? *s : std::string{};
}

// The user hash ("uhs") inside the `xui` claim — the `x=` value of the
// "XBL3.0 x=<uhs>;<token>" authorization header Xbox Live's REST API wants.
inline std::string jwt_user_hash(std::string_view jwt) {
    Json doc;
    if (!parse_json(jwt_payload(jwt), doc)) return {};
    const Json* xui = doc.member("xui");
    const Json* first = xui ? xui->at(0) : nullptr;
    const Json* uhs = first ? first->member("uhs") : nullptr;
    const std::string* s = uhs ? uhs->as_string() : nullptr;
    return s ? *s : std::string{};
}

// A gamertag as the console would draw it: trimmed, non-empty, bounded, and
// free of control characters. Anything else is not a name and is rejected.
inline std::string clean_gamertag(std::string_view raw) {
    std::size_t b = 0, e = raw.size();
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && space(raw[b])) ++b;
    while (e > b && space(raw[e - 1])) --e;
    if (e - b == 0 || e - b > 256) return {};
    for (std::size_t i = b; i < e; ++i) {
        unsigned char c = (unsigned char)raw[i];
        if (c < 0x20 || c == 0x7F) return {};
    }
    return std::string(raw.substr(b, e - b));
}

// profile.xboxlive.com/users/me/profile/settings?settings=Gamertag,PublicGamerpic
// — the call xodus makes. One user, one settings array; the gamertag is the
// `Gamertag` setting and the picture the `PublicGamerpic` URL.
inline bool parse_profile(std::string_view body, std::string& gamertag, std::string& picture_url) {
    gamertag.clear();
    picture_url.clear();
    Json doc;
    if (!parse_json(body, doc)) return false;
    const Json* users = doc.member("profileUsers");
    if (!users || users->type != Json::Array || users->items.size() != 1) return false;
    const Json* settings = users->items[0].member("settings");
    if (!settings || settings->type != Json::Array) return false;
    for (auto const& s : settings->items) {
        const Json* id = s.member("id");
        const Json* value = s.member("value");
        if (!id || !value) continue;
        const std::string* name = id->as_string();
        const std::string* text = value->as_string();
        if (!name || !text) continue;
        if (*name == "Gamertag") gamertag = clean_gamertag(*text);
        else if (*name == "PublicGamerpic") picture_url = *text;
    }
    return !gamertag.empty();
}

// Only the Xbox Live picture CDNs, over https, with no credentials and no
// explicit port — the same allowlist xodus applies before it fetches one.
inline bool allowed_picture_url(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size() || url.compare(0, scheme.size(), scheme) != 0) return false;
    std::string_view rest = url.substr(scheme.size());
    std::size_t slash = rest.find('/');
    std::string_view host = rest.substr(0, slash);
    if (host.empty() || host.find('@') != std::string_view::npos ||
        host.find(':') != std::string_view::npos) return false;
    for (std::string_view allowed : {"gamerpic.xboxlive.com", "images-eds-ssl.xboxlive.com",
                                     "avatar-ssl.xboxlive.com", "xboxavatargamerpic.azureedge.net"})
        if (host == allowed) return true;
    return false;
}

// The authorization header Xbox Live's REST API expects.
inline std::string authorization(std::string_view user_hash, std::string_view token) {
    if (user_hash.empty() || token.empty()) return {};
    return "XBL3.0 x=" + std::string(user_hash) + ";" + std::string(token);
}

// A sign-in address, not a name. On a desktop KnownUserProperties.AccountName
// is often the Microsoft address; on a console it is the Xbox account name.
inline bool looks_like_email(std::string_view s) {
    std::size_t at = s.find('@');
    return at != std::string_view::npos && at > 0 && at + 1 < s.size();
}

// Which of the two Windows.System.User properties to draw. AccountName first:
// on a console that is the Xbox account name (the gamertag the console's own
// profile page shows), while DisplayName is the Microsoft account's display
// name. An address is never a name, so it falls back to DisplayName.
inline std::string choose_name(std::string_view account_name, std::string_view display_name) {
    std::string account = clean_gamertag(account_name);
    if (!account.empty() && !looks_like_email(account)) return account;
    return clean_gamertag(display_name);
}

// GetPropertyAsync takes a property NAME, not an enum, so a console whose user
// property store carries more than the documented KnownUserProperties can be
// asked for the Xbox ones directly. These are tried in order, most specific
// first; the first non-empty, non-address value wins.
struct Property {
    const char* name;
    std::string value;
};
inline std::string choose_name_from_properties(const std::vector<Property>& properties,
                                               std::string* chosen_from = nullptr) {
    if (chosen_from) chosen_from->clear();
    // Two passes: the Xbox-specific names first, then the documented pair with
    // AccountName ahead of DisplayName (the console's account name beats the
    // Microsoft account's display name). A blank or address-valued property
    // never wins.
    auto rank = [](std::string_view name) {
        if (name == "AccountName") return 1;
        if (name == "DisplayName") return 2;
        return 0;   // the Xbox names
    };
    for (int wanted = 0; wanted <= 2; ++wanted) {
        for (auto const& p : properties) {
            if (rank(p.name) != wanted) continue;
            std::string name = clean_gamertag(p.value);
            if (name.empty() || looks_like_email(name)) continue;
            if (chosen_from) *chosen_from = p.name;
            return name;
        }
    }
    return {};
}

}  // namespace xbox_gamertag
