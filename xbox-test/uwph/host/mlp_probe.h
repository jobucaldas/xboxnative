#pragma once
#include "../user32/route.h"
#include <map>
#include <array>
#include <utility>
#include "entry_kernel.h"
#include "../../shared/pe_tail.h"
namespace mlp {
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "mlp_routes.h"
};
struct Summary { unsigned resolved=0, stubbed=0; std::string first="none"; };
static std::map<std::string, Summary> summaries;
static std::map<std::string, wxl::Image*> assets;
static std::map<std::string, HMODULE> systems;
static wxl::Log log;
static uintptr_t asset_export(wxl::Image& image, const char* name, WORD ordinal) {
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!dir.Size || !image.valid(dir.VirtualAddress,dir.Size) || dir.Size<sizeof(IMAGE_EXPORT_DIRECTORY)) return 0;
    auto e=(IMAGE_EXPORT_DIRECTORY*)(image.base+dir.VirtualAddress);
    if(!image.valid(e->AddressOfFunctions,size_t(e->NumberOfFunctions)*4) ||
       !image.valid(e->AddressOfNames,size_t(e->NumberOfNames)*4) ||
       !image.valid(e->AddressOfNameOrdinals,size_t(e->NumberOfNames)*2)) return 0;
    auto functions=(DWORD*)(image.base+e->AddressOfFunctions);
    DWORD index=DWORD(ordinal)-e->Base;
    if(name) {
        index=e->NumberOfFunctions;
        auto names=(DWORD*)(image.base+e->AddressOfNames);
        auto ordinals=(WORD*)(image.base+e->AddressOfNameOrdinals);
        for(DWORD i=0;i<e->NumberOfNames;i++) {
            auto n=image.str(names[i]); if(n && !strcmp(n,name)) { index=ordinals[i]; break; }
        }
    }
    if(index>=e->NumberOfFunctions) return 0;
    DWORD rva=functions[index];
    // Forwarders need loader recursion; explicitly missing in this probe.
    if(!rva || !image.valid(rva,1) || (rva>=dir.VirtualAddress && rva-dir.VirtualAddress<dir.Size)) return 0;
    return (uintptr_t)image.base+rva;
}
// App-partition-only native lookup. No desktop user32 fallback.
static uintptr_t native_kernel(const char* name,std::string& provider) {
    static const wchar_t* modules[]={L"kernel32.dll", L"kernelbase.dll", L"api-ms-win-core-synch-l1-2-0.dll", L"api-ms-win-core-libraryloader-l1-2-0.dll", L"api-ms-win-core-file-l1-2-0.dll", L"api-ms-win-core-memory-l1-1-0.dll", L"api-ms-win-core-processthreads-l1-1-0.dll", L"api-ms-win-core-rtlsupport-l1-1-0.dll"};
    for(auto module:modules) {
        std::wstring w=module; std::string key(w.begin(),w.end());
        auto it=systems.find(key);
        if(it==systems.end()) it=systems.emplace(key,LoadLibraryExW(module,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)).first;
        HMODULE h=it->second;
        if(h) if(auto p=GetProcAddress(h,name)) {
            provider="system:"+key;
            return (uintptr_t)p;
        }
    }
    return 0;
}
static uintptr_t resolve(const char* dll, const char* name, WORD ordinal) {
    std::string key=dll; for(auto& c:key) if(c>='A' && c<='Z') c+=32;
    std::string symbol=name ? name : "#"+std::to_string(ordinal);
    const char* kind="unclassified";
    for(auto& route:routes) if(key==route.dll) kind=route.kind;
    uintptr_t address=0;
    std::string provider=kind; bool named_stub=false;
    if(!strcmp(kind,"system")) {
        auto it=systems.find(key);
        if(it==systems.end()) {
            std::wstring wide(key.begin(),key.end());
            // Documented loader search restricted to System32/API sets.
            HMODULE module=LoadLibraryExW(wide.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            it=systems.emplace(key,module).first;
            char msg[512]; snprintf(msg,sizeof msg,"MLP SYSTEM %s loaded=%d error=%lu",key.c_str(),!!module,module?0:GetLastError()); log(msg);
        }
        if(it->second) address=(uintptr_t)GetProcAddress(it->second,name ? name : MAKEINTRESOURCEA(ordinal));
    } else if(!strcmp(kind,"uwph")) {
        auto it=assets.find(key);
        if(it!=assets.end()) address=asset_export(*it->second,name,ordinal);
        else if(name) for(auto& b:wxl::builtins) if(!_stricmp(dll,b.dll)&&!strcmp(name,b.name)) address=b.address;
    }
    if(key=="kernel32.dll" && name) {
        if(auto builtin=entry_kernel::resolve(name)) { address=builtin; provider="uwph:entry-kernel"; }
        else if(!address) {address=native_kernel(name,provider);}
    }
    if(auto core=user32_core::resolve(dll,name)) {address=core;provider="uwph:u32c-record";named_stub=false;}
    if(!address&&name&&key!="kernel32.dll") {
        address=pe_tail::resolve(key.c_str(),name);
        if(address)provider="system:pe-tail";
    }
    if(!address && name) {
        static HMODULE hid=LoadPackagedLibrary(L"uwph-hid.dll",0);
        static HMODULE driver=LoadPackagedLibrary(L"uwph-user32.dll",0);
        static HMODULE stubs=LoadPackagedLibrary(L"entry-stubs.dll",0);
        using SetLog=void (*)(wxl::Log);
        static bool configured=false;
        if(!configured) {
            if(hid) if(auto p=(SetLog)GetProcAddress(hid,"hid_stub_set_log")) p(log);
            if(stubs) if(auto p=(SetLog)GetProcAddress(stubs,"entry_stubs_set_log")) p(log);
            configured=true;
        }
        const char* route=nullptr;
        if((key=="hid.dll" || key=="setupapi.dll") && hid) { address=(uintptr_t)GetProcAddress(hid,name); route="named-stub-hid"; }
        if(key=="user32.dll" && driver) {
            const char* alias=name;
            if(!strcmp(name,"PeekMessageA")) alias="PeekMessageW";
            if(!strcmp(name,"GetMessageA")) alias="GetMessageW";
            if(!strcmp(name,"DispatchMessageA")) alias="DispatchMessageW";
            address=(uintptr_t)GetProcAddress(driver,alias); route="uwph-corewindow";
        }
        using Resolve=uintptr_t (*)(const char*,const char*);
        if(!address && stubs) if(auto p=(Resolve)GetProcAddress(stubs,"entry_stubs_resolve")) {address=p(key.c_str(),name);route="named-stub";}
        if(address) {provider=route; named_stub=provider.find("named-stub")==0;}
    }
    auto& sum=summaries[key];
    if(address) {
        if(named_stub) ++sum.stubbed; else ++sum.resolved;
        std::string category=named_stub ? "stub" : provider.find("system")==0 ? "system" : "uwph";
        log(("MLP ROUTE "+key+"!"+symbol+" kind="+category+" provider="+provider+" abi=named-x64").c_str());
        return address;
    }
    if(sum.first=="none")sum.first=symbol;
    log(("MLP MISSING "+key+"!"+symbol+" route="+kind).c_str());
    return 0; // Fail mapping; an unknown signature must never receive a placeholder.
}

static bool run(const std::wstring& root, wxl::Log logger) {
    log=logger; wxl::logger=logger; entry_kernel::logger=logger;
    log("MLP PROBE begin; NO entry/DllMain/TLS callbacks; zero RWX; every import requires a real export or named x64 ABI");
    // Dependencies first so Megabonk's UnityMain and Unity's baselib imports can
    // point to real mapped exports without invoking the Windows DLL loader.
    wxl::Image baselib, unity, exe;
    struct Target { const char* name; const wchar_t* path; wxl::Image* image; };
    Target targets[]={{"baselib.dll",L"baselib.dll",&baselib},{"UnityPlayer.dll",L"UnityPlayer.dll",&unity},{"Megabonk.exe",L"Megabonk.exe",&exe}};
    for(auto& target:targets) {
        summaries.clear(); log((std::string("MLP IMAGE ")+target.name).c_str());
        if(!wxl::run((root+L"\\"+target.path).c_str(),logger,target.image,resolve)) { log("MLP-PROBE-FAILED"); return false; }
        std::string key=target.name; for(auto& c:key) if(c>='A'&&c<='Z') c+=32;
        assets[key]=target.image;
        for(auto& row:summaries) {
            char msg[1024]; snprintf(msg,sizeof msg,"MLP SUMMARY image=%s dll=%s resolved=%u stubbed=%u first-missing=%s",target.name,row.first.c_str(),row.second.resolved,row.second.stubbed,row.second.first.c_str()); log(msg);
        }
    }
    log("MLP-PROBE-DONE no guest code executed; TLS/unwind deferred; zero RWX requested");
    assets.clear();
    return true;
}
}
