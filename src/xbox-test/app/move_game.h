// SPDX-License-Identifier: GPL-3.0-or-later
// Move an installed game to another root (issue 51 item 33): copy the install
// folder to the target root, verify file count and byte total, update the
// install marker's root, only then remove the source. Cancel or failure at
// any point removes the partial copy and leaves the source untouched; an
// existing destination is never overwritten. Win32 file walks (the same style
// as installs.h — no std::filesystem), so tests/move-game.cpp runs it against
// real temp directories.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "installs.h"

namespace move_game {

// The plan (pure, tested): one entry per file the source walk found.
struct MovePlan {
    std::wstring src_dir, dst_dir;   // the folder being moved, the copy target
    uint64_t bytes = 0;              // the byte total the verify step expects
    int files = 0;                   // the file count the verify step expects
};

// Refusals the UI shows before anything is copied. One reason per call; the
// caller renders it as the action sheet's disabled line.
enum class Refusal {
    none = 0,
    busy,            // the game runs or is installing/downloading
    same_root,       // the target root is the source root
    no_space,        // the target's free space < size + 1 GB
    exists,          // the destination folder already exists
};

inline const char* refusal_text(Refusal r) {
    switch (r) {
        case Refusal::busy:       return "Game is running or installing";
        case Refusal::same_root:  return "Already on this drive";
        case Refusal::no_space:   return "Not enough free space";
        case Refusal::exists:     return "Folder already exists there";
        default:                  return "";
    }
}

// The move's inputs, decided up front (pure so tests pin every refusal).
// src_root/dst_root are the game roots ("" = LocalState); src_dir/dst_dir the
// install folders under them; dst_exists pre-checked by the caller.
inline Refusal plan_check(bool game_busy, const std::string& src_root, const std::string& dst_root,
                          uint64_t src_bytes, bool dst_exists, uint64_t dst_free) {
    if (game_busy) return Refusal::busy;
    if (src_root == dst_root) return Refusal::same_root;
    if (dst_exists) return Refusal::exists;
    if (dst_free < src_bytes + 1024ull * 1024 * 1024) return Refusal::no_space;
    return Refusal::none;
}

// Progress for the sheet's percentage: the byte total and what is copied so far.
struct Progress {
    std::atomic<uint64_t> copied{0};      // bytes verified into the destination
    std::atomic<int> files{0};            // files copied
    uint64_t total = 0;                   // the plan's byte total (set once)
    int pct() const { return total ? (int)(copied.load() * 100 / total) : 0; }
};

// The copy walk: every file under src into the same relative path under dst.
// Creates directories as needed; never overwrites (a file that already exists
// in dst fails the move). Returns false on the first failure; the caller then
// removes the partial copy. `stop` ends the walk early (Cancel).
inline bool copy_tree(const std::wstring& src, const std::wstring& dst, Progress& p,
                      const std::atomic<bool>* stop = nullptr, int depth = 0) {
    if (depth > 64) return false;
    if (!CreateDirectoryW(dst.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((src + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        if (stop && *stop) { ok = false; break; }
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        const std::wstring s = src + L"\\" + n, d = dst + L"\\" + n;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ok = false; break; }   // never follow links out of the tree
            if (!copy_tree(s, d, p, stop, depth + 1)) { ok = false; break; }
        } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            ok = false; break;
        } else {
            if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES) { ok = false; break; }   // refuse overwrite
            if (!CopyFileW(s.c_str(), d.c_str(), TRUE)) { ok = false; break; }                     // TRUE: fail if it exists
            ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
            p.copied += sz.QuadPart; p.files++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

// The byte/file total a full copy of src would produce (the plan step).
inline MovePlan measure(const std::wstring& src, const std::wstring& dst) {
    MovePlan plan; plan.src_dir = src; plan.dst_dir = dst;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((src + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return plan;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            auto sub = measure(src + L"\\" + n, dst);
            plan.bytes += sub.bytes; plan.files += sub.files;
        } else if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
            plan.bytes += sz.QuadPart; plan.files++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return plan;
}

// Verify the copy: the destination holds at least the planned file count and
// byte total (per-file equality is CopyFileW's guarantee). Recursive: the
// whole tree counts, not just the top level.
inline bool verify_walk(const std::wstring& dir, uint64_t& bytes, int& files) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        const std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { FindClose(h); return false; }
            if (!verify_walk(dir + L"\\" + n, bytes, files)) { FindClose(h); return false; }
        } else {
            ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
            bytes += sz.QuadPart; files++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

inline bool verify_tree(const std::wstring& dst, uint64_t bytes, int files) {
    uint64_t got_bytes = 0; int got_files = 0;
    if (!verify_walk(dst, got_bytes, got_files)) return false;
    return got_bytes >= bytes && got_files >= files;
}

// Simple removal of a directory tree (no counters; used for the partial-copy
// cleanup so a failed move leaves nothing behind).
inline void remove_tree(const std::wstring& dir, int depth = 0) {
    if (depth > 64) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring path = dir + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(path, depth + 1);
            else DeleteFileW(path.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

// The whole move, run on a worker thread (main.cpp starts it). Order: copy,
// verify, write the destination marker, delete the source marker (skipped when
// marker_dst IS marker_src — an Internal move keeps the LocalState marker),
// remove the source tree. Cancel or any failure removes the partial copy (and
// the dst marker if it got written) and keeps the source untouched; the source
// marker only goes once the destination is verified and marked. Returns false
// when the move did not complete (with the partial copy already removed).
inline bool run(const std::wstring& src, const std::wstring& dst,
                const std::wstring& marker_src, const std::wstring& marker_dst,
                const std::string& marker_text_dst,
                Progress& p, const std::atomic<bool>* stop) {
    // never touch a destination that already exists (plan_check refuses it
    // too; this is the worker's own defence)
    if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    if (!copy_tree(src, dst, p, stop)) { remove_tree(dst); return false; }
    MovePlan plan = measure(src, dst);
    if (!verify_tree(dst, plan.bytes, plan.files)) { remove_tree(dst); return false; }
    if (stop && *stop) { remove_tree(dst); return false; }
    // destination marker first, then the source marker, then the source tree:
    // a crash between these steps leaves the game installed at exactly one root
    const bool same_marker = marker_src == marker_dst;   // an Internal move rewrites the one LocalState marker
    {
        FILE* f = _wfopen(marker_dst.c_str(), L"wb");
        if (!f) { remove_tree(dst); return false; }
        bool ok = fwrite(marker_text_dst.data(), 1, marker_text_dst.size(), f) == marker_text_dst.size();
        ok = fclose(f) == 0 && ok;
        if (!ok) { remove_tree(dst); return false; }
    }
    if (!same_marker && !DeleteFileW(marker_src.c_str())) { DeleteFileW(marker_dst.c_str()); remove_tree(dst); return false; }
    remove_tree(src);
    return true;
}

}  // namespace move_game
