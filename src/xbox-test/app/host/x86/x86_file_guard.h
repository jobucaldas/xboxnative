// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace x86_file_guard {
inline std::u16string normalize(std::u16string path, const std::u16string& cwd) {
    for (auto& c : path) {
        if (c == u'/') c = u'\\';
        if (c >= u'A' && c <= u'Z') c = char16_t(c + (u'a' - u'A'));
    }
    if (path.rfind(u"\\\\?\\unc\\", 0) == 0) path = u"\\\\" + path.substr(8);
    else if (path.rfind(u"\\\\?\\", 0) == 0) path.erase(0, 4);
    std::u16string base = cwd;
    for (auto& c : base) {
        if (c == u'/') c = u'\\';
        if (c >= u'A' && c <= u'Z') c = char16_t(c + (u'a' - u'A'));
    }
    if (base.rfind(u"\\\\?\\unc\\", 0) == 0) base = u"\\\\" + base.substr(8);
    else if (base.rfind(u"\\\\?\\", 0) == 0) base.erase(0, 4);
    if (path.empty()) return {};
    if (path.size() < 2 || path[1] != u':') {
        if (path.rfind(u"\\\\", 0) == 0) {
            const auto drive = base.size() >= 2 && base[1] == u':' ? base.substr(0, 2) : std::u16string();
            path = drive + path;
        } else path = base + (base.empty() || base.back() == u'\\' ? u"" : u"\\") + path;
    }
    std::u16string prefix;
    size_t start = 0;
    if (path.size() >= 2 && path[1] == u':') { prefix = path.substr(0, 2); start = 2; }
    else if (path.rfind(u"\\\\", 0) == 0) { prefix = u"\\\\"; start = 2; }
    std::vector<std::u16string> parts;
    while (start < path.size()) {
        while (start < path.size() && path[start] == u'\\') ++start;
        size_t end = path.find(u'\\', start);
        if (end == std::u16string::npos) end = path.size();
        auto part = path.substr(start, end - start);
        if (part == u"..") { if (!parts.empty() && parts.back() != u"..") parts.pop_back(); }
        else if (!part.empty() && part != u".") parts.push_back(std::move(part));
        start = end;
    }
    std::u16string out = prefix;
    if (!out.empty() && out != u"\\\\") out += u"\\";
    else if (out == u"\\\\") out = u"\\\\";
    for (const auto& part : parts) { if (!out.empty() && out.back() != u'\\') out += u"\\"; out += part; }
    return out;
}
inline bool inside(const std::u16string& path, const std::u16string& root) {
    if (root.empty()) return false;
    if (path == root) return true;
    return path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
           (root.back() == u'\\' || path[root.size()] == u'\\');
}
class Policy {
public:
    using Log = std::function<void(const std::string&)>;
    Policy(const std::vector<std::u16string>& protectedRoots, const std::vector<std::u16string>& allowedRoots,
           std::u16string cwd, Log log = {})
        : cwd_(normalize(std::move(cwd), {})), log_(std::move(log)) {
        for (const auto& root : protectedRoots) protectedRoots_.push_back(normalize(root, cwd_));
        for (const auto& root : allowedRoots) allowedRoots_.push_back(normalize(root, cwd_));
    }
    bool allow(const char* operation, const std::u16string& path, const std::u16string& current = {}) {
        const auto full = normalize(path, current.empty() ? cwd_ : current);
        bool protectedPath = false;
        for (const auto& root : protectedRoots_) protectedPath = protectedPath || inside(full, root);
        if (!protectedPath) return true;
        for (const auto& root : allowedRoots_) if (inside(full, root)) return true;
        if (logged_.insert(full).second && log_) {
            std::string op = operation ? operation : "file operation";
            log_("x86 file guard: refused " + op + " <redacted path>");
        }
        return false;
    }
private:
    std::u16string cwd_;
    std::vector<std::u16string> protectedRoots_;
    std::vector<std::u16string> allowedRoots_;
    std::set<std::u16string> logged_;
    Log log_;
};
}
