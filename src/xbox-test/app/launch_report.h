// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

// LocalState\launch_result.json, as launch.h lp_publish writes it. After a
// failed game the launcher may restart to unload it; on the next start the
// game's page shows why its last launch failed.
namespace launch_report {
// The value of "key": a JSON string (json_esc escapes only \" and \\) or a bare literal.
inline bool field(const std::string& json, const char* key, std::string& out) {
    out.clear();
    const std::string tag = std::string("\"") + key + "\":";
    size_t at = json.find(tag);
    if (at == std::string::npos) return false;
    at += tag.size();
    if (at < json.size() && json[at] == '"') {
        for (++at; at < json.size(); ++at) {
            char c = json[at];
            if (c == '"') return true;
            if (c == '\\' && at + 1 < json.size()) c = json[++at];
            out += c;
        }
        return false;   // unterminated
    }
    size_t end = json.find_first_of(",}", at);
    if (end == std::string::npos) return false;
    out = json.substr(at, end - at);
    return !out.empty();
}
// The failure text of a finished, failed launch of `appid`; empty otherwise.
inline std::string failure(const std::string& json, uint32_t appid) {
    std::string id, terminal, ok, result;
    if (!field(json, "appid", id) || !field(json, "terminal", terminal) || !field(json, "ok", ok) || !field(json, "result", result))
        return {};
    char* end = nullptr;
    unsigned long value = strtoul(id.c_str(), &end, 10);
    if (!end || *end || value != appid || terminal != "true" || ok != "false") return {};
    return result.empty() ? "unknown reason" : result;
}
}
