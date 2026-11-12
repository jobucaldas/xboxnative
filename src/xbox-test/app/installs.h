// SPDX-License-Identifier: GPL-3.0-or-later
// Installed games under one root (the launcher passes LocalState): read the
// downloader's dl_<appid>.done markers and remove an install folder. Win32
// only, no launcher globals, so tests/game-delete.sh runs it under Wine.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <set>
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

// ---- install health (marker vs payload)
// A dl_<appid>.done marker claims an install, but the payload folder can be
// gone or empty (a deploy that wiped app data, a move that left the marker on
// the source drive, a crash between the first file and the last). The marker
// text alone cannot tell; the folder is checked.
enum class Health { ok, missing, empty };
// Does the folder the marker names hold at least one real file? A directory
// alone (or only empty directories) is not a payload; sizes are not verified
// (the marker's byte total counts depot padding, not the on-disk sum).
inline Health payload_state(const std::wstring& folder) {
    DWORD a = GetFileAttributesW(folder.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return Health::missing;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((folder + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return Health::missing;   // unreadable: as good as gone
    bool any = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow) { any = true; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return any ? Health::ok : Health::empty;
}
// Health of appid's install as the marker records it. root is the marker's
// state root (kept for signature symmetry; the folder is already resolved).
inline Health health_of(const std::wstring& /*root*/, const Info& i) { return i.installed && !i.dir.empty() ? payload_state(i.folder) : Health::missing; }

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
// Every figure GetDiskFreeSpaceExW returns. lpFreeBytesAvailableToCaller is the one an
// install must be checked against: with a per-user quota or a separate pool it is below
// the volume's own free space, and a write past it fails even when the volume has room.
inline storage_roots::Space space_of(const std::wstring& path) {
    storage_roots::Space s; ULARGE_INTEGER a{}, t{}, f{};
    if (GetDiskFreeSpaceExW(path.c_str(), &a, &t, &f)) { s.ok = true; s.available = a.QuadPart; s.total = t.QuadPart; s.freeTotal = f.QuadPart; }
    return s;
}
inline uint64_t free_bytes(const std::wstring& path) { return space_of(path).available; }
// "label='X' fs=NTFS": the drive's own name, so a log line says which volume a letter is.
inline std::string volume_text(const std::wstring& path) {
    wchar_t label[64] = {}, fs[32] = {};
    std::string out;
    if (GetVolumeInformationW(path.c_str(), label, 64, nullptr, nullptr, nullptr, fs, 32)) {
        out = "label='"; for (const wchar_t* c = label; *c; ++c) out += *c < 128 ? (char)*c : '?'; out += "' fs=";
        for (const wchar_t* c = fs; *c; ++c) out += *c < 128 ? (char)*c : '?';
    } else out = "volume info error " + std::to_string(GetLastError());
    return out;
}
// "error 5 (Access is denied.)": the Win32 text, so the log explains a skipped drive.
inline std::string error_text(DWORD e) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
    std::string t = "error " + std::to_string(e);
    if (n && buf) { t += " ("; for (DWORD i = 0; i < n; ++i) { if (buf[i] == L'\r' || buf[i] == L'\n') break; t += buf[i] < 128 ? (char)buf[i] : '?'; } t += ")"; }
    if (buf) LocalFree(buf);
    return t;
}
inline std::vector<storage_roots::Choice> list_roots(const std::wstring& stateRoot, std::vector<std::string>* note = nullptr) {
    std::vector<storage_roots::Choice> out;
    { storage_roots::Choice c; c.name = "Internal"; c.type = DRIVE_FIXED; c.space = space_of(stateRoot); c.freeBytes = c.space.available; out.push_back(c); }
    const wchar_t home = stateRoot.empty() ? L'\0' : (wchar_t)towupper(stateRoot[0]);
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        wchar_t letter = (wchar_t)(L'A' + i); if (letter == home) continue;
        std::wstring drive = std::wstring(1, letter) + L":\\"; UINT t = GetDriveTypeW(drive.c_str());
        const std::string name(1, (char)letter);
        if (t != DRIVE_REMOVABLE && t != DRIVE_FIXED) { if (note) note->push_back(name + ": skipped, drive type " + std::to_string(t) + " (" + storage_roots::drive_type_name(t) + ")"); continue; }
        std::string root = name + ":\\" + kExtraFolder;
        std::wstring w = ascii(root);
        if (!CreateDirectoryW(w.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            DWORD e = GetLastError();
            if (note) note->push_back(name + ": not usable, create folder " + error_text(e) + ", drive type " + storage_roots::drive_type_name(t) + ", " + volume_text(drive));
            continue;
        }
        storage_roots::Choice c; c.root = root; c.type = t; c.name = storage_roots::drive_name(root, t);
        c.space = space_of(w); c.freeBytes = c.space.available;
        out.push_back(c);
    }
    return out;
}

// ---- launcher-owned leftovers
// Files the launcher itself writes into LocalState that no game or save needs. These are
// KB-to-MB, not the GB an install needs, so they are cleaned quietly at start and reported
// rather than offered as a button: the space an install lacks is the pool's, not theirs.
struct Leftover { std::string name; uint64_t bytes = 0; };
// A file whose last write is more than `days` ago (FILETIME is 100 ns since 1601).
inline bool older_than_days(const FILETIME& ft, double days) {
    ULARGE_INTEGER t; t.LowPart = ft.dwLowDateTime; t.HighPart = ft.dwHighDateTime;
    FILETIME nowft; GetSystemTimeAsFileTime(&nowft);
    ULARGE_INTEGER n; n.LowPart = nowft.dwLowDateTime; n.HighPart = nowft.dwHighDateTime;
    return n.QuadPart > t.QuadPart && (double)(n.QuadPart - t.QuadPart) / 1e7 / 86400.0 > days;
}
// name is relative to the state root ("storecache\123.json" for the cache). An appinfo
// cache is kept for an installed game: Play reads it to pick the game's launch option.
inline bool leftover_kind(const std::string& name, const std::set<uint32_t>& installed) {
    if (name == "boot.prev.log" || name == "launch_probe.previous.log" || name == "frametimes.csv") return true;
    if (name.compare(0, 7, "appinfo") != 0) return false;
    std::string d = name.substr(8); if (d.size() <= 4 || d.compare(d.size() - 4, 4, ".vdf") != 0) return false;
    d.resize(d.size() - 4);
    for (char c : d) if (c < '0' || c > '9') return false;
    return !installed.count((uint32_t)strtoul(d.c_str(), nullptr, 10));
}
inline std::vector<Leftover> launcher_leftovers(const std::wstring& root, const std::set<uint32_t>& installed) {
    std::vector<Leftover> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((root + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string name; for (const wchar_t* c = fd.cFileName; *c; ++c) name += *c < 128 ? (char)*c : '?';
            if (leftover_kind(name, installed)) out.push_back({name, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // Store info is cached per appid for 24 h (store.h) and the detail page reads it back
    // offline, so only an entry no page has touched for a week is stale: deleting a fresh
    // one would only make the next detail page wait on the network.
    HANDLE c = FindFirstFileExW((root + L"\\storecache\\*.json").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (c != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!older_than_days(fd.ftLastWriteTime, 7)) continue;
            std::string name; for (const wchar_t* p = fd.cFileName; *p; ++p) name += *p < 128 ? (char)*p : '?';
            out.push_back({"storecache\\" + name, ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
        } while (FindNextFileW(c, &fd));
        FindClose(c);
    }
    return out;
}
// Delete the leftovers and return the bytes freed. Only files launcher_leftovers names,
// and only under root or root\storecache, are ever touched.
inline uint64_t clean_leftovers(const std::wstring& root, const std::set<uint32_t>& installed) {
    uint64_t freed = 0;
    for (auto& l : launcher_leftovers(root, installed)) {
        std::wstring w(l.name.begin(), l.name.end());
        if (DeleteFileW((root + L"\\" + w).c_str())) freed += l.bytes;
    }
    return freed;
}

}  // namespace installs
