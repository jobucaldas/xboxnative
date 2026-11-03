#pragma once
#include "../../uwph/xinput/modules.h"
// Play owns mapped images for the application's lifetime. Windows owns the
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
namespace play_runtime {
constexpr DWORD missingException=0xe0520001, exitException=0xe0520002;
struct Module {
    std::wstring path;
    std::string key;
    wxl::Image image;
    bool mapped=false, mappingFailed=false, prepared=false, initialized=false, initializing=false, initializationFailed=false;
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
    std::mutex eventMutex;
    std::recursive_mutex loaderMutex;
    std::string last="loader not entered", result;
    DWORD code=0;
    unsigned carrierCount=0;
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
static bool exists(const std::wstring& path) { DWORD a=GetFileAttributesW(path.c_str()); return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY); }
static uintptr_t resolve(const char*,const char*,WORD);
static Module* map(const std::wstring& path);
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
    if(state->log) state->log(("PRUN OPTIONAL unresolved dynamic export: "+full).c_str());
    SetLastError(ERROR_PROC_NOT_FOUND);return 0;
}
// No supported guest child-process provider is installed here. These names
// must not acquire native implementations or callable failure placeholders.
// This is a feature boundary, not a sandbox: other launch APIs/ordinals and
// arbitrary native export aliases are not proven blocked by this policy.
static bool processCreation(const char* name) {
    return name && (!strcmp(name,"CreateProcessA") || !strcmp(name,"CreateProcessW"));
}
static uintptr_t unavailableProcess(const char* dll,const char* name) {
    state->event("guest child-process creation unavailable (no supported provider): "+lower(dll)+"!"+name);
    return missing(dll,name,0);
}
static void WINAPI exitProcess(UINT code) {
    state->code=code; state->event("ExitProcess code="+std::to_string(code));
    ULONG_PTR arg=code; RaiseException(exitException,EXCEPTION_NONCONTINUABLE,1,&arg);
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
static HMODULE WINAPI moduleA(LPCSTR name) { auto w=wide(name); return moduleW(name?w.c_str():nullptr); }
static DWORD copyPath(const std::wstring& path,LPWSTR out,DWORD capacity) {
    if(!capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    size_t n=std::min(path.size(),size_t(capacity-1)); memcpy(out,path.data(),n*2); out[n]=0;
    if(path.size()>=capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return capacity; } return DWORD(n);
}
static DWORD WINAPI filenameW(HMODULE handle,LPWSTR out,DWORD size) {
    if(xinput_core::module(handle)){SetLastError(ERROR_NOT_SUPPORTED);return 0;}
    auto m=handle?find(handle):state->exe;
    if(m) return copyPath(m->path,out,size);
    using Fn=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD); auto fn=(Fn)kernel_tail::native("GetModuleFileNameW"); return fn?fn(handle,out,size):0;
}
static DWORD WINAPI filenameA(HMODULE handle,LPSTR out,DWORD size) {
    wchar_t path[32768]; DWORD n=filenameW(handle,path,32768); if(!n) return 0;
    std::string s=narrow(path); if(!size) return 0; size_t take=std::min(s.size(),size_t(size-1)); memcpy(out,s.data(),take);out[take]=0;
    if(s.size()>=size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return size; } return DWORD(take);
}
static BOOL WINAPI moduleExW(DWORD flags,LPCWSTR name,HMODULE* out) {
    if(!out || ((flags&GET_MODULE_HANDLE_EX_FLAG_PIN)&&(flags&GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))) {SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    if(!(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) && name) {
        if(auto input=xinput_core::find_module(basename(name).c_str(),false)) {
            if(flags & ~(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT)) {SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
            if(!(flags&GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT))xinput_core::find_module(basename(name).c_str(),true);
            *out=input;return TRUE;
        }
    }
    if(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        for(auto& m:state->modules) if((uintptr_t)name>=(uintptr_t)m->image.base && (uintptr_t)name-(uintptr_t)m->image.base<m->image.size) {*out=(HMODULE)m->image.base;return TRUE;}
    } else { auto m=name?named(name):state->exe; if(m){*out=(HMODULE)m->image.base;return TRUE;} }
    using Fn=BOOL(WINAPI*)(DWORD,LPCWSTR,HMODULE*);auto fn=(Fn)kernel_tail::native("GetModuleHandleExW");return fn?fn(flags,name,out):FALSE;
}
static BOOL WINAPI moduleExA(DWORD flags,LPCSTR name,HMODULE* out) { auto w=(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)?std::wstring():wide(name);return moduleExW(flags,flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS?(LPCWSTR)name:name?w.c_str():nullptr,out); }
static HMODULE WINAPI loadExW(LPCWSTR name,HANDLE file,DWORD flags) {
    if(!name || file || (flags&(LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE))) {SetLastError(ERROR_NOT_SUPPORTED);return nullptr;}
    std::wstring requested=name;
    auto leaf=requested.substr(requested.find_last_of(L"\\/")+1);
    if(leaf.find(L'.')==std::wstring::npos) requested+=L".dll";
    // Apply the real-client prerequisite after Windows name normalization,
    // including extensionless requests and the client DLL itself.
    auto key=basename(requested);
    if(key=="steam_api64.dll" || key=="steamclient64.dll") {
        stopMissing(key+"!LoadLibrary (real client prerequisite item 36)",(uintptr_t)_ReturnAddress());return nullptr;
    }
    Module* m=nullptr;
    {std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
     m=named(requested);if(!m){std::wstring p=requested;if(p.find(L':')==std::wstring::npos && (p.empty()||p[0]!=L'\\'))p=state->root+L"\\"+p;if(exists(p)) {m=map(p);if(!m){SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;}}}
    }
    if(m) { if(m->mappingFailed || !m->mapped || !initializePending() || !m->initialized) {SetLastError(ERROR_DLL_INIT_FAILED);return nullptr;} return (HMODULE)m->image.base; }
    if(auto input=xinput_core::find_module(key.c_str(),true))return input;
    // Restrict fallback to documented system DLL search, never desktop paths.
    auto result=LoadLibraryExW(requested.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!result) {DWORD error=GetLastError();state->event("LoadLibrary failed name="+narrow(requested)+" error="+std::to_string(error));SetLastError(error);}
    return result;
}
static HMODULE WINAPI loadW(LPCWSTR name) { return loadExW(name,nullptr,0); }
static HMODULE WINAPI loadExA(LPCSTR name,HANDLE f,DWORD flags) { auto w=wide(name);return loadExW(name?w.c_str():nullptr,f,flags); }
static HMODULE WINAPI loadA(LPCSTR name) { return loadExA(name,nullptr,0); }
static BOOL WINAPI freeLibrary(HMODULE h) { if(auto input=xinput_core::module(h))return xinput_core::release_module(input); if(find(h)) return TRUE; return FreeLibrary(h); }
// Wine ntdll/loader.c find_forwarded_export resolves the last dot as the
// DLL/symbol separator, supports #ordinals and records a module dependency.
// Keep this execution behavior separate from the non-executing import probe.
static uintptr_t exported(Module& module,const char* name,WORD ordinal) {
    if(module.mappingFailed || !module.image.base) return 0;
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
static FARPROC WINAPI proc(HMODULE h,LPCSTR name) {
    if(auto input=xinput_core::module(h))return xinput_core::module_proc(input,name);
    if(auto m=find(h)) {
        WORD ordinal=IS_INTRESOURCE(name)?WORD((uintptr_t)name):0;
        std::lock_guard<std::recursive_mutex> lock(state->loaderMutex);
        size_t previousCount=state->modules.size();
        auto p=exported(*m,IS_INTRESOURCE(name)?nullptr:name,ordinal);
        // A dynamically resolved forwarder may have mapped a new target DLL.
        // Initialize its TLS and DllMain before returning callable guest code.
        if(p&&state->modules.size()!=previousCount&&!initializePending())p=0;
        if(!p) SetLastError(ERROR_PROC_NOT_FOUND);return (FARPROC)p;
    }
    // Native handles (including OS-resolved API sets/forwarders) must not
    // expose these names even when GetModuleFileNameW cannot identify them.
    // Keep owned mapped exports above separate from native fallback below.
    if(!IS_INTRESOURCE(name)) {
        if(processCreation(name)) return (FARPROC)unavailableProcess("native module",name);
        // Do not hand out raw GetProcAddress on the metadata-failure path:
        // a second lookup through it would bypass the unavailable-name gate.
        if(!strcmp(name,"GetProcAddress")) return (FARPROC)&proc;
    }
    // Route dynamic lookups back through the same process wrappers and router.
    wchar_t p[32768]{};using Fn=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD);auto fn=(Fn)kernel_tail::native("GetModuleFileNameW");
    if(fn && fn(h,p,32768)) {
        auto result=resolve(basename(p).c_str(),IS_INTRESOURCE(name)?nullptr:name,IS_INTRESOURCE(name)?WORD((uintptr_t)name):0);
        // Static imports are rejected while mapping; dynamic feature detection
        // observes NULL rather than a callable failure placeholder.
        return (FARPROC)result;
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
static LONG CALLBACK vectored(EXCEPTION_POINTERS* ep) {
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
    wxl::Carrier c; if(state->carrierCount>=16) return c;
    wchar_t name[64];swprintf(name,64,L"play-tls-%02u.dll",state->carrierCount++);
    HMODULE h=LoadPackagedLibrary(name,0);
    if(h){c.index=(DWORD(*)())GetProcAddress(h,"carrier_index");c.block=(BYTE*(*)())GetProcAddress(h,"carrier_block");}return c;
}
static Module* map(const std::wstring& path) {
    if(auto m=named(path)) return m->mappingFailed?nullptr:m;
    if(state->modules.size()>=128) {state->event("loader module capacity exceeded");return nullptr;}
    auto own=std::make_unique<Module>();auto m=own.get();m->path=path;m->key=basename(path);state->modules.push_back(std::move(own));state->moduleCount=int(state->modules.size());
    // Publish before fixing imports; circular imports can use the copied export
    // table, but no initialization runs until the complete graph is mapped.
    mlp::assets[m->key]=&m->image;
    state->event("mapping "+m->key);
    struct ImportScope {ImportScope(){++mappingImports;} ~ImportScope(){--mappingImports;}} imports;
    if(!wxl::run(path.c_str(),state->log,&m->image,resolve)) {m->mappingFailed=true;state->event("mapping failed: "+m->key+"; guest initialization blocked; "+state->snapshot());return nullptr;}
    m->mapped=true;++state->mappedCount;state->order.push_back(m);return m;
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
    // on a later LoadLibrary. Full detach/unload/reload is not supported yet.
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
    // Register every unwind table and TLS index before any guest callback.
    for(auto m:state->order) if(!prepare(*m)) return false;
    // Every template must exist before the first callback can call a sibling.
    for(auto m:state->order) if(m->image.tlsKey!=TLS_OUT_OF_INDEXES && !TlsGetValue(m->image.tlsKey) && !m->image.initThreadStorage()) {state->event("TLS storage failed: "+m->key);return false;}
    // Dependencies added by LoadLibrary can extend order while DllMain runs.
    for(size_t i=0;i<state->order.size();++i) if(!initialize(*state->order[i])) return false;return true;
}
struct ThreadStart { LPTHREAD_START_ROUTINE function;void* arg; };
static DWORD WINAPI child(void* data) {
    auto p=(ThreadStart*)data;auto function=p->function;auto arg=p->arg;delete p;guestThread=true;
    DWORD code=0;
    __try {
        for(auto m:state->order) if(m->initialized && !m->image.initThreadStorage()) RaiseException(missingException,EXCEPTION_NONCONTINUABLE,0,nullptr);
        for(auto m:state->order) if(m->initialized && !m->image.notifyThread(DLL_THREAD_ATTACH)) RaiseException(missingException,EXCEPTION_NONCONTINUABLE,0,nullptr);
        code=function(arg);
        for(auto i=state->order.rbegin();i!=state->order.rend();++i) if((*i)->initialized)(*i)->image.detachThread(DLL_THREAD_DETACH);
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
    HRESULT hr=fn(adapter,type,software,flags,levels,count,version,device,level,context);
    char b[160];snprintf(b,sizeof b,"guest D3D11CreateDevice returned hr=0x%08lx device=%p",(unsigned long)hr,device?*device:nullptr);state->event(b);
    if(SUCCEEDED(hr)&&device&&*device)state->milestone=2;return hr;
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
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal) {
    std::string key=lower(dll);
    if(key=="kernelbase.dll") key="kernel32.dll";
    // Reject static imports before map() can create a worker or initialize any
    // guest TLS/DllMain/entry. Mapped forwarders recurse through this same gate.
    if(processCreation(name) && (key=="kernel32.dll" || key.find("api-ms-win-core-")==0)) return unavailableProcess(dll,name);
    if(auto graphics=play_graphics::resolve(dll,name,ordinal))return graphics;
    if(name && (key=="kernel32.dll"||key=="kernelbase.dll"||key=="ntdll.dll"||key.find("api-ms-win-core-")==0)) if(auto p=processApi(name)) return p;
    if(key=="user32.dll" && name && !strcmp(name,"MessageBoxA")) return (uintptr_t)&messageBoxA;
    if(key=="user32.dll" && name && !strcmp(name,"MessageBoxW")) return (uintptr_t)&messageBoxW;
    if(key=="user32.dll" && name && !strcmp(name,"CreateWindowExW")) return (uintptr_t)&createWindow;
    if(key=="d3d11.dll" && name && !strcmp(name,"D3D11CreateDevice")) return (uintptr_t)&createDevice;
    if(key=="steam_api64.dll" || key=="steamclient64.dll") {
        // Forwarded dynamic exports must obey the same real-client gate as
        // LoadLibrary; never return a callable Steam placeholder.
        if(!mappingImports) stopMissing(key+"!"+(name?name:"#"+std::to_string(ordinal))+" (real client prerequisite item 36)",(uintptr_t)_ReturnAddress());
        return missing(dll,name,ordinal);
    }
    Module* owner=nullptr;
    // Current mapper is the last incomplete module; capture it before recursion.
    for(auto& m:state->modules) if(!m->mapped) owner=m.get();
    Module* dependency=named(wide(dll));
    if(!dependency && exists(state->root+L"\\"+wide(dll))) dependency=map(state->root+L"\\"+wide(dll));
    if(dependency) {
        if(owner && owner!=dependency) owner->dependencies.push_back(dependency);
        auto p=exported(*dependency,name,ordinal);return p?p:missing(dll,name,ordinal);
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
    state->event("registering guest exception observer");
    using AddVeh=PVOID(WINAPI*)(ULONG,LONG(CALLBACK*)(EXCEPTION_POINTERS*));
    auto addVeh=(AddVeh)kernel_tail::native("AddVectoredExceptionHandler");
    if(addVeh)state->veh=addVeh(1,vectored);
    state->event("creating guest worker stackReserve=16777216");
    state->worker=CreateThread(nullptr,16*1024*1024,worker,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    if(!state->worker){state->event("CreateThread failed error="+std::to_string(GetLastError()));state->result=state->snapshot();state->finished=true;}
    return true;
}
}
