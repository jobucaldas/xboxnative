#pragma once
#include "../../uwph/user32/route.h"
#include <map>
#include <array>
#include <utility>
#include "../../shared/kernel_tail.h"
#include "../../shared/kernel_tail_test.h"
#include "../../shared/kernel_tail_search_test.h"
#include "../../shared/pe_tail.h"
#include "../../shared/pe_tail_test.h"
namespace mlp {
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "mlp_routes.h"
};
struct Summary { unsigned resolved=0, stubbed=0; std::string first="none"; };
static std::map<std::string, Summary> summaries;
static std::map<std::string, wxl::Image*> assets;
static std::map<std::string, HMODULE> systems;
static std::vector<std::string> stub_names;
static wxl::Log log;
// Probe-only ABI: integer zero plus ERROR_CALL_NOT_IMPLEMENTED. These placeholders
// are not a runnable implementation of arbitrary Win32 signatures (e.g. HRESULT,
// floating returns or output parameters). No guest code may execute in this mode.
template<size_t N> static uintptr_t WINAPI failure() {
    std::string message="MLP STUB-CALL "+stub_names[N]+" return=0 error=120 probe-only";
    log(message.c_str()); SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0;
}
template<size_t... N> static auto stubs(std::index_sequence<N...>) {
    return std::array<uintptr_t, sizeof...(N)>{(uintptr_t)&failure<N>...};
}
static const auto stub_functions=stubs(std::make_index_sequence<2048>{});
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
static uintptr_t resolve(const char* dll, const char* name, WORD ordinal) {
    std::string key=dll; for(auto& c:key) if(c>='A' && c<='Z') c+=32;
    std::string symbol=name ? name : "#"+std::to_string(ordinal);
    const char* kind="unclassified";
    for(auto& route:routes) if(key==route.dll) kind=route.kind;
    uintptr_t address=0;
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
    if(auto core=user32_core::resolve(dll,name)) address=core;
    if(key=="kernel32.dll" && name && !address) {
        address=kernel_tail::resolve(name);
        log(("K32T ROUTE kernel32.dll!"+symbol+" found="+std::to_string(!!address)).c_str());
    }
    if(!address&&name&&key!="kernel32.dll") {
        address=pe_tail::resolve(key.c_str(),name);
        if(address)log(("K32T SYSTEM "+key+"!"+symbol).c_str());
    }
    static bool tested=false;
    if(!tested) { tested=true; kernel_tail::test(log); kernel_tail::test_search(log); pe_tail::discover(log); pe_tail::test(log); }
    auto& sum=summaries[key];
    if(address) { ++sum.resolved; return address; }
    ++sum.stubbed;
    bool planned=!strcmp(kind,"stub");
    if(!planned && sum.first=="none") sum.first=symbol;
    std::string msg=std::string(planned?"MLP STUB ":"MLP MISSING ")+key+"!"+symbol+" route="+kind+" return=0 error=120 probe-only";
    log(msg.c_str());
    if(stub_names.size()>=stub_functions.size()) { log("MLP FAIL stub capacity"); return 0; }
    stub_names.push_back(key+"!"+symbol);
    return stub_functions[stub_names.size()-1];
}
// Called at each completed image boundary; counts come from observed resolver
// outcomes. Kernel placeholders remain missing, never counted as native routes.
static void report_kernel() {
    auto it=summaries.find("kernel32.dll");if(it==summaries.end())return;
    char line[160];snprintf(line,sizeof line,"K32T SUMMARY kernel32 missing=%u resolved=%u",it->second.stubbed,it->second.resolved);log(line);
}
static bool run(const std::wstring& root, wxl::Log logger) {
    log=logger; wxl::logger=logger;
    log("MLP PROBE begin; NO entry/DllMain/TLS callbacks; zero RWX; failure placeholders are probe-only");
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
