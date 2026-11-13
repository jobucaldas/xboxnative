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
//    symbols kUcrtDeclared lists, except the width-sensitive names in
//    kWidthSensitive. Those stay unresolved.
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
// kWidthSensitive: names whose width, struct layout or return register differs between
// the msvcrt.dll surface and the UCRT, or that the surface does not implement as the
// UCRT does. A contract name for them is never aliased. Each entry cites the UCRT
// declaration (mingw-w64 14 headers, sys-include) that makes it width- or
// layout-dependent. Lowercase, as the binder compares.
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

// Width-sensitive names. Served by the surface (in kUcrtDeclared), excluded from
// the contract path until a thunk with the UCRT width exists:
//  time            time.h:236 time_t time(time_t*) __MINGW_ASM_CALL(_time32): the
//                  i686 mingw default is a 32-bit time_t (_mingw.h:394-396); the
//                  UCRT default is __time64_t (time.h:54-57, _time64 at time.h:186).
//  fgetpos,fsetpos stdio.h:555 "64bit only, no 32bit version"; fpos_t is __int64
//                  (stdio.h:112). The surface does not model the position.
//  _filelengthi64  io.h:249 returns __int64: EDX:EAX. The thunk sets EAX only.
//  _lseeki64       io.h:256 returns __int64 (EDX:EAX) and takes an __int64 offset.
//                  The thunk sets EAX only, so a direct msvcrt import would read a
//                  stale EDX. Excluded here; the thunk needs a 64-bit return path.
//  _fstat64        sys/stat.h:71 takes struct _stat64*, whose _off_t and time fields
//                  differ by time_t width (_mingw_stat64.h:37 and time fields).
//  localeconv      locale.h:91 returns struct lconv*. The surface writes its own
//                  48-byte block; the layout was not checked against the UCRT path.
//  wcsftime        time.h:198. The surface returns 0 with no output (a stub), so a
//                  contract import would silently produce nothing.
//  clock           the surface returns a constant 0 (a stub). A timing loop would
//                  stop measuring.
// Declared-not-served family (never aliased if a thunk is added later): the time_t
// family (time.h:142 asctime, :177 _difftime64, :180 _gmtime64, :182 _localtime64,
// :184 _mktime64, :186 _time64, :160 _time32, :240 difftime, :241 localtime,
// :243 gmtime, :245 ctime, :247 mktime), _ftime (sys/timeb.h:78, :88, :91),
// the stat family (sys/stat.h:71 _fstat64, :74 _stat64, :83 _wstat64;
// _mingw_stat64.h:13-24 macros), the find family (io.h:85-97 macros, :251 _findfirst64,
// :254 _findnext64), and utime (sys/utime.h:89-90 _utime64/_futime64, :105-106).
inline const char* const kWidthSensitive[] = {
    "time", "_time32", "_time64", "mktime", "_mktime64", "localtime", "_localtime64",
    "gmtime", "_gmtime64", "difftime", "_difftime64", "ctime", "asctime",
    "_ftime", "_ftime32", "_ftime64",
    "_stat", "_stat32", "_stat64", "_stat64i32", "_wstat", "_wstat32", "_wstat64", "_wstat64i32",
    "_fstat", "_fstat32", "_fstat64", "_fstat64i32", "_fstati64",
    "_findfirst", "_findfirst32", "_findfirst64", "_findfirst64i32",
    "_findnext", "_findnext32", "_findnext64", "_findnext64i32",
    "_utime", "_utime32", "_utime64", "_futime", "_futime32", "_futime64",
    "_filelengthi64", "_lseeki64", "fgetpos", "fsetpos",
    "localeconv", "wcsftime", "clock",
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
// True for a width-sensitive name (kWidthSensitive).
inline bool width_sensitive(const std::string& sym) {
    std::string s = lower(sym);
    for (const char* n : kWidthSensitive) if (s == n) return true;
    return false;
}
// The module whose surface answers an import of `sym` from `dll` (both lowercase
// as the binder reads them). Unmapped names return `dll` unchanged.
inline std::string served_by(const std::string& dll, const std::string& sym) {
    if (dll == "kernelbase.dll") return "kernel32.dll";
    if (dll.rfind("api-ms-win-core-com-l1-", 0) == 0) return "ole32.dll";
    if (dll.rfind("api-ms-win-core-", 0) == 0) return "kernel32.dll";
    if (ucrt_contract(dll)) return (declared(sym) && !width_sensitive(sym)) ? "msvcrt.dll" : dll;
    return dll;
}

} // namespace apiset
} // namespace x86
