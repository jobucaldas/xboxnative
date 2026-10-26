// Explicit resolver for a future Wine loader bridge. No native fallback for
// unknowns, Wine builtins, or stubs. The OS resolves this host's own imports.
#include <windows.h>
#include <cwctype>
#include <string>
#include <vector>
enum class RouteKind { system, uwph, stub };
struct Route { const wchar_t* name; RouteKind kind; };
#include "routes.generated.h"
using BuiltinResolver = FARPROC (*)(LPCWSTR,LPCSTR);
static BuiltinResolver builtin_resolver;
struct NativeModule { const wchar_t* name; HMODULE module; };
static std::vector<NativeModule> native_modules;
static SRWLOCK module_lock = SRWLOCK_INIT;
static HMODULE native_module(const wchar_t* name){
 AcquireSRWLockShared(&module_lock);
 for(auto& entry:native_modules)if(entry.name==name){
   HMODULE module=entry.module;ReleaseSRWLockShared(&module_lock);return module;
 }
 ReleaseSRWLockShared(&module_lock);
 // Loading may run DllMain and reenter the resolver: never hold our lock here.
 HMODULE loaded=LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!loaded)return nullptr;
 AcquireSRWLockExclusive(&module_lock);
 for(auto& entry:native_modules)if(entry.name==name){
   HMODULE module=entry.module;ReleaseSRWLockExclusive(&module_lock);
   FreeLibrary(loaded);return module;
 }
 native_modules.push_back({name,loaded});
 ReleaseSRWLockExclusive(&module_lock);
 return loaded;
}
extern "C" __declspec(dllexport) void uwph_set_builtin_resolver(BuiltinResolver resolver){builtin_resolver=resolver;}
extern "C" __declspec(dllexport) FARPROC uwph_resolve_import(LPCWSTR dll,LPCSTR symbol){
 if(!dll||!symbol){SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
 std::wstring name(dll);
 for(auto& c:name){if(c==L'/'||c==L'\\'||c==L':'){SetLastError(ERROR_INVALID_NAME);return nullptr;}c=(wchar_t)towlower(c);}
 if(name.find(L'.')==std::wstring::npos)name+=L".dll";
 for(auto& route:routes)if(name==route.name){
   if(route.kind!=RouteKind::system){
     // Callback owns Wine/game/stub loading and lifetime. It must never fall
     // through to native user32/kernel32/ntdll when unimplemented.
     if(builtin_resolver)return builtin_resolver(route.name,symbol);
     SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return nullptr;
   }
   HMODULE module=native_module(route.name);
   if(!module)return nullptr;
   // One retained reference per native route, including missing-export lookups.
   return GetProcAddress(module,symbol);
 }
 SetLastError(ERROR_MOD_NOT_FOUND);return nullptr;
}
