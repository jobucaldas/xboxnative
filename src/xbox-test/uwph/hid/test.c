#define _SETUPAPI_
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
static void (*log_line)(const char*);
static int failures,checks;
static void check(const char*n,int ok) { char b[160]; checks++; if(!ok) failures++; lstrcpyA(b,"HID CHECK "); lstrcatA(b,n); lstrcatA(b,ok?" PASS":" FAIL"); log_line(b); }
__declspec(dllexport) int run_hid_tests(HMODULE backend,void (*log)(const char*)) {
 log_line=log; failures=checks=0;
 HDEVINFO WINAPI  (*p_SetupDiGetClassDevsA)(CONST GUID *ClassGuid,PCSTR Enumerator,HWND hwndParent,DWORD Flags)=(HDEVINFO WINAPI  (*)(CONST GUID *ClassGuid,PCSTR Enumerator,HWND hwndParent,DWORD Flags))GetProcAddress(backend,"SetupDiGetClassDevsA");
 if(!p_SetupDiGetClassDevsA) { log("HID MISSING SetupDiGetClassDevsA"); return 1; }
 HDEVINFO WINAPI  (*p_SetupDiGetClassDevsW)(CONST GUID *ClassGuid,PCWSTR Enumerator,HWND hwndParent,DWORD Flags)=(HDEVINFO WINAPI  (*)(CONST GUID *ClassGuid,PCWSTR Enumerator,HWND hwndParent,DWORD Flags))GetProcAddress(backend,"SetupDiGetClassDevsW");
 if(!p_SetupDiGetClassDevsW) { log("HID MISSING SetupDiGetClassDevsW"); return 1; }
 WINBOOL WINAPI  (*p_SetupDiEnumDeviceInterfaces)(HDEVINFO DeviceInfoSet,PSP_DEVINFO_DATA DeviceInfoData,CONST GUID *InterfaceClassGuid,DWORD MemberIndex,PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData)=(WINBOOL WINAPI  (*)(HDEVINFO DeviceInfoSet,PSP_DEVINFO_DATA DeviceInfoData,CONST GUID *InterfaceClassGuid,DWORD MemberIndex,PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData))GetProcAddress(backend,"SetupDiEnumDeviceInterfaces");
 if(!p_SetupDiEnumDeviceInterfaces) { log("HID MISSING SetupDiEnumDeviceInterfaces"); return 1; }
 WINBOOL WINAPI  (*p_SetupDiGetDeviceInterfaceDetailW)(HDEVINFO DeviceInfoSet,PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,DWORD DeviceInterfaceDetailDataSize,PDWORD RequiredSize,PSP_DEVINFO_DATA DeviceInfoData)=(WINBOOL WINAPI  (*)(HDEVINFO DeviceInfoSet,PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData,PSP_DEVICE_INTERFACE_DETAIL_DATA_W DeviceInterfaceDetailData,DWORD DeviceInterfaceDetailDataSize,PDWORD RequiredSize,PSP_DEVINFO_DATA DeviceInfoData))GetProcAddress(backend,"SetupDiGetDeviceInterfaceDetailW");
 if(!p_SetupDiGetDeviceInterfaceDetailW) { log("HID MISSING SetupDiGetDeviceInterfaceDetailW"); return 1; }
 WINBOOL WINAPI  (*p_SetupDiEnumDeviceInfo)(HDEVINFO DeviceInfoSet,DWORD MemberIndex,PSP_DEVINFO_DATA DeviceInfoData)=(WINBOOL WINAPI  (*)(HDEVINFO DeviceInfoSet,DWORD MemberIndex,PSP_DEVINFO_DATA DeviceInfoData))GetProcAddress(backend,"SetupDiEnumDeviceInfo");
 if(!p_SetupDiEnumDeviceInfo) { log("HID MISSING SetupDiEnumDeviceInfo"); return 1; }
 WINBOOL WINAPI  (*p_SetupDiDestroyDeviceInfoList)(HDEVINFO DeviceInfoSet)=(WINBOOL WINAPI  (*)(HDEVINFO DeviceInfoSet))GetProcAddress(backend,"SetupDiDestroyDeviceInfoList");
 if(!p_SetupDiDestroyDeviceInfoList) { log("HID MISSING SetupDiDestroyDeviceInfoList"); return 1; }
 NTSTATUS NTAPI  (*p_HidP_GetData)(HIDP_REPORT_TYPE ReportType, PHIDP_DATA DataList, PULONG DataLength, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength)=(NTSTATUS NTAPI  (*)(HIDP_REPORT_TYPE ReportType, PHIDP_DATA DataList, PULONG DataLength, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength))GetProcAddress(backend,"HidP_GetData");
 if(!p_HidP_GetData) { log("HID MISSING HidP_GetData"); return 1; }
 ULONG NTAPI  (*p_HidP_MaxDataListLength)(HIDP_REPORT_TYPE ReportType, PHIDP_PREPARSED_DATA PreparsedData)=(ULONG NTAPI  (*)(HIDP_REPORT_TYPE ReportType, PHIDP_PREPARSED_DATA PreparsedData))GetProcAddress(backend,"HidP_MaxDataListLength");
 if(!p_HidP_MaxDataListLength) { log("HID MISSING HidP_MaxDataListLength"); return 1; }
 BOOLEAN NTAPI  (*p_HidD_GetSerialNumberString)(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength)=(BOOLEAN NTAPI  (*)(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength))GetProcAddress(backend,"HidD_GetSerialNumberString");
 if(!p_HidD_GetSerialNumberString) { log("HID MISSING HidD_GetSerialNumberString"); return 1; }
 BOOLEAN NTAPI  (*p_HidD_GetManufacturerString)(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength)=(BOOLEAN NTAPI  (*)(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength))GetProcAddress(backend,"HidD_GetManufacturerString");
 if(!p_HidD_GetManufacturerString) { log("HID MISSING HidD_GetManufacturerString"); return 1; }
 BOOLEAN NTAPI  (*p_HidD_GetProductString)(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength)=(BOOLEAN NTAPI  (*)(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength))GetProcAddress(backend,"HidD_GetProductString");
 if(!p_HidD_GetProductString) { log("HID MISSING HidD_GetProductString"); return 1; }
 BOOLEAN NTAPI  (*p_HidD_FreePreparsedData)(PHIDP_PREPARSED_DATA PreparsedData)=(BOOLEAN NTAPI  (*)(PHIDP_PREPARSED_DATA PreparsedData))GetProcAddress(backend,"HidD_FreePreparsedData");
 if(!p_HidD_FreePreparsedData) { log("HID MISSING HidD_FreePreparsedData"); return 1; }
 BOOLEAN NTAPI  (*p_HidD_GetPreparsedData)(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA *PreparsedData)=(BOOLEAN NTAPI  (*)(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA *PreparsedData))GetProcAddress(backend,"HidD_GetPreparsedData");
 if(!p_HidD_GetPreparsedData) { log("HID MISSING HidD_GetPreparsedData"); return 1; }
 void NTAPI  (*p_HidD_GetHidGuid)(LPGUID HidGuid)=(void NTAPI  (*)(LPGUID HidGuid))GetProcAddress(backend,"HidD_GetHidGuid");
 if(!p_HidD_GetHidGuid) { log("HID MISSING HidD_GetHidGuid"); return 1; }
 NTSTATUS NTAPI  (*p_HidP_SetUsages)(HIDP_REPORT_TYPE ReportType, USAGE UsagePage, USHORT LinkCollection, PUSAGE UsageList, PULONG UsageLength, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength)=(NTSTATUS NTAPI  (*)(HIDP_REPORT_TYPE ReportType, USAGE UsagePage, USHORT LinkCollection, PUSAGE UsageList, PULONG UsageLength, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength))GetProcAddress(backend,"HidP_SetUsages");
 if(!p_HidP_SetUsages) { log("HID MISSING HidP_SetUsages"); return 1; }
 NTSTATUS NTAPI  (*p_HidP_SetUsageValue)(HIDP_REPORT_TYPE ReportType, USAGE UsagePage, USHORT LinkCollection, USAGE Usage, ULONG UsageValue, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength)=(NTSTATUS NTAPI  (*)(HIDP_REPORT_TYPE ReportType, USAGE UsagePage, USHORT LinkCollection, USAGE Usage, ULONG UsageValue, PHIDP_PREPARSED_DATA PreparsedData, PCHAR Report, ULONG ReportLength))GetProcAddress(backend,"HidP_SetUsageValue");
 if(!p_HidP_SetUsageValue) { log("HID MISSING HidP_SetUsageValue"); return 1; }
 NTSTATUS NTAPI  (*p_HidP_GetButtonCaps)(HIDP_REPORT_TYPE ReportType, PHIDP_BUTTON_CAPS ButtonCaps, PUSHORT ButtonCapsLength, PHIDP_PREPARSED_DATA PreparsedData)=(NTSTATUS NTAPI  (*)(HIDP_REPORT_TYPE ReportType, PHIDP_BUTTON_CAPS ButtonCaps, PUSHORT ButtonCapsLength, PHIDP_PREPARSED_DATA PreparsedData))GetProcAddress(backend,"HidP_GetButtonCaps");
 if(!p_HidP_GetButtonCaps) { log("HID MISSING HidP_GetButtonCaps"); return 1; }
 NTSTATUS NTAPI  (*p_HidP_GetValueCaps)(HIDP_REPORT_TYPE ReportType, PHIDP_VALUE_CAPS ValueCaps, PUSHORT ValueCapsLength, PHIDP_PREPARSED_DATA PreparsedData)=(NTSTATUS NTAPI  (*)(HIDP_REPORT_TYPE ReportType, PHIDP_VALUE_CAPS ValueCaps, PUSHORT ValueCapsLength, PHIDP_PREPARSED_DATA PreparsedData))GetProcAddress(backend,"HidP_GetValueCaps");
 if(!p_HidP_GetValueCaps) { log("HID MISSING HidP_GetValueCaps"); return 1; }
 BOOLEAN NTAPI  (*p_HidD_GetAttributes)(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes)=(BOOLEAN NTAPI  (*)(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes))GetProcAddress(backend,"HidD_GetAttributes");
 if(!p_HidD_GetAttributes) { log("HID MISSING HidD_GetAttributes"); return 1; }
 NTSTATUS NTAPI  (*p_HidP_GetCaps)(PHIDP_PREPARSED_DATA PreparsedData, PHIDP_CAPS Capabilities)=(NTSTATUS NTAPI  (*)(PHIDP_PREPARSED_DATA PreparsedData, PHIDP_CAPS Capabilities))GetProcAddress(backend,"HidP_GetCaps");
 if(!p_HidP_GetCaps) { log("HID MISSING HidP_GetCaps"); return 1; }
 void (*setlog)(void (*)(const char*))=(void*)GetProcAddress(backend,"hid_stub_set_log"); if(!setlog) return 1; setlog(log);
 GUID hid_guid={0}; p_HidD_GetHidGuid(&hid_guid);
 SP_DEVICE_INTERFACE_DATA iface={0}; iface.cbSize=sizeof iface;
 SP_DEVINFO_DATA info={0}; info.cbSize=sizeof info;
 HDEVINFO a=p_SetupDiGetClassDevsA(&hid_guid,0,0,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE),w=p_SetupDiGetClassDevsW(&hid_guid,0,0,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
 check("SetupDiGetClassDevsA",a!=INVALID_HANDLE_VALUE); check("SetupDiGetClassDevsW",w!=INVALID_HANDLE_VALUE && w!=a);
 PHIDP_PREPARSED_DATA pp=(void*)1;
 { SetLastError(0); LONG r=p_SetupDiEnumDeviceInterfaces(a,0,&hid_guid,0,&iface); check("SetupDiEnumDeviceInterfaces",!r && GetLastError()==ERROR_NO_MORE_ITEMS); }
 { SetLastError(0); LONG r=p_SetupDiEnumDeviceInfo(a,0,&info); check("SetupDiEnumDeviceInfo",!r && GetLastError()==ERROR_NO_MORE_ITEMS); }
 { SetLastError(0); LONG r=p_SetupDiGetDeviceInterfaceDetailW(a,0,0,0,0,0); check("SetupDiGetDeviceInterfaceDetailW",!r && GetLastError()==ERROR_INVALID_PARAMETER); }
 { SetLastError(0); LONG r=p_SetupDiDestroyDeviceInfoList(a); check("SetupDiDestroyDeviceInfoList",r); }
 { SetLastError(0); LONG r=p_HidD_GetSerialNumberString(INVALID_HANDLE_VALUE,0,0); check("HidD_GetSerialNumberString",!r && GetLastError()==ERROR_INVALID_HANDLE); }
 { SetLastError(0); LONG r=p_HidD_GetManufacturerString(INVALID_HANDLE_VALUE,0,0); check("HidD_GetManufacturerString",!r && GetLastError()==ERROR_INVALID_HANDLE); }
 { SetLastError(0); LONG r=p_HidD_GetProductString(INVALID_HANDLE_VALUE,0,0); check("HidD_GetProductString",!r && GetLastError()==ERROR_INVALID_HANDLE); }
 { SetLastError(0); LONG r=p_HidD_FreePreparsedData(0); check("HidD_FreePreparsedData",!r && GetLastError()==ERROR_INVALID_HANDLE); }
 { SetLastError(0); LONG r=p_HidD_GetPreparsedData(INVALID_HANDLE_VALUE,&pp); check("HidD_GetPreparsedData",!r && GetLastError()==ERROR_INVALID_HANDLE && pp==0); }
 { SetLastError(0); LONG r=p_HidD_GetAttributes(INVALID_HANDLE_VALUE,0); check("HidD_GetAttributes",!r && GetLastError()==ERROR_INVALID_HANDLE); }
 { SetLastError(0); LONG r=p_HidP_GetData(HidP_Input,0,0,0,0,0); check("HidP_GetData",r==HIDP_STATUS_INVALID_PREPARSED_DATA); }
 { SetLastError(0); LONG r=p_HidP_MaxDataListLength(HidP_Input,0); check("HidP_MaxDataListLength",r==0); }
 { SetLastError(0); LONG r=p_HidP_SetUsages(HidP_Output,0,0,0,0,0,0,0); check("HidP_SetUsages",r==HIDP_STATUS_INVALID_PREPARSED_DATA); }
 { SetLastError(0); LONG r=p_HidP_SetUsageValue(HidP_Output,0,0,0,0,0,0,0); check("HidP_SetUsageValue",r==HIDP_STATUS_INVALID_PREPARSED_DATA); }
 { SetLastError(0); LONG r=p_HidP_GetButtonCaps(HidP_Input,0,0,0); check("HidP_GetButtonCaps",r==HIDP_STATUS_INVALID_PREPARSED_DATA); }
 { SetLastError(0); LONG r=p_HidP_GetValueCaps(HidP_Input,0,0,0); check("HidP_GetValueCaps",r==HIDP_STATUS_INVALID_PREPARSED_DATA); }
 { SetLastError(0); LONG r=p_HidP_GetCaps(0,0); check("HidP_GetCaps",r==HIDP_STATUS_INVALID_PREPARSED_DATA); }
 GUID g={0}; p_HidD_GetHidGuid(&g); const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}}; check("HidD_GetHidGuid",g.Data1==expected.Data1 && g.Data2==expected.Data2 && g.Data3==expected.Data3 && g.Data4[0]==0x88 && g.Data4[1]==0xcb && g.Data4[2]==0 && g.Data4[3]==0x11 && g.Data4[4]==0x11 && g.Data4[5]==0 && g.Data4[6]==0 && g.Data4[7]==0x30);
 check("destroy W set",p_SetupDiDestroyDeviceInfoList(w));
 check("stale set rejected",!p_SetupDiEnumDeviceInfo(a,0,&info) && GetLastError()==ERROR_INVALID_HANDLE);
 check("double destroy rejected",!p_SetupDiDestroyDeviceInfoList(a) && GetLastError()==ERROR_INVALID_HANDLE);
 setlog(0); if(!failures && checks==23) log("HID-STUB-OK symbols=20 checks=23"); else log("HID-STUB-FAILED"); return failures || checks!=23;
}
#ifdef HID_STANDALONE
static void puts_log(const char*s) { puts(s); }
int main(void) { HMODULE h=LoadLibraryW(L"uwph-hid.dll"); if(!h) return 2; return run_hid_tests(h,puts_log); }
#endif
