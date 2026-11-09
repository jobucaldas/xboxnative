#pragma once
#include "route.h"
#include <atomic>
#include <string>
namespace xinput_core {
// Loader-owned module identities for the built-in WGI implementation. These
// are never passed to the OS loader and are distinct from guest PE addresses.
struct Module { const char* name; std::atomic<unsigned> references{0}; };
inline Module modules[]={{"xinput1_4.dll"},{"xinput1_3.dll"},{"xinput9_1_0.dll"}};
inline Module* module(HMODULE handle) {
    for(auto& value:modules)if(handle==reinterpret_cast<HMODULE>(&value))return &value;
    return nullptr;
}
// GetModuleHandle/LoadLibrary add ".dll" to a name without an extension, so
// InControl's and Unity's "xinput1_3" spellings reach the same module.
inline HMODULE find_module(const char* name,bool acquire) {
    if(!name)return nullptr;
    std::string key=name;
    if(key.find('.')==std::string::npos)key+=".dll";
    for(auto& value:modules)if(!_stricmp(key.c_str(),value.name)) {
        if(acquire)++value.references;
        if(value.references.load())return reinterpret_cast<HMODULE>(&value);
    }
    return nullptr;
}
inline FARPROC module_proc(Module* value,LPCSTR name) {
    if(!value || !value->references.load()){SetLastError(ERROR_INVALID_HANDLE);return nullptr;}
    uintptr_t address=IS_INTRESOURCE(name)?0:resolve(value->name,name);
    if(!address)SetLastError(ERROR_PROC_NOT_FOUND);
    return reinterpret_cast<FARPROC>(address);
}
inline BOOL release_module(Module* value) {
    auto count=value->references.load();
    while(count)if(value->references.compare_exchange_weak(count,count-1))return TRUE;
    SetLastError(ERROR_INVALID_HANDLE);return FALSE;
}
}
