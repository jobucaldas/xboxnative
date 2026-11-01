#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
namespace rawinput_core {
// No raw HID endpoints or WM_INPUT packets are issued by this backend. WGI
// gamepads use XInput; keyboard events use the owner DLL's message queue.
// Unknown handles must fail even when callers ask only for a buffer size.
inline UINT WINAPI data(HANDLE,UINT,void*,UINT*,UINT) {
    SetLastError(ERROR_INVALID_HANDLE);return UINT(-1);
}
inline UINT WINAPI device_info(HANDLE,UINT command,void*,UINT* size) {
    if(!size)SetLastError(ERROR_NOACCESS);
    else if(command!=0x20000005 && command!=0x20000007 && command!=0x2000000b)SetLastError(ERROR_INVALID_PARAMETER);
    else SetLastError(ERROR_INVALID_HANDLE);
    return UINT(-1);
}
inline UINT WINAPI buffer(void*,UINT* size,UINT headerSize) {
    if(headerSize!=24 || !size){SetLastError(ERROR_INVALID_PARAMETER);return UINT(-1);}
    *size=0;return 0; // No next buffered WM_INPUT packet exists.
}
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!dll || !name || _stricmp(dll,"user32.dll"))return 0;
    if(!strcmp(name,"GetRawInputData"))return reinterpret_cast<uintptr_t>(&data);
    if(!strcmp(name,"GetRawInputDeviceInfoW"))return reinterpret_cast<uintptr_t>(&device_info);
    if(!strcmp(name,"GetRawInputBuffer"))return reinterpret_cast<uintptr_t>(&buffer);
    return 0;
}
}
