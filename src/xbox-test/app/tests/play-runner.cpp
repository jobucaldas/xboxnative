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
#include "../host/play_runtime.h"
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
    }
    if(!play_runtime::start(root,game?L"Megabonk.exe":std::string(args)=="missing"?L"play-missing.exe":std::string(args)=="static-steam"?L"play-steam.exe":L"play-guest.exe",game?L"-force-d3d11":play_runtime::wide(args),logline))return 1;
    auto r=play_runtime::state;
    // Inject an already reached graphics milestone, then exercise the real
    // Steam prerequisite failure. Progress must never mask that terminal stop.
    if(std::string(args)=="steam-api-milestone")r->milestone=1;
    if(r->worker)WaitForSingleObject(r->worker,game?10000:2000);
    auto last=r->snapshot();char summary[8192];snprintf(summary,sizeof summary,"PLAY-TEST executed=%d finished=%d success=%d code=%lu last=%s",int(r->executed),int(r->finished),r->success,r->code,last.c_str());
    logline(summary);
    std::string mode=args;
    bool ok=r->executed;
    if(game)ok=ok&&r->finished;
    else if(mode=="observer-log")ok=ok&&r->finished&&r->success&&r->code==42&&r->firstChanceCount.load()>0&&r->firstChanceCode.load()==0x40010006&&r->firstChanceAddress.load()!=0;
    else if(mode=="steam-forward")ok=ok&&r->finished&&!r->success&&last.find("steam_api64.dll!SteamAPI_Init")!=std::string::npos&&last.find("caller=0x")!=std::string::npos;
    else if(mode=="steam-api"||mode=="steam-client"||mode=="steam-api-milestone") {
        ok=ok&&r->finished&&!r->success&&last.find(mode=="steam-client"?"steamclient64.dll!LoadLibrary (real client prerequisite item 36)":"steam_api64.dll!LoadLibrary (real client prerequisite item 36)")!=std::string::npos&&last.find("caller=0x")!=std::string::npos;
        if(mode=="steam-api-milestone")ok=ok&&r->milestone==1&&launch_status::observe(r->finished,r->success,r->milestone,true)==launch_status::Outcome::failed;
    }
    else if(mode=="missing")ok=!r->executed&&r->finished&&!r->success&&!r->worker&&mlp::stub_names.empty()&&last.find("mapping failed: play-missing.exe; guest initialization blocked")!=std::string::npos&&last.find("play-missing.dll!DeliberatelyMissingPlayImport")!=std::string::npos;
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
