// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
// Off-device Wine adapter only; do not change production FromApp/W^X policy.
static BOOL WINAPI hostProtect(void* p,SIZE_T n,DWORD flags,DWORD* old) {
    using Fn=BOOL(WINAPI*)(void*,SIZE_T,DWORD,DWORD*);
    auto f=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualProtect");return f(p,n,flags,old);
}
static HMODULE WINAPI hostPackaged(LPCWSTR name,DWORD) {
    using Fn=HMODULE(WINAPI*)(LPCWSTR);
    auto f=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");return f(name);
}
#ifdef PROCESS_TEST_SENTINEL
// Test-only native lookup double: no process function can ever be called.
// Also force the module-name failure that formerly reached raw GetProcAddress.
static bool hideModuleName=false;
static unsigned processNativeLookups=0, fallbackLookups=0;
static int WINAPI sentinel() { return 99; }
static DWORD WINAPI moduleFilename(HMODULE h,LPWSTR p,DWORD n) {
    return hideModuleName?0:GetModuleFileNameW(h,p,n);
}
static FARPROC WINAPI nativeProc(HMODULE h,LPCSTR name) {
    if(!IS_INTRESOURCE(name)) {
        if(!strcmp(name,"CreateProcessA") || !strcmp(name,"CreateProcessW")) {
            ++processNativeLookups;return (FARPROC)&sentinel;
        }
        if(!strcmp(name,"GetModuleFileNameW"))return (FARPROC)&moduleFilename;
        if(!strcmp(name,"BoundarySentinel")) {++fallbackLookups;return (FARPROC)&sentinel;}
    }
    return GetProcAddress(h,name);
}
#define GetProcAddress nativeProc
#endif
#define VirtualProtectFromApp hostProtect
#define LoadPackagedLibrary hostPackaged
#include "../host/wxl_mapper.h"
#include "../host/mlp_probe.h"
#include "../host/play_runtime.h"
#undef GetProcAddress
static void logline(const char* text) { puts(text);fflush(stdout); }
static bool resolverTest(const std::wstring& root) {
    using namespace play_runtime;
    state=new State();state->root=root;state->log=logline;
    mlp::log=logline;wxl::logger=logline;mlp::missingResolver=missing;
    const char* dlls[]={"kernel32.dll","KeRnElBaSe.DlL",
        "api-ms-win-core-processthreads-l1-1-0.dll","API-MS-WIN-CORE-PROCESSTHREADS-L1-1-2.DLL"};
    const char* names[]={"CreateProcessA","CreateProcessW"};
    const char* progress="owned execution progress retained";
    for(auto dll:dlls)for(auto name:names) {
        state->event(progress);
        SetLastError(0);
        if(resolve(dll,name,0) || GetLastError()!=ERROR_PROC_NOT_FOUND ||
           state->snapshot()!=progress)return false;
        ++mappingImports;auto p=resolve(dll,name,0);--mappingImports;
        if(p || state->snapshot().find("unresolved static import:")==std::string::npos ||
           state->snapshot().find("no supported provider")==std::string::npos)return false;
    }
    HMODULE kernel=GetModuleHandleW(L"kernel32.dll");if(!kernel)return false;
    // A similarly named query is not evidence that an unrelated module has it.
    HMODULE unrelated=GetModuleHandleW(L"ntdll.dll");if(!unrelated)return false;
    state->event(progress);
    SetLastError(0);
    if(proc(unrelated,"GetProcAddress") || GetLastError()!=ERROR_PROC_NOT_FOUND)return false;
    for(auto name:names) {
        SetLastError(0);if(proc(kernel,name) || GetLastError()!=ERROR_PROC_NOT_FOUND)return false;
    }
    auto nested=(decltype(&proc))proc(kernel,"GetProcAddress");
    if(nested!=&proc)return false;
    for(auto name:names) {
        SetLastError(0);if(nested(kernel,name) || GetLastError()!=ERROR_PROC_NOT_FOUND)return false;
    }
#ifdef PROCESS_TEST_SENTINEL
    hideModuleName=true;
    SetLastError(0);
    if(proc(unrelated,"GetProcAddress") || GetLastError()!=ERROR_PROC_NOT_FOUND)return false;
    nested=(decltype(&proc))proc(kernel,"GetProcAddress");
    if(nested!=&proc)return false;
    for(auto name:names) {
        SetLastError(0);if(proc(kernel,name) || GetLastError()!=ERROR_PROC_NOT_FOUND)return false;
        SetLastError(0);if(nested(kernel,name) || GetLastError()!=ERROR_PROC_NOT_FOUND)return false;
    }
    // This control demonstrates that the failing metadata path and raw native
    // fallback are actually exercised, rather than trusting a wrapper list.
    if(proc(kernel,"BoundarySentinel")!=(FARPROC)&sentinel || fallbackLookups!=1)return false;
    hideModuleName=false;
#endif
    return state->snapshot()==progress &&
        processApi("CreateThread")==uintptr_t(&createThread) && !state->worker &&
        !state->executed && state->thread==0 && !state->success &&
        state->modules.empty() && mlp::stub_names.empty();
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR args,int) {
    using namespace play_runtime;
    wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))return 1;
    std::wstring root=path;root.resize(root.find_last_of(L"\\/"));
    std::string mode=args;bool ok=false;
    if(mode=="resolver")ok=resolverTest(root);
    else {
        auto exe=mode=="dynamic"?L"process-dynamic.exe":wide((mode+".exe").c_str());
        if(!start(root,exe,L"",logline))return 1;
        auto r=state;
        if(r->worker)WaitForSingleObject(r->worker,10000);
        if(mode=="dynamic")ok=r->finished && r->executed && r->success && r->code==42;
        else {
            auto last=r->snapshot();
            ok=r->finished && !r->executed && !r->success && !r->worker && r->thread==0 && r->code==0 &&
                last.find("unresolved static import:")!=std::string::npos &&
                last.find("guest initialization blocked")!=std::string::npos &&
                last.find("no supported provider")!=std::string::npos &&
                (last.find("!CreateProcessA")!=std::string::npos || last.find("!CreateProcessW")!=std::string::npos);
            for(auto& module:r->modules)ok=ok && !module->prepared && !module->initialized &&
                !module->image.tlsAttached && !module->image.attached;
        }
        // Retain the single-guest restriction; failure cannot enable a retry.
        ok=ok && !start(root,L"process-dynamic.exe",L"",logline) && state==r;
        ok=ok && mlp::stub_names.empty();
    }
#ifdef PROCESS_TEST_SENTINEL
    ok=ok && processNativeLookups==0;
    printf("PROCESS-BOUNDARY %s %s native-process-lookups=%u\n",mode.c_str(),ok?"PASS":"FAIL",processNativeLookups);
#else
    printf("PROCESS-BOUNDARY %s %s native-resolver\n",mode.c_str(),ok?"PASS":"FAIL");
#endif
    fflush(stdout);ExitProcess(ok?0:1);
}
