// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using DWORD = unsigned long;
using HANDLE = void*;
constexpr DWORD FILE_ATTRIBUTE_DIRECTORY = 0x10;
#define INVALID_HANDLE_VALUE reinterpret_cast<HANDLE>(intptr_t(-1))
struct WIN32_FIND_DATAW { DWORD dwFileAttributes = 0; wchar_t cFileName[260]{}; };
struct Search { std::vector<std::filesystem::directory_entry> entries; size_t next = 0; };
static bool find_data(Search* s, WIN32_FIND_DATAW* data) {
    if (s->next >= s->entries.size()) return false;
    const auto& entry = s->entries[s->next++];
    auto name = entry.path().filename().wstring();
    if (name.size() >= 260) return false;
    std::fill(std::begin(data->cFileName), std::end(data->cFileName), L'\0');
    std::copy(name.begin(), name.end(), data->cFileName);
    data->dwFileAttributes = entry.is_directory() ? FILE_ATTRIBUTE_DIRECTORY : 0;
    return true;
}
static HANDLE FindFirstFileW(const wchar_t* pattern, WIN32_FIND_DATAW* data) {
    std::wstring path(pattern); auto slash = path.find_last_of(L"\\/");
    auto* s = new Search;
    for (const auto& entry : std::filesystem::directory_iterator(path.substr(0, slash))) s->entries.push_back(entry);
    if (!find_data(s, data)) { delete s; return INVALID_HANDLE_VALUE; }
    return s;
}
static bool FindNextFileW(HANDLE handle, WIN32_FIND_DATAW* data) { return find_data(static_cast<Search*>(handle), data); }
static void FindClose(HANDLE handle) { delete static_cast<Search*>(handle); }
static FILE* test_wfopen(const wchar_t* path, const wchar_t*) {
    std::string name; for (; *path; ++path) name.push_back(*path == L'\\' ? '/' : char(*path));
    return std::fopen(name.c_str(), "rb");
}
#define _wfopen test_wfopen
#define _fseeki64 fseeko
#define _ftelli64 ftello

static std::vector<std::string> lines;
void lp_sink(const char* line);
#include "../wine_gap.h"

void lp_sink(const char* line) { lines.emplace_back(line); }

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    if (wine_gap::route_for_symbol("shell32.dll", "SHGetSpecialFolderPathW", "stub") != "uwph" ||
        wine_gap::route_for_symbol("winmm.dll", "joyGetPos", "stub") != "uwph" ||
        wine_gap::route_for_symbol("winmm.dll", "timeGetTime", "stub") != "stub") {
        std::fprintf(stderr, "per-export GAP route override regression\n");
        return 1;
    }
    wine_gap::report(std::wstring(argv[1], argv[1] + std::char_traits<char>::length(argv[1])));
    // report() probes x86 routes; a fresh guest reservation must remain available
    // after the probe has returned rather than being held by process-lifetime state.
    x86::Memory after_probe;
    if (!after_probe.base()) {
        std::fprintf(stderr, "x86 route probe retained or exhausted the guest reservation\n");
        return 1;
    }
    if (argc == 3 && std::string(argv[2]) == "--assert-fixture") {
        const std::vector<std::string> expected = {
            "GAP: dll=xinput1_3.dll system=1 donor=0 stub=0 missing=0",
            // The api-ms-win-crt sets resolve to the i386 ucrtbase donor.
            "GAP: dll=api-ms-win-crt-heap-l1-1-0.dll system=0 donor=1 stub=0 missing=0",
        };
        for (const auto& want : expected) {
            bool found = false;
            for (const auto& line : lines) if (line == want) found = true;
            if (!found) {
                std::fprintf(stderr, "missing expected line: %s\n", want.c_str());
                for (const auto& line : lines) std::fprintf(stderr, "%s\n", line.c_str());
                return 1;
            }
        }
    }
    for (const auto& line : lines) std::puts(line.c_str());
    return 0;
}
