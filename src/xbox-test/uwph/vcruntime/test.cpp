// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contract test for the vcruntime140 -> ucrtbase route. No console use.
#include "route.h"
#include <cstdio>
#include <string>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
static std::string last;
static void* image = reinterpret_cast<void*>(0x140000000);
static PVOID WINAPI header(PVOID, PVOID* base) { *base = image; return image; }
static uintptr_t host(const char* dll, const char* name) {
    last = std::string(dll) + "!" + name;
    if (!strcmp(dll, "kernel32.dll") && !strcmp(name, "RtlPcToFileHeader")) return reinterpret_cast<uintptr_t>(&header);
    if (!strcmp(dll, "ucrtbase.dll") && strcmp(name, "_NLG_Return")) return 0x1000 + strlen(name);
    return 0;
}
static ULONG_PTR record[5];
static LONG CALLBACK capture(EXCEPTION_POINTERS* info) {
    auto r = info->ExceptionRecord;
    if (r->ExceptionCode != 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    record[4] = r->NumberParameters | (r->ExceptionFlags & EXCEPTION_NONCONTINUABLE) << 8;
    for (int i = 0; i < 4; ++i) record[i] = r->ExceptionInformation[i];
    return EXCEPTION_CONTINUE_SEARCH;
}
int main() {
    using vcruntime_core::resolve;
    CHECK(!resolve("msvcp140.dll", "memcpy", host));
    CHECK(!resolve("vcruntime140.dll", nullptr, host));
    CHECK(!resolve("vcruntime140.dll", "memcpy", nullptr));
    CHECK(!resolve("VCRUNTIME140.dll", "memcpy", host));  // the host lowercases keys
    CHECK(resolve("vcruntime140.dll", "memcpy", host) == 0x1000 + 6 && last == "ucrtbase.dll!memcpy");
    CHECK(resolve("vcruntime140.dll", "__CxxFrameHandler3", host) == 0x1000 + 18);
    CHECK(!resolve("vcruntime140.dll", "__CxxFrameHandler4", host));      // vcruntime140_1 only
    CHECK(resolve("vcruntime140_1.dll", "__CxxFrameHandler4", host) == 0x1000 + 18);
    CHECK(!resolve("vcruntime140_1.dll", "memcpy", host));
    CHECK(resolve("vcruntime140.dll", "__TypeMatch", host) == 0x1000 + 11);  // offered when native has it
    CHECK(!resolve("vcruntime140.dll", "_NLG_Return", host));                // and absent when it has not
    last.clear();
    CHECK(!resolve("vcruntime140.dll", "NotAVcruntimeExport", host) && last.empty());
    // Owned helpers never come from ucrtbase.
    for (auto name : {"__telemetry_main_invoke_trigger", "__uncaught_exceptions", "__vcrt_GetModuleHandleW",
                      "__vcrt_LoadLibraryExW", "__vcrt_GetModuleFileNameW", "__vcrt_InitializeCriticalSectionEx",
                      "__report_gsfailure", "unexpected", "_CxxThrowException"}) {
        auto p = resolve("vcruntime140.dll", name, host);
        CHECK(p && p >= 0x10000);
    }
    // __vcrt_GetModuleHandleW goes through the host's kernel32.
    auto handle = reinterpret_cast<HMODULE (__cdecl*)(LPCWSTR)>(resolve("vcruntime140.dll", "__vcrt_GetModuleHandleW", host));
    SetLastError(0); CHECK(!handle(L"x.dll") && GetLastError() == ERROR_PROC_NOT_FOUND && last == "kernel32.dll!GetModuleHandleW");
    // The x64 throw record carries the image base from the host's RtlPcToFileHeader.
    auto raise = reinterpret_cast<void (__stdcall*)(void*, const void*)>(resolve("vcruntime140.dll", "_CxxThrowException", host));
    AddVectoredExceptionHandler(1, capture);
    static const unsigned info[4] = {0x8, 0, 0, 0};  // TI_IsPure ThrowInfo
    int object = 73;
    __try { raise(&object, info); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    CHECK(record[0] == 0x01994000 && record[1] == (ULONG_PTR)&object && record[2] == (ULONG_PTR)info);
    CHECK(record[3] == (ULONG_PTR)image && record[4] == (4 | EXCEPTION_NONCONTINUABLE << 8));
    __try { raise(nullptr, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    CHECK(record[0] == 0x19930520 && !record[1] && !record[2] && !record[3]);
    puts("VCRUNTIME-ROUTE PASS");
    return 0;
}
