#include "../../uwph/gdi32/route.h"
#include "../../uwph/opengl32/route.h"
#include "../../uwph/host/reg_builtin.h"
#pragma once
#include "../../uwph/xinput/route.h"
#include "../../uwph/user32/route.h"
#include "../../uwph/imm32/route.h"
#include "../../uwph/kernel32/route.h"
#include "../../uwph/sysx/native.h"
#include "../../uwph/dwmapi/route.h"
#include <map>
#include <set>
#include <mutex>
#include <array>
#include <utility>
#include "../../shared/kernel_tail.h"
#include "../../shared/kernel_tail_test.h"
#include "../../shared/kernel_tail_search_test.h"
#include "../../shared/pe_tail.h"
#include "../../shared/pe_tail_test.h"
#include "../../audio/probe.h"
#include "../../audio/routes.h"
#include "../../uwph/kernel32/desktop_policy.h"
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
static wxl::Resolver donor_resolve=nullptr;
// Execution callers reject unresolved addresses. Probe callers retain their
// existing placeholders and never enter guest code.
static uintptr_t (*missingResolver)(const char*,const char*,WORD) = nullptr;
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
// Games retry failed lookups every frame (IL2CPP P/Invoke, Unity plugin
// probing): on the console a flushed log line per attempt grew the probe log by
// megabytes a minute and stuttered play. Report each missing name once.
inline bool first_report(const std::string& line) {
    static std::mutex mutex;static std::set<std::string> seen;
    std::lock_guard<std::mutex> lock(mutex);
    return seen.size()<4096&&seen.insert(line).second;
}
// Unlisted DLLs default to the system loader (System32 and API sets only; a
// game's own DLLs are mapped before this resolver). Excluded: core API sets,
// which take the guarded kernel32 route below; and native UI libraries whose
// APIs take window or device-context handles, because guest HWNDs are user32
// records that native code must never receive. A DLL the console lacks still
// fails to load, exactly as before.
inline bool default_system_dll(const std::string& key) {
    static const char* const ui[] = {"uxtheme.dll","comctl32.dll","comdlg32.dll","msimg32.dll","glu32.dll",
        "ddraw.dll","oleacc.dll","dwrite.dll","d2d1.dll","gdiplus.dll","usp10.dll"};
    if(key.size()<5 || key.compare(key.size()-4,4,".dll")) return false;
    if(key.rfind("api-ms-win-core-",0)==0 || key.rfind("ext-ms-",0)==0) return false;
    for(auto name:ui) if(key==name) return false;
    return true;
}
static uintptr_t resolve(const char* dll, const char* name, WORD ordinal) {
    std::string key=dll; for(auto& c:key) if(c>='A' && c<='Z') c+=32;
    std::string symbol=name ? name : "#"+std::to_string(ordinal);
    const char* kind="unclassified";
    for(auto& route:routes) if(key==route.dll) kind=route.kind;
    // The Universal CRT API sets (api-ms-win-crt-*) are system contracts
    // served by the console's ucrtbase through the OS API-set schema.
    if(key.rfind("api-ms-win-crt-",0)==0) kind="system";
    // Any other DLL resolves from System32, as the Windows loader does, so a
    // new game needs no per-title table (default_system_dll).
    if(!strcmp(kind,"unclassified") && default_system_dll(key)) kind="system";
    uintptr_t address=kernel32_core::resolve(dll,name);
    // The console refuses HKCU writes (ERROR_ACCESS_DENIED): Unity PlayerPrefs and
    // other guests use the uwph registry (LocalState\registry.sqlite) instead.
    if(!address && name && key=="advapi32.dll") address=regb::lookup(name);
    if(!address && !strcmp(kind,"wine-pe")) {
        if(donor_resolve)address=donor_resolve(key.c_str(),name,ordinal);
        if(address)log(("DWPE ROUTE "+key+"!"+symbol).c_str());
    } else if(!address && !strcmp(kind,"system")) {
        auto it=systems.find(key);
        if(it==systems.end()) {
            std::wstring wide(key.begin(),key.end());
            // Documented loader search restricted to System32/API sets.
            HMODULE module=LoadLibraryExW(wide.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            it=systems.emplace(key,module).first;
            char msg[512]; snprintf(msg,sizeof msg,"MLP SYSTEM %s loaded=%d error=%lu",key.c_str(),!!module,module?0:GetLastError()); log(msg);
        }
        if(it->second) address=(uintptr_t)GetProcAddress(it->second,name ? name : MAKEINTRESOURCEA(ordinal));
    } else if(!address && !strcmp(kind,"uwph")) {
        auto it=assets.find(key);
        if(it!=assets.end()) address=asset_export(*it->second,name,ordinal);
        else if(name) for(auto& b:wxl::builtins) if(!_stricmp(dll,b.dll)&&!strcmp(name,b.name)) address=b.address;
    }
    if(auto audio=audio_routes::resolve(dll,name)) address=audio;
    if(auto input=xinput_core::resolve(dll,name,ordinal)) address=input;
    if(auto core=user32_core::resolve(dll,name)) address=core;
    if(!address) address=imm32_core::resolve(dll,name, reinterpret_cast<imm32_core::IsWindow>(
        user32_core::resolve("user32.dll","IsWindow")));
    // Override system DWM before a guest record HWND can reach desktop DWM.
    if(key=="dwmapi.dll" && name && !dwmapi_core::native_safe(name)) address=dwmapi_core::resolve(dll,name,
        reinterpret_cast<dwmapi_core::IsWindow>(user32_core::resolve("user32.dll","IsWindow")),
        reinterpret_cast<dwmapi_core::WindowRect>(user32_core::resolve("user32.dll","GetWindowRect")),
        reinterpret_cast<dwmapi_core::WindowLong>(user32_core::resolve("user32.dll","GetWindowLongPtrW")));
    // Desktop process policies and services (AppPolicy*, ktmw32): before the
    // system loader, whose answers describe the UWP host package.
    if(auto policy=desktop_policy::resolve(key.c_str(),name)) address=policy;
    if(!address) address=sysx_native::resolve(dll,name);
    // No guest OpenGL driver: Wine-equivalent no-context wgl answers.
    if(!address) address=opengl32_core::resolve(dll,name);
    if(!address && name && key=="gdi32.dll") {
        static HMODULE records=LoadPackagedLibrary(L"uwph-u32c.dll",0);
        static const auto caps=records?reinterpret_cast<gdi32_core::DeviceCaps>(
            GetProcAddress(records,"uwph_u32c_device_caps")):nullptr;
        static const auto metrics=records?reinterpret_cast<gdi32_core::Metrics>(
            GetProcAddress(records,"GetSystemMetrics")):nullptr;
        address=gdi32_core::resolve(dll,name,caps,metrics);
    }
    if(key=="kernel32.dll" && name && !address) {
        address=kernel_tail::resolve(name);
        log(("K32T ROUTE kernel32.dll!"+symbol+" found="+std::to_string(!!address)).c_str());
    }
    // Core API sets (api-ms-win-core-*) name kernel32/kernelbase functions for
    // the most part: take the same guarded kernel32 route (W^X memory, loader
    // wrappers, then native exports). Play's loader/process APIs were already
    // matched for these names before this probe. Sets hosted elsewhere (COM in
    // combase) resolve through the system loader's own API-set schema.
    bool coreSet=key.rfind("api-ms-win-core-",0)==0 && strcmp(kind,"system");
    if(coreSet && name && !address) {
        address=kernel_tail::resolve(name);
        if(!address) {
            static std::map<std::string,HMODULE> sets;
            auto it=sets.find(key);
            if(it==sets.end()) {
                std::wstring wide(key.begin(),key.end());
                it=sets.emplace(key,LoadLibraryExW(wide.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)).first;
            }
            if(it->second) address=(uintptr_t)GetProcAddress(it->second,name);
        }
        log(("K32T APISET "+key+"!"+symbol+" found="+std::to_string(!!address)).c_str());
    }
    if(!address&&name&&key!="kernel32.dll"&&strcmp(kind,"wine-pe")) {
        address=pe_tail::resolve(key.c_str(),name);
        if(address)log(("K32T SYSTEM "+key+"!"+symbol).c_str());
    }
    static bool tested=false;
    if(!tested) { tested=true; audio_routes::discover(log); audio_probe::run(log); kernel_tail::test(log); kernel_tail::test_search(log); pe_tail::discover(log); pe_tail::test(log); sysx_native::discover(log); }
    auto& sum=summaries[key];
    if(address) { ++sum.resolved; return address; }
    ++sum.stubbed;
    bool planned=!strcmp(kind,"stub");
    if(!planned && sum.first=="none") sum.first=symbol;
    std::string msg=std::string(planned?"MLP STUB ":"MLP MISSING ")+key+"!"+symbol+" route="+kind+(missingResolver?" execution=reject":" return=0 error=120 probe-only");
    if(first_report(msg)) log(msg.c_str());
    if(missingResolver) return missingResolver(dll,name,ordinal);
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
