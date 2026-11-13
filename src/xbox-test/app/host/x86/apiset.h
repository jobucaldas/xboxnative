// SPDX-License-Identifier: GPL-3.0-or-later
// API-set names for the x86 import binder. An import from an API-set DLL
// (api-ms-win-*) is a contract name, not a module that holds the code: Windows
// resolves it through the API-set schema to the module that implements it. The
// x86 binder maps only the two families below, from documented facts:
//
//  * api-ms-win-crt-<area>-l1-1-0.dll: Universal CRT contracts. Microsoft's
//    "Universal CRT" deployment and "Windows API sets" documentation (Microsoft
//    Learn) state that these contracts are implemented by ucrtbase.dll. The x86
//    surface serves the UCRT functions under msvcrt.dll, the same policy the
//    x64 side uses (play_runtime.h crtExitApi), so a contract name binds to
//    msvcrt.dll. The "private" contract is not a public surface and is not
//    mapped.
//  * api-ms-win-core-*: forwarded to kernel32.dll (kernelbase.dll is served by
//    kernel32 here, the same rule as play_runtime.h resolve()); the
//    api-ms-win-core-com-l1-* contract goes to ole32.dll, as play_runtime.h does.
//
// A UCRT contract name binds only when the UCRT headers declare the symbol
// (kUcrtDeclared). Undeclared names (the msvcrt-only legacy exports) stay
// unresolved from a contract DLL and show up in the launcher's "x86 gap:" list,
// so nothing is guessed. Every other API-set name is left as it is.
//
// kUcrtDeclared was derived from the mingw-w64 14 UCRT headers, with
// `gcc -fsyntax-only -aux-info` over the standard CRT headers (stdio, stdlib,
// string, time, locale, math, ctype, wchar, io, process, sys/stat). Each entry is
// the function's declared parameter count in 32-bit dwords (a 64-bit integer is
// two dwords). The static check in tests/x86-apiset.cpp compares the registry
// against this table, so a convention or arity drift fails there.
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
// The module whose surface answers an import of `sym` from `dll` (both lowercase
// as the binder reads them). Unmapped names return `dll` unchanged.
inline std::string served_by(const std::string& dll, const std::string& sym) {
    if (dll == "kernelbase.dll") return "kernel32.dll";
    if (dll.rfind("api-ms-win-core-com-l1-", 0) == 0) return "ole32.dll";
    if (dll.rfind("api-ms-win-core-", 0) == 0) return "kernel32.dll";
    if (ucrt_contract(dll)) return declared(sym) ? "msvcrt.dll" : dll;
    return dll;
}

} // namespace apiset
} // namespace x86
