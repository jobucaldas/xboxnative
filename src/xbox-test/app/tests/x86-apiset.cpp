// SPDX-License-Identifier: GPL-3.0-or-later
// The x86 binder's API-set names (host/x86/apiset.h), checked in four parts:
//  1. the mapping of single names (contract DLL -> serving module);
//  2. the registry against the UCRT table: each declared name the surface serves is
//     cdecl with the header's dword count, the table is served in full, and every
//     msvcrt registration the headers do not declare is on the reviewed list;
//  3. both directions over the whole table: every compatible served name binds to
//     msvcrt.dll, while ABI- or behavior-incompatible names stay on their contract DLL;
//  4. the PE32 header-layout probe lists the reviewed mismatches (lconv, generic
//     time_t/off_t structs and FILE); _get_osfhandle still binds on matching i386 ABI;
//  5. with a fixture (argv[1]): the bound and unbound imports run end to end, and
//     the gap list names each unbound import exactly.
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

// Served names that must stay unresolved for ABI/layout reasons, including lconv.
static const std::set<std::string> kServedAbiSensitive = {"time", "_filelengthi64", "_lseeki64", "localeconv"};
static const std::set<std::string> kServedBehaviorSensitive = {
    "_fstat64", "__setusermatherr", "_wfopen", "clock", "feof", "fgetpos",
    "fopen", "fread", "fsetpos", "getenv", "wcsftime"};

int main(int argc, char** argv) {
    const std::string crt_time = "api-ms-win-crt-time-l1-1-0.dll";
    const std::string crt_fs = "api-ms-win-crt-filesystem-l1-1-0.dll";
    const std::string crt_locale = "api-ms-win-crt-locale-l1-1-0.dll";

    // 1. Mapping of single names.
    CHECK(apiset::served_by("api-ms-win-crt-string-l1-1-0.dll", "strlen") == "msvcrt.dll");
    CHECK(apiset::served_by("api-ms-win-crt-string-l1-1-0.dll", "_lock") == "api-ms-win-crt-string-l1-1-0.dll");
    CHECK(apiset::served_by("api-ms-win-crt-private-l1-1-0.dll", "strlen") == "api-ms-win-crt-private-l1-1-0.dll");
    CHECK(apiset::served_by("api-ms-win-foo-l1-1-0.dll", "frobnicate") == "api-ms-win-foo-l1-1-0.dll");
    CHECK(apiset::served_by("api-ms-win-core-synch-l1-2-0.dll", "Sleep") == "kernel32.dll");
    CHECK(apiset::served_by("kernelbase.dll", "Sleep") == "kernel32.dll");
    CHECK(apiset::served_by("api-ms-win-core-com-l1-1-0.dll", "CoCreateInstance") == "ole32.dll");
    CHECK(apiset::served_by("ucrtbase.dll", "strlen") == "ucrtbase.dll");
    CHECK(apiset::served_by("msvcrt.dll", "strlen") == "msvcrt.dll");
    CHECK(apiset::served_by(crt_time, "time") == crt_time);
    CHECK(apiset::served_by(crt_time, "TIME") == crt_time);  // case-insensitive, as the binder
    CHECK(apiset::served_by(crt_locale, "localeconv") == crt_locale);

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

    // 3. Both directions over the served table, and the guard list.
    std::set<std::string> excluded_abi, excluded_behavior;
    for (const auto& d : apiset::kUcrtDeclared) {
        const bool compatible = apiset::ucrt_compatible(d.name);
        const std::string want = compatible ? "msvcrt.dll" : crt_time;
        if (apiset::served_by(crt_time, d.name) != want) { ++failures; printf("FAIL direction: %s should resolve to %s\n", d.name, want.c_str()); }
        ++checks;
        if (apiset::abi_sensitive(d.name)) excluded_abi.insert(apiset::lower(d.name));
        if (apiset::behavior_sensitive(d.name)) excluded_behavior.insert(apiset::lower(d.name));
    }
    CHECK(excluded_abi == kServedAbiSensitive);
    CHECK(excluded_behavior == kServedBehaviorSensitive);
    // ABI- and behavior-incompatible names of any kind stay on their contract DLL.
    for (const char* n : apiset::kWidthSensitive) {
        if (apiset::served_by(crt_time, n) != crt_time) { ++failures; printf("FAIL ABI-sensitive name aliased: %s\n", n); }
        ++checks;
    }
    for (const char* n : apiset::kBehaviorSensitive) {
        if (apiset::served_by(crt_time, n) != crt_time) { ++failures; printf("FAIL behavior-sensitive name aliased: %s\n", n); }
        ++checks;
    }
    // The intptr_t helper binds: _get_osfhandle is declared, compatible, and aliases to msvcrt.
    CHECK(apiset::declared("_get_osfhandle") && apiset::ucrt_compatible("_get_osfhandle"));
    CHECK(apiset::served_by(crt_fs, "_get_osfhandle") == "msvcrt.dll");
    CHECK(reg.map.count({"msvcrt.dll", "_get_osfhandle"}) == 1);

    // 4. Fixture: the API-set imports end to end.
    if (argc >= 2) {
        std::ifstream f(argv[1], std::ios::binary);
        if (!f) { printf("FAIL cannot open %s\n", argv[1]); return 2; }
        std::vector<uint8_t> file{std::istreambuf_iterator<char>(f), {}};
        auto r = x86::run_image(file, "apiset.exe", "\"apiset.exe\"",
            [](const std::string&) {}, [](const x86::PlayResult&) {}, x86::kGuestRunCap);
        CHECK(r.loaded && r.finished && r.exit_code == 0);
        // ExitProcess, strlen and _get_osfhandle bind; localeconv, _lseeki64, time and frobnicate do not.
        CHECK(r.imports == 7 && r.bound == 3);
        CHECK(r.unresolved.size() == 4);
        std::set<std::string> unresolved(r.unresolved.begin(), r.unresolved.end());
        CHECK(unresolved.count(crt_fs + "!_lseeki64") == 1);
        CHECK(unresolved.count(crt_locale + "!localeconv") == 1);
        CHECK(unresolved.count(crt_time + "!time") == 1);
        CHECK(unresolved.count("api-ms-win-foo-l1-1-0.dll!frobnicate") == 1);
        // The launcher names all three, grouped by DLL, with the total.
        auto rep = x86_route::report(r, "apiset.exe");
        std::vector<std::string> want_gap = {
            "x86 gap: " + crt_fs + "!_lseeki64",
            "x86 gap: " + crt_locale + "!localeconv",
            "x86 gap: " + crt_time + "!time",
            "x86 gap: api-ms-win-foo-l1-1-0.dll!frobnicate",
            "x86 gap: total=4"};
        CHECK(rep.gap.size() == want_gap.size());
        for (size_t i = 0; i < want_gap.size() && i < rep.gap.size(); ++i)
            if (rep.gap[i] != want_gap[i]) { ++failures; printf("FAIL gap[%zu]: got %s, want %s\n", i, rep.gap[i].c_str(), want_gap[i].c_str()); }
    }
    printf("x86-apiset: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
