// Off-device only; desktop memory API aliases mirror tests/mapper-lifecycle.cpp.
// They do not validate UWP FromApp behavior or probe provenance.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"

static unsigned marker_calls, tls_callbacks, dll_attaches, named_calls, ordinal_calls;
static int delay_reject=0;
static int marker_value;
static char last_missing[256];
static int WINAPI callable_marker(int value);
static int WINAPI callable_named(int value) { return value + 10; }
static int WINAPI callable_ordinal(int value) { return value + 20; }
static void log_line(const char* s) { puts(s); if (!strcmp(s,"TLS callback attach")) ++tls_callbacks; if (!strcmp(s,"TLS DllMain attach")) ++dll_attaches; }
static bool injected_zero=false;
static wxl::strict_policy::Classification injected_class=wxl::strict_policy::Classification::callable;
static wxl::strict_policy::Decision resolve_fixture(const char* dll, const char* name, WORD ordinal) {
    char request[256]; snprintf(request, sizeof(request), "RESOLVE %s!%s ordinal=%u", dll, name ? name : "<ordinal>", (unsigned)ordinal); log_line(request);
    if (_stricmp(dll, "strict-host-callback.dll") == 0 && name && strcmp(name, "entry_marker") == 0)
        return {injected_class,(uintptr_t)&callable_marker};
    if (_stricmp(dll, "strict-provider.dll") != 0) goto missing;
    if (name && strcmp(name, "fixture_named") == 0) { ++named_calls; if (delay_reject==1) return {wxl::strict_policy::Classification::probe_only,(uintptr_t)&callable_named}; if (delay_reject==2) return {static_cast<wxl::strict_policy::Classification>(255),(uintptr_t)&callable_named}; if (delay_reject==3) return {wxl::strict_policy::Classification::callable,0}; return {injected_class,injected_zero ? 0 : (uintptr_t)&callable_named}; }
    if (!name && ordinal == 7) { ++ordinal_calls; if (delay_reject==4) return {wxl::strict_policy::Classification::missing,0}; return {injected_class,(uintptr_t)&callable_ordinal}; }
missing:
    char symbol[256];
    if (name) snprintf(symbol, sizeof(symbol), "%s!%s", dll, name);
    else snprintf(symbol, sizeof(symbol), "%s!#%u", dll, (unsigned)ordinal);
    strncpy(last_missing, symbol, sizeof(last_missing)-1); last_missing[sizeof(last_missing)-1]=0;
    char message[320]; snprintf(message, sizeof(message), "IMPORT FAIL %s reason=missing", symbol);
    log_line(message);
    return {wxl::strict_policy::Classification::missing,0};
}
static int WINAPI callable_marker(int value) { ++marker_calls; marker_value = value; return value; }
static bool run_case(const wchar_t* path, bool should_succeed, const char* expected_missing=nullptr) {
    marker_calls = 0; marker_value = 0; last_missing[0]=0;
    wxl::Image image;
    bool ok = wxl::load(image, path, log_line, {}, nullptr, false, resolve_fixture);
    if (ok != should_succeed) return false;
    return should_succeed ? (marker_calls == 1 && marker_value == 33)
                          : (marker_calls == 0 && expected_missing && strcmp(last_missing, expected_missing)==0);
}
static bool arg_path(const char* arg, wchar_t* out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, arg, -1, out, (int)cap) != 0;
}
int main(int argc, char** argv) {
    if (argc != 5) return 2;
    wchar_t paths[4][1024];
    for (int i=0;i<4;i++) if (!arg_path(argv[i+1], paths[i], 1024)) return 2;
    if (!run_case(paths[0], true)) { puts("FAIL named/ordinal positive"); return 1; }
    puts("PASS named/ordinal positive imports");
    if (!run_case(paths[1], false, "strict-provider.dll!absent_name")) { puts("FAIL missing named import diagnostic/marker"); return 1; }
    puts("PASS missing named import rejected before entry");
    if (!run_case(paths[2], false, "strict-provider.dll!#9")) { puts("FAIL missing ordinal diagnostic/marker"); return 1; }
    puts("PASS missing ordinal rejected before entry");
    for (auto classification : {wxl::strict_policy::Classification::probe_only, static_cast<wxl::strict_policy::Classification>(255)}) {
        injected_class=classification; marker_calls=0; wxl::Image image;
        if (wxl::load(image,paths[0],log_line,{},nullptr,false,resolve_fixture) || marker_calls!=0) return 1;
    }
    injected_class=wxl::strict_policy::Classification::callable; injected_zero=true; marker_calls=0;
    { wxl::Image image; if (wxl::load(image,paths[0],log_line,{},nullptr,false,resolve_fixture) || marker_calls) return 1; }
    puts("PASS nonzero probe-only, unknown classification and callable-zero rejected before entry");
    injected_zero=false;
    static BYTE tls_block[64];
    wxl::Carrier carrier{[]()->DWORD{return 0;},[]()->BYTE*{return tls_block;},sizeof tls_block};
    for (int test=0;test<=4;++test) {
        delay_reject=test; tls_callbacks=dll_attaches=named_calls=ordinal_calls=0;
        wxl::Image image; bool ok=wxl::load(image,paths[3],log_line,carrier,nullptr,false,resolve_fixture);
        if (test==0) { if (!ok || named_calls!=1 || ordinal_calls!=1 || tls_callbacks!=1 || dll_attaches!=1 || !image.close()) return 1; }
        else if (ok || tls_callbacks || dll_attaches || image.base) return 1;
    }
    puts("PASS delay named and ordinal callable; probe-only unknown zero missing rejected before TLS and DllMain");
    puts("STRICT-IMPORTS-HARNESS-PASS");
    return 0;
}
