// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device donor lifetime regression; does not load or execute a game.
// msvc_load_config.h's std::min({...}) breaks under minwindef.h's min macro,
// so NOMINMAX must be set before windows.h is included.
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
// hidsdi.h and shellscalingapi.h declare their exports only for the desktop
// family; the runner is built --uwp (WINAPI_FAMILY=APP). Widen the family for
// these two headers so decltype derives the real SDK declarations, then
// restore the app family the rest of the translation unit compiles under.
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#include <shellscalingapi.h>
#include <hidsdi.h>
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_APP
#include <cstdio>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <type_traits>
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
    // Disposable process: even a failed negative assertion can mean a worker
    // unexpectedly exists. Never unwind CRT static state on a runner outcome.
    HANDLE thread=CreateThread(nullptr,0,probeThread,nullptr,0,nullptr);
    if(!thread || WaitForSingleObject(thread,30000)!=WAIT_OBJECT_0)ExitProcess(1);
    DWORD code=1;
    if(!GetExitCodeThread(thread,&code) || code)ExitProcess(2);
    CloseHandle(thread);
    auto address=donor_pe::exported("hid.dll","HidD_GetHidGuid",0);
    if(!address)ExitProcess(3);
    GUID guid{};
    // Typed from the real hidsdi.h declaration, not a hand-written form.
    using GetHidGuid=decltype(&HidD_GetHidGuid);
    ((GetHidGuid)address)(&guid);
    const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};
    if(memcmp(&guid,&expected,sizeof guid))ExitProcess(4);
    // A later probe must not unload code still held by a retained guest.
    if(!donor_pe::probe(logline) || address!=donor_pe::exported("hid.dll","HidD_GetHidGuid",0))ExitProcess(5);
    // Delay imports resolve through the donor router: shcore's user32 DPI
    // internals reach the record core, never host user32. Awareness set via
    // shcore must be visible in the record core's own state. Typed from the
    // real SDK headers (shellscalingapi.h; GetProcessDpiAwarenessInternal is
    // undocumented, so its own host declaration stands).
    using Dpi=decltype(&GetDpiForMonitor);using SetAware=decltype(&SetProcessDpiAwareness);
    using CoreAware=BOOL(WINAPI*)(HANDLE,int*);
    auto dpi=(Dpi)donor_pe::exported("shcore.dll","GetDpiForMonitor",0);
    auto setAware=(SetAware)donor_pe::exported("shcore.dll","SetProcessDpiAwareness",0);
    auto coreAware=(CoreAware)user32_core::resolve("user32.dll","GetProcessDpiAwarenessInternal");
    UINT dx=0,dy=0;int aware=-1;
    if(!dpi||!setAware||!coreAware||dpi(nullptr,MDT_EFFECTIVE_DPI,&dx,&dy)!=S_OK||dx!=96||dy!=96)ExitProcess(13);
    if(dpi(nullptr,MONITOR_DPI_TYPE(3),&dx,&dy)!=HRESULT_FROM_WIN32(ERROR_BAD_ARGUMENTS))ExitProcess(14);
    if(setAware(PROCESS_PER_MONITOR_DPI_AWARE)!=S_OK||!coreAware(nullptr,&aware)||aware!=2)ExitProcess(15);
    puts("DONOR-DELAY PASS: shcore delay-loaded user32 DPI internals resolve to the record core");
    // A guest's COM creation of DirectInput (CLSID_DirectInput8, as Puyo Puyo
    // Tetris 2 does) reaches the gated dinput8 donor: create, Initialize, then
    // the system keyboard device. Unrelated CLSIDs never reach a donor.
    {
        mlp::donor_resolve=donor_pe::exported;
        const GUID input8={0x25e609e4,0xb259,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
        const GUID input8W={0xbf798031,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
        const GUID keyboard={0x6f1d2b61,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
        // Vtable layout from the real dinput.h IDirectInput8W (IUnknown's
        // three, then CreateDevice, EnumDevices, GetDeviceStatus,
        // RunControlPanel, Initialize): the struct keeps the unused slots
        // between the methods it calls.
        struct Table {void* query;void* addRef;ULONG(WINAPI*release)(void*);
                      HRESULT(WINAPI*createDevice)(void*,const GUID&,void**,IUnknown*);
                      void* enumDevices;void* status;void* panel;
                      HRESULT(WINAPI*initialize)(void*,HINSTANCE,DWORD);};
        void* input=nullptr;
        if(play_runtime::coCreateInstance(input8,nullptr,CLSCTX_INPROC_SERVER,input8W,&input)!=S_OK||!input)ExitProcess(16);
        auto table=*(Table**)input;void* device=nullptr;
        if(table->initialize(input,GetModuleHandleW(nullptr),0x0800)!=S_OK)ExitProcess(17);
        if(table->createDevice(input,keyboard,&device,nullptr)!=S_OK||!device)ExitProcess(18);
        ((IUnknown*)device)->Release();table->release(input);
        const GUID other={0x12345678,0x1234,0x1234,{1,2,3,4,5,6,7,8}};void* none=(void*)1;
        if(SUCCEEDED(play_runtime::coCreateInstance(other,nullptr,CLSCTX_INPROC_SERVER,input8W,&none)))ExitProcess(19);
        mlp::donor_resolve=nullptr;
        puts("DONOR-COM PASS: CoCreateInstance(CLSID_DirectInput8) served by the dinput8 donor; keyboard device created");
    }
    puts("DONOR-RUNTIME PASS: behavior gate, cross-thread publication, retained addresses; no game code");
    // The d3d10 shim's load route (Bugbot r4559176265, host-side): a by-name
    // LoadLibraryExW("d3dcompiler_47.dll") through play_runtime::loadExW must
    // return the probed donor's image via the bridge, and GetProcAddress on
    // that handle must reach donor_pe::from_handle — the exact pair launch.h
    // wires. The compile itself is proven by the d3d10-compile fixture; here
    // only the routing is, without a guest.
    {
        HMODULE before=GetModuleHandleW(L"d3dcompiler_47.dll");   // not loaded in this process
        (void)before;
        play_runtime::set_donor_bridge(donor_pe::module,donor_pe::from_handle);
        // No state yet: loadExW needs a started runtime. The bridge answers
        // through the same code path only once state exists, so start a
        // rejected launch first (the missing-donor guest never runs code) and
        // route against that live state.
        wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);
        if(!n||n>=32768)ExitProcess(7);
        std::wstring root=path;root.resize(root.find_last_of(L"\\/"));
        mlp::donor_resolve=donor_pe::exported;
        if(!play_runtime::start(root,L"donor-missing.exe",L"",logline))ExitProcess(8);
        auto r=play_runtime::state;
        // A launch rejected at map() never creates a worker: start() mapped the
        // exe on the calling thread, logged "mapping failed" and finished there.
        // Only a launch that got as far as CreateThread has a handle to wait on.
        if(r->worker&&WaitForSingleObject(r->worker,30000)!=WAIT_OBJECT_0)ExitProcess(10);
        if(!r->finished.load()||r->executed.load())ExitProcess(11);
        HMODULE donor=play_runtime::loadExW(L"d3dcompiler_47.dll",nullptr,0);
        if(!donor)ExitProcess(20);
        if(donor!=(HMODULE)donor_pe::module(L"d3dcompiler_47.dll"))ExitProcess(21);
        // Typed from the real d3dcompiler.h pD3DCompile (Bugbot r4559946114:
        // a hand-typed 10-argument form dropped Flags2 and shifted the two
        // out-params). static_assert keeps the arity honest if the SDK header
        // ever changes under it.
        using Compile=decltype(&D3DCompile);
        using Sig=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,
            ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
        static_assert(std::is_same_v<Compile,Sig>,
            "D3DCompile signature drifted from d3dcompiler.h");
        auto compile=(Sig)play_runtime::proc(donor,"D3DCompile");
        if(!compile)ExitProcess(22);
        static const char src[]="float4 main():SV_Target { return float4(1,0,0,1); }";
        ID3DBlob* blob=nullptr;ID3DBlob* errors=nullptr;
        HRESULT hr=compile(src,sizeof src-1,nullptr,nullptr,nullptr,"main","ps_4_0",0,0,&blob,&errors);
        if(errors)errors->Release();
        if(FAILED(hr)||!blob)ExitProcess(23);
        const unsigned char* bits=static_cast<const unsigned char*>(blob->GetBufferPointer());
        SIZE_T size=blob->GetBufferSize();
        if(size<4||bits[0]!='D'||bits[1]!='X'||bits[2]!='B'||bits[3]!='C')ExitProcess(24);
        blob->Release();
        // An unbridged name must still answer nothing: the gate stays
        // authoritative for names the probed set does not hold.
        if(play_runtime::loadExW(L"deliberately-absent-donor.dll",nullptr,0))ExitProcess(25);
        puts("DONOR-BRIDGE PASS: loadExW(d3dcompiler_47) returns the probed donor image; proc(D3DCompile) compiles a real DXBC");
        fflush(stdout);
        // The remaining guest stages below start their own launches; the
        // rejected guest ran no code, so discard it (also resets the bridge —
        // the next launch re-wires it, exactly as launch.h does).
        if(!play_runtime::discard_unstarted())ExitProcess(26);
        mlp::donor_resolve=nullptr;
    }
    std::string mode=args;
    if(mode!="normal"&&mode!="missing"&&mode!="disabled")ExitProcess(6);
    wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if(!n||n>=32768)ExitProcess(7);
    std::wstring root=path;root.resize(root.find_last_of(L"\\/"));
    mlp::donor_resolve=mode=="disabled"?nullptr:donor_pe::exported;
    if(!play_runtime::start(root,mode=="missing"?L"donor-missing.exe":L"donor-guest.exe",L"",logline))ExitProcess(8);
    auto r=play_runtime::state;
    if(mode!="normal") {
        bool blocked=r->finished.load()&&!r->executed.load()&&!r->worker&&!r->success&&mlp::stub_names.empty();
        auto last=r->snapshot();
        blocked=blocked&&last.find(mode=="missing"?"hid.dll!DeliberatelyAbsentDonorExport":"hid.dll!HidD_GetHidGuid")!=std::string::npos;
        if(!blocked)ExitProcess(9);
        printf("DONOR-GUEST PASS mode=%s: rejected before guest initialization; no placeholders\n",args);
        fflush(stdout);ExitProcess(0);
    }
    if(!r->worker) {printf("DONOR-GUEST FAIL: %s\n",r->snapshot().c_str());ExitProcess(10);}
    if(WaitForSingleObject(r->worker,30000)!=WAIT_OBJECT_0)ExitProcess(10);
    if(!r->finished.load()||!r->executed.load()||!r->success||r->code!=42||!mlp::stub_names.empty()||!donorIat(r->exe->image))ExitProcess(11);
    if(address!=donor_pe::exported("hid.dll","HidD_GetHidGuid",0))ExitProcess(12);
    puts("DONOR-GUEST PASS: actual Play runtime; 8 verified donor IAT slots; behavior exit=42; owned fixture only");
    fflush(stdout);ExitProcess(0);
}
