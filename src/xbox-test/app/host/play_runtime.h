#pragma once
#include "../../uwph/xinput/modules.h"
#include "../../uwph/opengl32/modules.h"
// Play tracks mapped image ownership and dependencies. Windows owns the
// actual AppContainer PEB/TEB, native threads and static TLS carrier slots.
// Guest process APIs below expose the guest identity without rewriting the
// launcher's PEB or installing a private segment/exception mechanism.
#include <atomic>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <array>
#include <d3d11.h>
#include <d3d12.h>
#include <intrin.h>
#include "play_graphics.h"
#include "d3d11_diag.h"
#include "d3d12_diag.h"
#include "../../uwph/vcruntime/route.h"
#include "steam_app_ticket.h"
#include "redact.h"
#include "../../shared/rwx_emulation.h"
#include "tls_slot0.h"
namespace play_runtime {
constexpr DWORD missingException=0xe0520001, exitException=0xe0520002;
struct Module {
    std::wstring path;
    std::string key;
    wxl::Image image;
    bool mapped=false, mappingFailed=false, prepared=false, initialized=false, initializing=false, initializationFailed=false;
    size_t references=0;
    size_t notifications=0;
    bool pinned=false;
    std::vector<Module*> dependencies;
};
// Play's loader lock (recursive, like the OS loader lock). It records its
// owner so a guest thread that ends without unwinding (ExitProcess or a crash
// contained by Play, both ExitThread) releases what it held; otherwise every
// later GetProcAddress/LoadLibrary would wait forever on a dead owner.
class LoaderMutex {
    std::recursive_mutex mutex;
    std::atomic<DWORD> owner{0};
    unsigned depth=0;
public:
    void lock(){mutex.lock();owner.store(GetCurrentThreadId());++depth;}
    bool try_lock(){if(!mutex.try_lock())return false;owner.store(GetCurrentThreadId());++depth;return true;}
    void unlock(){if(--depth==0)owner.store(0);mutex.unlock();}
    // Called on a thread about to end without unwinding.
    void abandon(){
        if(owner.load()!=GetCurrentThreadId())return;
        unsigned held=depth;depth=0;owner.store(0);
        while(held--)mutex.unlock();
    }
};
struct State {
    std::wstring root, cwd, command;
    std::string commandA;
    wxl::Log log=nullptr;
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<Module*> order;
    Module* exe=nullptr;
    std::atomic<bool> executed{false}, finished{false};
    std::atomic<int> milestone{0};
    std::atomic<DWORD> thread{0};
    std::atomic<DWORD> firstChanceCode{0};
    std::atomic<uintptr_t> firstChanceAddress{0};
    std::atomic<unsigned> firstChanceCount{0};
    std::atomic<unsigned> invalidHandles{0};   // strict-handle-check raises continued (see vectored)
    std::mutex eventMutex;
    LoaderMutex loaderMutex;
    std::string last="loader not entered", result;
    DWORD code=0;
    std::array<wxl::Carrier,16> carriers;
    std::atomic<int> mappedCount{0}, moduleCount{0};
    bool success=false;
    HANDLE worker=nullptr;
    PVOID veh=nullptr;
    void event(const std::string& text) {
        { std::lock_guard<std::mutex> lock(eventMutex); last=text; }
        if(log) log(("PRUN EVENT "+text).c_str());
    }
    std::string snapshot() { std::lock_guard<std::mutex> lock(eventMutex); return last; }
};
static State* state=nullptr; // One guest process per launcher lifetime.
static std::atomic<State*> observed{nullptr};
static thread_local bool guestThread=false;
static thread_local unsigned mappingImports=0;
// Only the current dynamic lookup's export chain contributes pending work.
// Historic dependencies (including retained failures) must not poison lookups
// that reach only already initialized images. Nested callback lookups restore
// their caller's collection scope.
static thread_local bool* pendingExportLookup=nullptr;
static std::string lower(std::string s) { for(auto& c:s) if(c>='A'&&c<='Z') c+=32; return s; }
static std::string narrow(const std::wstring& s) {
    if(s.empty()) return {}; int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr); return r;
}
static std::wstring wide(const char* s) {
    if(!s) return {}; int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0); if(!n) return {};
    std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,0,s,-1,r.data(),n); r.pop_back(); return r;
}
static std::string basename(const std::wstring& s) { auto p=s.find_last_of(L"\\/"); return lower(narrow(p==std::wstring::npos?s:s.substr(p+1))); }
static Module* find(HMODULE h) { for(auto& m:state->modules) if(m->image.base==(BYTE*)h) return m.get(); return nullptr; }
static Module* named(const std::wstring& name) {
    std::string key=basename(name); if(key.find('.')==std::string::npos) key+=".dll";
    for(auto& m:state->modules) if(m->key==key) return m.get(); return nullptr;
}
#include "../../uwph/kernel32/guest_loader_queries.h"
#include "../../uwph/kernel32/guest_resource_queries.h"
static bool exists(const std::wstring& path) { DWORD a=GetFileAttributesW(path.c_str()); return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY); }
#include "../../uwph/kernel32/guest_loader_search.h"
static uintptr_t resolve(const char*,const char*,WORD);
static Module* map(const std::wstring& path,const std::string& asKey={},wxl::Resolver resolver=nullptr);
static bool initialize(Module&);
static bool initializePending();
static std::string describe(uintptr_t);
static void stopMissing(const std::string& name,uintptr_t caller=0) {
    std::string text="missing import hit: "+name;
    if(caller){char b[64];snprintf(b,sizeof b," caller=0x%llx ",(unsigned long long)caller);text+=b;text+=describe(caller);}
    state->event(text);
    RaiseException(missingException,EXCEPTION_NONCONTINUABLE,0,nullptr);
}
static uintptr_t missing(const char* dll,const char* name,WORD ordinal) {
    std::string full=lower(dll)+"!"+(name?name:"#"+std::to_string(ordinal));
    if(mappingImports && wxl::resolvingDelay) {
        if(state->log && mlp::first_report("delay "+full)) state->log(("PRUN delay import not found yet, bound on first call: "+full).c_str());
        return 0;
    }
    if(mappingImports) {state->event("unresolved static import: "+full+"; guest initialization blocked; "+state->snapshot());return 0;}
    // Optional dynamic feature detection observes NULL. No unresolved import
    // receives executable code, including the probe-only failure stubs.
    // Optional detection is diagnostic, not a new execution milestone/result.
    if(state->log && mlp::first_report("optional "+full)) state->log(("PRUN OPTIONAL unresolved dynamic export: "+full).c_str());
    SetLastError(ERROR_PROC_NOT_FOUND);return 0;
}
// No guest child-process provider exists: the console's UWP container cannot
// start desktop processes, and Play never reaches a native process export.
// As on Windows, the import itself resolves (UnityPlayer.dll statically
// imports CreateProcessW for its optional out-of-process crash reporter); every
// call fails with ERROR_NOT_SUPPORTED. A refused call is diagnostic only and
// does not replace launch progress: the guest decides how to carry on.
// This is a feature boundary, not a sandbox: other launch APIs/ordinals and
// arbitrary native export aliases are not proven blocked by this policy.
static bool processCreation(const char* name) {
    return name && (!strcmp(name,"CreateProcessA") || !strcmp(name,"CreateProcessW"));
}
static BOOL unsupportedProcess(const char* name,LPPROCESS_INFORMATION info) {
    if(info) memset(info,0,sizeof *info);
    if(state->log) state->log((std::string("PRUN OPTIONAL ")+name+" refused: no guest child-process provider (ERROR_NOT_SUPPORTED)").c_str());
    SetLastError(ERROR_NOT_SUPPORTED);return FALSE;
}
static BOOL WINAPI createProcessA(LPCSTR,LPSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCSTR,LPSTARTUPINFOA,LPPROCESS_INFORMATION info) {
    return unsupportedProcess("CreateProcessA",info);
}
static BOOL WINAPI createProcessW(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION info) {
    return unsupportedProcess("CreateProcessW",info);
}
// ShellExecute(Ex) starts programs and URL handlers, the same boundary as
// CreateProcess: every form resolves to a refusal (SE_ERR_ACCESSDENIED /
// FALSE with ERROR_NOT_SUPPORTED), never to native shell32. The guest decides
// how to carry on (SteamStub asks Steam to relaunch the game this way).
static bool shellLaunch(const char* name) {
    return name && (!strcmp(name,"ShellExecuteA") || !strcmp(name,"ShellExecuteW") ||
                    !strcmp(name,"ShellExecuteExA") || !strcmp(name,"ShellExecuteExW"));
}
struct ShellExecuteInfo { DWORD size,mask; HWND window; const void* verb,*file,*parameters,*directory; int show; HINSTANCE instance; void* idList; const void* className; HKEY classKey; DWORD hotKey; HANDLE icon,process; };
static_assert(sizeof(ShellExecuteInfo)==112,"SHELLEXECUTEINFO x64 layout");
static void refusedShell(const char* name) {
    if(state->log) state->log((std::string("PRUN OPTIONAL ")+name+" refused: no guest program launcher (ERROR_NOT_SUPPORTED)").c_str());
    SetLastError(ERROR_NOT_SUPPORTED);
}
static HINSTANCE WINAPI shellExecuteA(HWND,LPCSTR,LPCSTR,LPCSTR,LPCSTR,INT) {refusedShell("ShellExecuteA");return (HINSTANCE)5;}  // SE_ERR_ACCESSDENIED
static HINSTANCE WINAPI shellExecuteW(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT) {refusedShell("ShellExecuteW");return (HINSTANCE)5;}
static BOOL shellExecuteEx(ShellExecuteInfo* info,const char* name) {
    if(info && info->size==sizeof *info) {info->instance=(HINSTANCE)5;info->process=nullptr;}
    refusedShell(name);return FALSE;
}
static BOOL WINAPI shellExecuteExA(ShellExecuteInfo* info) {return shellExecuteEx(info,"ShellExecuteExA");}
static BOOL WINAPI shellExecuteExW(ShellExecuteInfo* info) {return shellExecuteEx(info,"ShellExecuteExW");}
static uintptr_t shellExport(const char* name) {
    return !strcmp(name,"ShellExecuteA")?uintptr_t(&shellExecuteA):!strcmp(name,"ShellExecuteW")?uintptr_t(&shellExecuteW):
           !strcmp(name,"ShellExecuteExA")?uintptr_t(&shellExecuteExA):uintptr_t(&shellExecuteExW);
}
static uintptr_t processExport(const char* name) {
    return !strcmp(name,"CreateProcessA")?uintptr_t(&createProcessA):uintptr_t(&createProcessW);
}
// Native modules that genuinely export the process names, identified without
// any native process-export lookup: by handle, then by file name.
static bool processModule(HMODULE h) {
    using Handle=HMODULE(WINAPI*)(LPCWSTR);
    if(auto handle=(Handle)kernel_tail::native("GetModuleHandleW"))
        for(auto module:{L"kernel32.dll",L"kernelbase.dll"}) if(h && h==handle(module)) return true;
    wchar_t p[32768]{};using File=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD);
    auto file=(File)kernel_tail::native("GetModuleFileNameW");
    if(!file || !file(h,p,32768)) return false;
    auto key=basename(p);
    return key=="kernel32.dll" || key=="kernelbase.dll" || key.find("api-ms-win-core-")==0;
}
// The guest ends where it calls ExitProcess, without unwinding, as on Windows.
// Raising an exception to unwind back to the worker took the whole launcher
// down when a frame had no unwind data (SteamStub's DRM runs from memory it
// maps itself) and could be swallowed by a guest catch(...). The calling
// thread exits; game code has run, so the launcher reports the end and
// restarts to unload the rest of the guest.
static void WINAPI exitProcess(UINT code) {
    state->code=code; state->event("ExitProcess code="+std::to_string(code));
    state->success=true; state->result=state->snapshot();
    if(GetCurrentThreadId()==state->thread) guestThread=false;
    state->finished.store(true,std::memory_order_release);
    state->loaderMutex.abandon();
    ExitThread(code);
}
static BOOL WINAPI terminateProcess(HANDLE process,UINT code) {
    if(process==GetCurrentProcess()) { exitProcess(code); return TRUE; }
    SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
static void WINAPI rtlExit(LONG code) { exitProcess(UINT(code)); }
static LPWSTR WINAPI commandW() { return state->command.data(); }
static LPSTR WINAPI commandA() { return state->commandA.data(); }
static HMODULE WINAPI moduleW(LPCWSTR name) {
    if(name)if(auto input=xinput_core::find_module(basename(name).c_str(),false))return input;
    if(name)if(auto gl=opengl32_core::find_module(basename(name).c_str(),false))return gl;
    if(!name) return (HMODULE)state->exe->image.base;
    if(auto m=named(name)) return (HMODULE)m->image.base;
    using Fn=HMODULE(WINAPI*)(LPCWSTR); auto fn=(Fn)kernel_tail::native("GetModuleHandleW");
    return fn?fn(name):nullptr;
}
#include "../../uwph/kernel32/guest_module_filename.h"
#include "../../uwph/kernel32/guest_loader_lifetime.h"
// Emulated Steam client for an owned game (AGENTS.md rule 3): when the
// launcher has staged one after its ownership check, a guest load of
// steamclient64.dll (SteamStub's locator, the game's own steam_api64) maps
// that file. Without one, steamclient64.dll stays refused (item 36).
static std::wstring steamClient, steamClientSigned;
// `signedClient` is Valve's own signed steamclient64.dll, fetched from Valve
// (never shipped), for games whose Steam DRM wrapper checks the client's
// signature; see signedClientModule().
static void set_steam_client(const std::wstring& path,const std::wstring& signedClient={}) { steamClient=path; steamClientSigned=signedClient; }
static Module* signedClientModule();
// The environment Steam's client locator reads, as Steam itself (and gbe_fork's
// loader) publishes it: HKCU\Software\Valve\Steam\ActiveProcess with this
// process as the client (pid), the client DLL path, the universe and the
// signed-in account ID. It lives in the guest registry only.
static bool seed_steam_client_registry(const std::wstring& client,DWORD accountId) {
    HKEY key=nullptr;
    if(regb::RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",0,nullptr,0,0,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    DWORD pid=GetCurrentProcessId();
    auto text=[&](LPCWSTR name,const std::wstring& value){return regb::RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),DWORD((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;};
    bool ok=regb::RegSetValueExW(key,L"pid",0,REG_DWORD,(const BYTE*)&pid,sizeof pid)==ERROR_SUCCESS&&
        regb::RegSetValueExW(key,L"ActiveUser",0,REG_DWORD,(const BYTE*)&accountId,sizeof accountId)==ERROR_SUCCESS&&
        text(L"SteamClientDll64",client)&&text(L"Universe",L"Public");
    regb::RegCloseKey(key);
    return ok;
}
static HMODULE WINAPI loadExW(LPCWSTR name,HANDLE file,DWORD flags) {
    if(!name || file) {SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    constexpr DWORD knownFlags= DONT_RESOLVE_DLL_REFERENCES|LOAD_LIBRARY_AS_DATAFILE|
        LOAD_WITH_ALTERED_SEARCH_PATH|LOAD_IGNORE_CODE_AUTHZ_LEVEL|LOAD_LIBRARY_AS_IMAGE_RESOURCE|
        LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_REQUIRE_SIGNED_TARGET|
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|
        LOAD_LIBRARY_SEARCH_USER_DIRS|LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS|
        0x00002000|0x00004000|0x00008000; // SAFE_CURRENT_DIRS, SYSTEM32_NO_FORWARDER, OS_INTEGRITY_CONTINUITY
    if(flags&~knownFlags) {SetLastError(ERROR_INVALID_PARAMETER);return nullptr;}
    // The mapper has no resource-only image handles or signature-policy
    // verifier. Never turn either contract into an executable successful load.
    if(flags&(LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE|
              LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_IGNORE_CODE_AUTHZ_LEVEL)) {
        SetLastError(ERROR_NOT_SUPPORTED);return nullptr;
    }
    std::lock_guard<LoaderMutex> loaderLock(state->loaderMutex);
    std::wstring requested=name;
    auto leaf=requested.substr(requested.find_last_of(L"\\/")+1);
    if(leaf.find(L'.')==std::wstring::npos) requested+=L".dll";
    // There is no genuine Steam client (item 36). Refuse the client DLL after
    // Windows name normalization, even when a copy ships in the game root, so
    // nothing but a real client could ever answer the game's Steam API. The
    // game's original steam_api64.dll maps unmodified like any other game DLL
    // and its SteamAPI_Init fails naturally without a client.
    auto key=basename(requested);
    if(key=="steamclient64.dll") {
        if(steamClient.empty()) {
            state->event("LoadLibrary refused name="+narrow(requested)+": no genuine Steam client (item 36)");
            SetLastError(ERROR_MOD_NOT_FOUND);return nullptr;
        }
        if(flags&~(LOAD_WITH_ALTERED_SEARCH_PATH|LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
        if(!steamClientSigned.empty()) {
            auto client=signedClientModule();
            if(!client) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
            if(!initializePending()) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
            ++client->references;return (HMODULE)client->image.base;
        }
        requested=steamClient;flags=0;
        state->event("steamclient64.dll served by the staged emulated Steam client (owned game)");
    }
    Module* m=nullptr;
    {std::lock_guard<LoaderMutex> lock(state->loaderMutex);
     m=named(requested);
     if(!m && !apiSetName(requested) && !systemDirectoryPath(requested)) {
         // Flags that only steer where the file is searched (Mono/Unity pass
         // LOAD_WITH_ALTERED_SEARCH_PATH or the SEARCH_* set for plugins) do not
         // change what is loaded; anything else is refused below, as before.
         constexpr DWORD searchOnly=LOAD_WITH_ALTERED_SEARCH_PATH|LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|
             LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_USER_DIRS|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
         auto p=guestLibraryPath(requested,!(flags&~searchOnly));
         if(exists(p)) {
             if(flags&~searchOnly) {
                 state->event("LoadLibrary refused name="+narrow(requested)+" flags="+std::to_string(flags)+": unsupported load flags");
                 SetLastError(ERROR_NOT_SUPPORTED);return nullptr;
             }
             m=map(p);
             if(!m) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
         }
     }
    }
    if(m) {
        if(flags&~(LOAD_WITH_ALTERED_SEARCH_PATH|LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|
                   LOAD_LIBRARY_SEARCH_USER_DIRS|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
        if(m->mappingFailed || !m->mapped) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
        ++m->references;
        // A DllMain may load a module whose own DllMain is still running on
        // this thread (Wine's concrt140 loads msvcp140 while msvcp140 attaches
        // it). Windows and Wine return that handle; the recursive loader lock
        // means no other thread can be inside this branch meanwhile.
        if(!initializePending() || !(m->initialized || m->initializing)) {--m->references;SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
        return (HMODULE)m->image.base;
    }
    if(auto input=xinput_core::find_module(key.c_str(),true)) {
        if(state->log)state->log(("XINPUT LoadLibrary "+narrow(requested)+" -> Play's emulated module").c_str());
        return input;
    }
    // No console opengl32.dll: serve Play's WGL state surface, not a host DLL.
    if(auto gl=opengl32_core::find_module(key.c_str(),true))return gl;
    // Restrict fallback to documented system DLL search, never desktop paths.
    if(flags&~(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_REQUIRE_SIGNED_TARGET)) {
        SetLastError(ERROR_NOT_SUPPORTED);return nullptr;
    }
    auto result=LoadLibraryExW(requested.c_str(),nullptr,flags|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!result) {DWORD error=GetLastError();state->event("LoadLibrary failed name="+narrow(requested)+" error="+std::to_string(error));SetLastError(error);}
    return result;
}
static HMODULE WINAPI loadW(LPCWSTR name) { return loadExW(name,nullptr,0); }
#include "../../uwph/kernel32/guest_loader_ansi.h"
// Wine ntdll/loader.c find_forwarded_export resolves the last dot as the
// DLL/symbol separator, supports #ordinals and records a module dependency.
// Keep this execution behavior separate from the non-executing import probe.
static uintptr_t exported(Module& module,const char* name,WORD ordinal) {
    // Retained failed images are diagnostic only, including forwarder sources
    // and targets. Do not require initialized: circular imports/TLS setup may
    // legitimately resolve exports before initialization has finished.
    if(module.mappingFailed || module.initializationFailed || !module.image.base) return 0;
    if(pendingExportLookup && !mappingImports && !module.initialized) {
        // The recursive lock permits the initializing thread to resolve its
        // prepared siblings (all TLS/unwind setup precedes callbacks). A new,
        // unprepared dynamic target still needs initialization before return.
        bool recursive=false;
        if(module.prepared)for(auto m:state->order)if(m->initializing){recursive=true;break;}
        if(!recursive)*pendingExportLookup=true;
    }
    auto& image=module.image;
    if(auto p=mlp::asset_export(image,name,ordinal)) return p;
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!image.valid(dir.VirtualAddress,dir.Size)||dir.Size<sizeof(IMAGE_EXPORT_DIRECTORY))return 0;
    auto e=(IMAGE_EXPORT_DIRECTORY*)(image.base+dir.VirtualAddress);
    if(!image.valid(e->AddressOfFunctions,size_t(e->NumberOfFunctions)*4)||
       !image.valid(e->AddressOfNames,size_t(e->NumberOfNames)*4)||
       !image.valid(e->AddressOfNameOrdinals,size_t(e->NumberOfNames)*2))return 0;
    DWORD index=DWORD(ordinal)-e->Base;
    if(name) {
        index=e->NumberOfFunctions;
        for(DWORD i=0;i<e->NumberOfNames;++i) {
            auto n=image.str(((DWORD*)(image.base+e->AddressOfNames))[i]);
            if(n&&!strcmp(n,name)){index=((WORD*)(image.base+e->AddressOfNameOrdinals))[i];break;}
        }
    }
    if(index>=e->NumberOfFunctions)return 0;
    DWORD rva=((DWORD*)(image.base+e->AddressOfFunctions))[index];
    if(rva<dir.VirtualAddress||rva-dir.VirtualAddress>=dir.Size)return 0;
    auto start=(const char*)image.base+rva;
    auto end=(const char*)memchr(start,0,dir.Size-(rva-dir.VirtualAddress));
    if(!end)return 0;
    std::string forward(start,end);auto dot=forward.find_last_of('.');
    if(dot==std::string::npos||dot==0||dot+1==forward.size())return 0;
    auto dll=forward.substr(0,dot), symbol=forward.substr(dot+1);
    if(dll.find('.')==std::string::npos)dll+=".dll";
    WORD targetOrdinal=0;const char* targetName=symbol.c_str();
    if(symbol[0]=='#') {
        if(symbol.size()==1)return 0;
        unsigned value=0;
        for(size_t i=1;i<symbol.size();++i) {
            if(symbol[i]<'0'||symbol[i]>'9')return 0;
            value=value*10+unsigned(symbol[i]-'0');if(value>65535)return 0;
        }
        targetOrdinal=WORD(value);targetName=nullptr;
    }
    static thread_local unsigned depth=0;
    if(depth>=32)return 0; // Reject cyclic/pathological forwarder chains.
    ++depth;auto result=resolve(dll.c_str(),targetName,targetOrdinal);--depth;
    if(auto dependency=named(wide(dll.c_str()))) {
        if(dependency!=&module&&std::find(module.dependencies.begin(),module.dependencies.end(),dependency)==module.dependencies.end())
            module.dependencies.push_back(dependency);
    }
    return result;
}
// ---- Rich presence the game sets through the Steam API provider
// (steam_presence.h). gbe_fork keeps the key/value set in memory and sends it
// only to its own LAN peers, so the launcher's Steam session never sees it and
// friends cannot read it. The provider's own SetRichPresence/ClearRichPresence
// still run (the game reads back what it set); the launcher observes the same
// calls here, from the game's own thread, and uploads the set to Steam.
// This is observation, not replacement: nothing on disk changes, and the
// provider answers the game exactly as before.
struct RichPresenceTap {
    std::function<void(const std::string& key,const char* value)> set;   // value NULL: the key is deleted
    std::function<void()> clear;
};
static std::mutex richPresenceMutex;
static RichPresenceTap richPresence;
static void set_rich_presence_tap(RichPresenceTap tap) {
    std::lock_guard<std::mutex> lock(richPresenceMutex);
    richPresence=std::move(tap);
}
// The tap is copied out before it runs: it belongs to the launcher and takes
// its own lock, which must not be held here.
static void richPresenceSet(const std::string& key,const char* value) {
    RichPresenceTap tap; {std::lock_guard<std::mutex> lock(richPresenceMutex);tap=richPresence;}
    if(tap.set)tap.set(key,value);
}
static void richPresenceClear() {
    RichPresenceTap tap; {std::lock_guard<std::mutex> lock(richPresenceMutex);tap=richPresence;}
    if(tap.clear)tap.clear();
}
// The real provider addresses are re-read on every resolution: a released
// launch unmaps the provider, so the next one may map it at a different base.
static std::atomic<uintptr_t> richPresenceRealSet{0}, richPresenceRealClear{0};
static uintptr_t richPresenceHook(const char* name,uintptr_t real) {
    if(!real) return 0;
    if(!strcmp(name,"SteamAPI_ISteamFriends_SetRichPresence")) {
        using Fn=uint32_t(*)(void*,const char*,const char*);
        richPresenceRealSet.store(real,std::memory_order_release);
        static Fn hook=[](void* self,const char* key,const char* value){
            auto inner=(Fn)richPresenceRealSet.load(std::memory_order_acquire);
            uint32_t ok=inner?inner(self,key,value):0;
            if(key)richPresenceSet(key,value);
            return ok;
        };
        return (uintptr_t)hook;
    }
    if(!strcmp(name,"SteamAPI_ISteamFriends_ClearRichPresence")) {
        using Fn=void(*)(void*);
        richPresenceRealClear.store(real,std::memory_order_release);
        static Fn hook=[](void* self){
            auto inner=(Fn)richPresenceRealClear.load(std::memory_order_acquire);
            if(inner)inner(self);
            richPresenceClear();
        };
        return (uintptr_t)hook;
    }
    return 0;
}
// ---- Leaderboards the game asks for through the Steam API provider
// (steam_leaderboards.h). gbe_fork answers Find/Download from its own disk
// cache and its LAN peer network, so with no peers a rank screen shows no
// players. The launcher asks the real Steam backend over its own CM session and
// writes the provider's cache file. The provider reads that file once, the
// first time a board is found in the process, so the seed runs BEFORE the
// provider's own Find; the game's call then still runs and still receives the
// provider's handle, and the rows it downloads are the real ones.
struct LeaderboardTap {
    // Runs on the game's thread before the provider's Find. Must be bounded:
    // the game is blocked in it. Returning false leaves the provider's answer
    // exactly as it was.
    std::function<bool(const std::string& name,int sort,int display,bool create)> seed;
    std::function<void(const std::string& name,int32_t score,const int32_t* details,int count,int method)> upload;
    // The rows the launcher holds for a board, fresher than the provider's
    // in-memory copy (which it read once, at the board's first Find). Called on
    // the game's thread from GetDownloadedLeaderboardEntry. `nullopt` means the
    // launcher holds nothing for that board, so the provider's own entry stands;
    // an empty vector means Steam answered that the board is empty. `details`
    // is the raw int32 array Steam sent for that row, so the entry the game
    // reads is whole.
    struct Row { uint64_t steamid; int32_t rank,score; std::string details; };
    std::function<std::optional<std::vector<Row>>(const std::string& name)> rows;
};
static std::mutex leaderboardMutex;
static LeaderboardTap leaderboard;
static void set_leaderboard_tap(LeaderboardTap tap) {
    std::lock_guard<std::mutex> lock(leaderboardMutex);
    leaderboard=std::move(tap);
}
static bool leaderboardSeed(const std::string& name,int sort,int display,bool create) {
    LeaderboardTap tap; {std::lock_guard<std::mutex> lock(leaderboardMutex);tap=leaderboard;}
    return tap.seed?tap.seed(name,sort,display,create):false;
}
static void leaderboardUpload(const std::string& name,int32_t score,const int32_t* details,int count,int method) {
    LeaderboardTap tap; {std::lock_guard<std::mutex> lock(leaderboardMutex);tap=leaderboard;}
    if(tap.upload)tap.upload(name,score,details,count,method);
}
static std::optional<std::vector<LeaderboardTap::Row>> leaderboardRows(const std::string& name) {
    LeaderboardTap tap; {std::lock_guard<std::mutex> lock(leaderboardMutex);tap=leaderboard;}
    return tap.rows?tap.rows(name):std::nullopt;
}
// A score is addressed by the SteamLeaderboard_t the game passes back, which is
// the handle from LeaderboardFindResult_t, NOT the SteamAPICall_t that Find
// returns. That handle is the provider's own (gbe_fork: an index into its
// cached_leaderboards), so the provider is asked which board it names rather
// than the launcher guessing from Find order.
static std::atomic<Module*> leaderboardModule{nullptr};   // the provider image, while a launch runs
static void set_leaderboard_provider(Module* module) { leaderboardModule.store(module,std::memory_order_release); }
static std::string leaderboardHandleName(void* self,uint64_t handle) {
    Module* module=leaderboardModule.load(std::memory_order_acquire);
    if(!module||!state) return std::string();
    // exported() reads the image's export table, so this is the provider's real
    // GetLeaderboardName even when the game also bound a hook of ours. Resolve
    // under the loader lock, call outside it: the provider is guest code.
    uintptr_t p=0;
    {std::lock_guard<LoaderMutex> lock(state->loaderMutex);
     p=exported(*module,"SteamAPI_ISteamUserStats_GetLeaderboardName",0);}
    if(!p) return std::string();
    using Fn=const char*(*)(void*,uint64_t);
    const char* name=((Fn)p)(self,handle);
    return name?std::string(name):std::string();
}
static std::atomic<uintptr_t> leaderboardRealFind{0}, leaderboardRealCreate{0}, leaderboardRealUpload{0}, leaderboardRealEntry{0};
static uintptr_t leaderboardHook(const char* name,uintptr_t real) {
    if(!real) return 0;
    if(!strcmp(name,"SteamAPI_ISteamUserStats_FindLeaderboard")) {
        using Fn=uint64_t(*)(void*,const char*);
        leaderboardRealFind.store(real,std::memory_order_release);
        static Fn hook=[](void* self,const char* board)->uint64_t{
            auto inner=(Fn)leaderboardRealFind.load(std::memory_order_acquire);
            // Seed first: the provider reads the cache file only on the first
            // Find of a board in this process.
            if(board&&*board)leaderboardSeed(board,2/*Descending*/,1/*Numeric*/,false);
            return inner?inner(self,board):0;
        };
        return (uintptr_t)hook;
    }
    if(!strcmp(name,"SteamAPI_ISteamUserStats_FindOrCreateLeaderboard")) {
        using Fn=uint64_t(*)(void*,const char*,int,int);
        leaderboardRealCreate.store(real,std::memory_order_release);
        static Fn hook=[](void* self,const char* board,int sort,int display)->uint64_t{
            auto inner=(Fn)leaderboardRealCreate.load(std::memory_order_acquire);
            if(board&&*board)leaderboardSeed(board,sort,display,true);
            return inner?inner(self,board,sort,display):0;
        };
        return (uintptr_t)hook;
    }
    if(!strcmp(name,"SteamAPI_ISteamUserStats_UploadLeaderboardScore")) {
        using Fn=uint64_t(*)(void*,uint64_t,int,int32_t,const int32_t*,int);
        leaderboardRealUpload.store(real,std::memory_order_release);
        static Fn hook=[](void* self,uint64_t board,int method,int32_t score,const int32_t* details,int count)->uint64_t{
            auto inner=(Fn)leaderboardRealUpload.load(std::memory_order_acquire);
            uint64_t call=inner?inner(self,board,method,score,details,count):0;
            // The provider's own handle -> name, so the score reaches the board
            // the game actually uploaded to.
            auto found=leaderboardHandleName(self,board);
            if(!found.empty())leaderboardUpload(found,score,details,count,method);
            return call;
        };
        return (uintptr_t)hook;
    }
    if(!strcmp(name,"SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry")) {
        using Fn=uint32_t(*)(void*,uint64_t,int,void*,int32_t*,int);
        leaderboardRealEntry.store(real,std::memory_order_release);
        static Fn hook=[](void* self,uint64_t entries,int index,void* entry,int32_t* details,int maxDetails)->uint32_t{
            auto inner=(Fn)leaderboardRealEntry.load(std::memory_order_acquire);
            uint32_t ok=inner?inner(self,entries,index,entry,details,maxDetails):0;
            // The provider answered from the rows it cached at the board's first
            // Find. When the launcher holds fresher rows for that board (a
            // refresh, or a score this account just uploaded), the entry the game
            // reads is replaced in place: same struct, same layout, the game's
            // own call still ran.
            //
            // The row COUNT is not ours to change: the game loops
            // `index < m_cEntryCount`, and that number is fixed by the
            // provider's own DownloadLeaderboardEntries from its in-memory copy
            // (gbe_fork: cached_leaderboards[handle-1].entries.size()) and
            // delivered in its CallResult. So a board that GREW since its first
            // Find shows only the rows the provider reports; the tap replaces
            // those, it cannot add more. A board that was empty at its first
            // Find therefore stays empty for the launch — except for this
            // account's own upload, which the provider appends to its copy
            // itself (gbe_fork's UploadLeaderboardScore -> update_leaderboard_entry).
            // Growing that count would mean writing into the provider's board,
            // which only its own score-upload path may do.
            if(!ok||!entry)return ok;
            auto found=leaderboardHandleName(self,entries);
            if(found.empty())return ok;
            auto rows=leaderboardRows(found);
            if(!rows)return ok;   // nothing held for this board: the provider's entry stands
            // Steam answered that the board is empty: the provider's first-Find
            // copy is stale, so the game reads an empty entry rather than a
            // player who is no longer on the board.
            if(rows->empty())return 0;
            // Past the launcher's rows the player is no longer on the board,
            // even though the provider's first-Find copy still has them.
            if(index<0||size_t(index)>=rows->size())return 0;
            struct LeaderboardEntry_t { uint64_t user; int32_t rank,score,details; uint64_t ugc; };
            auto* e=(LeaderboardEntry_t*)entry;
            // The provider's cached list and the launcher's rows are both in
            // global rank order, so index i is global rank i+1 in each. Patch
            // only when the launcher's row agrees on that rank: a shorter
            // refresh or a provider that honours the download range would
            // otherwise mix rows from different positions.
            if((*rows)[size_t(index)].rank!=index+1)return ok;
            e->user=(*rows)[size_t(index)].steamid;
            e->rank=(*rows)[size_t(index)].rank;
            e->score=(*rows)[size_t(index)].score;
            // The row's details are part of what the game reads; a refresh that
            // changed the score but left the provider's first-Find details in
            // place would hand the game a mismatched entry.
            const auto& raw=(*rows)[size_t(index)].details;
            int32_t have=int32_t(raw.size()/sizeof(int32_t));
            e->details=have;
            for(int32_t i=0;details&&i<have&&i<maxDetails;++i)memcpy(&details[i],raw.data()+size_t(i)*sizeof(int32_t),sizeof(int32_t));
            return ok;
        };
        return (uintptr_t)hook;
    }
    return 0;
}
// ---- Persona names the game asks for through the Steam API provider
// (steam_persona.h). gbe_fork answers GetFriendPersonaName from its own friend
// list, which only holds peers on its LAN network; with disable_networking=1
// that list is empty, so every leaderboard row reads "Unknown User". The
// launcher asks the real Steam backend for the names and answers here, in
// memory. The provider's own call still runs; its answer is replaced only when
// the launcher has a name for that SteamID.
struct PersonaTap {
    // A name for this SteamID, or nullptr when Steam has not answered for it.
    // The pointer must stay valid for the launch (it is handed to the game).
    std::function<const char*(uint64_t steamid)> name;
    // The account's own persona name, or nullptr.
    std::function<const char*()> self;
    // The game asked about a SteamID outside the seeded leaderboard rows; queue
    // it for the keeper (never blocks the game's thread).
    std::function<void(uint64_t steamid)> want;
};
static std::mutex personaMutex;
static PersonaTap personaTap;
static void set_persona_tap(PersonaTap tap) {
    std::lock_guard<std::mutex> lock(personaMutex);
    personaTap=std::move(tap);
}
static const char* personaName(uint64_t steamid) {
    PersonaTap tap; {std::lock_guard<std::mutex> lock(personaMutex);tap=personaTap;}
    return tap.name?tap.name(steamid):nullptr;
}
static const char* personaSelf() {
    PersonaTap tap; {std::lock_guard<std::mutex> lock(personaMutex);tap=personaTap;}
    return tap.self?tap.self():nullptr;
}
static void personaWant(uint64_t steamid) {
    PersonaTap tap; {std::lock_guard<std::mutex> lock(personaMutex);tap=personaTap;}
    if(tap.want)tap.want(steamid);
}
// A SteamID the game asked about is queued for the keeper, which requests it
// once. The queue is bounded by distinct ids in the bridge (steam_persona.h),
// so a rank screen polling the same unresolved rows costs nothing.
static std::atomic<uintptr_t> personaRealName{0}, personaRealSelf{0}, personaRealRequest{0};
static uintptr_t personaHook(const char* name,uintptr_t real) {
    if(!real) return 0;
    if(!strcmp(name,"SteamAPI_ISteamFriends_GetFriendPersonaName")) {
        using Fn=const char*(*)(void*,uint64_t);
        personaRealName.store(real,std::memory_order_release);
        static Fn hook=[](void* self,uint64_t steamid)->const char*{
            auto inner=(Fn)personaRealName.load(std::memory_order_acquire);
            // The provider's own call still runs; its answer ("Unknown User" for
            // a SteamID that is not on its LAN friend list) stands unless the
            // launcher has the name Steam gave.
            const char* theirs=inner?inner(self,steamid):nullptr;
            if(const char* ours=personaName(steamid))return ours;
            // Not answered yet: the game asked, so the keeper should request it.
            personaWant(steamid);
            return theirs?theirs:"Unknown User";
        };
        return (uintptr_t)hook;
    }
    if(!strcmp(name,"SteamAPI_ISteamFriends_GetPersonaName")) {
        using Fn=const char*(*)(void*);
        personaRealSelf.store(real,std::memory_order_release);
        static Fn hook=[](void* self)->const char*{
            auto inner=(Fn)personaRealSelf.load(std::memory_order_acquire);
            const char* theirs=inner?inner(self):nullptr;
            if(const char* ours=personaSelf())return ours;
            return theirs?theirs:"Player";
        };
        return (uintptr_t)hook;
    }
    if(!strcmp(name,"SteamAPI_ISteamFriends_RequestUserInformation")) {
        using Fn=uint32_t(*)(void*,uint64_t,uint32_t);
        personaRealRequest.store(real,std::memory_order_release);
        static Fn hook=[](void* self,uint64_t steamid,uint32_t nameOnly)->uint32_t{
            auto inner=(Fn)personaRealRequest.load(std::memory_order_acquire);
            // The provider's own call still runs (gbe_fork's is a TODO that
            // answers "already known"); its answer is not what the game acts on.
            if(inner)inner(self,steamid,nameOnly);
            // The API's contract: true means "being fetched, you will get a
            // PersonaStateChange_t"; false means "already known, call the
            // getters now". A name the launcher already holds is known. One it
            // does not is queued and reported as in flight — but nothing here
            // posts PersonaStateChange_t (the provider owns that callback
            // plumbing), so a game that waits on it would wait forever. Answer
            // false instead: the getters are then called, and they return the
            // provider's answer until Steam's name lands, at which point the
            // tap hands out the real one. That is the same answer gbe_fork's
            // TODO gives, and it never strands a row.
            if(personaName(steamid))return 0;
            personaWant(steamid);
            return 0;
        };
        return (uintptr_t)hook;
    }
    return 0;
}
static uintptr_t steamApiExport(Module& module,const char* name,uintptr_t address) {
    if(!address||!name||strcmp(module.key.c_str(),"steam_api64.dll")) return address;
    // The provider image is the only image whose exports reach here, so it is
    // the one to ask for a leaderboard handle's name later.
    if(&module!=leaderboardModule.load(std::memory_order_acquire))set_leaderboard_provider(&module);
    if(auto hook=richPresenceHook(name,address)) {
        if(state->log&&mlp::first_report(std::string("steam rich presence ")+name))
            state->log((std::string("STEAM rich presence: ")+name+" observed; the set is uploaded to Steam by the launcher").c_str());
        return hook;
    }
    if(auto hook=leaderboardHook(name,address)) {
        if(state->log&&mlp::first_report(std::string("steam leaderboards ")+name))
            state->log((std::string("STEAM leaderboards: ")+name+" observed; rows come from the launcher's Steam session").c_str());
        return hook;
    }
    if(auto hook=personaHook(name,address)) {
        if(state->log&&mlp::first_report(std::string("steam persona ")+name))
            state->log((std::string("STEAM persona: ")+name+" observed; names come from the launcher's Steam session").c_str());
        return hook;
    }
    return address;
}
struct ExportLookup {
    bool pending=false;
    bool* previous=pendingExportLookup;
    ExportLookup() {pendingExportLookup=&pending;}
    ~ExportLookup() {pendingExportLookup=previous;}
    FARPROC finish(uintptr_t p) {
        // Decide from every image reached by this export chain, even if an
        // earlier failed lookup mapped its target. Resolve the complete chain
        // (and record dependencies) before running any new guest callbacks.
        if(p && pending && !mappingImports && !initializePending())p=0;
        if(!p)SetLastError(ERROR_PROC_NOT_FOUND);return (FARPROC)p;
    }
};
static FARPROC WINAPI proc(HMODULE h,LPCSTR name) {
    if(auto input=xinput_core::module(h))return xinput_core::module_proc(input,name);
    if(auto gl=opengl32_core::module(h))return opengl32_core::module_proc(gl,name);
    // find() iterates vector storage that map() may reallocate. Native-handle
    // resolution below can also inspect/map guest modules, so retain the lock
    // for the entire lookup, not just the mapped-export branch.
    std::lock_guard<LoaderMutex> lock(state->loaderMutex);
    ExportLookup lookup;
    if(auto m=find(h)) {
        WORD ordinal=IS_INTRESOURCE(name)?WORD((uintptr_t)name):0;
        return lookup.finish(steamApiExport(*m,IS_INTRESOURCE(name)?nullptr:name,exported(*m,IS_INTRESOURCE(name)?nullptr:name,ordinal)));
    }
    // Native handles (including OS-resolved API sets/forwarders) never expose
    // the native process exports, even when GetModuleFileNameW cannot identify
    // them. Modules that genuinely provide the names return Play's refusal;
    // others observe NULL, as Windows does for a name they do not export.
    // Keep owned mapped exports above separate from native fallback below.
    if(!IS_INTRESOURCE(name)) {
        if(processCreation(name)) {
            if(processModule(h)) return (FARPROC)processExport(name);
            SetLastError(ERROR_PROC_NOT_FOUND);return nullptr;
        }
        if(shellLaunch(name)) {
            wchar_t path[1024]{};using File=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD);
            auto file=(File)kernel_tail::native("GetModuleFileNameW");
            if(file&&file(h,path,1024)&&basename(path)=="shell32.dll") return (FARPROC)shellExport(name);
            SetLastError(ERROR_PROC_NOT_FOUND);return nullptr;
        }
        // Do not hand out raw GetProcAddress on the metadata-failure path:
        // a second lookup through it would bypass the unavailable-name gate.
        if(!strcmp(name,"GetProcAddress"))
            return GetProcAddress(h,name)?(FARPROC)&proc:nullptr;
    }
    // Route dynamic lookups back through the same process wrappers and router.
    wchar_t p[32768]{};using Fn=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD);auto fn=(Fn)kernel_tail::native("GetModuleFileNameW");
    if(fn && fn(h,p,32768)) {
        auto result=resolve(basename(p).c_str(),IS_INTRESOURCE(name)?nullptr:name,IS_INTRESOURCE(name)?WORD((uintptr_t)name):0);
        // Static imports are rejected while mapping; dynamic feature detection
        // observes NULL rather than a callable failure placeholder.
        return lookup.finish(result);
    }
    return GetProcAddress(h,name);
}
static std::string describe(uintptr_t address) {
    char b[160];
    for(auto& m:state->modules) if(address>=(uintptr_t)m->image.base && address-(uintptr_t)m->image.base<m->image.size) {
        DWORD rva=DWORD(address-(uintptr_t)m->image.base); DWORD64 base=0; auto f=RtlLookupFunctionEntry(address,&base,nullptr);
        snprintf(b,sizeof b,"%s+0x%lx (function RVA=0x%lx; private symbols unavailable)",m->key.c_str(),rva,f?f->BeginAddress:0); return b;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if(VirtualQuery((void*)address,&mbi,sizeof mbi)) { wchar_t path[32768]{};filenameW((HMODULE)mbi.AllocationBase,path,32768);snprintf(b,sizeof b,"%s+0x%llx",basename(path).c_str(),(unsigned long long)(address-(uintptr_t)mbi.AllocationBase));return b; }
    return "unknown module/symbol";
}
// Diagnostics only: inspect registered module metadata and unwind a copy of
// the fault context. Never alter the real exception or choose a guest handler.
static bool diagnosticUnwind(CONTEXT* context) {
    __try {
        DWORD64 base=0;auto function=RtlLookupFunctionEntry(context->Rip,&base,nullptr);
        if(function) {
            PVOID handler=nullptr;DWORD64 frame=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context->Rip,function,context,&handler,&frame,nullptr);
        } else {context->Rip=*(DWORD64*)context->Rsp;context->Rsp+=8;}
        return context->Rip!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static void exceptionDiagnostics(EXCEPTION_POINTERS* ep) {
    auto record=ep->ExceptionRecord;
    if(record->ExceptionCode==0xe06d7363 && record->NumberParameters>=4) {
        char parameters[224];
        snprintf(parameters,sizeof parameters,"C++ throw parameters magic=0x%llx object=0x%llx info=0x%llx imageBase=0x%llx",
            (unsigned long long)record->ExceptionInformation[0],(unsigned long long)record->ExceptionInformation[1],
            (unsigned long long)record->ExceptionInformation[2],(unsigned long long)record->ExceptionInformation[3]);
        state->event(parameters);
        auto module=find((HMODULE)record->ExceptionInformation[3]);
        if(module) {
            auto& image=module->image;auto pointer=record->ExceptionInformation[2];
            if(pointer>=(uintptr_t)image.base && pointer-(uintptr_t)image.base<=MAXDWORD) {
                DWORD info=DWORD(pointer-(uintptr_t)image.base);
                if(image.valid(info,16)) {
                    DWORD array=*(DWORD*)(image.base+info+12);
                    if(image.valid(array,8) && *(DWORD*)(image.base+array)>0) {
                        DWORD catchable=*(DWORD*)(image.base+array+4);
                        if(image.valid(catchable,8)) {
                            DWORD type=*(DWORD*)(image.base+catchable+4);
                            if(image.valid(type,17)) {
                                std::string name;
                                for(unsigned n=16;n<272 && image.valid(type,n+1);++n) {
                                    char ch=*(char*)(image.base+type+n);if(!ch)break;
                                    name+=ch>=32&&ch<127?ch:'?';
                                }
                                state->event("C++ thrown type="+name+" metadata="+module->key);
                            }
                        }
                    }
                }
            }
        }
    }
    CONTEXT context=*ep->ContextRecord;
    for(unsigned frame=0;frame<20 && context.Rip;++frame) {
        state->event("exception stack["+std::to_string(frame)+"] "+describe(context.Rip));
        DWORD64 previousRip=context.Rip,previousRsp=context.Rsp;
        if(!diagnosticUnwind(&context)||(context.Rip==previousRip&&context.Rsp==previousRsp))break;
    }
}
static LONG filter(EXCEPTION_POINTERS* ep) {
    auto e=ep->ExceptionRecord;
    if(e->ExceptionCode==exitException) {state->success=true;return EXCEPTION_EXECUTE_HANDLER;}
    if(e->ExceptionCode==missingException) return EXCEPTION_EXECUTE_HANDLER;
    exceptionDiagnostics(ep);
    char b[256];snprintf(b,sizeof b,"unhandled exception code=0x%08lx address=0x%llx ",e->ExceptionCode,(unsigned long long)(uintptr_t)e->ExceptionAddress);
    std::string text=b; text+=describe((uintptr_t)e->ExceptionAddress);
    if(e->ExceptionCode==EXCEPTION_ACCESS_VIOLATION && e->NumberParameters>=2) {snprintf(b,sizeof b," access=%llu target=0x%llx",(unsigned long long)e->ExceptionInformation[0],(unsigned long long)e->ExceptionInformation[1]);text+=b;}
    state->event(text);return EXCEPTION_EXECUTE_HANDLER;
}
// The packaged process runs with strict handle checking: closing an invalid
// handle raises STATUS_INVALID_HANDLE from ntdll instead of returning it (as
// on a desktop process without that policy). gbe_fork's socket teardown hits
// this on quit. Continue such raises so the call returns the error status.
static uintptr_t ntdllBase=0,ntdllEnd=0;
static void find_ntdll() {
    using Handle=HMODULE(WINAPI*)(LPCWSTR);
    auto handle=(Handle)kernel_tail::native("GetModuleHandleW");
    auto base=handle?(uintptr_t)handle(L"ntdll.dll"):0;
    if(!base)return;
    auto dos=(IMAGE_DOS_HEADER*)base;auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    ntdllBase=base;ntdllEnd=base+nt->OptionalHeader.SizeOfImage;
}
static LONG CALLBACK vectored(EXCEPTION_POINTERS* ep) {
    auto e=ep->ExceptionRecord;auto at=(uintptr_t)e->ExceptionAddress;
    // Guest RWX memory (JIT code) is RW or RX per page; flip on the faulting
    // access and resume. Must run before any guest handler sees the fault.
    if(e->ExceptionCode==EXCEPTION_ACCESS_VIOLATION && rwx_emulation::fault(ep)==EXCEPTION_CONTINUE_EXECUTION)
        return EXCEPTION_CONTINUE_EXECUTION;
    if(e->ExceptionCode==STATUS_INVALID_HANDLE && !(e->ExceptionFlags&EXCEPTION_NONCONTINUABLE) &&
       at>=ntdllBase && at<ntdllEnd) {
        state->invalidHandles.fetch_add(1,std::memory_order_relaxed);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if(guestThread && ep->ExceptionRecord->ExceptionCode!=missingException && ep->ExceptionRecord->ExceptionCode!=exitException) {
        // No logger, CRT formatting, allocation or mutex here: OutputDebugString
        // and file writes can themselves raise exceptions while a log lock is
        // held. Reentering event() from VEH exhausted the Xbox worker stack.
        // The SEH filter reports unhandled exceptions after stack unwinding.
        state->firstChanceCode.store(ep->ExceptionRecord->ExceptionCode,std::memory_order_relaxed);
        state->firstChanceAddress.store((uintptr_t)ep->ExceptionRecord->ExceptionAddress,std::memory_order_relaxed);
        state->firstChanceCount.fetch_add(1,std::memory_order_relaxed);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static wxl::Carrier carrier() {
    // Native carrier DLLs stay loaded, but a retired guest no longer owns its
    // slot. Bound simultaneous TLS images, rather than all loads ever made.
    for(size_t slot=0;slot<state->carriers.size();++slot) {
        auto& c=state->carriers[slot];
        if(c.index) {
            bool used=false;
            for(auto& module:state->modules)
                if(module->image.carrier.index==c.index) {used=true;break;}
            if(used)continue;
        } else {
            wchar_t name[64];swprintf(name,64,L"play-tls-%02u.dll",unsigned(slot));
            HMODULE h=LoadPackagedLibrary(name,0);
            if(h){c.index=(DWORD(*)())GetProcAddress(h,"carrier_index");c.block=(BYTE*(*)())GetProcAddress(h,"carrier_block");}
            if(!c.index || !c.block)return {};
        }
        return c;
    }
    return {};
}
// Slot 0: the launcher EXE's own static TLS block, whose first bytes
// tls_slot0.cpp reserves. Only the guest EXE may use it, because MSVC /GA code
// reads its TLS through TEB slot 0 rather than _tls_index. Guest DLLs keep
// carriers. Windows gives each thread its own copy of the block and
// Image::initThreadStorage fills it from the guest template, as for carriers.
static BYTE* slot0Block() {
    PlaySlot0 info{};
    return play_slot0_info(&info)?info.block:nullptr;
}
static DWORD slot0Index() {
    PlaySlot0 info{};
    return play_slot0_info(&info)?DWORD(info.index):TLS_OUT_OF_INDEXES;
}
// Whether the guest EXE can take slot 0; otherwise says why not (empty = yes).
static std::string slot0Refusal(Module& m,const IMAGE_NT_HEADERS64* nt) {
    if(&m!=state->exe || m.image.dll) return "not the guest EXE";
    PlaySlot0 info{};
    if(!play_slot0_info(&info)) return "launcher has no usable reserve at the start of its TLS block";
    if(info.index!=0) return "launcher TLS index is "+std::to_string(info.index)+", not 0";
    for(auto& other:state->modules)
        if(other.get()!=&m && other->image.carrier.block==&slot0Block) return "slot 0 already owned";
    auto td=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if(td.Size<sizeof(IMAGE_TLS_DIRECTORY64) || !m.image.valid(td.VirtualAddress,td.Size)) return "bad TLS directory";
    auto t=(const IMAGE_TLS_DIRECTORY64*)(m.image.base+td.VirtualAddress);
    if(t->EndAddressOfRawData<t->StartAddressOfRawData) return "bad TLS template";
    ULONGLONG size=t->EndAddressOfRawData-t->StartAddressOfRawData;
    if(size>info.capacity || t->SizeOfZeroFill>info.capacity-size)
        return "TLS template ("+std::to_string(size)+"+"+std::to_string(t->SizeOfZeroFill)+" bytes) exceeds the "+std::to_string(info.capacity)+"-byte reserve";
    return {};
}
// Optional Steam API provider for an owned game (AGENTS rule 3), e.g. a pinned
// gbe_fork steam_api64.dll staged with its steam_settings in a writable folder.
// When set, any steam_api64.dll the game loads is mapped from this file instead;
// the game's own file on disk is never modified. Set before start().
static std::wstring steamProvider;
static void set_steam_provider(const std::wstring& path) { steamProvider=path; }
// Discord's SDKs reach a Discord client over the local IPC pipe
// \?\pipe\discord-ipc-N, which only a running desktop client serves. A console
// has no such client, so the SDK's connect fails with ERROR_FILE_NOT_FOUND and
// the SDK carries on without presence, exactly as on a desktop where Discord is
// not running (discord-rpc's BaseConnection::Open walks discord-ipc-0..9 and
// returns false; the Game SDK's DiscordCreate returns DiscordResult_InternalError
// with no device). Play needs no substitute: the pipe route answers in the
// AppContainer namespace and the shell route refuses the discord:// launch the
// Game SDK would otherwise attempt, so nothing blocks. Report it once per game
// so a missing presence is explained rather than mysterious.
// Whether a mapped image exports this name. Reads the export table only; it
// never binds or executes anything, so it is safe before initialization.
static bool exported_any(const wxl::Image& image,const char* name) {
    if(!image.base || !name) return false;
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!dir.Size || !image.valid(dir.VirtualAddress,dir.Size) || dir.Size<sizeof(IMAGE_EXPORT_DIRECTORY)) return false;
    auto e=(IMAGE_EXPORT_DIRECTORY*)(image.base+dir.VirtualAddress);
    if(!image.valid(e->AddressOfNames,size_t(e->NumberOfNames)*4)) return false;
    auto names=(DWORD*)(image.base+e->AddressOfNames);
    for(DWORD i=0;i<e->NumberOfNames;++i) { auto n=image.str(names[i]); if(n && !strcmp(n,name)) return true; }
    return false;
}
static bool discordSdk(const Module& m) {
    static const char* const names[]={"discord_game_sdk.dll","discord-rpc.dll","discord_rpc.dll","discordrpc.dll","discord_game_sdk_x64.dll"};
    for(auto name:names) if(m.key==name) return true;
    // A Unity plugin or a renamed copy: identify it by the API it exports.
    return m.image.base && (exported_any(m.image,"DiscordCreate") || exported_any(m.image,"Discord_Initialize"));
}
static void note_discord(const Module& m) {
    if(!state || !discordSdk(m)) return;
    static std::atomic<bool> reported{false};
    if(reported.exchange(true)) return;
    state->event("Discord SDK present ("+m.key+"); no Discord client on this console, so the game runs without Rich Presence");
}
// Vulkan (research/VULKAN-PLAN.md): the Khronos loader finds drivers through a JSON manifest. On Windows its search
// order is VK_DRIVER_FILES (an environment override), the Microsoft.D3DMappingLayers app package, then
// HKLM\SOFTWARE\Khronos\Vulkan\Drivers (RegEnumValueA). The console has no mapping-layers package, and the loader only
// honours the environment override when the process is not elevated: loader_secure_getenv() calls is_high_integrity()
// (OpenProcessToken/GetTokenInformation TokenIntegrityLevel) and ignores the variable at SECURITY_MANDATORY_HIGH_RID or
// above. Play's process is at high integrity, so VK_DRIVER_FILES is silently dropped and the registry is the only route
// that works (verified off-device: the loader logs "Loader is running with elevated permissions. Environment variable
// VK_DRIVER_FILES will be ignored" and then "Found no registry files in HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\Drivers").
// Play therefore seeds that key in the guest registry (regb, LocalState\registry.sqlite), the same registry the loader's
// RegOpenKeyExA/RegEnumValueA imports already reach. The value name is the manifest path and the data is 0, the layout
// the loader requires; the manifest's library_path is a bare name, so the loader's own LoadLibraryW reaches Play's guest
// search and finds vulkan-null-icd.dll in the runtime directory (redistributable()). Seeded before the loader maps, so
// its first vkCreateInstance sees it; removed when the launch ends (reset_launch_config) so the next game starts clean.
static const wchar_t* vulkanDriversKey=L"SOFTWARE\\Khronos\\Vulkan\\Drivers";
// Drivers listed in the guest registry, in the order the loader should try them. dzn first (a real physical device over
// D3D12), the null ICD second: the loader's terminator_CreateInstance skips an ICD whose CreateInstance fails and only
// returns VK_ERROR_INCOMPATIBLE_DRIVER when none succeed, so a dzn that cannot initialise (no D3D12 device, no DXIL
// acceptance) leaves the guest with the same clean "no devices" answer milestone 1 gives, never a hard failure.
// A manifest that is not packaged is skipped, so a launcher built without dzn behaves exactly as before.
static const wchar_t* const vulkanDriverManifests[]={L"vulkan-dzn.json",L"vulkan-null-icd.json"};
static void publishVulkanDriverFiles(const std::wstring& path) {
    if(basename(path)!="vulkan-1.dll") return;
    // A game that ships its own loader keeps its own driver discovery: only the
    // packaged runtime copy is ours to configure.
    if(runtimeDirectory.empty() || _wcsicmp(path.c_str(),(runtimeDirectory+L"\\vulkan-1.dll").c_str())) return;
    std::vector<std::wstring> listed;
    for(auto name:vulkanDriverManifests) {
        auto manifest=runtimeDirectory+L"\\"+name;
        if(exists(manifest)) listed.push_back(manifest);
    }
    if(listed.empty()) return;
    HKEY key=nullptr;
    if(regb::RegCreateKeyExW(HKEY_LOCAL_MACHINE,vulkanDriversKey,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,nullptr)!=ERROR_SUCCESS) return;
    DWORD zero=0;
    LONG set=ERROR_SUCCESS;
    for(auto& manifest:listed) {
        set=regb::RegSetValueExW(key,manifest.c_str(),0,REG_DWORD,(const BYTE*)&zero,sizeof zero);
        if(set!=ERROR_SUCCESS) break;
    }
    regb::RegCloseKey(key);
    if(set!=ERROR_SUCCESS) return;
    const bool dzn=listed.size()>1;
    state->event(dzn?
        "Vulkan loader mapped from the runtime directory; the guest registry lists the packaged dzn driver (Vulkan on D3D12) with the null ICD as fallback":
        "Vulkan loader mapped from the runtime directory; the guest registry lists the packaged null ICD (zero physical devices)");
}
// The loader caches driver discovery per process, so the key only has to be right before the first vkCreateInstance.
// It must not outlive the launch: the next game would inherit a driver entry it never asked for.
static void clearVulkanDriverFiles() {
    HKEY key=nullptr;
    if(regb::RegOpenKeyExW(HKEY_LOCAL_MACHINE,vulkanDriversKey,0,KEY_ALL_ACCESS,&key)!=ERROR_SUCCESS) return;
    if(!runtimeDirectory.empty()) for(auto name:vulkanDriverManifests) regb::RegDeleteValueW(key,(runtimeDirectory+L"\\"+name).c_str());
    regb::RegCloseKey(key);
}
static Module* map(const std::wstring& requested,const std::string& asKey,wxl::Resolver resolver) {
    if(auto m=named(asKey.empty()?requested:wide(asKey.c_str()))) return m->mappingFailed?nullptr:m;
    // A delay import may map its DLL here; that DLL's own static imports are
    // not delay imports, so their misses must fail and be reported as such.
    struct DelayScope {bool outer=wxl::resolvingDelay;DelayScope(){wxl::resolvingDelay=false;}~DelayScope(){wxl::resolvingDelay=outer;}} delayScope;
    const bool provided=basename(requested)=="steam_api64.dll" && !steamProvider.empty();
    const std::wstring& path=provided?steamProvider:requested;
    publishVulkanDriverFiles(path);
    if(state->modules.size()>=128) {state->event("loader module capacity exceeded");return nullptr;}
    auto own=std::make_unique<Module>();auto m=own.get();m->path=path;m->key=asKey.empty()?basename(path):asKey;state->modules.push_back(std::move(own));state->moduleCount=int(state->modules.size());
    // Publish before fixing imports; circular imports can use the copied export
    // table, but no initialization runs until the complete graph is mapped.
    mlp::assets[m->key]=&m->image;
    state->event("mapping "+m->key);
    if(provided) state->event("steam_api64.dll served by the configured Steam API provider; the game's file is unchanged");
    else if(m->key=="steam_api64.dll") state->event(steamClient.empty()?
        "steam_api64.dll is the game's original file, mapped unmodified; no Steam client, so SteamAPI_Init is expected to fail (item 36)":
        "steam_api64.dll is the game's original file, mapped unmodified; it reaches the staged Steam client");
    struct ImportScope {ImportScope(){++mappingImports;} ~ImportScope(){--mappingImports;}} imports;
    if(!wxl::run(path.c_str(),state->log,&m->image,resolver?resolver:resolve,wxl::LoadConfig::msvc_x64)) {
        // Withdraw the early publication: a failed image's export table must
        // never satisfy another import (it is never protected or initialized).
        mlp::assets.erase(m->key);
        m->mappingFailed=true;state->event("mapping failed: "+m->key+"; guest initialization blocked; "+state->snapshot());return nullptr;
    }
    m->mapped=true;++state->mappedCount;state->order.push_back(m);note_discord(*m);return m;
}
// Steam's DRM wrapper (SteamStub) finds the module that provides
// steamclient64.dll!CreateInterface, reads that module's file and verifies
// Valve's signature over it. As Proton does, the module the guest sees is then
// Valve's genuine signed steamclient64.dll, mapped from its own unmodified
// file. None of its code runs: its imports bind to a refusal, it gets no TLS
// or DllMain, and each export entry is overwritten in memory with a jump to
// the emulated client's export of the same name. Every Steam call is answered
// by the emulated client, as without the wrapper. Nothing is patched on disk,
// and the game's own wrapper code is untouched.
// The Steam DRM wrapper then asks the client for the account's app ownership
// ticket and verifies Valve's signature over it. The emulated client cannot
// sign one, so ISteamClient (versions 012-021, where slot 12 is
// GetISteamGenericInterface) is handed out through a forwarding wrapper whose
// STEAMAPPTICKET_INTERFACE_VERSION001 serves the genuine ticket the launcher
// obtained from Steam for the signed-in owner. Without one it answers no
// ticket, and the wrapper fails as on Windows without ownership.
static std::string appTicket;
static void set_app_ticket(const std::string& ticket) { appTicket=ticket; }
static uint32_t ticketData(void*,uint32_t appid,void* buffer,uint32_t capacity,uint32_t* appidOffset,uint32_t* steamidOffset,uint32_t* signatureOffset,uint32_t* signatureSize) {
    uint32_t size=steam_app_ticket::copy(appTicket,appid,buffer,capacity,appidOffset,steamidOffset,signatureOffset,signatureSize);
    state->event(size?"app ownership ticket for app "+std::to_string(appid)+" served: the genuine one Steam issued to the signed-in owner"
                     :"no app ownership ticket for app "+std::to_string(appid)+" (not owned, offline, or not requested)");
    return size;
}
static void* ticketVtable[]={(void*)&ticketData};
static struct {void** vtable;} ticketInterface{ticketVtable};
struct ClientWrapper {void** vtable;void* inner;};
using ClientMethod=uintptr_t(*)(void*,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
// Integer/pointer arguments pass through unchanged (no ISteamClient method
// takes floating-point arguments); extra slots read the caller's own frame.
template<size_t I> static uintptr_t clientForward(ClientWrapper* w,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g) {
    return ((ClientMethod)(*(void***)w->inner)[I])(w->inner,a,b,c,d,e,f,g);
}
static void* clientGenericInterface(ClientWrapper* w,uintptr_t user,uintptr_t pipe,const char* version) {
    if(version&&!strcmp(version,"STEAMAPPTICKET_INTERFACE_VERSION001")) return &ticketInterface;
    return (void*)clientForward<12>(w,user,pipe,(uintptr_t)version,0,0,0,0);
}
template<size_t... I> static std::array<void*,sizeof...(I)> clientTable(std::index_sequence<I...>) {
    return {{(I==12?(void*)&clientGenericInterface:(void*)&clientForward<I>)...}};
}
static auto clientVtable=clientTable(std::make_index_sequence<40>());
using ClientCreate=void*(*)(const char*,int*);
static ClientCreate emulatedCreate;
static void* clientCreateInterface(const char* version,int* result) {
    void* inner=emulatedCreate(version,result);
    if(!inner||!version||strncmp(version,"SteamClient",11)) return inner;
    int n=atoi(version+11);
    if(n<12||n>21) return inner;
    std::lock_guard<LoaderMutex> lock(state->loaderMutex);
    static std::vector<std::unique_ptr<ClientWrapper>> wrappers;
    for(auto& w:wrappers) if(w->inner==inner) return w.get();
    wrappers.push_back(std::make_unique<ClientWrapper>(ClientWrapper{clientVtable.data(),inner}));
    return wrappers.back().get();
}
static void signedClientCode() {
    state->event("Valve steamclient64.dll code reached outside its redirected exports; refused");
    RaiseException(missingException,EXCEPTION_NONCONTINUABLE,0,nullptr);
}
static uintptr_t signedClientImport(const char*,const char*,WORD) { return (uintptr_t)&signedClientCode; }
static bool redirectExports(Module& client,Module& emulated) {
    auto& image=client.image;
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!image.valid(dir.VirtualAddress,dir.Size)||dir.Size<sizeof(IMAGE_EXPORT_DIRECTORY))return false;
    auto e=(IMAGE_EXPORT_DIRECTORY*)(image.base+dir.VirtualAddress);
    if(!image.valid(e->AddressOfNames,size_t(e->NumberOfNames)*4)||!image.valid(e->AddressOfNameOrdinals,size_t(e->NumberOfNames)*2)||
       !image.valid(e->AddressOfFunctions,size_t(e->NumberOfFunctions)*4))return false;
    auto functions=(DWORD*)(image.base+e->AddressOfFunctions);
    // Every entry must be code with room for the 14-byte jump before the next
    // entry; otherwise this client build is refused rather than half-redirected.
    std::vector<DWORD> starts(functions,functions+e->NumberOfFunctions);std::sort(starts.begin(),starts.end());
    auto section=IMAGE_FIRST_SECTION(nt);
    auto code=[&](DWORD rva){for(WORD i=0;i<nt->FileHeader.NumberOfSections;++i){auto& sh=section[i];
        if(rva>=sh.VirtualAddress&&rva-sh.VirtualAddress<sh.Misc.VirtualSize)return (sh.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0;}return false;};
    unsigned redirected=0,missing=0;
    for(DWORD i=0;i<e->NumberOfNames;++i) {
        auto name=image.str(((DWORD*)(image.base+e->AddressOfNames))[i]);
        WORD index=((WORD*)(image.base+e->AddressOfNameOrdinals))[i];
        if(!name||index>=e->NumberOfFunctions)return false;
        DWORD rva=functions[index];
        auto next=std::upper_bound(starts.begin(),starts.end(),rva);
        if(!code(rva)||(next!=starts.end()&&*next-rva<14)||!image.valid(rva,14)) {state->event(std::string("Valve client export not redirectable: ")+name);return false;}
        uintptr_t target=exported(emulated,name,0);
        if(target&&!strcmp(name,"CreateInterface")) {emulatedCreate=(ClientCreate)target;target=(uintptr_t)&clientCreateInterface;}
        if(!target) {target=(uintptr_t)&signedClientCode;++missing;state->event(std::string("emulated client lacks ")+name+"; Valve's entry refuses");}
        BYTE jump[14]={0xff,0x25,0,0,0,0};memcpy(jump+6,&target,8);   // jmp [rip+0]; dq target
        DWORD old=0;auto at=image.base+rva;
        if(!VirtualProtect(at,sizeof jump,PAGE_EXECUTE_READWRITE,&old))return false;
        memcpy(at,jump,sizeof jump);VirtualProtect(at,sizeof jump,old,&old);++redirected;
    }
    FlushInstructionCache(GetCurrentProcess(),image.base,image.size);
    state->event("Valve's signed steamclient64.dll mapped unmodified from its file; "+std::to_string(redirected)+
                 " exports answered by the emulated client ("+std::to_string(missing)+" refused)");
    return true;
}
static Module* signedClientModule() {
    if(auto m=named(L"steamclient64.dll")) return m->mappingFailed?nullptr:m;
    auto emulated=map(steamClient,"steamclient64-emulated.dll");
    if(!emulated) return nullptr;
    state->event("steamclient64.dll: emulated Steam client for an owned game, behind Valve's signed client");
    auto client=map(steamClientSigned,"steamclient64.dll",signedClientImport);
    if(!client) return nullptr;
    if(!redirectExports(*client,*emulated)) {client->mappingFailed=true;mlp::assets.erase(client->key);state->event("Valve client redirection failed");return nullptr;}
    // Valve's code never runs: no TLS index, unwind data or DllMain.
    client->prepared=true;client->initialized=true;client->pinned=true;
    client->dependencies.push_back(emulated);emulated->pinned=true;
    return client;
}
static bool prepare(Module& m) {
    if(m.prepared) return true;
    auto& image=m.image;
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto td=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if(td.Size) {
        wxl::Carrier c;
        std::string refusal=slot0Refusal(m,nt);
        if(refusal.empty()) {
            // The guest EXE's code may assume it owns TLS slot 0 (MSVC /GA).
            c.index=&slot0Index;c.block=&slot0Block;c.capacity=65536;
            state->event("TLS slot 0 for "+m.key);
        } else {
            if(&m==state->exe) state->event("TLS slot 0 unavailable for "+m.key+": "+refusal+"; using a carrier");
            state->event("loading TLS carrier for "+m.key);
            c=carrier();
            if(!c.index || !c.block) {state->event("TLS carrier unavailable: "+m.key);return false;}
        }
        auto tls=(IMAGE_TLS_DIRECTORY64*)(image.base+td.VirtualAddress);
        // TLS index lives in writable data in supported images. prepareTls
        // validates section ownership before writing it.
        if(!wxl::prepareTls(image,state->log,c)) {state->event("TLS preparation failed: "+m.key);return false;}
        (void)tls;
    }
    state->event("registering unwind for "+m.key);
    if(!wxl::prepareUnwind(image,state->log)) {state->event("unwind registration failed: "+m.key);return false;}
    // No exception directory means no unwind data: a fault inside this image
    // cannot be unwound to the guest thread's __try/__except, so it reaches no
    // handler at all and the launcher dies with no crash record. That is how a
    // fault in a mapped shim or donor stayed invisible; name the image at load
    // time so the next one is not silent. Play's own shims and donors carry
    // unwind data (their build scripts gate on it), so this is a diagnostic for
    // a guest or third-party image, not an expected line.
    {
        auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
        auto xd=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if(!xd.Size)
            state->event("unwind none for "+m.key+": no exception directory, a fault inside it cannot be dispatched");
    }
    m.prepared=true;return true;
}
static bool initialize(Module& m) {
    // Failed images stay mapped for diagnostics, but must never become callable
    // on a later LoadLibrary. Successful images use the lifetime collector.
    if(m.initializationFailed) return false;
    if(m.initialized || m.initializing) return true;
    if(!m.mapped || !m.prepared) return false;m.initializing=true;
    auto failed=[&m]() {m.initializing=false;m.initializationFailed=true;return false;};
    for(auto dep:m.dependencies) if(!initialize(*dep)) return failed();
    auto& image=m.image;
    // Set only immediately before an actual callback/entry call.
    if(!image.tlsCallbacks.empty()) {state->event("TLS PROCESS_ATTACH "+m.key);state->executed=true;}
    if(!image.notifyThread(DLL_PROCESS_ATTACH)) {state->event("TLS attach failed: "+m.key);return failed();}
    image.tlsAttached=true;
    if(image.dll && image.entry) {
        image.dllEntry=(wxl::Image::DllEntry)(image.base+image.entry);
        state->event("DllMain PROCESS_ATTACH "+m.key);state->executed=true;
        if(!image.dllEntry((HINSTANCE)image.base,DLL_PROCESS_ATTACH,nullptr)){state->event("DllMain rejected PROCESS_ATTACH: "+m.key);return failed();}
        image.attached=true;
    }
    m.initialized=true;m.initializing=false;return true;
}
static bool initializePending() {
    // All entry paths, including initial runImpl(), publish initialization and
    // failure while owning proc/loadExW's recursive loader lock. Other threads
    // cannot observe initializing=true as permission to use pending exports.
    std::lock_guard<LoaderMutex> lock(state->loaderMutex);
    // Register every unwind table and TLS index before any guest callback.
    for(auto m:state->order) if(!prepare(*m)) return false;
    // Every template must exist before the first callback can call a sibling.
    for(auto m:state->order) if(m->image.tlsKey!=TLS_OUT_OF_INDEXES && !TlsGetValue(m->image.tlsKey) && !m->image.initThreadStorage()) {state->event("TLS storage failed: "+m->key);return false;}
    // Dependencies added by LoadLibrary can extend order while DllMain runs.
    for(size_t i=0;i<state->order.size();++i) if(!initialize(*state->order[i])) return false;return true;
}
#include "../../uwph/kernel32/guest_thread_exit.h"
struct ThreadStart { LPTHREAD_START_ROUTINE function;void* arg; };
static DWORD WINAPI child(void* data) {
    auto p=(ThreadStart*)data;auto function=p->function;auto arg=p->arg;delete p;guestThread=true;
    DWORD code=0;
    __try {
        if(!attachGuestThread()) RaiseException(missingException,EXCEPTION_NONCONTINUABLE,0,nullptr);
        code=function(arg);
        detachGuestThread();
    } __except(filter(GetExceptionInformation())) {code=state->code;}
    guestThread=false;return code;
}
static HANDLE WINAPI createThread(LPSECURITY_ATTRIBUTES a,SIZE_T stack,LPTHREAD_START_ROUTINE entry,LPVOID arg,DWORD flags,LPDWORD id) {
    auto p=new ThreadStart{entry,arg};HANDLE h=CreateThread(a,std::max(stack,SIZE_T(16*1024*1024)),child,p,flags,id);if(!h)delete p;return h;
}
static PVOID WINAPI pcToFileHeader(PVOID pc,PVOID* base) {
    // Like the native loader lookup, consult the owning image registry under
    // its loader lock. RtlAddFunctionTable alone does not register an image
    // with the OS loader; the static Microsoft CRT needs this base for EH RVAs.
    {
        std::lock_guard<LoaderMutex> lock(state->loaderMutex);
        auto address=(uintptr_t)pc;
        for(auto& module:state->modules) {
            auto start=(uintptr_t)module->image.base;
            if(start && address>=start && address-start<module->image.size) {
                *base=(PVOID)start;return *base;
            }
        }
    }
    using Fn=PVOID(WINAPI*)(PVOID,PVOID*);
    auto native=(Fn)kernel_tail::native("RtlPcToFileHeader");
    if(native)return native(pc,base);
    *base=nullptr;return nullptr;
}
// The game's wide debug output goes to launch_probe.log, like
// OutputDebugStringA (wxl builtin). Nothing else captures it on the console,
// and engines report fatal errors this way just before they trap.
static void WINAPI debugStringW(LPCWSTR text) {
    if(!text||!state||!state->log)return;
    const size_t n=wcsnlen(text,4096);
    const int bytes=n?WideCharToMultiByte(CP_UTF8,0,text,int(n),nullptr,0,nullptr,nullptr):0;
    std::string line(size_t(bytes>0?bytes:0),'\0');
    if(bytes>0)WideCharToMultiByte(CP_UTF8,0,text,int(n),&line[0],bytes,nullptr,nullptr);
    state->log(redact::steam_ids(line).c_str());
}
static void WINAPI debugStringA(LPCSTR text) {
    if(!text||!state||!state->log)return;
    state->log(redact::steam_ids(std::string(text,strnlen(text,4096))).c_str());
}
// In-process COM servers Play supplies as gated Wine donors. Windows finds
// them through their registry entries; the console has none, so a guest's
// CoCreateInstance/CoGetClassObject for these CLSIDs reaches the donor's
// DllGetClassObject (only once the donor passed its gate). Every other CLSID
// goes to the console's COM unchanged.
struct DonorServer {const char* dll;GUID clsid;const char* label;};
static const DonorServer donorServers[]={
    {"dinput8.dll",{0x25e609e4,0xb259,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}},"CLSID_DirectInput8"},
    {"dinput8.dll",{0x25e609e5,0xb259,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}},"CLSID_DirectInputDevice8"},
};
static const DonorServer* donorServer(REFCLSID clsid,DWORD context) {
    if(!(context&CLSCTX_INPROC_SERVER)) return nullptr;
    for(auto& server:donorServers) if(IsEqualGUID(clsid,server.clsid)) return &server;
    return nullptr;
}
static HRESULT donorClassObject(const DonorServer& server,REFIID iid,void** out) {
    using Get=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
    auto get=mlp::donor_resolve?(Get)mlp::donor_resolve(server.dll,"DllGetClassObject",0):nullptr;
    return get?get(server.clsid,iid,out):REGDB_E_CLASSNOTREG;
}
template<class F> static F nativeCom(const char* name) {
    static HMODULE ole=LoadLibraryExW(L"ole32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    return ole?(F)GetProcAddress(ole,name):nullptr;
}
static HRESULT WINAPI coGetClassObject(REFCLSID clsid,DWORD context,COSERVERINFO* info,REFIID iid,void** out) {
    if(auto server=donorServer(clsid,context)) {
        if(!out) return E_POINTER;
        *out=nullptr;HRESULT hr=donorClassObject(*server,iid,out);
        if(state) state->event(std::string("guest CoGetClassObject ")+server->label+" served by the "+server->dll+" donor hr="+std::to_string(hr));
        return hr;
    }
    using Fn=HRESULT(WINAPI*)(REFCLSID,DWORD,COSERVERINFO*,REFIID,void**);
    auto fn=nativeCom<Fn>("CoGetClassObject");return fn?fn(clsid,context,info,iid,out):E_NOTIMPL;
}
static HRESULT WINAPI coCreateInstance(REFCLSID clsid,IUnknown* outer,DWORD context,REFIID iid,void** out) {
    if(auto server=donorServer(clsid,context)) {
        if(!out) return E_POINTER;
        *out=nullptr;IClassFactory* factory=nullptr;
        HRESULT hr=donorClassObject(*server,IID_IClassFactory,(void**)&factory);
        if(SUCCEEDED(hr)) {hr=factory->CreateInstance(outer,iid,out);factory->Release();}
        char line[160];snprintf(line,sizeof line,"guest CoCreateInstance %s served by the %s donor hr=0x%08lx",server->label,server->dll,(unsigned long)hr);
        if(state) state->event(line);
        return hr;
    }
    using Fn=HRESULT(WINAPI*)(REFCLSID,IUnknown*,DWORD,REFIID,void**);
    auto fn=nativeCom<Fn>("CoCreateInstance");return fn?fn(clsid,outer,context,iid,out):E_NOTIMPL;
}
// Unhandled exceptions on threads Play did not create (CRT _beginthreadex,
// SDL threads) reach the process-wide filter, not Play's per-thread guard,
// and used to end the whole launcher with no trace. Play keeps the process
// filter. A guest's own handler (SetUnhandledExceptionFilter, e.g. Crashpad)
// still runs first. Then, for an exception in guest code, the crash is
// recorded and only that thread ends; the launcher reports it and restarts to
// unload the game. Exceptions in launcher code keep their previous handling.
static LPTOP_LEVEL_EXCEPTION_FILTER guestTopLevel=nullptr,hostTopLevel=nullptr;
static LONG WINAPI topLevelFilter(EXCEPTION_POINTERS* ep) {
    State* s=state;
    bool guest=s&&guestThread;
    if(s&&!guest) {
        std::unique_lock<LoaderMutex> lock(s->loaderMutex,std::try_to_lock);
        const auto address=(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        if(lock.owns_lock()) for(auto& m:s->modules) {
            const auto base=(uintptr_t)m->image.base;
            if(base&&address>=base&&address-base<m->image.size){guest=true;break;}
        }
    }
    if(!guest) return hostTopLevel?hostTopLevel(ep):EXCEPTION_CONTINUE_SEARCH;
    if(auto handler=guestTopLevel) {LONG r=handler(ep);if(r==EXCEPTION_CONTINUE_EXECUTION)return r;}
    filter(ep);   // records the exception and its stack, as for Play's threads
    s->result=s->snapshot();
    if(GetCurrentThreadId()==s->thread) guestThread=false;
    s->finished.store(true,std::memory_order_release);
    s->loaderMutex.abandon();
    ExitThread(ep->ExceptionRecord->ExceptionCode);
}
static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI setGuestTopLevel(LPTOP_LEVEL_EXCEPTION_FILTER handler) {
    auto previous=guestTopLevel;guestTopLevel=handler;return previous;
}
// Files a guest opens for writing are logged (path, result, handle type), so
// a save that stalls shows where it writes. SteamIDs in paths are redacted:
// the log may be shared.
static std::string redactedPath(const std::string& text) { return redact::steam_ids(text); }
// Handle -> redacted path for the most recent write-opens, so a stalled write
// (which can only name its handle) shows which file it is on.
struct WriteOpen {HANDLE handle;std::string path;};
static std::mutex writeOpensM;
static std::array<WriteOpen,64> writeOpens;
static size_t writeOpenNext=0;
static void rememberWriteOpen(HANDLE h,const std::string& path) {
    std::lock_guard<std::mutex> lock(writeOpensM);
    for(auto& open:writeOpens) if(open.handle==h){open.path=path;return;}
    writeOpens[writeOpenNext]={h,path};writeOpenNext=(writeOpenNext+1)%writeOpens.size();
}
static std::string writeOpenPath(HANDLE h) {
    std::lock_guard<std::mutex> lock(writeOpensM);
    for(auto& open:writeOpens) if(open.handle==h)return open.path;
    return "?";
}
// What the file system sees for a disk file the guest just opened for writing:
// the resolved path, size and free space of its volume. Runs on the opening
// thread right after the open, so no other thread is using the handle.
static std::string diskDetail(HANDLE h,const std::wstring& requested) {
    std::string out;
    wchar_t final_path[1024];
    DWORD n=GetFinalPathNameByHandleW(h,final_path,1024,VOLUME_NAME_DOS);
    if(n&&n<1024)out+=" final="+redactedPath(narrow(final_path));
    LARGE_INTEGER size{};
    if(GetFileSizeEx(h,&size)){char t[48];snprintf(t,sizeof t," size=%lld",(long long)size.QuadPart);out+=t;}
    std::wstring dir=requested;
    size_t slash=dir.find_last_of(L"\\/");
    if(slash!=std::wstring::npos)dir.resize(slash+1);
    ULARGE_INTEGER avail{},total{},freeBytes{};
    if(GetDiskFreeSpaceExW(dir.empty()?nullptr:dir.c_str(),&avail,&total,&freeBytes)){
        char t[64];snprintf(t,sizeof t," free-mb=%llu",(unsigned long long)(avail.QuadPart>>20));out+=t;
    }
    return out;
}
static void logWriteOpen(const std::wstring& path,DWORD access,DWORD disposition,HANDLE h) {
    if(!state||!(access&(GENERIC_WRITE|FILE_WRITE_DATA|FILE_APPEND_DATA|GENERIC_ALL)))return;
    // The guest reads the open's last error next (ERROR_ALREADY_EXISTS after a
    // successful CREATE_ALWAYS/OPEN_ALWAYS): logging must not change it.
    const DWORD error=GetLastError();
    struct Keep {DWORD e;~Keep(){SetLastError(e);}} keep{error};
    std::string shown=redactedPath(narrow(path));
    if(h!=INVALID_HANDLE_VALUE)rememberWriteOpen(h,shown);
    static std::atomic<unsigned> logged{0};
    if(logged.fetch_add(1)>=256)return;
    DWORD type=h==INVALID_HANDLE_VALUE?0:GetFileType(h);
    char tail[96];snprintf(tail,sizeof tail," disposition=%lu -> %s type=%lu error=%lu",(unsigned long)disposition,
        h==INVALID_HANDLE_VALUE?"failed":"open",(unsigned long)type,(unsigned long)error);
    state->event("guest file write-open "+shown+tail+(type==FILE_TYPE_DISK?diskDetail(h,path):std::string()));
}
// A guest WriteFile that stalls (see kernel32_core::write_guard) is logged
// with its file; a cancelled one is reported so the failed save is explained.
static void guestWriteStall(const kernel32_core::WriteStall& e) {
    if(!state)return;
    static std::atomic<unsigned> logged{0};
    if(logged.fetch_add(1)>=32)return;
    char tail[160];
    snprintf(tail,sizeof tail," %lums size=%lu done=%lu type=%s%s",(unsigned long)e.elapsed_ms,(unsigned long)e.size,(unsigned long)e.done,
        e.disk?"disk":"other",e.cancelled?(e.cancel_error?" cancel failed":" cancelled (ERROR_OPERATION_ABORTED to the game)"):"");
    std::string line="guest WriteFile stalled on "+writeOpenPath(e.file)+tail;
    if(e.cancelled&&e.cancel_error){char t[32];snprintf(t,sizeof t," error=%lu",(unsigned long)e.cancel_error);line+=t;}
    state->event(line);
}
[[maybe_unused]] static const bool writeGuardHooked=(kernel32_core::write_guard().hook=guestWriteStall,true);
static HANDLE WINAPI guestCreateFileW(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile) {
    HANDLE h=kernel32_core::create_file_w(path,access,share,security,disposition,flags,templateFile);
    if(path)logWriteOpen(path,access,disposition,h);
    return h;
}
static HANDLE WINAPI guestCreateFileA(LPCSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile) {
    HANDLE h=kernel32_core::create_file_a(path,access,share,security,disposition,flags,templateFile);
    if(path){DWORD error=GetLastError();std::wstring wide;for(const char* c=path;*c;++c)wide+=wchar_t((unsigned char)*c);SetLastError(error);logWriteOpen(wide,access,disposition,h);}   // logWriteOpen keeps the last error
    return h;
}
// A guest's first-in-chain vectored handler (Unity's Mono installs one) would
// otherwise run before Play's and mistake the page-flip faults of emulated RWX
// memory for managed exceptions. After a guest registers a first handler, Play
// adds a fresh first handler and only then retires the old one: Play stays
// ahead and the chain is never empty.
static PVOID WINAPI guestAddVeh(ULONG first,LONG(CALLBACK* handler)(EXCEPTION_POINTERS*)) {
    using AddVeh=PVOID(WINAPI*)(ULONG,LONG(CALLBACK*)(EXCEPTION_POINTERS*));
    using RemoveVeh=ULONG(WINAPI*)(PVOID);
    static std::mutex order;
    std::lock_guard<std::mutex> lock(order);
    auto add=(AddVeh)kernel_tail::native("AddVectoredExceptionHandler");
    auto remove=(RemoveVeh)kernel_tail::native("RemoveVectoredExceptionHandler");
    if(!add){SetLastError(ERROR_PROC_NOT_FOUND);return nullptr;}
    PVOID guest=add(first,handler);
    if(guest&&first&&state&&state->veh&&remove) {
        if(PVOID fresh=add(1,vectored)){PVOID old=state->veh;state->veh=fresh;remove(old);}
    }
    return guest;
}
// The C runtime's exit family (exit, _exit, quick_exit, _Exit, abort) runs in
// the console's native ucrtbase/msvcrt, which ends the process through its
// own ExitProcess import and would take the launcher down with the game.
// For a guest, exit() still does the CRT's exit-time work through _cexit
// (atexit handlers, static destructors, stream flush) in that same CRT, then
// ends like Play's ExitProcess: the guest thread ends and the launcher reports
// the result and restarts to unload the game. The quick/underscore forms skip
// that work, as on Windows; abort() reports code 3 like the CRT does.
static HMODULE crtModule(bool msvcrt) {
    static HMODULE ucrt=LoadLibraryExW(L"ucrtbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static HMODULE legacy=LoadLibraryExW(L"msvcrt.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    return msvcrt?legacy:ucrt;
}
template<bool Msvcrt> static void __cdecl crtExit(int code) {
    using Cleanup=void(__cdecl*)();
    HMODULE crt=crtModule(Msvcrt);
    auto cleanup=crt?(Cleanup)GetProcAddress(crt,"_cexit"):nullptr;
    if(state) state->event("guest CRT exit("+std::to_string(code)+")");
    if(cleanup) cleanup();
    exitProcess(UINT(code));
}
static void __cdecl crtQuickExit(int code) {
    if(state) state->event("guest CRT quick exit("+std::to_string(code)+")");
    exitProcess(UINT(code));
}
static void __cdecl crtAbort() {
    if(state) state->event("guest CRT abort()");
    exitProcess(3);
}
static uintptr_t crtExitApi(const std::string& key,const char* name) {
    if(!name) return 0;
    const bool msvcrt=key=="msvcrt.dll";
    if(!msvcrt && key!="ucrtbase.dll" && key.rfind("api-ms-win-crt-runtime-",0)!=0) return 0;
    // A CRT the game ships is mapped by Play; its own ExitProcess import
    // already reaches Play, and its exit-time work belongs to that copy.
    if(key=="ucrtbase.dll"||msvcrt) {const auto own=wide(key.c_str());if(named(own)||exists(guestLibraryPath(own)))return 0;}
    if(!strcmp(name,"exit")) return msvcrt?(uintptr_t)&crtExit<true>:(uintptr_t)&crtExit<false>;
    if(!strcmp(name,"_exit")||!strcmp(name,"_Exit")||!strcmp(name,"quick_exit")) return (uintptr_t)&crtQuickExit;
    if(!strcmp(name,"abort")) return (uintptr_t)&crtAbort;
    return 0;
}
static uintptr_t processApi(const char* name) {
#define PLAY_API(n,f) if(!strcmp(name,n)) return (uintptr_t)&f
    PLAY_API("OutputDebugStringW",debugStringW);PLAY_API("OutputDebugStringA",debugStringA);
    PLAY_API("SetUnhandledExceptionFilter",setGuestTopLevel);
    PLAY_API("VirtualAlloc",rwx_emulation::alloc);PLAY_API("VirtualProtect",rwx_emulation::protect);PLAY_API("VirtualFree",rwx_emulation::release);PLAY_API("VirtualQuery",rwx_emulation::query);
    PLAY_API("AddVectoredExceptionHandler",guestAddVeh);
    PLAY_API("CreateFileW",guestCreateFileW);PLAY_API("CreateFileA",guestCreateFileA);
    PLAY_API("K32GetModuleBaseNameW",kernelModuleBaseName);
    PLAY_API("FindResourceA",kernelFindResourceA);PLAY_API("FindResourceW",kernelFindResourceW);
    PLAY_API("EnumResourceNamesA",kernelEnumResourceNamesA);PLAY_API("LoadResource",kernelLoadResource);
    PLAY_API("SizeofResource",kernelSizeofResource);PLAY_API("LockResource",LockResource);
    PLAY_API("ExitThread",kernelExitThread);
    PLAY_API("FreeLibraryAndExitThread",kernelFreeLibraryAndExitThread);
    PLAY_API("ExitProcess",exitProcess);PLAY_API("TerminateProcess",terminateProcess);PLAY_API("RtlExitUserProcess",rtlExit);
    PLAY_API("GetCommandLineW",commandW);PLAY_API("GetCommandLineA",commandA);
    PLAY_API("GetModuleHandleW",moduleW);PLAY_API("GetModuleHandleA",moduleA);PLAY_API("GetModuleHandleExW",moduleExW);PLAY_API("GetModuleHandleExA",moduleExA);
    PLAY_API("GetModuleFileNameW",filenameW);PLAY_API("GetModuleFileNameA",filenameA);
    PLAY_API("LoadLibraryW",loadW);PLAY_API("LoadLibraryA",loadA);PLAY_API("LoadLibraryExW",loadExW);PLAY_API("LoadLibraryExA",loadExA);
    PLAY_API("GetProcAddress",proc);PLAY_API("FreeLibrary",freeLibrary);PLAY_API("CreateThread",createThread);
    PLAY_API("RtlPcToFileHeader",pcToFileHeader);
#undef PLAY_API
    return 0;
}
// These hooks are reachable only from guest imports/dynamic lookups, never
// from the launcher's own renderer. They record actual calls and results.
static HWND WINAPI createWindow(DWORD ex,LPCWSTR cls,LPCWSTR title,DWORD style,int x,int y,int width,int height,HWND parent,HMENU menu,HINSTANCE instance,LPVOID param) {
    using Fn=HWND(WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
    auto fn=(Fn)user32_core::resolve("user32.dll","CreateWindowExW");
    state->event("guest CreateWindowExW caller="+describe((uintptr_t)_ReturnAddress()));
    if(!fn){stopMissing("user32.dll!CreateWindowExW");return nullptr;}
    HWND window=fn(ex,cls,title,style,x,y,width,height,parent,menu,instance,param);
    char b[160];snprintf(b,sizeof b,"guest CreateWindowExW returned hwnd=%p error=%lu (guest window record; native window bridge pending)",window,window?0:GetLastError());state->event(b);
    if(window)state->milestone=1;return window;
}
static std::string hex32(unsigned long long v){char b[20];snprintf(b,sizeof b,"%x",unsigned(v));return b;}
static HRESULT WINAPI createDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT version,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
    using Fn=decltype(&createDevice);
    auto fn=(Fn)mlp::resolve("d3d11.dll","D3D11CreateDevice",0);
    state->event("guest D3D11CreateDevice caller="+describe((uintptr_t)_ReturnAddress()));
    if(!fn) {stopMissing("d3d11.dll!D3D11CreateDevice");return E_NOINTERFACE;}
    // Adapters from the guest factory are wrappers (virtual console output);
    // the native device needs the real adapter.
    IDXGIAdapter* real=corewindow_dxgi::real_adapter(adapter);
    // What the guest asked for (feature levels in its order) and what it got.
    {std::string asked="guest D3D11CreateDevice request type="+std::to_string(int(type))+" flags=0x"+hex32(flags)+" adapter="+(adapter?"given":"default")+" levels=[";
     for(UINT i=0;levels&&i<count&&i<8;++i)asked+=(i?",0x":"0x")+hex32(levels[i]);
     state->event(asked+(levels?"]":"default]"));}
    HRESULT hr=fn(real?real:adapter,type,software,flags,levels,count,version,device,level,context);
    if(real)real->Release();
    char b[160];snprintf(b,sizeof b,"guest D3D11CreateDevice returned hr=0x%08lx device=%p",(unsigned long)hr,device?*device:nullptr);state->event(b);
    if(level){snprintf(b,sizeof b,"guest D3D11CreateDevice feature level got=0x%x",unsigned(*level));state->event(b);}
    if(SUCCEEDED(hr)&&device&&*device){
        // Failed D3D11 calls are logged through the probe sink (not as the
        // run's "last event"), once per distinct call.
        d3d11_diag::configure([](const char* text){if(state&&state->log)state->log((std::string("PRUN D3D11 ")+text).c_str());});
        d3d11_diag::adopt(*device);
        play_graphics::adopt_device(*device);state->milestone=2;}return hr;
}
// A D3D12 guest reaches the device through d3d12.dll's D3D12CreateDevice, not
// through a d3d11 device, so the D3D11 hook above never sees it. The route
// logs the request and result the same way and gives the returned device the
// D3D12 diagnostics; the swap chain still goes through the same CoreWindow
// adapter, which takes the command queue as its device argument.
static HRESULT WINAPI createD3D12Device(IUnknown* adapter,D3D_FEATURE_LEVEL minimum,REFIID id,void** device) {
    using Fn=HRESULT(WINAPI*)(IUnknown*,D3D_FEATURE_LEVEL,REFIID,void**);
    auto fn=(Fn)mlp::resolve("d3d12.dll","D3D12CreateDevice",0);
    state->event("guest D3D12CreateDevice caller="+describe((uintptr_t)_ReturnAddress()));
    if(!fn) {stopMissing("d3d12.dll!D3D12CreateDevice");return E_NOINTERFACE;}
    // Adapters from the guest factory are wrappers (virtual console output);
    // the native device needs the real adapter. D3D12CreateDevice takes an
    // IUnknown* adapter, so the wrapper is unwrapped through its adapter
    // interface first.
    IDXGIAdapter* real=nullptr;
    if(adapter) {
        IDXGIAdapter* asAdapter=nullptr;
        if(SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter),(void**)&asAdapter))&&asAdapter) {
            real=corewindow_dxgi::real_adapter(asAdapter);
            asAdapter->Release();
        }
    }
    {char b[200];snprintf(b,sizeof b,"guest D3D12CreateDevice request adapter=%s minimum=0x%x iid=%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        adapter?"given":"default",unsigned(minimum),unsigned(id.Data1),unsigned(id.Data2),unsigned(id.Data3),
        id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);state->event(b);}
    HRESULT hr=fn(real?static_cast<IUnknown*>(real):adapter,minimum,id,device);
    if(real)real->Release();
    {char b[160];snprintf(b,sizeof b,"guest D3D12CreateDevice returned hr=0x%08lx device=%p",(unsigned long)hr,device?*device:nullptr);state->event(b);}
    if(SUCCEEDED(hr)&&device&&*device) {
        // Failed D3D12 calls are logged through the probe sink (not as the
        // run's "last event"), once per distinct call.
        d3d12_diag::configure([](const char* text){if(state&&state->log)state->log((std::string("PRUN D3D12 ")+text).c_str());});
        ID3D12Device* d3d=nullptr;
        if(SUCCEEDED(static_cast<IUnknown*>(*device)->QueryInterface(__uuidof(ID3D12Device),(void**)&d3d))&&d3d) {
            d3d12_diag::adopt(d3d);
            // The factory the guest reaches through the device must route guest
            // windows to the CoreWindow, exactly as it does for D3D11.
            play_graphics::adopt_factory_of(d3d);
            d3d->Release();
        }
        state->milestone=2;
    }
    return hr;
}
// D3D12GetDebugInterface and the root-signature helpers are the other d3d12.dll
// exports a guest imports. The debug layer's absence is reported by the
// console/Wine as DXGI_ERROR_SDK_COMPONENT_MISSING; a guest that treats that as
// fatal would otherwise fail with no trace, so the answer is logged once.
static HRESULT WINAPI d3d12GetDebugInterface(REFIID id,void** debug) {
    using Fn=HRESULT(WINAPI*)(REFIID,void**);
    auto fn=(Fn)mlp::resolve("d3d12.dll","D3D12GetDebugInterface",0);
    if(!fn)return E_NOINTERFACE;
    HRESULT hr=fn(id,debug);
    if(state->log&&mlp::first_report("d3d12 debug interface"))
        state->log(("PRUN EVENT guest D3D12GetDebugInterface hr=0x"+hex32(hr)+" (debug layer absent when DXGI_ERROR_SDK_COMPONENT_MISSING)").c_str());
    return hr;
}
static HRESULT WINAPI d3d12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc,D3D_ROOT_SIGNATURE_VERSION version,ID3DBlob** blob,ID3DBlob** error) {
    using Fn=HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*,D3D_ROOT_SIGNATURE_VERSION,ID3DBlob**,ID3DBlob**);
    auto fn=(Fn)mlp::resolve("d3d12.dll","D3D12SerializeRootSignature",0);
    if(!fn)return E_NOINTERFACE;
    HRESULT hr=fn(desc,version,blob,error);
    if(FAILED(hr)&&state->log&&mlp::first_report("d3d12 serialize root signature"))
        state->log(("PRUN D3D12 D3D12SerializeRootSignature hr=0x"+hex32(hr)+" version="+std::to_string(int(version))+" params="+std::to_string(desc?desc->NumParameters:0)).c_str());
    return hr;
}
static HRESULT WINAPI d3d12CreateRootSignatureDeserializer(const void* data,SIZE_T length,REFIID id,void** deserializer) {
    using Fn=HRESULT(WINAPI*)(const void*,SIZE_T,REFIID,void**);
    auto fn=(Fn)mlp::resolve("d3d12.dll","D3D12CreateRootSignatureDeserializer",0);
    if(!fn)return E_NOINTERFACE;
    HRESULT hr=fn(data,length,id,deserializer);
    if(FAILED(hr)&&state->log&&mlp::first_report("d3d12 deserialize root signature"))
        state->log(("PRUN D3D12 D3D12CreateRootSignatureDeserializer hr=0x"+hex32(hr)+" bytes="+std::to_string(length)).c_str());
    return hr;
}
static HRESULT WINAPI d3d12EnableExperimentalFeatures(UINT count,const IID* features,void* configurations,UINT* sizes) {
    using Fn=HRESULT(WINAPI*)(UINT,const IID*,void*,UINT*);
    auto fn=(Fn)mlp::resolve("d3d12.dll","D3D12EnableExperimentalFeatures",0);
    if(!fn)return E_NOINTERFACE;
    HRESULT hr=fn(count,features,configurations,sizes);
    if(state->log&&mlp::first_report("d3d12 experimental features"))
        state->log(("PRUN EVENT guest D3D12EnableExperimentalFeatures count="+std::to_string(count)+" hr=0x"+hex32(hr)).c_str());
    return hr;
}
// Until a real modal UI bridge exists, retain fatal game diagnostics and stop.
// Never return a fabricated button selection that would advance startup.
static std::string dialogText(LPCSTR text,size_t limit=2048) {
    std::string out;
    if(!text) return "<null>";
    size_t i=0;
    for(;i<limit && text[i];++i) {
        unsigned char c=text[i];
        if(c=='\n') out+="\\n";
        else if(c=='\r') out+="\\r";
        else if(c=='\t') out+="\\t";
        else if(c=='"' || c=='\\') {out+='\\';out+=char(c);}
        else if(c<32) out+='?';
        else out+=char(c);
    }
    if(i==limit) out+="<truncated>";
    return out;
}
static int WINAPI messageBoxA(HWND,LPCSTR text,LPCSTR caption,UINT type) {
    stopMissing("user32.dll!MessageBoxA caption=\""+dialogText(caption)+"\" text=\""+dialogText(text)+"\" type="+std::to_string(type),(uintptr_t)_ReturnAddress());
    return 0;
}
static int WINAPI messageBoxW(HWND,LPCWSTR text,LPCWSTR caption,UINT type) {
    // Bound UTF-16 reads before converting; preserve Unicode in durable UTF-8 logs.
    auto convert=[](LPCWSTR value) {
        if(!value) return std::string("<null>");
        size_t n=0;while(n<2048 && value[n])++n;
        bool truncated=n==2048;
        // Do not split a UTF-16 surrogate pair at the input bound.
        if(truncated && value[n-1]>=0xd800 && value[n-1]<=0xdbff)--n;
        std::string result=narrow(std::wstring(value,n));
        // Conversion can exceed 2048 bytes. Escape the complete bounded UTF-8
        // string rather than applying the ANSI byte bound a second time.
        result=dialogText(result.c_str(),result.size()+1);
        if(truncated) result+="<truncated>";
        return result;
    };
    auto message=convert(text), title=convert(caption);
    stopMissing("user32.dll!MessageBoxW caption=\""+title+"\" text=\""+message+"\" type="+std::to_string(type),(uintptr_t)_ReturnAddress());
    return 0;
}
static uintptr_t vcruntimeLookup(const char* dll,const char* name) {return resolve(dll,name,0);}
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal) {
    std::string key=lower(dll);
    if(key=="kernelbase.dll") key="kernel32.dll";
    // Process creation binds Play's refusal before any native lookup, for static,
    // dynamic and mapped-forwarder imports alike (see processCreation).
    if(processCreation(name) && (key=="kernel32.dll" || key.find("api-ms-win-core-")==0)) return processExport(name);
    if(shellLaunch(name) && key=="shell32.dll") return shellExport(name);
    if(auto graphics=play_graphics::resolve(dll,name,ordinal))return graphics;
    if(name && (key=="kernel32.dll"||key=="kernelbase.dll"||key=="ntdll.dll"||key.find("api-ms-win-core-")==0)) if(auto p=processApi(name)) return p;
    if(name && (key=="ole32.dll"||key=="combase.dll"||key.find("api-ms-win-core-com-l1-")==0)) {
        if(!strcmp(name,"CoCreateInstance")) return (uintptr_t)&coCreateInstance;
        if(!strcmp(name,"CoGetClassObject")) return (uintptr_t)&coGetClassObject;
    }
    if(key=="user32.dll" && name && !strcmp(name,"MessageBoxA")) return (uintptr_t)&messageBoxA;
    if(key=="user32.dll" && name && !strcmp(name,"MessageBoxW")) return (uintptr_t)&messageBoxW;
    if(key=="user32.dll" && name && !strcmp(name,"CreateWindowExW")) return (uintptr_t)&createWindow;
    if(key=="d3d11.dll" && name && !strcmp(name,"D3D11CreateDevice")) return (uintptr_t)&createDevice;
    // D3D12: the device entry point and the helpers a D3D12 guest imports. Every
    // other d3d12.dll export (layered device, GetBehaviorValue) falls through to
    // the system route in mlp, which loads the console's own d3d12.dll.
    if(key=="d3d12.dll" && name) {
        if(!strcmp(name,"D3D12CreateDevice")) return (uintptr_t)&createD3D12Device;
        if(!strcmp(name,"D3D12GetDebugInterface")) return (uintptr_t)&d3d12GetDebugInterface;
        if(!strcmp(name,"D3D12SerializeRootSignature")) return (uintptr_t)&d3d12SerializeRootSignature;
        if(!strcmp(name,"D3D12CreateRootSignatureDeserializer")) return (uintptr_t)&d3d12CreateRootSignatureDeserializer;
        if(!strcmp(name,"D3D12EnableExperimentalFeatures")) return (uintptr_t)&d3d12EnableExperimentalFeatures;
    }
    // Same client gate as LoadLibrary, for static and forwarded imports. Never
    // a callable Steam placeholder: the import stays unresolved, as on Windows
    // without the DLL. steam_api64.dll resolves from the game's own file below.
    if(auto p=crtExitApi(key,name)) return p;
    if(key=="steamclient64.dll") {
        if(steamClient.empty()) return missing(dll,name,ordinal);
        auto client=steamClientSigned.empty()?named(steamClient):signedClientModule();
        if(!client) client=map(steamClient);
        if(!client||client->mappingFailed) return missing(dll,name,ordinal);
        auto p=exported(*client,name,ordinal);return p?p:missing(dll,name,ordinal);
    }
    Module* owner=nullptr;
    // Current mapper is the last incomplete module; capture it before recursion.
    for(auto& m:state->modules) if(!m->mapped) owner=m.get();
    Module* dependency=named(wide(dll));
    // A dependency whose mapping failed leaves its imports unresolved, as on
    // Windows; neither it nor the probe registry may supply an address.
    if(dependency && dependency->mappingFailed) return missing(dll,name,ordinal);
    if(!dependency && !apiSetName(wide(dll))) {
        auto path=guestLibraryPath(wide(dll));
        if(exists(path)) {dependency=map(path);if(!dependency)return missing(dll,name,ordinal);}
    }
    if(dependency) {
        if(owner && owner!=dependency) owner->dependencies.push_back(dependency);
        auto p=exported(*dependency,name,ordinal);return p?steamApiExport(*dependency,name,p):missing(dll,name,ordinal);
    }
    // VC++ runtime DLLs the console lacks (no game copy was found above):
    // forwarded to the console's ucrtbase, as Wine's vcruntime140 does.
    if(vcruntime_core::handles(key.c_str())) {
        auto p=name?vcruntime_core::resolve(key.c_str(),name,&vcruntimeLookup):0;
        std::string full=key+"!"+(name?name:"#"+std::to_string(ordinal));
        if(state->log && mlp::first_report("vcrt "+full)) state->log(("VCRT ROUTE "+full+" found="+std::to_string(!!p)).c_str());
        return p?p:missing(dll,name,ordinal);
    }
    return mlp::resolve(key.c_str(),name,ordinal);
}
static void runImpl() {
    state->event("guest environment: native AppContainer PEB/TEB; guest module/command APIs; current directory="+narrow(state->cwd));
    using SetDir=BOOL(WINAPI*)(LPCWSTR);auto setDir=(SetDir)kernel_tail::native("SetCurrentDirectoryW");
    state->event("SetCurrentDirectoryW guest root");
    if(!setDir || !setDir(state->cwd.c_str())) {state->event("SetCurrentDirectoryW failed error="+std::to_string(GetLastError()));return;}
    if(!initializePending()) return;
    char entry[128];snprintf(entry,sizeof entry,"EXE entry %s+0x%lx",state->exe->key.c_str(),state->exe->image.entry);state->event(entry);state->executed=true;
    state->code=((DWORD(WINAPI*)())(state->exe->image.base+state->exe->image.entry))();
    state->success=true;state->event("EXE returned code="+std::to_string(state->code));
}
static DWORD WINAPI worker(void*) {
    guestThread=true;state->thread=GetCurrentThreadId();
    __try {runImpl();} __except(filter(GetExceptionInformation())) {}
    state->result=state->snapshot();state->finished=true;guestThread=false;return state->code;
}
// gdi32's pixel-format and SwapBuffers entry points forward to the wgl* exports of whichever opengl32.dll the guest
// has mapped (Mesa from the runtime directory, or one the game ships). None mapped: Play's failing stubs answer.
static uintptr_t openglExport(const char* name) {
    if(!state||!name)return 0;
    std::lock_guard<LoaderMutex> lock(state->loaderMutex);
    auto m=named(L"opengl32.dll");
    if(!m||m->mappingFailed||!m->mapped)return 0;
    ExportLookup lookup;
    return (uintptr_t)lookup.finish(exported(*m,name,0));
}
// cwd (optional) is the guest's working directory, as a Steam launch option's
// workingdir names it; empty means the executable's own directory.
static bool start(const std::wstring& root,const std::wstring& exe,const std::wstring& args,wxl::Log log,const std::wstring& cwd=std::wstring()) {
    if(state) return false;
    // A previous launch may have left its packaged ICD listed (a game that ran
    // game code keeps its process state); this launch publishes its own only if
    // it maps the runtime loader, so start from a clean registry.
    clearVulkanDriverFiles();
    state=new State();state->root=root;state->cwd=cwd.empty()?root:cwd;state->log=log;observed.store(state,std::memory_order_release);state->command=L"\""+root+L"\\"+exe+L"\" "+args;state->commandA=narrow(state->command);
    mlp::log=log;wxl::logger=log;
    xinput_core::note=[](const char* text){if(mlp::log)mlp::log(text);};mlp::missingResolver=missing;mlp::assets.clear();mlp::summaries.clear();
    gdi32_core::gl_lookup.store(&openglExport);
    state->exe=map(root+L"\\"+exe);if(!state->exe) {state->result=state->snapshot();state->finished=true;return true;}
    find_ntdll();
    {static bool installed=false;if(!installed){installed=true;hostTopLevel=SetUnhandledExceptionFilter(topLevelFilter);}}
    rwx_emulation::state().notify=[](const char* text){if(state)state->event(text);};
    state->event("registering guest exception observer");
    using AddVeh=PVOID(WINAPI*)(ULONG,LONG(CALLBACK*)(EXCEPTION_POINTERS*));
    auto addVeh=(AddVeh)kernel_tail::native("AddVectoredExceptionHandler");
    if(addVeh)state->veh=addVeh(1,vectored);
    state->event("creating guest worker stackReserve=16777216");
    state->worker=CreateThread(nullptr,16*1024*1024,worker,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    if(!state->worker){state->event("CreateThread failed error="+std::to_string(GetLastError()));state->result=state->snapshot();state->finished=true;}
    return true;
}
// Per-launch Steam routing, cleared before each Play so one game's provider,
// client or ownership ticket can never reach the next game.
static void reset_launch_config() {
    steamProvider.clear();steamClient.clear();steamClientSigned.clear();appTicket.clear();emulatedCreate=nullptr;
    set_rich_presence_tap({});
    set_leaderboard_tap({});
    set_leaderboard_provider(nullptr);   // the image may be unloaded with the launch
    clearVulkanDriverFiles();
}
// A launch that ended before any game code ran (no TLS callback, DllMain or
// entry point: `executed` is set before each) left nothing executing: the
// worker has returned and only this loader refers to the images. Free them so
// the next Play starts a fresh loader without restarting the launcher. Once
// game code ran, its threads, hooks and addresses may live on, so the guest
// stays retained and only a launcher restart ends it. The State object itself
// is kept (it is small): launcher threads may still read its last event.
static bool discard_unstarted() {
    State* s=observed.load(std::memory_order_acquire);
    if(!s || !s->finished.load(std::memory_order_acquire) || s->executed.load()) return false;
    if(s->worker) {
        if(WaitForSingleObject(s->worker,10000)!=WAIT_OBJECT_0) return false;
        if(s->executed.load()) return false;
        CloseHandle(s->worker);s->worker=nullptr;
    }
    if(s->veh) {
        auto remove=(ULONG(WINAPI*)(PVOID))kernel_tail::native("RemoveVectoredExceptionHandler");
        if(!remove || !remove(s->veh)) return false;
        s->veh=nullptr;
    }
    {
        std::lock_guard<LoaderMutex> lock(s->loaderMutex);
        mlp::assets.clear();
        s->order.clear();s->exe=nullptr;
        s->modules.clear();   // ~Image: no entry ran, so this only unmaps and frees TLS indices
    }
    reset_launch_config();
    state=nullptr;observed.store(nullptr,std::memory_order_release);
    return true;
}
}
