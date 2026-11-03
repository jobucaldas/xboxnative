// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device donor lifetime regression; does not load or execute a game.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>
static BOOL WINAPI hostProtect(void* p,SIZE_T n,DWORD flags,DWORD* old) {
    using Fn=BOOL(WINAPI*)(void*,SIZE_T,DWORD,DWORD*);
    return ((Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualProtect"))(p,n,flags,old);
}
static HMODULE WINAPI hostPackaged(LPCWSTR name,DWORD) {
    using Fn=HMODULE(WINAPI*)(LPCWSTR);
    return ((Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"))(name);
}
#define VirtualProtectFromApp hostProtect
#define LoadPackagedLibrary hostPackaged
#include "../host/wxl_mapper.h"
#include "../host/mlp_probe.h"
#include "../donors/probe.h"
#include "../host/play_runtime.h"
static void logline(const char* line) {puts(line);fflush(stdout);}
static DWORD WINAPI probeThread(void*) {return donor_pe::probe(logline)?0:1;}
// Inspect the actual mapped fixture's IAT, not resolver-call counts. Every
// donor slot must equal the successfully initialized production donor export.
static bool donorIat(wxl::Image& image) {
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto d=(IMAGE_IMPORT_DESCRIPTOR*)(image.base+dir.VirtualAddress);
    const std::map<std::string,unsigned> expected={{"hid.dll",1},{"setupapi.dll",2},
        {"version.dll",3},{"shcore.dll",1},{"avrt.dll",1}};
    std::map<std::string,unsigned> seen;
    for(;d->Name;++d) {
        std::string name=image.str(d->Name);
        if(!expected.count(name))continue;
        auto names=(IMAGE_THUNK_DATA64*)(image.base+d->OriginalFirstThunk);
        auto slots=(IMAGE_THUNK_DATA64*)(image.base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))return false;
            auto symbol=(IMAGE_IMPORT_BY_NAME*)(image.base+names->u1.AddressOfData);
            auto address=donor_pe::exported(name.c_str(),(const char*)symbol->Name,0);
            if(!address||slots->u1.Function!=address)return false;
            ++seen[name];
        }
    }
    return seen==expected;
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR args,int) {
    HANDLE thread=CreateThread(nullptr,0,probeThread,nullptr,0,nullptr);
    if(!thread || WaitForSingleObject(thread,30000)!=WAIT_OBJECT_0)return 1;
    DWORD code=1;
    if(!GetExitCodeThread(thread,&code) || code)return 2;
    CloseHandle(thread);
    auto address=donor_pe::exported("hid.dll","HidD_GetHidGuid",0);
    if(!address)return 3;
    GUID guid{};
    ((void(WINAPI*)(GUID*))address)(&guid);
    const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};
    if(memcmp(&guid,&expected,sizeof guid))return 4;
    // A later probe must not unload code still held by a retained guest.
    if(!donor_pe::probe(logline) || address!=donor_pe::exported("hid.dll","HidD_GetHidGuid",0))return 5;
    puts("DONOR-RUNTIME PASS: behavior gate, cross-thread publication, retained addresses; no game code");
    std::string mode=args;
    if(mode!="normal"&&mode!="missing"&&mode!="disabled")return 6;
    wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(!n||n>=32768)return 7;
    std::wstring root=path;root.resize(root.find_last_of(L"\\/"));
    mlp::donor_resolve=mode=="disabled"?nullptr:donor_pe::exported;
    if(!play_runtime::start(root,mode=="missing"?L"donor-missing.exe":L"donor-guest.exe",L"",logline))return 8;
    auto r=play_runtime::state;
    if(mode!="normal") {
        bool blocked=r->finished.load()&&!r->executed.load()&&!r->worker&&!r->success&&mlp::stub_names.empty();
        auto last=r->snapshot();
        blocked=blocked&&last.find(mode=="missing"?"hid.dll!DeliberatelyAbsentDonorExport":"hid.dll!HidD_GetHidGuid")!=std::string::npos;
        if(!blocked)return 9;
        printf("DONOR-GUEST PASS mode=%s: rejected before guest initialization; no placeholders\n",args);
        return 0;
    }
    if(!r->worker) {printf("DONOR-GUEST FAIL: %s\n",r->snapshot().c_str());return 10;}
    if(WaitForSingleObject(r->worker,30000)!=WAIT_OBJECT_0)ExitProcess(10);
    if(!r->finished.load()||!r->executed.load()||!r->success||r->code!=42||!mlp::stub_names.empty()||!donorIat(r->exe->image))return 11;
    if(address!=donor_pe::exported("hid.dll","HidD_GetHidGuid",0))return 12;
    puts("DONOR-GUEST PASS: actual Play runtime; 8 verified donor IAT slots; behavior exit=42; owned fixture only");
    return 0;
}
