// Off-device only; desktop memory API aliases mirror tests/mapper-lifecycle.cpp.
// They do not validate UWP FromApp behavior or probe provenance.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"

static unsigned marker_calls;
static int marker_value;
static char last_missing[256];
static int WINAPI callable_marker(int value);
static int WINAPI callable_named(int value) { return value + 10; }
static int WINAPI callable_ordinal(int value) { return value + 20; }
static void log_line(const char* s) { puts(s); }
static uintptr_t resolve_fixture(const char* dll, const char* name, WORD ordinal) {
    char request[256]; snprintf(request, sizeof(request), "RESOLVE %s!%s ordinal=%u", dll, name ? name : "<ordinal>", (unsigned)ordinal); log_line(request);
    if (_stricmp(dll, "strict-host-callback.dll") == 0 && name && strcmp(name, "entry_marker") == 0)
        return (uintptr_t)&callable_marker;
    if (_stricmp(dll, "strict-provider.dll") != 0) goto missing;
    if (name && strcmp(name, "fixture_named") == 0) return (uintptr_t)&callable_named;
    if (!name && ordinal == 7) return (uintptr_t)&callable_ordinal;
missing:
    char symbol[256];
    if (name) snprintf(symbol, sizeof(symbol), "%s!%s", dll, name);
    else snprintf(symbol, sizeof(symbol), "%s!#%u", dll, (unsigned)ordinal);
    strncpy(last_missing, symbol, sizeof(last_missing)-1); last_missing[sizeof(last_missing)-1]=0;
    char message[320]; snprintf(message, sizeof(message), "IMPORT FAIL %s reason=missing", symbol);
    log_line(message);
    return 0;
}
static int WINAPI callable_marker(int value) { ++marker_calls; marker_value = value; return value; }
static bool run_case(const wchar_t* path, bool should_succeed, const char* expected_missing=nullptr) {
    marker_calls = 0; marker_value = 0; last_missing[0]=0;
    wxl::Image image;
    bool ok = wxl::load(image, path, log_line, {}, resolve_fixture, false);
    if (ok != should_succeed) return false;
    return should_succeed ? (marker_calls == 1 && marker_value == 33)
                          : (marker_calls == 0 && expected_missing && strcmp(last_missing, expected_missing)==0);
}
static bool arg_path(const char* arg, wchar_t* out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, arg, -1, out, (int)cap) != 0;
}
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    wchar_t paths[3][1024];
    for (int i=0;i<3;i++) if (!arg_path(argv[i+1], paths[i], 1024)) return 2;
    if (!run_case(paths[0], true)) { puts("FAIL named/ordinal positive"); return 1; }
    puts("PASS named/ordinal positive imports");
    if (!run_case(paths[1], false, "strict-provider.dll!absent_name")) { puts("FAIL missing named import diagnostic/marker"); return 1; }
    puts("PASS missing named import rejected before entry");
    if (!run_case(paths[2], false, "strict-provider.dll!#9")) { puts("FAIL missing ordinal diagnostic/marker"); return 1; }
    puts("PASS missing ordinal rejected before entry");
    puts("STRICT-IMPORTS-HARNESS-PASS");
    return 0;
}
