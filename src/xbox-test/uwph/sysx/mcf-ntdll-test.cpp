// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device Wine check of the closed ntdll list for the mingw-w64 threading runtime (mcf_ntdll.h).
#include "mcf_ntdll.h"
#include "native.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
template<class T> T api(const char* name) { return reinterpret_cast<T>(mcf_ntdll::resolve("NTDLL.DLL", name)); }
int main() {
    using namespace mcf_ntdll;
    // The list is closed: other Nt*/Rtl*/Ldr* names stay unresolved, here and in sysx_native.
    for (auto name : {"NtOpenProcess", "NtAllocateVirtualMemory", "NtProtectVirtualMemory", "NtCreateThreadEx", "LdrLoadDll", "RtlAllocateHeap", "NtMapViewOfSectionEx"})
        CHECK(!resolve("ntdll.dll", name) && !sysx_native::resolve("ntdll.dll", name));
    CHECK(!resolve("kernel32.dll", "NtClose") && !resolve(nullptr, "NtClose") && !resolve("ntdll.dll", nullptr) && !resolve("ntdll.dll", "ntclose"));
    auto close = api<NTSTATUS_ (NTAPI*)(HANDLE)>("NtClose");
    auto delay = api<NTSTATUS_ (NTAPI*)(BOOLEAN, PLARGE_INTEGER)>("NtDelayExecution");
    auto create = api<CreateSection>("NtCreateSection");
    auto map = api<MapView>("NtMapViewOfSection");
    auto unmap = api<NTSTATUS_ (NTAPI*)(HANDLE, PVOID)>("NtUnmapViewOfSection");
    auto hard = api<NTSTATUS_ (NTAPI*)(NTSTATUS_, ULONG, ULONG, PULONG_PTR, ULONG, PULONG)>("NtRaiseHardError");
    for (auto name : {"NtDuplicateObject", "NtWaitForSingleObject", "NtWaitForKeyedEvent", "NtReleaseKeyedEvent", "RtlDllShutdownInProgress"})
        CHECK(resolve("ntdll.dll", name) == reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name)));
    CHECK(close && delay && create && map && unmap && hard);
    // Plain primitives are the real ones.
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(event && close(event) == 0);
    LARGE_INTEGER pause{}; pause.QuadPart = -10000; // 1 ms, relative
    CHECK(delay(FALSE, &pause) == 0);
    // Sections: read/write works; anything executable is refused before reaching the OS.
    LARGE_INTEGER size{}; size.QuadPart = 0x10000;
    HANDLE section = nullptr;
    CHECK(create(&section, SECTION_MAP_READ | SECTION_MAP_WRITE, nullptr, &size, PAGE_READWRITE, SEC_COMMIT, nullptr) == 0 && section);
    HANDLE denied = nullptr;
    // SECTION_ALL_ACCESS (what the runtime asks for) is accepted, minus the execute right.
    HANDLE all = nullptr;
    CHECK(create(&all, SECTION_ALL_ACCESS, nullptr, &size, PAGE_READWRITE, SEC_COMMIT, nullptr) == 0 && all);
    PVOID view_all = nullptr; SIZE_T view_size = 0;
    CHECK(map(all, GetCurrentProcess(), &view_all, 0, 0, nullptr, &view_size, 2, 0, PAGE_READWRITE) == 0 && view_all);
    CHECK(unmap(GetCurrentProcess(), view_all) == 0 && close(all) == 0);
    CHECK(create(&denied, SECTION_MAP_READ, nullptr, &size, PAGE_EXECUTE_READWRITE, SEC_COMMIT, nullptr) == kInvalidParameter && !denied);
    CHECK(create(&denied, SECTION_MAP_READ, nullptr, &size, PAGE_READONLY, SEC_IMAGE, nullptr) == kInvalidParameter && !denied);
    PVOID base = nullptr; SIZE_T view = 0;
    CHECK(map(section, GetCurrentProcess(), &base, 0, 0, nullptr, &view, 2 /* ViewUnmap */, 0, PAGE_EXECUTE_READWRITE) == kInvalidParameter && !base);
    CHECK(map(section, GetCurrentProcess(), &base, 0, 0, nullptr, &view, 2, 0, PAGE_READWRITE) == 0 && base && view >= 0x10000);
    static_cast<volatile char*>(base)[0] = 1;
    MEMORY_BASIC_INFORMATION info{}; CHECK(VirtualQuery(base, &info, sizeof info) && info.Protect == PAGE_READWRITE);
    CHECK(unmap(GetCurrentProcess(), base) == 0 && close(section) == 0);
    // The fatal-error report never reaches the OS.
    ULONG response = 0xdead; CHECK(hard(static_cast<NTSTATUS_>(0xC0000005), 0, 0, nullptr, 0, &response) == kNotImplemented && response == 0xdead);
    std::puts("MCF-NTDLL PASS closed list, real primitives, executable sections refused, no hard errors");
    return 0;
}
