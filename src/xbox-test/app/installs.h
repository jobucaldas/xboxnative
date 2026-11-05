// SPDX-License-Identifier: GPL-3.0-or-later
// Installed games under one root (the launcher passes LocalState): read the
// downloader's dl_<appid>.done markers and remove an install folder. Win32
// only, no launcher globals, so tests/game-delete.sh runs it under Wine.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include "game_library.h"

namespace installs {

inline std::wstring ascii(const std::string& s) { return std::wstring(s.begin(), s.end()); }
inline std::wstring marker_path(const std::wstring& root, uint32_t appid) { return root + L"\\dl_" + ascii(std::to_string(appid)) + L".done"; }

struct Info { bool installed = false; uint64_t bytes = 0; std::string dir; };
inline Info read(const std::wstring& root, uint32_t appid) {
    Info r; FILE* f = _wfopen(marker_path(root, appid).c_str(), L"rb"); if (!f) return r;
    char b[512] = {}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
    auto m = game_library::parse_marker(std::string(b, n));
    r.installed = true; r.bytes = m.bytes; r.dir = m.installdir;
    if (r.dir.empty()) r.dir = appid == 480 ? "spacewar" : appid == 3405340 ? "megabonk" : appid == 1007 ? "steamworks_sdk_redist" : "";   // markers written before 0.5.0.0 carry no installdir
    return r;
}

// (appid, installdir) for every marker; withDirs=false skips reading the markers
inline std::vector<std::pair<uint32_t, std::string>> scan(const std::wstring& root, bool withDirs) {
    std::vector<std::pair<uint32_t, std::string>> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((root + L"\\dl_*.done").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name; for (const wchar_t* c = fd.cFileName; *c; ++c) name += *c < 128 ? (char)*c : '?';
        uint32_t id = 0; if (!game_library::marker_appid(name, id)) continue;
        out.emplace_back(id, withDirs ? read(root, id).dir : std::string());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

struct Progress { std::atomic<uint64_t> removed{0}; std::atomic<unsigned> failed{0}; };

// Depth-first removal of dir and everything under it. A junction or symlink is
// removed as a link and never followed. stop (optional) ends the walk early.
inline void remove_tree(const std::wstring& dir, Progress& p, const std::atomic<bool>* stop = nullptr, int depth = 0) {
    if (depth > 64) { p.failed++; return; }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue;
            const std::wstring path = dir + L"\\" + n; const DWORD a = fd.dwFileAttributes;
            if (a & FILE_ATTRIBUTE_READONLY) {
                DWORD keep = a & (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_TEMPORARY);
                SetFileAttributesW(path.c_str(), keep ? keep : FILE_ATTRIBUTE_NORMAL);
            }
            if ((a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(path, p, stop, depth + 1);
            else if (a & FILE_ATTRIBUTE_DIRECTORY) { if (!RemoveDirectoryW(path.c_str())) p.failed++; }
            else if (DeleteFileW(path.c_str())) p.removed++;
            else p.failed++;
        } while (!(stop && *stop) && FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!RemoveDirectoryW(dir.c_str())) { DWORD e = GetLastError(); if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) p.failed++; }
}

// Remove appid's marker first (an interrupted delete then never shows as installed;
// a reinstall re-verifies what is left), then its folder unless another install
// shares it. Only a game_library::deletable_installdir folder is touched.
// Returns false (nothing removed) when the marker cannot be deleted.
inline bool remove_install(const std::wstring& root, uint32_t appid, const std::string& dir, bool shared, Progress& p, const std::atomic<bool>* stop = nullptr) {
    if (!shared && !game_library::deletable_installdir(dir)) { p.failed++; return false; }
    if (!DeleteFileW(marker_path(root, appid).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) { p.failed++; return false; }
    if (shared) return true;
    const std::wstring folder = root + L"\\" + ascii(dir); const DWORD a = GetFileAttributesW(folder.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) { if (!RemoveDirectoryW(folder.c_str())) p.failed++; return true; }
    remove_tree(folder, p, stop);
    return true;
}

}  // namespace installs
