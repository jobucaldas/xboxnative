// SPDX-License-Identifier: GPL-3.0-or-later
// The x86 binder's API-set names (host/x86/apiset.h), checked three ways:
//  1. the mapping: which module serves an import from a contract DLL, and which
//     names stay unresolved;
//  2. the registry against the UCRT table: every declared name the surface
//     serves is cdecl with the header's dword count, the table is served in
//     full, and every msvcrt registration the headers do not declare is on the
//     reviewed msvcrt-only list (a new registration cannot reach the alias path
//     unreviewed);
//  3. with a fixture (argv[1]), an import through api-ms-win-crt-string-l1-1-0.dll
//     binds and returns the right value, and an unmapped API-set name stays
//     unresolved and is named by the launcher's gap list, exactly.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include "../host/x86/apiset.h"
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_play.h"
#include "../x86_route.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

// msvcrt registrations that no UCRT header declares (checked against mingw-w64
// 14 and its sys-include tree). They bind only from a literal msvcrt.dll import
// and are never reachable through an api-ms-win-crt-* name: apiset::declared()
// is false for them, so served_by() leaves the contract name in place.
// _fstat and _fstati64 are macros over _fstat32/_fstat64i32/_fstat64 in mingw.
static const std::set<std::string> kMsvcrtOnly = {
    "_amsg_exit", "_free", "_fstat", "_fstati64", "__getmainargs", "__lc_codepage", "_lock",
    "_malloc", "__p__commode", "__p___initenv", "__p__iob", "__p___mb_cur_max", "__set_app_type",
    "_unlock", "__wgetmainargs"};

int main(int argc, char** argv) {
    // 1. Mapping.
    CHECK(apiset::served_by("api-ms-win-crt-string-l1-1-0.dll", "strlen") == "msvcrt.dll");
    CHECK(apiset::served_by("api-ms-win-crt-string-l1-1-0.dll", "_lock") == "api-ms-win-crt-string-l1-1-0.dll");
    CHECK(apiset::served_by("api-ms-win-crt-private-l1-1-0.dll", "strlen") == "api-ms-win-crt-private-l1-1-0.dll");
    CHECK(apiset::served_by("api-ms-win-foo-l1-1-0.dll", "frobnicate") == "api-ms-win-foo-l1-1-0.dll");
    CHECK(apiset::served_by("api-ms-win-core-synch-l1-2-0.dll", "Sleep") == "kernel32.dll");
    CHECK(apiset::served_by("kernelbase.dll", "Sleep") == "kernel32.dll");
    CHECK(apiset::served_by("api-ms-win-core-com-l1-1-0.dll", "CoCreateInstance") == "ole32.dll");
    CHECK(apiset::served_by("ucrtbase.dll", "strlen") == "ucrtbase.dll");
    CHECK(apiset::served_by("msvcrt.dll", "strlen") == "msvcrt.dll");

    // 2. Registry against the header table.
    Memory mem; ThunkTable table; Host host; Registry reg;
    Guest guest(mem, host, reg);
    guest.register_all();
    size_t served = 0;
    for (const auto& d : apiset::kUcrtDeclared) {
        auto it = reg.map.find({"msvcrt.dll", Registry::key_sym(d.name)});
        if (it == reg.map.end()) { printf("FAIL table name not served: %s\n", d.name); ++failures; continue; }
        ++served;
        if (it->second.conv != Conv::Cdecl || it->second.argc != d.dwords)
            { ++failures; printf("FAIL %s: registry conv/argc differ from the UCRT header (dwords %u)\n", d.name, d.dwords); }
    }
    constexpr size_t kTableSize = sizeof(apiset::kUcrtDeclared) / sizeof(apiset::kUcrtDeclared[0]);
    CHECK(served == kTableSize);
    CHECK(kTableSize == 99);
    for (const auto& kv : reg.map) {
        if (kv.first.first != "msvcrt.dll") continue;
        const std::string& name = kv.first.second;
        if (apiset::declared(name)) continue;
        if (!kMsvcrtOnly.count(name)) { ++failures; printf("FAIL msvcrt registration not declared by the UCRT headers and not reviewed: %s\n", name.c_str()); }
    }
    ++checks;  // the reviewed list is only a reviewed list if every entry is still served
    for (const auto& n : kMsvcrtOnly) if (reg.map.find({"msvcrt.dll", Registry::key_sym(n)}) == reg.map.end()) { ++failures; printf("FAIL reviewed msvcrt-only name is not served: %s\n", n.c_str()); }

    // 3. Fixture: the API-set imports end to end.
    if (argc >= 2) {
        std::ifstream f(argv[1], std::ios::binary);
        if (!f) { printf("FAIL cannot open %s\n", argv[1]); return 2; }
        std::vector<uint8_t> file{std::istreambuf_iterator<char>(f), {}};
        auto r = x86::run_image(file, "apiset.exe", "\"apiset.exe\"",
            [](const std::string&) {}, [](const x86::PlayResult&) {}, x86::kGuestRunCap);
        CHECK(r.loaded && r.finished && r.exit_code == 0);
        // strlen and ExitProcess bind; frobnicate is the one unresolved import.
        CHECK(r.imports == 3 && r.bound == 2);
        CHECK(r.unresolved.size() == 1 && r.unresolved[0] == "api-ms-win-foo-l1-1-0.dll!frobnicate");
        // The launcher names it, with the API-set name the game imported.
        auto rep = x86_route::report(r, "apiset.exe");
        CHECK(rep.gap.size() == 2 && rep.gap[0] == "x86 gap: api-ms-win-foo-l1-1-0.dll!frobnicate" && rep.gap[1] == "x86 gap: total=1");
    }
    printf("x86-apiset: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
