// SPDX-License-Identifier: GPL-3.0-or-later
// API-set names for the x86 import binder. An import from an API-set DLL
// (api-ms-win-*) is a contract name, not a module that holds the code. Microsoft's
// API set documentation (Microsoft Learn, "API sets"; "Universal CRT deployment")
// lists these as documented API set contract names, and says that ucrtbase.dll
// hosts the Universal CRT implementations. The x86 binder maps only the families
// below; the mapping rules mirror the x64 side (play_runtime.h resolve() and
// crtExitApi). Page titles are cited here; the pages were not re-read in this
// environment, so check them when this table changes.
//
//  * api-ms-win-crt-<area>-l1-1-0.dll (public areas): served by msvcrt.dll for the
//    symbols kUcrtDeclared lists, except ABI-incompatible names in kWidthSensitive
//    and incomplete thunks in kBehaviorSensitive. Those stay unresolved.
//  * api-ms-win-core-*: kernel32.dll. kernelbase.dll: kernel32.dll.
//    api-ms-win-core-com-l1-*: ole32.dll.
//
// Anything else stays as it is: unresolved, and named by the launcher's "x86 gap:"
// list. Nothing is guessed.
//
// kUcrtDeclared: the functions the surface serves under msvcrt.dll that the mingw-w64
// 14 UCRT headers declare, with the declared parameter count in 32-bit dwords (a
// 64-bit integer is two dwords). Derived with `gcc -fsyntax-only -aux-info` over the
// standard CRT headers; vsprintf is from its declaration line (stdio.h:459). The
// static check in tests/x86-apiset.cpp compares the registry to this table.
//
// kWidthSensitive: names whose i386 ABI differs between the msvcrt.dll surface and
// UCRT, by width or structure layout. A 32-bit intptr_t, HANDLE, size_t, long or int is
// the same on i386 and binds. Each listed name cites its UCRT declaration below.
// kBehaviorSensitive separately names served partial/stub thunks that do not satisfy
// the UCRT contract and therefore must remain unresolved. All names are lowercase.
#pragma once
#include <string>
namespace x86 {
namespace apiset {

struct UcrtDecl { const char* name; unsigned dwords; };
inline const UcrtDecl kUcrtDeclared[] = {
    {"__iob_func", 0},
    {"__p__fmode", 0},
    {"__setusermatherr", 1},
    {"_cexit", 0},
    {"_errno", 0},
    {"_exit", 1},
    {"_fdopen", 2},
    {"_filelengthi64", 1},
    {"_fileno", 1},
    {"_fstat64", 2},
    {"_get_osfhandle", 1},
    {"_initterm", 2},
    {"_initterm_e", 2},
    {"_isatty", 1},
    {"_lrotl", 2},
    {"_lrotr", 2},
    {"_lseeki64", 4},
    {"_onexit", 1},
    {"_read", 3},
    {"_set_fmode", 1},
    {"_setmode", 2},
    {"_snprintf", 3},
    {"_stricmp", 2},
    {"_vsnprintf", 4},
    {"_wfopen", 2},
    {"_write", 3},
    {"abort", 0},
    {"atexit", 1},
    {"atoi", 1},
    {"atol", 1},
    {"calloc", 2},
    {"clock", 0},
    {"exit", 1},
    {"fclose", 1},
    {"feof", 1},
    {"ferror", 1},
    {"fflush", 1},
    {"fgetpos", 2},
    {"fopen", 2},
    {"fprintf", 2},
    {"fputc", 2},
    {"fputs", 2},
    {"fread", 4},
    {"free", 1},
    {"fsetpos", 2},
    {"fwrite", 4},
    {"getc", 1},
    {"getenv", 1},
    {"getwc", 1},
    {"iswctype", 2},
    {"localeconv", 0},
    {"malloc", 1},
    {"memchr", 3},
    {"memcmp", 3},
    {"memcpy", 3},
    {"memmove", 3},
    {"memset", 3},
    {"printf", 1},
    {"putc", 2},
    {"putchar", 1},
    {"puts", 1},
    {"putwc", 2},
    {"qsort", 4},
    {"realloc", 2},
    {"setbuf", 2},
    {"setlocale", 2},
    {"setvbuf", 4},
    {"signal", 2},
    {"snprintf", 3},
    {"sprintf", 2},
    {"strcat", 2},
    {"strchr", 2},
    {"strcmp", 2},
    {"strcoll", 2},
    {"strcpy", 2},
    {"strerror", 1},
    {"strftime", 4},
    {"strlen", 1},
    {"strncmp", 3},
    {"strncpy", 3},
    {"strnlen", 2},
    {"strrchr", 2},
    {"strstr", 2},
    {"strtoul", 3},
    {"strxfrm", 3},
    {"time", 1},
    {"tolower", 1},
    {"toupper", 1},
    {"towlower", 1},
    {"towupper", 1},
    {"ungetc", 2},
    {"ungetwc", 2},
    {"vfprintf", 3},
    {"vprintf", 2},
    {"vsprintf", 3},
    {"wcscoll", 2},
    {"wcsftime", 4},
    {"wcslen", 1},
    {"wcsxfrm", 3},
};

// ABI-sensitive names, with UCRT declarations (mingw-w64 sys-include) for every
// served exclusion. Width and structure-layout differences both count.
//
// Served by the surface (in kUcrtDeclared) and excluded:
//  time            time.h:236 "time_t time(time_t*) __MINGW_ASM_CALL(_time32)". The UCRT
//                  default makes time_t __time64_t, an __int64 (time.h:54-57; _time64 at
//                  time.h:186). The surface returns a 32-bit value in EAX.
//  _filelengthi64  io.h:249 "__int64 _filelengthi64(int)": a 64-bit return in EDX:EAX.
//                  The thunk sets EAX only.
//  _lseeki64       io.h:256 "__int64 _lseeki64(int, __int64, int)": a 64-bit offset and
//                  a 64-bit return in EDX:EAX. The thunk sets EAX only.
//  localeconv      locale.h:45-74 "struct lconv"; UCRT adds eight trailing wchar_t*
//                  members (_W_decimal_point through _W_negative_sign, lines 64-73).
//                  PE32 layout probe: 48-byte msvcrt 7 view vs 80-byte UCRT view;
//                  the thunk allocates/writes only 48 bytes, so an UCRT caller would
//                  read past it. Keep the API-set import unresolved.
//
// Declared, not served: a guard for a later thunk; they bind to nothing today.
//  time_t parameter or return (32 vs 64-bit time_t, time.h:54-57): _time32 time.h:160,
//    _time64 time.h:186, mktime time.h:247, _mktime64 time.h:184, localtime time.h:241,
//    _localtime64 time.h:182, gmtime time.h:243, _gmtime64 time.h:180, difftime
//    time.h:240, _difftime64 time.h:177, ctime time.h:245.
//  struct with time_t or _off_t fields: _ftime (sys/timeb.h:88, macro over
//    _ftime32/_ftime64; struct __timeb64 at sys/timeb.h:78 holds __time64_t); _stat
//    (_mingw_stat64.h:15,22, macro over _stat32/_stat64i32); _stat64 sys/stat.h:74;
//    _wstat (_mingw_stat64.h:17,24) and _wstat64 sys/stat.h:83; _fstat (_mingw_stat64.h:
//    13,20, macro over _fstat32/_fstat64i32); _findfirst and _findnext (io.h:85-97,
//    macros over the 32- and 64-bit finddata structs), _findfirst64 io.h:251,
//    _findnext64 io.h:254; _utime and _futime (sys/utime.h:105-106, macros over
//    _utime32/_utime64), _utime64 sys/utime.h:89 (struct __utimbuf64), _futime64
//    sys/utime.h:90.
//
// The compile-and-run layout probe compares all of these from both header views:
// struct tm, the fixed-width _stat* and _finddata* variants, __timeb64, div_t/ldiv_t/
// lldiv_t, _exception and _complex match. Generic struct stat, _finddata_t variants,
// and _timeb differ because their fields follow time_t/off_t; their names remain in
// this guard list and are not served. FILE is 32 bytes in msvcrt 7 and one pointer in
// UCRT (stdio.h:32-48), but UCRT exposes FILE as opaque and defines no inline accessor
// that dereferences its fields; this surface uses 32-byte private tokens and dispatches
// every FILE* operation by token address, so the guest never reads the msvcrt fields.
// _get_osfhandle (io.h:311; intptr_t is int on i386, corecrt.h:64) and
// _open_osfhandle (io.h:312; intptr_t argument) have matching widths.

// These served names bind only on a literal msvcrt.dll import. Their current thunks
// are partial/stubs and do not satisfy the UCRT contract for general callers.
inline const char* const kBehaviorSensitive[] = {
    "_fstat64", "__setusermatherr", "_wfopen", "clock", "feof", "fgetpos",
    "fopen", "fread", "fsetpos", "getenv", "wcsftime",
};
inline const char* const kWidthSensitive[] = {
    // served and excluded
    "time", "_filelengthi64", "_lseeki64", "localeconv",
    // declared, not served: time_t parameter or return
    "_time32", "_time64", "mktime", "_mktime64", "localtime", "_localtime64", "gmtime",
    "_gmtime64", "difftime", "_difftime64", "ctime",
    // declared, not served: struct with time_t or _off_t fields
    "_ftime", "_ftime32", "_ftime64",
    "stat", "fstat", "wstat", "_stat", "_stat32", "_stat32i64", "_stat64", "_stat64i32",
    "_wstat", "_wstat32", "_wstat32i64", "_wstat64", "_wstat64i32",
    "_fstat", "_fstat32", "_fstat32i64", "_fstat64i32",
    "_findfirst", "_findfirsti64", "_findfirst32", "_findfirst32i64", "_findfirst64", "_findfirst64i32",
    "_findnext", "_findnexti64", "_findnext32", "_findnext32i64", "_findnext64", "_findnext64i32",
    "_wfindfirst", "_wfindfirsti64", "_wfindfirst32", "_wfindfirst32i64", "_wfindfirst64", "_wfindfirst64i32",
    "_wfindnext", "_wfindnexti64", "_wfindnext32", "_wfindnext32i64", "_wfindnext64", "_wfindnext64i32",
    "_utime", "_utime32", "_utime64", "_futime", "_futime32", "_futime64",
};

inline std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return s;
}
// True for the UCRT contract names the surface maps (every public area).
inline bool ucrt_contract(const std::string& dll) {
    static const char* const areas[] = {"conio", "convert", "environment", "filesystem", "heap", "locale",
                                        "math", "multibyte", "process", "runtime", "stdio", "string", "time", "utility"};
    for (const char* a : areas) if (dll == std::string("api-ms-win-crt-") + a + "-l1-1-0.dll") return true;
    return false;
}
// True when the UCRT headers declare `sym` (case-insensitive, as the registry).
inline bool declared(const std::string& sym) {
    std::string s = lower(sym);
    for (const auto& d : kUcrtDeclared) if (s == lower(d.name)) return true;
    return false;
}
// True for a width- or layout-sensitive name (kWidthSensitive).
inline bool abi_sensitive(const std::string& sym) {
    std::string s = lower(sym);
    for (const char* n : kWidthSensitive) if (s == n) return true;
    return false;
}
inline bool behavior_sensitive(const std::string& sym) {
    std::string s = lower(sym);
    for (const char* n : kBehaviorSensitive) if (s == n) return true;
    return false;
}
inline bool ucrt_compatible(const std::string& sym) {
    return !abi_sensitive(sym) && !behavior_sensitive(sym);
}
// The module whose surface answers an import of `sym` from `dll` (both lowercase
// as the binder reads them). Unmapped names return `dll` unchanged.
inline std::string served_by(const std::string& dll, const std::string& sym) {
    if (dll == "kernelbase.dll") return "kernel32.dll";
    if (dll.rfind("api-ms-win-core-com-l1-", 0) == 0) return "ole32.dll";
    if (dll.rfind("api-ms-win-core-", 0) == 0) return "kernel32.dll";
    if (ucrt_contract(dll)) return (declared(sym) && ucrt_compatible(sym)) ? "msvcrt.dll" : dll;
    return dll;
}

} // namespace apiset
} // namespace x86
