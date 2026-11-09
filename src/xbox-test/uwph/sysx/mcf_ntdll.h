// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

// The ntdll natives that mingw-w64 GCC's statically linked threading runtime (mcfgthread, used by libgcc's unwinder
// and libstdc++) imports directly: handle close and sleep, keyed events and waits, a duplicate-handle call, a named
// shared-memory section used to share the runtime's global state, and a fatal-error report. Mesa's d3d12 OpenGL
// driver (src/tools/build-mesa-d3d12.sh) carries that runtime, and a guest module with an unresolved import is not
// initialized. This is a separate, closed list from sysx_native::allowed (which still refuses every other Nt*/Rtl*/Ldr*
// name): the plain primitives are the real ntdll's; sections never become executable (W^X: executable memory comes
// only from the loader's own VirtualAllocFromApp path; the execute access right is cleared from section handles); the hard-error report never reaches the OS.
namespace mcf_ntdll {
using NTSTATUS_ = LONG;
constexpr NTSTATUS_ kInvalidParameter = static_cast<NTSTATUS_>(0xC000000D);
constexpr NTSTATUS_ kNotImplemented = static_cast<NTSTATUS_>(0xC0000002);
constexpr ULONG kExecuteProtections = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr ULONG kSectionImage = 0x01000000;   // SEC_IMAGE
constexpr ACCESS_MASK kSectionMapExecute = 0x0008;
inline FARPROC native(const char* name) {
    static HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? GetProcAddress(ntdll, name) : nullptr;
}
using CreateSection = NTSTATUS_ (NTAPI*)(PHANDLE, ACCESS_MASK, PVOID, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
using MapView = NTSTATUS_ (NTAPI*)(HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, ULONG, ULONG, ULONG);
inline NTSTATUS_ NTAPI create_section(PHANDLE section, ACCESS_MASK access, PVOID attributes, PLARGE_INTEGER size,
                                      ULONG protection, ULONG allocation, HANDLE file) {
    if ((protection & kExecuteProtections) || (allocation & kSectionImage)) return kInvalidParameter;
    access &= ~kSectionMapExecute;   // SECTION_ALL_ACCESS callers keep working; the handle can never map executable
    auto fn = reinterpret_cast<CreateSection>(native("NtCreateSection"));
    return fn ? fn(section, access, attributes, size, protection, allocation, file) : kNotImplemented;
}
inline NTSTATUS_ NTAPI map_view(HANDLE section, HANDLE process, PVOID* base, ULONG_PTR zero, SIZE_T commit,
                                PLARGE_INTEGER offset, PSIZE_T size, ULONG inherit, ULONG type, ULONG protection) {
    if (protection & kExecuteProtections) return kInvalidParameter;
    auto fn = reinterpret_cast<MapView>(native("NtMapViewOfSection"));
    return fn ? fn(section, process, base, zero, commit, offset, size, inherit, type, protection) : kNotImplemented;
}
// The runtime reports unrecoverable internal errors here and then traps; the guest must not raise an OS hard error.
inline NTSTATUS_ NTAPI raise_hard_error(NTSTATUS_, ULONG, ULONG, PULONG_PTR, ULONG, PULONG) { return kNotImplemented; }
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name || _stricmp(dll, "ntdll.dll")) return 0;
    static const char* const plain[] = {"NtClose", "NtDelayExecution", "NtDuplicateObject", "NtWaitForSingleObject",
        "NtWaitForKeyedEvent", "NtReleaseKeyedEvent", "NtUnmapViewOfSection", "RtlDllShutdownInProgress"};
    for (auto symbol : plain) if (!strcmp(name, symbol)) return reinterpret_cast<uintptr_t>(native(name));
    if (!strcmp(name, "NtCreateSection")) return reinterpret_cast<uintptr_t>(&create_section);
    if (!strcmp(name, "NtMapViewOfSection")) return reinterpret_cast<uintptr_t>(&map_view);
    if (!strcmp(name, "NtRaiseHardError")) return reinterpret_cast<uintptr_t>(&raise_hard_error);
    return 0;
}
}
