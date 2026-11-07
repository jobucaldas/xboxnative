#pragma once
#include "../../uwph/xinput/modules.h"
// Play tracks mapped image ownership and dependencies. Windows owns the
// actual AppContainer PEB/TEB, native threads and static TLS carrier slots.
// Guest process APIs below expose the guest identity without rewriting the
// launcher's PEB or installing a private segment/exception mechanism.
#include <atomic>
#include <mutex>
#include <memory>
#include <array>
#include <d3d11.h>
#include <intrin.h>
#include "play_graphics.h"
#include "../../uwph/vcruntime/route.h"
#include "steam_app_ticket.h"
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
struct State {
    std::wstring root, command;
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
    std::recursive_mutex loaderMutex;
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
    std::lock_guard<std::recursive_mutex> loaderLock(state->loaderMutex);
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
    {std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
     m=named(requested);
     if(!m) {
         auto p=guestLibraryPath(requested,!flags);
         if(exists(p)) {
             if(flags) {SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
             m=map(p);
             if(!m) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
         }
     }
    }
    if(m) {
        if(flags) {SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
        if(m->mappingFailed || !m->mapped) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
        ++m->references;
        // A DllMain may load a module whose own DllMain is still running on
        // this thread (Wine's concrt140 loads msvcp140 while msvcp140 attaches
        // it). Windows and Wine return that handle; the recursive loader lock
        // means no other thread can be inside this branch meanwhile.
        if(!initializePending() || !(m->initialized || m->initializing)) {--m->references;SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}
        return (HMODULE)m->image.base;
    }
    if(auto input=xinput_core::find_module(key.c_str(),true))return input;
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
    // find() iterates vector storage that map() may reallocate. Native-handle
    // resolution below can also inspect/map guest modules, so retain the lock
    // for the entire lookup, not just the mapped-export branch.
    std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
    ExportLookup lookup;
    if(auto m=find(h)) {
        WORD ordinal=IS_INTRESOURCE(name)?WORD((uintptr_t)name):0;
        return lookup.finish(exported(*m,IS_INTRESOURCE(name)?nullptr:name,ordinal));
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
// Optional Steam API provider for an owned game (AGENTS rule 3), e.g. a pinned
// gbe_fork steam_api64.dll staged with its steam_settings in a writable folder.
// When set, any steam_api64.dll the game loads is mapped from this file instead;
// the game's own file on disk is never modified. Set before start().
static std::wstring steamProvider;
static void set_steam_provider(const std::wstring& path) { steamProvider=path; }
static Module* map(const std::wstring& requested,const std::string& asKey,wxl::Resolver resolver) {
    if(auto m=named(asKey.empty()?requested:wide(asKey.c_str()))) return m->mappingFailed?nullptr:m;
    const bool provided=basename(requested)=="steam_api64.dll" && !steamProvider.empty();
    const std::wstring& path=provided?steamProvider:requested;
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
    m->mapped=true;++state->mappedCount;state->order.push_back(m);return m;
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
    std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
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
        state->event("loading TLS carrier for "+m.key);
        auto c=carrier();
        if(!c.index || !c.block) {state->event("TLS carrier unavailable: "+m.key);return false;}
        auto tls=(IMAGE_TLS_DIRECTORY64*)(image.base+td.VirtualAddress);
        // TLS index lives in writable data in supported images. prepareTls
        // validates section ownership before writing it.
        if(!wxl::prepareTls(image,state->log,c)) {state->event("TLS preparation failed: "+m.key);return false;}
        (void)tls;
    }
    state->event("registering unwind for "+m.key);
    if(!wxl::prepareUnwind(image,state->log)) {state->event("unwind registration failed: "+m.key);return false;}
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
    std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
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
        std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
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
static uintptr_t processApi(const char* name) {
#define PLAY_API(n,f) if(!strcmp(name,n)) return (uintptr_t)&f
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
static HRESULT WINAPI createDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT version,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context) {
    using Fn=decltype(&createDevice);
    auto fn=(Fn)mlp::resolve("d3d11.dll","D3D11CreateDevice",0);
    state->event("guest D3D11CreateDevice caller="+describe((uintptr_t)_ReturnAddress()));
    if(!fn) {stopMissing("d3d11.dll!D3D11CreateDevice");return E_NOINTERFACE;}
    // Adapters from the guest factory are wrappers (virtual console output);
    // the native device needs the real adapter.
    IDXGIAdapter* real=corewindow_dxgi::real_adapter(adapter);
    HRESULT hr=fn(real?real:adapter,type,software,flags,levels,count,version,device,level,context);
    if(real)real->Release();
    char b[160];snprintf(b,sizeof b,"guest D3D11CreateDevice returned hr=0x%08lx device=%p",(unsigned long)hr,device?*device:nullptr);state->event(b);
    if(SUCCEEDED(hr)&&device&&*device){play_graphics::adopt_device(*device);state->milestone=2;}return hr;
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
    if(key=="user32.dll" && name && !strcmp(name,"MessageBoxA")) return (uintptr_t)&messageBoxA;
    if(key=="user32.dll" && name && !strcmp(name,"MessageBoxW")) return (uintptr_t)&messageBoxW;
    if(key=="user32.dll" && name && !strcmp(name,"CreateWindowExW")) return (uintptr_t)&createWindow;
    if(key=="d3d11.dll" && name && !strcmp(name,"D3D11CreateDevice")) return (uintptr_t)&createDevice;
    // Same client gate as LoadLibrary, for static and forwarded imports. Never
    // a callable Steam placeholder: the import stays unresolved, as on Windows
    // without the DLL. steam_api64.dll resolves from the game's own file below.
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
    if(!dependency) {
        auto path=guestLibraryPath(wide(dll));
        if(exists(path)) {dependency=map(path);if(!dependency)return missing(dll,name,ordinal);}
    }
    if(dependency) {
        if(owner && owner!=dependency) owner->dependencies.push_back(dependency);
        auto p=exported(*dependency,name,ordinal);return p?p:missing(dll,name,ordinal);
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
    state->event("guest environment: native AppContainer PEB/TEB; guest module/command APIs; current directory="+narrow(state->root));
    using SetDir=BOOL(WINAPI*)(LPCWSTR);auto setDir=(SetDir)kernel_tail::native("SetCurrentDirectoryW");
    state->event("SetCurrentDirectoryW guest root");
    if(!setDir || !setDir(state->root.c_str())) {state->event("SetCurrentDirectoryW failed error="+std::to_string(GetLastError()));return;}
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
static bool start(const std::wstring& root,const std::wstring& exe,const std::wstring& args,wxl::Log log) {
    if(state) return false;
    state=new State();state->root=root;state->log=log;observed.store(state,std::memory_order_release);state->command=L"\""+root+L"\\"+exe+L"\" "+args;state->commandA=narrow(state->command);
    mlp::log=log;wxl::logger=log;mlp::missingResolver=missing;mlp::assets.clear();mlp::summaries.clear();
    state->exe=map(root+L"\\"+exe);if(!state->exe) {state->result=state->snapshot();state->finished=true;return true;}
    find_ntdll();
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
        std::lock_guard<std::recursive_mutex> lock(s->loaderMutex);
        mlp::assets.clear();
        s->order.clear();s->exe=nullptr;
        s->modules.clear();   // ~Image: no entry ran, so this only unmaps and frees TLS indices
    }
    reset_launch_config();
    state=nullptr;observed.store(nullptr,std::memory_order_release);
    return true;
}
}
