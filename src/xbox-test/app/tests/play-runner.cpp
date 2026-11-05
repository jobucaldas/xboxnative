#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <new>
// Wine 10 lacks VirtualProtectFromApp; this host-only alias preserves W^X.
static BOOL WINAPI hostProtect(void* p,SIZE_T n,DWORD flags,DWORD* old) {
    using Fn=BOOL(WINAPI*)(void*,SIZE_T,DWORD,DWORD*);
    auto f=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualProtect");return f(p,n,flags,old);
}
static HMODULE WINAPI hostPackaged(LPCWSTR name,DWORD) {
    using Fn=HMODULE(WINAPI*)(LPCWSTR);
    auto f=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");return f(name);
}
// Observe ownership from a different native thread at native resolver calls.
// This is deterministic contention, not a sleep-based vector crash race.
static bool probeLoaderLock=false, hideModuleFilename=false;
static unsigned loaderLockChecks=0, loaderLockFailures=0;
static void observeLoaderLock();
static DWORD WINAPI hostFilename(HMODULE h,LPWSTR out,DWORD size) {
    if(probeLoaderLock)observeLoaderLock();
    return hideModuleFilename?0:GetModuleFileNameW(h,out,size);
}
static FARPROC WINAPI hostProc(HMODULE h,LPCSTR name) {
    if(!IS_INTRESOURCE(name)) {
        if(!strcmp(name,"GetModuleFileNameW"))return (FARPROC)&hostFilename;
        if(probeLoaderLock)observeLoaderLock();
    }
    return GetProcAddress(h,name);
}
#define GetProcAddress hostProc
#define VirtualProtectFromApp hostProtect
#define LoadPackagedLibrary hostPackaged
#include "../host/wxl_mapper.h"
#include "../host/mlp_probe.h"
#include "../donors/probe.h"
#include "../host/play_runtime.h"
#include "../launch_arguments.h"
// OFF-DEVICE stand-in for the application's CoreWindow (game mode only). Play's
// unchanged factory adapter still validates every guest HWND; only its final
// CreateSwapChainForCoreWindow step presents to a native Wine window instead.
namespace host_surface {
static HWND window=nullptr;
struct Token final : IUnknown {
    LONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override {return InterlockedDecrement(&refs);}
};
static Token token;
static HRESULT present(IDXGIFactory2* factory,IUnknown* device,IUnknown*,const DXGI_SWAP_CHAIN_DESC1* desc,IDXGIOutput* output,IDXGISwapChain1** chain) {
    return factory->CreateSwapChainForHwnd(device,window,desc,nullptr,output,chain);
}
// The runner is a UWP build: desktop user32 is reached by name, with its
// Win64 types declared here (WNDCLASSW, MSG and the constants used).
using WndProc=LRESULT(CALLBACK*)(HWND,UINT,WPARAM,LPARAM);
struct WndClass {UINT style;WndProc proc;int classExtra,windowExtra;HINSTANCE instance;HANDLE icon,cursor,background;LPCWSTR menu,name;};
struct HostMsg {HWND hwnd;UINT message;WPARAM wParam;LPARAM lParam;DWORD time;POINT pt;};
static_assert(sizeof(WndClass)==72&&sizeof(HostMsg)==48,"Win64 user32 layouts");
constexpr DWORD overlappedVisible=0x00CF0000|0x10000000; // WS_OVERLAPPEDWINDOW|WS_VISIBLE
constexpr DWORD allInput=0x04FF; // QS_ALLINPUT
template<class F> static F user(const char* name) {
    static HMODULE module=LoadLibraryW(L"user32.dll");
    return module?(F)GetProcAddress(module,name):nullptr;
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    return user<WndProc>("DefWindowProcW")(h,m,w,l);
}
static bool create() {
    auto registerClass=user<ATOM(WINAPI*)(const WndClass*)>("RegisterClassW");
    auto createWindow=user<HWND(WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HANDLE,HINSTANCE,void*)>("CreateWindowExW");
    if(!registerClass||!createWindow)return false;
    WndClass cls{};cls.proc=proc;cls.instance=GetModuleHandleW(nullptr);cls.name=L"play-host-surface";
    if(!registerClass(&cls))return false;
    window=createWindow(0,cls.name,L"Play off-device surface",overlappedVisible,0,0,1280,720,nullptr,nullptr,cls.instance,nullptr);
    return window!=nullptr;
}
// Optional synthetic gamepad (PRUN_GAME_PAD="a@45,start@60"): pad 0 is
// connected idle, then each button is pressed for 200 ms at its second. It
// feeds the same xinput_core slots the launcher publishes from real pads.
// Every press is logged as SYNTHETIC; without the variable nothing changes.
struct PadPress {WORD buttons;ULONGLONG at;bool pressed=false,released=false;};
static std::vector<PadPress> presses;
static void (*padLog)(const char*)=nullptr;
static void schedulePad(void (*log)(const char*)) {
    wchar_t value[512];DWORD n=GetEnvironmentVariableW(L"PRUN_GAME_PAD",value,512);
    if(!n||n>=512)return;
    padLog=log;std::wstring text(value,n);
    static const std::pair<const wchar_t*,WORD> names[]={{L"a",XINPUT_GAMEPAD_A},{L"b",XINPUT_GAMEPAD_B},
        {L"x",XINPUT_GAMEPAD_X},{L"y",XINPUT_GAMEPAD_Y},{L"start",XINPUT_GAMEPAD_START},{L"back",XINPUT_GAMEPAD_BACK},
        {L"up",XINPUT_GAMEPAD_DPAD_UP},{L"down",XINPUT_GAMEPAD_DPAD_DOWN},{L"left",XINPUT_GAMEPAD_DPAD_LEFT},
        {L"right",XINPUT_GAMEPAD_DPAD_RIGHT}};
    for(size_t pos=0;pos<text.size();) {
        size_t comma=text.find(L',',pos);if(comma==std::wstring::npos)comma=text.size();
        auto item=text.substr(pos,comma-pos);pos=comma+1;
        auto at=item.find(L'@');if(at==std::wstring::npos)continue;
        auto button=item.substr(0,at);
        for(auto& name:names)if(button==name.first)presses.push_back({name.second,ULONGLONG(_wtoi(item.c_str()+at+1))*1000});
    }
    if(presses.empty())return;
    XINPUT_GAMEPAD idle{};xinput_core::publish(0,&idle);
    char line[96];snprintf(line,sizeof line,"SYNTHETIC PAD connected idle; %zu scheduled presses",presses.size());log(line);
}
static void drivePad(ULONGLONG elapsed) {
    for(auto& p:presses) {
        XINPUT_GAMEPAD pad{};
        if(!p.pressed&&elapsed>=p.at) {
            pad.wButtons=p.buttons;xinput_core::publish(0,&pad);p.pressed=true;
            char line[96];snprintf(line,sizeof line,"SYNTHETIC PAD press buttons=0x%04x at %llu ms",p.buttons,elapsed);padLog(line);
        } else if(p.pressed&&!p.released&&elapsed>=p.at+200) {
            xinput_core::publish(0,&pad);p.released=true;padLog("SYNTHETIC PAD release");
        }
    }
}
// Pump the native surface window while the guest worker runs.
static void wait(HANDLE worker,DWORD ms) {
    auto msgWait=user<DWORD(WINAPI*)(DWORD,const HANDLE*,BOOL,DWORD,DWORD)>("MsgWaitForMultipleObjects");
    auto peek=user<BOOL(WINAPI*)(HostMsg*,HWND,UINT,UINT,UINT)>("PeekMessageW");
    auto dispatch=user<LRESULT(WINAPI*)(const HostMsg*)>("DispatchMessageW");
    ULONGLONG start=GetTickCount64(),end=start+ms;
    for(;;) {
        ULONGLONG now=GetTickCount64();if(now>=end)return;
        drivePad(now-start);
        DWORD left=DWORD(std::min<ULONGLONG>(end-now,100));
        if(msgWait(worker?1:0,worker?&worker:nullptr,FALSE,left,allInput)==WAIT_OBJECT_0&&worker)return;
        HostMsg m;while(peek(&m,nullptr,0,0,1/*PM_REMOVE*/))dispatch(&m);
    }
}
}
#include "../launch_status.h"
#undef GetProcAddress
static DWORD WINAPI tryLoaderLock(void*) {
    auto& mutex=play_runtime::state->loaderMutex;
    if(!mutex.try_lock())return ERROR_BUSY;
    mutex.unlock();return 0;
}
static void observeLoaderLock() {
    ++loaderLockChecks;
    HANDLE thread=CreateThread(nullptr,0,tryLoaderLock,nullptr,0,nullptr);
    DWORD code=0;
    bool held=thread && WaitForSingleObject(thread,10000)==WAIT_OBJECT_0 &&
        GetExitCodeThread(thread,&code) && code==ERROR_BUSY;
    if(thread)CloseHandle(thread);
    if(!held)++loaderLockFailures;
}
static void* guardedModule=nullptr;
static unsigned registryReads=0;
static LONG CALLBACK registryRead(EXCEPTION_POINTERS* ep) {
    auto record=ep->ExceptionRecord;
    if(record->ExceptionCode!=0x80000001 || record->NumberParameters<2 ||
       record->ExceptionInformation[1]<(uintptr_t)guardedModule ||
       record->ExceptionInformation[1]-(uintptr_t)guardedModule>=sizeof(play_runtime::Module))return EXCEPTION_CONTINUE_SEARCH;
    // PAGE_GUARD clears on the first metadata read. Check lock ownership at
    // find(), before any native resolver callback, then retry that same read.
    ++registryReads;observeLoaderLock();return EXCEPTION_CONTINUE_EXECUTION;
}
static bool loaderLockTest() {
    auto kernel=GetModuleHandleW(L"kernel32.dll");
    auto add=(PVOID(WINAPI*)(ULONG,PVECTORED_EXCEPTION_HANDLER))GetProcAddress(kernel,"AddVectoredExceptionHandler");
    auto remove=(ULONG(WINAPI*)(PVOID))GetProcAddress(kernel,"RemoveVectoredExceptionHandler");
    if(!add||!remove)return false;
    // A dedicated RW-only metadata page makes the registry's first read
    // observable without a timing race or executable test stub.
    guardedModule=VirtualAlloc(nullptr,sizeof(play_runtime::Module),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!guardedModule)return false;
    auto module=new(guardedModule) play_runtime::Module();
    auto observer=add(1,registryRead);
    if(!observer){module->~Module();VirtualFree(guardedModule,0,MEM_RELEASE);return false;}
    {std::lock_guard<std::recursive_mutex> lock(play_runtime::state->loaderMutex);
     play_runtime::state->modules.emplace_back(module);}
    DWORD old=0;
    bool guarded=VirtualProtect(guardedModule,sizeof(*module),PAGE_READWRITE|PAGE_GUARD,&old)!=FALSE;
    probeLoaderLock=true;
    bool ok=play_runtime::proc(kernel,"InitializeCriticalSectionEx")!=nullptr;
    ok=play_runtime::proc(kernel,"GetProcAddress")==FARPROC(&play_runtime::proc)&&ok;
    hideModuleFilename=true;
    ok=play_runtime::proc(kernel,"InitializeCriticalSectionEx")!=nullptr&&ok;
    hideModuleFilename=false;probeLoaderLock=false;
    remove(observer);
    // Clear any unconsumed guard before destroying test-only metadata.
    VirtualProtect(guardedModule,sizeof(*module),PAGE_READWRITE,&old);
    {std::lock_guard<std::recursive_mutex> lock(play_runtime::state->loaderMutex);
     play_runtime::state->modules.back().release();play_runtime::state->modules.pop_back();}
    module->~Module();VirtualFree(guardedModule,0,MEM_RELEASE);guardedModule=nullptr;
    printf("PLAY-LOADER-LOCK registry-reads=%u checks=%u unlocked=%u\n",registryReads,loaderLockChecks,loaderLockFailures);fflush(stdout);
    return ok && guarded && registryReads==1 && loaderLockChecks>=4 && !loaderLockFailures;
}
// Pause after the owned DllMain has returned FALSE but before the runtime
// publishes initializationFailed. Events and try_lock discriminate ownership;
// no scheduler delay and no rejected/uninitialized export invocation is used.
static HANDLE attachReached=nullptr, attachRelease=nullptr, lookupAttempted=nullptr, lookupDone=nullptr;
static HMODULE attachingModule=nullptr, attachResult=nullptr;
static FARPROC attachExport=nullptr;
static DWORD attachError=0, lookupError=0;
static bool attachWaitOK=false, lookupOwned=false, checkStartupLock=false;
static DWORD WINAPI rejectingLoad(void*) {
    attachResult=play_runtime::loadW(L"play-reject.dll");attachError=GetLastError();return 0;
}
static DWORD WINAPI lookupDuringAttach(void*) {
    auto& mutex=play_runtime::state->loaderMutex;
    lookupOwned=mutex.try_lock();if(lookupOwned)mutex.unlock();
    SetEvent(lookupAttempted);
    attachExport=play_runtime::proc(attachingModule,"dependency_check");lookupError=GetLastError();
    SetEvent(lookupDone);return 0;
}
static bool attachPublicationTest() {
    attachReached=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    attachRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    lookupAttempted=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    lookupDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!attachReached||!attachRelease||!lookupAttempted||!lookupDone)return false;
    HANDLE loader=CreateThread(nullptr,0,rejectingLoad,nullptr,0,nullptr), lookup=nullptr;
    bool reached=loader&&WaitForSingleObject(attachReached,10000)==WAIT_OBJECT_0;
    bool attempted=false, completed=false;
    if(reached) {
        lookup=CreateThread(nullptr,0,lookupDuringAttach,nullptr,0,nullptr);
        attempted=lookup&&WaitForSingleObject(lookupAttempted,10000)==WAIT_OBJECT_0;
        // The old unlocked path must finish its lookup BEFORE attach resumes.
        // The fixed path owns the mutex: release attach so proc can acquire it.
        if(attempted&&lookupOwned)completed=WaitForSingleObject(lookupDone,10000)==WAIT_OBJECT_0;
    }
    SetEvent(attachRelease);
    bool loaded=loader&&WaitForSingleObject(loader,10000)==WAIT_OBJECT_0;
    if(lookup)completed=WaitForSingleObject(lookupDone,10000)==WAIT_OBJECT_0&&WaitForSingleObject(lookup,10000)==WAIT_OBJECT_0;
    auto module=loaded?play_runtime::named(L"play-reject.dll"):nullptr;
    bool ok=reached&&attempted&&completed&&loaded&&attachWaitOK&&!lookupOwned&&
        !attachExport&&lookupError==ERROR_PROC_NOT_FOUND&&!attachResult&&attachError==ERROR_DLL_INIT_FAILED&&
        module&&module->initializationFailed&&!module->initializing&&!module->initialized&&
        module->image.tlsAttached&&!module->image.attached;
    printf("PLAY-ATTACH-PUBLICATION reached=%d attempted=%d unlocked=%d escaped=%d lookup-error=%lu load-error=%lu\n",
        int(reached),int(attempted),int(lookupOwned),int(attachExport!=nullptr),lookupError,attachError);fflush(stdout);
    if(lookup)CloseHandle(lookup);if(loader)CloseHandle(loader);
    for(auto h:{attachReached,attachRelease,lookupAttempted,lookupDone})CloseHandle(h);
    attachReached=attachRelease=lookupAttempted=lookupDone=nullptr;return ok;
}
static FILE* hostEvents=nullptr;
static bool injectLogException=false;
static void handledLogException() {
    __try {RaiseException(0x40010006,0,0,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void logline(const char* s){
    if(injectLogException)handledLogException();puts(s);fflush(stdout);
    if(hostEvents){fprintf(hostEvents,"%s\n",s);fflush(hostEvents);}
    if(checkStartupLock&&!strcmp(s,"PRUN EVENT DllMain PROCESS_ATTACH play-dependency.dll"))observeLoaderLock();
    if(attachReached&&!strcmp(s,"PRUN EVENT DllMain rejected PROCESS_ATTACH: play-reject.dll")) {
        attachingModule=(HMODULE)play_runtime::named(L"play-reject.dll")->image.base;
        SetEvent(attachReached);attachWaitOK=WaitForSingleObject(attachRelease,10000)==WAIT_OBJECT_0;
    }
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR args,int){
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);std::wstring root=path;root.resize(root.find_last_of(L"\\/"));
    bool game=std::string(args)=="game";
    injectLogException=std::string(args)=="observer-log";
    checkStartupLock=std::string(args)=="startup-lock";
    if(game) {
        hostEvents=_wfopen((root+L"\\host-game-events.log").c_str(),L"wb");
        if(!hostEvents)return 2;
        wchar_t gameRoot[32768];DWORD n=GetEnvironmentVariableW(L"PRUN_GAME_ROOT",gameRoot,32768);
        if(!n||n>=32768)return 2;
        root.assign(gameRoot,n);
        logline("OFF-DEVICE Wine diagnostic only; no console or Steam client success claim");
        // Same donor gate and routing as the launcher (launch.h): source-built
        // Wine donors serve hid/setupapi/version/shcore/avrt imports.
        if(!donor_pe::probe(logline)) {logline("DWPE FAIL donor behavior gate; no game code executed");return 3;}
        mlp::donor_resolve=donor_pe::exported;
        if(!host_surface::create()) {logline("HOST SURFACE FAIL native Wine window unavailable");return 3;}
        play_graphics::configure(&host_surface::token,host_surface::present,logline);
        host_surface::schedulePad(logline);
        logline("HOST SURFACE native Wine window stands in for the CoreWindow (off-device only)");
    }
    if(!play_runtime::start(root,game?L"Megabonk.exe":std::string(args)=="missing"?L"play-missing.exe":std::string(args)=="failed-dependency"?L"play-failed-dependency.exe":std::string(args)=="static-steam"?L"play-steam.exe":L"play-guest.exe",game?play_runtime::wide(launch_arguments::for_app(3405340)):play_runtime::wide(args),logline))return 1;
    auto r=play_runtime::state;
    // Inject an already reached graphics milestone, then exercise a terminal
    // guest stop (fatal dialog). Progress must never mask that failure.
    if(std::string(args)=="dialog-milestone")r->milestone=1;
    if(game) {
        wchar_t seconds[16];DWORD n=GetEnvironmentVariableW(L"PRUN_GAME_SECONDS",seconds,16);
        DWORD limit=n&&n<16?DWORD(_wtoi(seconds)):10;
        host_surface::wait(r->worker,limit*1000);
    } else if(r->worker)WaitForSingleObject(r->worker,2000);
    auto last=r->snapshot();char summary[8192];snprintf(summary,sizeof summary,"PLAY-TEST executed=%d finished=%d success=%d code=%lu last=%s",int(r->executed),int(r->finished),r->success,r->code,last.c_str());
    logline(summary);
    std::string mode=args;
    bool ok=r->executed;
    if(game)ok=ok&&r->finished;
    else if(mode=="observer-log")ok=ok&&r->finished&&r->success&&r->code==42&&r->firstChanceCount.load()>0&&r->firstChanceCode.load()==0x40010006&&r->firstChanceAddress.load()!=0;
    else if(mode=="steam-forward"||mode=="steam-api"||mode=="steam-client")
        ok=ok&&r->finished&&r->success&&r->code==42&&!play_runtime::named(L"steam_api64.dll")&&!play_runtime::named(L"steamclient64.dll");
    else if(mode=="root-steam") {
        auto api=play_runtime::named(L"steam_api64.dll");
        ok=ok&&r->finished&&r->success&&r->code==42&&api&&api->mapped&&api->initialized&&!play_runtime::named(L"steamclient64.dll");
    }
    else if(mode=="dialog-milestone")ok=ok&&r->finished&&!r->success&&last.find("user32.dll!MessageBoxA caption=\"Fatal error\"")!=std::string::npos&&
        r->milestone==1&&launch_status::observe(r->finished,r->success,r->milestone,true)==launch_status::Outcome::failed;
    else if(mode=="missing")ok=!r->executed&&r->finished&&!r->success&&!r->worker&&mlp::stub_names.empty()&&last.find("mapping failed: play-missing.exe; guest initialization blocked")!=std::string::npos&&last.find("play-missing.dll!DeliberatelyMissingPlayImport")!=std::string::npos;
    else if(mode=="failed-dependency")ok=!r->executed&&r->finished&&!r->success&&!r->worker&&
        last.find("mapping failed: play-failed-dependency.exe; guest initialization blocked")!=std::string::npos&&
        last.find("unresolved static import: baselib.dll!dependency_check")!=std::string::npos;
    else if(mode=="static-steam")ok=!r->executed&&r->finished&&!r->success&&!r->worker&&mlp::stub_names.empty()&&last.find("steam_api64.dll!SteamAPI_Init")!=std::string::npos;
    else if(mode=="loader-lock")ok=ok&&r->finished&&r->success&&r->code==42&&loaderLockTest();
    else if(mode=="startup-lock") {
        printf("PLAY-STARTUP-LOCK checks=%u unlocked=%u\n",loaderLockChecks,loaderLockFailures);fflush(stdout);
        ok=ok&&r->finished&&r->success&&r->code==42&&loaderLockChecks==1&&!loaderLockFailures;
    }
    else if(mode=="attach-publication")ok=ok&&r->finished&&r->success&&r->code==42&&attachPublicationTest();
    else if(mode.find("pending-")==0) {
        bool reject=mode.find("pending-reject-")==0;
        auto target=play_runtime::named(reject?L"play-pending-reject.dll":L"play-pending-target.dll");
        printf("PLAY-PENDING-LIFECYCLE prepared=%d initialized=%d rejected=%d tls=%d attached=%d\n",
            target&&target->prepared,target&&target->initialized,target&&target->initializationFailed,
            target&&target->image.tlsAttached,target&&target->image.attached);fflush(stdout);
        ok=ok&&r->finished&&r->success&&r->code==42&&target&&target->prepared&&!target->initializing&&
            target->image.tlsAttached&&target->initialized==!reject&&target->image.attached==!reject&&
            target->initializationFailed==reject;
    }
    else if(mode.find("reject-export-")==0) {
        auto rejected=play_runtime::named(L"play-reject.dll");
        ok=ok&&r->finished&&r->success&&r->code==42&&rejected&&rejected->initializationFailed&&
            !rejected->initializing&&!rejected->initialized&&rejected->image.tlsAttached&&!rejected->image.attached;
    }
    else if(mode=="reject-mapping") {
        auto failed=play_runtime::named(L"play-unresolved.dll");
        ok=ok&&r->finished&&r->success&&r->code==42&&failed&&failed->mappingFailed&&!failed->prepared&&!failed->initialized;
    }
    else if(mode=="wide-long") {
        std::string expected;for(int i=0;i<2047;++i)expected+="\xc3\xa7";
        ok=ok&&r->finished&&!r->success&&last.find("user32.dll!MessageBoxW")!=std::string::npos&&last.find(expected+"<truncated>\"")!=std::string::npos&&last.find("\xef\xbf\xbd")==std::string::npos;
    }
    else if(mode=="wide-dialog")ok=ok&&r->finished&&!r->success&&last.find("user32.dll!MessageBoxW caption=\"Erro \xc3\xa7\"")!=std::string::npos&&last.find("Falha IL2CPP \xc3\xa7\\nprobe \\\"quoted\\\"")!=std::string::npos&&last.find("caller=0x")!=std::string::npos;
    else if(mode=="dialog")ok=ok&&r->finished&&!r->success&&last.find("user32.dll!MessageBoxA caption=\"Fatal error\"")!=std::string::npos&&last.find("Failed to initialize IL2CPP\\nprobe \\\"quoted\\\"")!=std::string::npos&&last.find("caller=0x")!=std::string::npos;
    else if(mode=="crash")ok=ok&&r->finished&&!r->success&&last.find("unhandled exception code=0xe0424242")!=std::string::npos;
    else if(mode=="exit")ok=ok&&r->finished&&r->success&&r->code==37;
    else if(mode=="hang")ok=ok&&!r->finished&&last.find("EXE entry play-guest.exe")!=std::string::npos;
    else ok=ok&&r->finished&&r->success&&r->code==42;
    // The runner is a disposable off-device process, like launcher restart.
    ExitProcess(ok?0:1);
}
