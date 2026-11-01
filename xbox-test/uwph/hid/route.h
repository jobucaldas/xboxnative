#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
namespace hid_core {
// HID headers are desktop-partitioned in the UWP SDK. Keep descriptor and
// report pointers opaque here: this backend does not create HID endpoints or
// preparsed blocks. The regression compiles calls with the desktop SDK types.
constexpr LONG invalid_preparsed=LONG(0xc0110001u);
constexpr LONG invalid_report_length=LONG(0xc0110003u);
// No native HID file handles or preparsed allocations are exposed by this
// AppContainer backend. WGI controllers use the separate XInput routes. Never
// pass guest handles or preparsed addresses to the system HID implementation.
inline LONG NTAPI c_HidP_GetData (int ReportType, void* DataList, PULONG DataLength, void* PreparsedData, PCHAR Report, ULONG ReportLength) { if(!ReportLength)return invalid_report_length; if(DataLength)*DataLength=0; return invalid_preparsed; }
inline ULONG NTAPI c_HidP_MaxDataListLength (int ReportType, void* PreparsedData) { return 0; }
inline BOOLEAN NTAPI c_HidD_GetSerialNumberString (HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
inline BOOLEAN NTAPI c_HidD_GetManufacturerString (HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
inline BOOLEAN NTAPI c_HidD_GetProductString (HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
inline BOOLEAN NTAPI c_HidD_FreePreparsedData (void* PreparsedData) { if(!PreparsedData)return TRUE; SetLastError(ERROR_INVALID_PARAMETER);return FALSE; }
inline BOOLEAN NTAPI c_HidD_GetPreparsedData (HANDLE HidDeviceObject, void* PreparsedData) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
inline void NTAPI c_HidD_GetHidGuid (LPGUID HidGuid) { if(HidGuid) { const GUID g={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}}; *HidGuid=g; } else SetLastError(ERROR_INVALID_PARAMETER); }
inline LONG NTAPI c_HidP_SetUsages (int ReportType, USHORT UsagePage, USHORT LinkCollection, USHORT* UsageList, PULONG UsageLength, void* PreparsedData, PCHAR Report, ULONG ReportLength) { if(!ReportLength)return invalid_report_length; return invalid_preparsed; }
inline LONG NTAPI c_HidP_SetUsageValue (int ReportType, USHORT UsagePage, USHORT LinkCollection, USHORT Usage, ULONG UsageValue, void* PreparsedData, PCHAR Report, ULONG ReportLength) { if(!ReportLength)return invalid_report_length; return invalid_preparsed; }
inline LONG NTAPI c_HidP_GetButtonCaps (int ReportType, void* ButtonCaps, PUSHORT ButtonCapsLength, void* PreparsedData) { return invalid_preparsed; }
inline LONG NTAPI c_HidP_GetValueCaps (int ReportType, void* ValueCaps, PUSHORT ValueCapsLength, void* PreparsedData) { return invalid_preparsed; }
inline BOOLEAN NTAPI c_HidD_GetAttributes (HANDLE HidDeviceObject, void* Attributes) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
inline LONG NTAPI c_HidP_GetCaps (void* PreparsedData, void* Capabilities) { return invalid_preparsed; }
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!dll || !name || _stricmp(dll,"hid.dll"))return 0;
    if(!strcmp(name,"HidP_GetData"))return reinterpret_cast<uintptr_t>(&c_HidP_GetData);
    if(!strcmp(name,"HidP_MaxDataListLength"))return reinterpret_cast<uintptr_t>(&c_HidP_MaxDataListLength);
    if(!strcmp(name,"HidD_GetSerialNumberString"))return reinterpret_cast<uintptr_t>(&c_HidD_GetSerialNumberString);
    if(!strcmp(name,"HidD_GetManufacturerString"))return reinterpret_cast<uintptr_t>(&c_HidD_GetManufacturerString);
    if(!strcmp(name,"HidD_GetProductString"))return reinterpret_cast<uintptr_t>(&c_HidD_GetProductString);
    if(!strcmp(name,"HidD_FreePreparsedData"))return reinterpret_cast<uintptr_t>(&c_HidD_FreePreparsedData);
    if(!strcmp(name,"HidD_GetPreparsedData"))return reinterpret_cast<uintptr_t>(&c_HidD_GetPreparsedData);
    if(!strcmp(name,"HidD_GetHidGuid"))return reinterpret_cast<uintptr_t>(&c_HidD_GetHidGuid);
    if(!strcmp(name,"HidP_SetUsages"))return reinterpret_cast<uintptr_t>(&c_HidP_SetUsages);
    if(!strcmp(name,"HidP_SetUsageValue"))return reinterpret_cast<uintptr_t>(&c_HidP_SetUsageValue);
    if(!strcmp(name,"HidP_GetButtonCaps"))return reinterpret_cast<uintptr_t>(&c_HidP_GetButtonCaps);
    if(!strcmp(name,"HidP_GetValueCaps"))return reinterpret_cast<uintptr_t>(&c_HidP_GetValueCaps);
    if(!strcmp(name,"HidD_GetAttributes"))return reinterpret_cast<uintptr_t>(&c_HidD_GetAttributes);
    if(!strcmp(name,"HidP_GetCaps"))return reinterpret_cast<uintptr_t>(&c_HidP_GetCaps);
    return 0;
}
}
