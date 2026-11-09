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
#include "storage_roots.h"

namespace installs {

inline std::wstring ascii(const std::string& s) { return std::wstring(s.begin(), s.end()); }
inline std::wstring marker_path(const std::wstring& root, uint32_t appid) { return root + L"\\dl_" + ascii(std::to_string(appid)) + L".done"; }

// The marker always lives in `root` (LocalState); the game folder is `folder`: under LocalState
// unless the marker names another root (USB drive). root is "" for LocalState.
struct Info { bool installed = false; uint64_t bytes = 0; std::string dir, root; std::wstring folder; };
inline std::wstring folder_of(const std::wstring& stateRoot, const std::string& gameRoot, const std::string& dir) {
    return (gameRoot.empty() || !game_library::valid_root(gameRoot) ? stateRoot : ascii(gameRoot)) + L"\\" + ascii(dir);
}
inline Info read(const std::wstring& root, uint32_t appid) {
    Info r; FILE* f = _wfopen(marker_path(root, appid).c_str(), L"rb"); if (!f) return r;
    char b[512] = {}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
    auto m = game_library::parse_marker(std::string(b, n));
    r.installed = true; r.bytes = m.bytes; r.dir = m.installdir; r.root = game_library::valid_root(m.root) ? m.root : "";
    if (r.dir.empty()) r.dir = appid == 480 ? "spacewar" : appid == 3405340 ? "megabonk" : appid == 1007 ? "steamworks_sdk_redist" : "";   // markers written before 0.5.0.0 carry no installdir
    r.folder = folder_of(root, r.root, r.dir);
    return r;
}

// (appid, share key) for every marker (game_library::share_key: installdir, root-qualified off LocalState);
// withDirs=false skips reading the markers
inline std::vector<std::pair<uint32_t, std::string>> scan(const std::wstring& root, bool withDirs) {
    std::vector<std::pair<uint32_t, std::string>> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((root + L"\\dl_*.done").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name; for (const wchar_t* c = fd.cFileName; *c; ++c) name += *c < 128 ? (char)*c : '?';
        uint32_t id = 0; if (!game_library::marker_appid(name, id)) continue;
        if (withDirs) { Info i = read(root, id); out.emplace_back(id, game_library::share_key(i.root, i.dir)); } else out.emplace_back(id, std::string());
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
// a reinstall re-verifies what is left), then its folder unless keepFiles (folder
// shared with another install, or unknown). Only a game_library::deletable_installdir
// folder is touched. Returns false (nothing removed) when the marker cannot be deleted.
inline bool remove_install(const std::wstring& root, uint32_t appid, const std::string& dir, bool keepFiles, Progress& p, const std::atomic<bool>* stop = nullptr, const std::string& gameRoot = std::string()) {
    if (!keepFiles && !game_library::deletable_installdir(dir)) { p.failed++; return false; }
    if (!DeleteFileW(marker_path(root, appid).c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) { p.failed++; return false; }
    if (keepFiles) return true;
    const std::wstring folder = folder_of(root, gameRoot, dir); const DWORD a = GetFileAttributesW(folder.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) { if (!RemoveDirectoryW(folder.c_str())) p.failed++; return true; }
    remove_tree(folder, p, stop);
    return true;
}

// ---- unfinished downloads
// A fresh download writes dl_<appid>.partial ("app <id> installdir <dir>") before it
// creates any file and removes it once dl_<appid>.done is written. A failed or
// cancelled download, or one cut off by a crash or power loss (swept at the next
// start), then has its folder removed so its preallocated files do not keep
// the disk full. The folder stays when a finished install of the same app owns
// it (an update that failed), when another installed app shares it, or when its
// name is not a downloader install folder.
inline std::wstring partial_path(const std::wstring& root, uint32_t appid) { return root + L"\\dl_" + ascii(std::to_string(appid)) + L".partial"; }
inline bool begin_partial(const std::wstring& root, uint32_t appid, const std::string& dir, const std::string& gameRoot = std::string()) {
    FILE* f = _wfopen(partial_path(root, appid).c_str(), L"wb"); if (!f) return false;
    bool ok = (gameRoot.empty() ? std::fprintf(f, "app %u installdir %s\n", appid, dir.c_str()) : std::fprintf(f, "app %u root %s installdir %s\n", appid, gameRoot.c_str(), dir.c_str())) > 0; ok = std::fclose(f) == 0 && ok;
    return ok;
}
inline void end_partial(const std::wstring& root, uint32_t appid) { DeleteFileW(partial_path(root, appid).c_str()); }
inline game_library::Marker read_partial(const std::wstring& root, uint32_t appid) {
    FILE* f = _wfopen(partial_path(root, appid).c_str(), L"rb"); if (!f) return {};
    char b[512] = {}; size_t n = fread(b, 1, sizeof b - 1, f); fclose(f);
    auto m = game_library::parse_marker(std::string(b, n)); if (!game_library::valid_root(m.root)) m.root.clear();
    return m;
}

enum class Discard { removed, kept_installed, kept_shared, kept_unsafe, incomplete };
inline const char* discard_name(Discard d) {
    switch (d) { case Discard::removed: return "folder removed"; case Discard::kept_installed: return "kept (finished install of this app)";
                 case Discard::kept_shared: return "kept (shared with another install)"; case Discard::kept_unsafe: return "kept (not an install folder)";
                 default: return "some files could not be removed (retried next start)"; }
}
// Undo an unfinished download of appid into dir. The .partial marker goes once
// nothing is left to retry; on Discard::incomplete it stays for the next start.
inline Discard discard_partial(const std::wstring& root, uint32_t appid, const std::string& dir, Progress& p, const std::atomic<bool>* stop = nullptr, const std::string& gameRoot = std::string()) {
    Discard r;
    if (GetFileAttributesW(marker_path(root, appid).c_str()) != INVALID_FILE_ATTRIBUTES) r = Discard::kept_installed;
    else if (!game_library::deletable_installdir(dir)) r = Discard::kept_unsafe;
    else if (game_library::installdir_shared(appid, game_library::share_key(gameRoot, dir), scan(root, true))) r = Discard::kept_shared;
    else {
        const std::wstring folder = folder_of(root, gameRoot, dir); const DWORD a = GetFileAttributesW(folder.c_str());
        unsigned before = p.failed;
        if (a != INVALID_FILE_ATTRIBUTES) { if (a & FILE_ATTRIBUTE_REPARSE_POINT) { if (!RemoveDirectoryW(folder.c_str())) p.failed++; } else remove_tree(folder, p, stop); }
        r = p.failed != before || (stop && *stop) ? Discard::incomplete : Discard::removed;
    }
    if (r != Discard::incomplete) end_partial(root, appid);
    return r;
}
// Every dl_<appid>.partial left by an earlier run: (appid, installdir, outcome).
struct Swept { uint32_t appid; std::string dir, root; Discard result; };
inline std::vector<Swept> sweep_partials(const std::wstring& root, const std::atomic<bool>* stop = nullptr) {
    std::vector<uint32_t> ids;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((root + L"\\dl_*.partial").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name; for (const wchar_t* c = fd.cFileName; *c; ++c) name += *c < 128 ? (char)*c : '?';
            uint32_t id = 0; if (game_library::marker_appid(name, id, ".partial")) ids.push_back(id);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::vector<Swept> out;
    for (uint32_t id : ids) { Progress p; auto m = read_partial(root, id); out.push_back({id, m.installdir, m.root, discard_partial(root, id, m.installdir, p, stop, m.root)}); }
    return out;
}

// ---- install locations
// Internal (the state root = LocalState) first, then every other mounted drive where
// "<X>:\\XboxNativeGames" can be created (a USB drive the app may write to; a drive the
// sandbox blocks fails here and is not listed). Drives are probed through Win32 drive
// letters, the same way the downloader and the Play loader open files. note (optional)
// receives one line per skipped drive for the log.
inline const char* kExtraFolder = "XboxNativeGames";
inline uint64_t free_bytes(const std::wstring& path) { ULARGE_INTEGER fb{}, tb{}, tf{}; return GetDiskFreeSpaceExW(path.c_str(), &fb, &tb, &tf) ? fb.QuadPart : 0; }
inline std::vector<storage_roots::Choice> list_roots(const std::wstring& stateRoot, std::vector<std::string>* note = nullptr) {
    std::vector<storage_roots::Choice> out; out.push_back({"", "Internal", free_bytes(stateRoot)});
    const wchar_t home = stateRoot.empty() ? L'\0' : (wchar_t)towupper(stateRoot[0]);
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        wchar_t letter = (wchar_t)(L'A' + i); if (letter == home) continue;
        std::wstring drive = std::wstring(1, letter) + L":\\"; UINT t = GetDriveTypeW(drive.c_str());
        if (t != DRIVE_REMOVABLE && t != DRIVE_FIXED) { if (note) note->push_back(std::string(1, (char)letter) + ": skipped, drive type " + std::to_string(t)); continue; }
        std::string root = std::string(1, (char)letter) + ":\\" + kExtraFolder;
        std::wstring w = ascii(root);
        if (!CreateDirectoryW(w.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) { if (note) note->push_back(std::string(1, (char)letter) + ": not usable, create folder error " + std::to_string(GetLastError())); continue; }
        out.push_back({root, storage_roots::drive_name(root), free_bytes(w)});
    }
    return out;
}

}  // namespace installs
