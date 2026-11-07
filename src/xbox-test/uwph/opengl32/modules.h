// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "route.h"
#include <atomic>
namespace opengl32_core {
// Loader-owned identity for a guest's runtime LoadLibrary("OPENGL32.DLL"),
// which SDL and GL loaders issue before creating a window. The console has no
// opengl32.dll (LoadLibrary fails with ERROR_MOD_NOT_FOUND there) while Wine
// has one that would leak a host GL stack, so the answer must not depend on the
// host: the handle is a Play-owned token, never passed to the OS loader, and
// GetProcAddress serves only resolve() above. A guest-shipped or donor-built
// opengl32.dll is mapped first and takes precedence (the lookup order in
// play_runtime loadExW), which is how a real driver will replace these stubs.
struct Module { const char* name; std::atomic<unsigned> references{0}; };
inline Module module_entry{"opengl32.dll"};
inline Module* module(HMODULE handle) {
    return handle==reinterpret_cast<HMODULE>(&module_entry)?&module_entry:nullptr;
}
inline HMODULE find_module(const char* name,bool acquire) {
    if(!name || _stricmp(name,module_entry.name))return nullptr;
    if(acquire)++module_entry.references;
    return module_entry.references.load()?reinterpret_cast<HMODULE>(&module_entry):nullptr;
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
