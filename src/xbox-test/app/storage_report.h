// SPDX-License-Identifier: GPL-3.0-or-later
// Free-space diagnostics for the launcher: every figure Win32 and WinRT report
// for LocalState, and the launcher's own leftovers, in one `storage:` log line
// and in LocalState\storage_report.txt. The rules that turn those numbers into
// text live in storage_roots.h (tests/game_library_test.cpp); this file only
// reads them. Win32 + WinRT, no launcher globals, so tests/storage-report.cpp
// runs it under Wine.
#pragma once
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
// lower case: the cross toolchain's projection ships this one only as windows.storage.fileproperties.h
// (the same header the Windows SDK has; NTFS makes the two spellings the same file there)
#include <winrt/windows.storage.fileproperties.h>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "installs.h"
#include "storage_roots.h"

namespace storage_report {

// What WinRT says about the folder: System.FreeSpace and System.Capacity of the
// volume it lives on. A UWP app's own view of its storage; the Win32 call above
// can answer differently under a quota or a separate pool, and the two together
// say which. Never throws: an Xbox without the property, or a folder the broker
// refuses, leaves winrt false.
inline void query_winrt(const std::wstring& path, storage_roots::Space& s) {
    try {
        using namespace winrt::Windows::Storage;
        auto folder = StorageFolder::GetFolderFromPathAsync(path).get();
        auto props = folder.Properties().RetrievePropertiesAsync({L"System.FreeSpace", L"System.Capacity"}).get();
        auto get = [&](const wchar_t* key, uint64_t& out) {
            if (!props.HasKey(key)) return false;
            auto v = props.Lookup(key);
            if (!v) return false;
            out = winrt::unbox_value<uint64_t>(v);
            return true;
        };
        bool a = get(L"System.FreeSpace", s.winrtFree), b = get(L"System.Capacity", s.winrtCapacity);
        s.winrt = a || b;
    } catch (...) { s.winrt = false; }
}

// "Internal path=... GetDiskFreeSpaceEx available=... total=... free_total=... WinRT ..."
inline std::string line(const std::string& name, const std::string& path, const storage_roots::Space& s, const std::string& extra = std::string()) {
    return storage_roots::space_line(name, path, s, extra);
}

// The internal location on its own (no drive probing, no folder creation): what the
// start-of-session report needs.
inline storage_roots::Choice internal_root(const std::wstring& stateRoot) {
    storage_roots::Choice c; c.name = "Internal"; c.type = storage_roots::kFixed;
    c.space = installs::space_of(stateRoot); c.freeBytes = c.space.available;
    return c;
}

// The whole report: one line per install location, the launcher's leftovers, and
// what the numbers mean. Written to LocalState\storage_report.txt so a console
// session can be read without the log, and logged as one `storage:` line.
inline std::string text(const std::wstring& stateRoot, const std::vector<storage_roots::Choice>& roots, const std::set<uint32_t>& installed) {
    std::string t = "storage report\n";
    t += "internal path=" + std::string(stateRoot.begin(), stateRoot.end()) + "\n";
    for (auto& c : roots) {
        std::string path = c.root.empty() ? std::string(stateRoot.begin(), stateRoot.end()) : c.root;
        t += "  " + line(c.name, path, c.space, std::string("type=") + storage_roots::drive_type_name(c.type)) + "\n";
    }
    auto left = installs::launcher_leftovers(stateRoot, installed);
    uint64_t bytes = 0; for (auto& l : left) bytes += l.bytes;
    t += "launcher leftovers: " + std::to_string(left.size()) + " files, " + storage_roots::size_text(bytes) + "\n";
    for (auto& l : left) t += "  " + l.name + " " + storage_roots::size_text(l.bytes) + "\n";
    if (!roots.empty()) {
        const storage_roots::Space& s = roots[0].space;
        if (s.ok) {
            t += "internal space: " + storage_roots::space_text(s) + "\n";
            if (s.total && s.freeTotal > s.total + s.total / 20)
                t += "note: the volume has more free space than the pool this app writes into (a per-user quota or a separate partition), so the console's storage settings show more than an install here can use\n";
        }
    }
    return t;
}

// The report's per-root lines ("  Internal path=..."), without the indent, so the
// caller can log each as one `storage:` line.
inline std::vector<std::string> log_lines(const std::string& report) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < report.size()) {
        size_t j = report.find('\n', i);
        if (j == std::string::npos) j = report.size();
        if (j - i > 2 && report.compare(i, 2, "  ") == 0) out.push_back(report.substr(i + 2, j - i - 2));
        i = j + 1;
    }
    return out;
}

// Query WinRT for the internal root (the others are plain drive letters) and write
// the report. Returns the text written, so the caller can log it.
inline std::string write(const std::wstring& stateRoot, std::vector<storage_roots::Choice>& roots, const std::set<uint32_t>& installed) {
    if (!roots.empty() && roots[0].root.empty()) query_winrt(stateRoot, roots[0].space);
    std::string t = text(stateRoot, roots, installed);
    FILE* f = _wfopen((stateRoot + L"\\storage_report.txt").c_str(), L"wb");
    if (f) { std::fwrite(t.data(), 1, t.size(), f); std::fclose(f); }
    return t;
}

}  // namespace storage_report
