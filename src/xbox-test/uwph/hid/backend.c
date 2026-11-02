#define _SETUPAPI_
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

static void (*logger)(const char*);
__declspec(dllexport) void hid_stub_set_log(void (*fn)(const char*)) { logger=fn; }
static SRWLOCK log_lock=SRWLOCK_INIT;
static const char* logged[32];
static void note(const char* name) {
 BOOL fresh=FALSE;
 AcquireSRWLockExclusive(&log_lock);
 for(unsigned i=0;i<32;i++) {
  if(logged[i] && !lstrcmpA(logged[i],name)) break;
  if(!logged[i]) {logged[i]=name;fresh=TRUE;break;}
 }
 ReleaseSRWLockExclusive(&log_lock);
 if(fresh) { if(logger) logger(name); else { OutputDebugStringA(name); OutputDebugStringA("\n"); } }
}
static SRWLOCK lock=SRWLOCK_INIT;
static ULONG_PTR next_id=0x10000;
static HDEVINFO sets[64];
static HDEVINFO new_set(void) {
 HDEVINFO h=INVALID_HANDLE_VALUE;
 AcquireSRWLockExclusive(&lock);
 for(unsigned i=0;i<64;i++) if(!sets[i]) { h=(HDEVINFO)++next_id; sets[i]=h; break; }
 ReleaseSRWLockExclusive(&lock);
 if(h==INVALID_HANDLE_VALUE) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
 return h;
}
static BOOL valid_set(HDEVINFO h, BOOL destroy) {
 BOOL ok=FALSE;
 AcquireSRWLockExclusive(&lock);
 for(unsigned i=0;i<64;i++) if(sets[i] && sets[i]==h) {ok=TRUE; if(destroy) sets[i]=0; break;}
 ReleaseSRWLockExclusive(&lock);
 if(!ok) SetLastError(ERROR_INVALID_HANDLE);
 return ok;
}

__declspec(dllexport) HDEVINFO WINAPI SetupDiGetClassDevsA(CONST GUID *ClassGuid,PCSTR Enumerator,HWND hwndParent,DWORD Flags) { note("SetupDiGetClassDevsA"); return new_set(); }

__declspec(dllexport) HDEVINFO WINAPI SetupDiGetClassDevsW(CONST GUID *ClassGuid,PCWSTR Enumerator,HWND hwndParent,DWORD Flags) { note("SetupDiGetClassDevsW"); return new_set(); }

__declspec(dllexport) WINBOOL WINAPI SetupDiEnumDeviceInterfaces(HDEVINFO DeviceInfoSet,PSP_DEVINFO_DATA DeviceInfoData,CONST GUID *InterfaceClassGuid,DWORD MemberIndex,PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData) { note("SetupDiEnumDeviceInterfaces"); if(valid_set(DeviceInfoSet,FALSE)) SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }

__declspec(dllexport) WINBOOL WINAPI SetupDiGetDeviceInterfaceDetailW(HDEVINFO DeviceInfoSet,PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,DWORD DeviceInterfaceDetailDataSize,PDWORD RequiredSize,PSP_DEVINFO_DATA DeviceInfoData) { note("SetupDiGetDeviceInterfaceDetailW"); if(valid_set(DeviceInfoSet,FALSE)) SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

__declspec(dllexport) WINBOOL WINAPI SetupDiEnumDeviceInfo(HDEVINFO DeviceInfoSet,DWORD MemberIndex,PSP_DEVINFO_DATA DeviceInfoData) { note("SetupDiEnumDeviceInfo"); if(valid_set(DeviceInfoSet,FALSE)) SetLastError(ERROR_NO_MORE_ITEMS); return FALSE; }

__declspec(dllexport) WINBOOL WINAPI SetupDiDestroyDeviceInfoList(HDEVINFO DeviceInfoSet) { note("SetupDiDestroyDeviceInfoList"); return valid_set(DeviceInfoSet,TRUE); }

__declspec(dllexport) NTSTATUS NTAPI HidP_GetData (HIDP_REPORT_TYPE ReportType, PHIDP_DATA DataList, PULONG DataLength, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength) { note("HidP_GetData"); return HIDP_STATUS_INVALID_PREPARSED_DATA; }

__declspec(dllexport) ULONG NTAPI HidP_MaxDataListLength (HIDP_REPORT_TYPE ReportType, PHIDP_PREPARSED_DATA PreparsedData) { note("HidP_MaxDataListLength"); return 0; }

__declspec(dllexport) BOOLEAN NTAPI HidD_GetSerialNumberString (HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) { note("HidD_GetSerialNumberString"); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

__declspec(dllexport) BOOLEAN NTAPI HidD_GetManufacturerString (HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) { note("HidD_GetManufacturerString"); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

__declspec(dllexport) BOOLEAN NTAPI HidD_GetProductString (HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength) { note("HidD_GetProductString"); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

__declspec(dllexport) BOOLEAN NTAPI HidD_FreePreparsedData (PHIDP_PREPARSED_DATA PreparsedData) { note("HidD_FreePreparsedData"); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

__declspec(dllexport) BOOLEAN NTAPI HidD_GetPreparsedData (HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA *PreparsedData) { note("HidD_GetPreparsedData"); if(PreparsedData) *PreparsedData=0; SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

__declspec(dllexport) void NTAPI HidD_GetHidGuid (LPGUID HidGuid) { note("HidD_GetHidGuid"); if(HidGuid) { const GUID g={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}}; *HidGuid=g; } else SetLastError(ERROR_INVALID_PARAMETER); }

__declspec(dllexport) NTSTATUS NTAPI HidP_SetUsages (HIDP_REPORT_TYPE ReportType, USAGE UsagePage, USHORT LinkCollection, PUSAGE UsageList, PULONG UsageLength, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength) { note("HidP_SetUsages"); return HIDP_STATUS_INVALID_PREPARSED_DATA; }

__declspec(dllexport) NTSTATUS NTAPI HidP_SetUsageValue (HIDP_REPORT_TYPE ReportType, USAGE UsagePage, USHORT LinkCollection, USAGE Usage, ULONG UsageValue, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength) { note("HidP_SetUsageValue"); return HIDP_STATUS_INVALID_PREPARSED_DATA; }

__declspec(dllexport) NTSTATUS NTAPI HidP_GetButtonCaps (HIDP_REPORT_TYPE ReportType, PHIDP_BUTTON_CAPS ButtonCaps, PUSHORT ButtonCapsLength, PHIDP_PREPARSED_DATA PreparsedData) { note("HidP_GetButtonCaps"); return HIDP_STATUS_INVALID_PREPARSED_DATA; }

__declspec(dllexport) NTSTATUS NTAPI HidP_GetValueCaps (HIDP_REPORT_TYPE ReportType, PHIDP_VALUE_CAPS ValueCaps, PUSHORT ValueCapsLength, PHIDP_PREPARSED_DATA PreparsedData) { note("HidP_GetValueCaps"); return HIDP_STATUS_INVALID_PREPARSED_DATA; }

__declspec(dllexport) BOOLEAN NTAPI HidD_GetAttributes (HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes) { note("HidD_GetAttributes"); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }

__declspec(dllexport) NTSTATUS NTAPI HidP_GetCaps (PHIDP_PREPARSED_DATA PreparsedData, PHIDP_CAPS Capabilities) { note("HidP_GetCaps"); return HIDP_STATUS_INVALID_PREPARSED_DATA; }
