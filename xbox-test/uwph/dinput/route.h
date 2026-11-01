#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstring>
#include <cstdint>
namespace dinput_core {
inline const GUID input8a={0xbf798030,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
inline const GUID input8w={0xbf798031,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
// No DirectInput device backend exists in this AppContainer. The supported
// controller path is WGI/XInput. Preserve creation validation errors, and fail
// unsupported creation explicitly instead of returning a dummy COM object.
inline HRESULT WINAPI create(HINSTANCE instance,DWORD version,REFIID iid,void** out,IUnknown* outer) {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=input8a && iid!=input8w && iid!=__uuidof(IUnknown))return E_NOINTERFACE;
    if(outer)return CLASS_E_NOAGGREGATION;
    if(!instance)return E_INVALIDARG;
    if(!version)return HRESULT_FROM_WIN32(ERROR_NOT_READY); // DIERR_NOTINITIALIZED
    if(version<0x0800)return HRESULT_FROM_WIN32(ERROR_RMODE_APP);
    if(version>0x0800)return HRESULT_FROM_WIN32(ERROR_OLD_WIN_VERSION);
    return E_NOTIMPL; // DIERR_UNSUPPORTED
}
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!dll || !name || _stricmp(dll,"dinput8.dll"))return 0;
    return !strcmp(name,"DirectInput8Create")?reinterpret_cast<uintptr_t>(&create):0;
}
}
