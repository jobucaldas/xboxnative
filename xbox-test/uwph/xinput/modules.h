#pragma once
#include "route.h"
#include "../dinput/route.h"
#include <atomic>
namespace xinput_core {
// Loader-owned module identities for WGI and explicit unsupported DirectInput. These
// are never passed to the OS loader and are distinct from guest PE addresses.
struct Module { const char* name; std::atomic<unsigned> references{0}; };
inline Module modules[]={{"xinput1_4.dll"},{"xinput1_3.dll"},{"xinput9_1_0.dll"},{"dinput8.dll"}};
inline Module* module(HMODULE handle) {
    for(auto& value:modules)if(handle==reinterpret_cast<HMODULE>(&value))return &value;
    return nullptr;
}
inline HMODULE find_module(const char* name,bool acquire) {
    if(!name)return nullptr;
    for(auto& value:modules)if(!_stricmp(name,value.name)) {
        if(acquire)++value.references;
        if(value.references.load())return reinterpret_cast<HMODULE>(&value);
    }
    return nullptr;
}
inline FARPROC module_proc(Module* value,LPCSTR name) {
    if(!value || !value->references.load()){SetLastError(ERROR_INVALID_HANDLE);return nullptr;}
    uintptr_t address=0;
    if(!IS_INTRESOURCE(name)) {
        address=resolve(value->name,name);
        if(!address)address=dinput_core::resolve(value->name,name);
    }
    if(!address)SetLastError(ERROR_PROC_NOT_FOUND);
    return reinterpret_cast<FARPROC>(address);
}
inline BOOL release_module(Module* value) {
    auto count=value->references.load();
    while(count)if(value->references.compare_exchange_weak(count,count-1))return TRUE;
    SetLastError(ERROR_INVALID_HANDLE);return FALSE;
}
}
