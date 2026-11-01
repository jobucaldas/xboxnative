#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>

namespace hid_core {
// Wine 10 dlls/hid/hidd.c and hid.spec: void stdcall HidD_GetHidGuid(ptr).
// GUID_DEVINTERFACE_HID identifies the device class; it does not enumerate
// devices or imply that a HID device backend is available.
inline void WINAPI get_hid_guid(GUID* guid) {
    *guid = GUID{0x4d1e55b2, 0xf16f, 0x11cf,
                {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
}
// No HID collection handles are issued by this launcher backend. Wine 10
// hidd.c queries collection information before writing attributes/preparsed
// data. Invalid handles fail without touching caller buffers.
// Never forward a guest handle
// to a host DeviceIoControl: it could alias an unrelated AppContainer handle.
inline BOOLEAN WINAPI unavailable_query(HANDLE, void*) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name || _stricmp(dll, "hid.dll")) return 0;
    if (!strcmp(name, "HidD_GetHidGuid")) return reinterpret_cast<uintptr_t>(&get_hid_guid);
    if (!strcmp(name, "HidD_GetAttributes") || !strcmp(name, "HidD_GetPreparsedData"))
        return reinterpret_cast<uintptr_t>(&unavailable_query);
    return 0;
}
}
