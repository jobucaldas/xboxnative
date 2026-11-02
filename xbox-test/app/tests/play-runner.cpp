#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
// Wine 10 lacks VirtualProtectFromApp; this host-only alias preserves W^X.
static BOOL WINAPI hostProtect(void* p,SIZE_T n,DWORD flags,DWORD* old) {
    using Fn=BOOL(WINAPI*)(void*,SIZE_T,DWORD,DWORD*);
    auto f=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualProtect");return f(p,n,flags,old);
}
static HMODULE WINAPI hostPackaged(LPCWSTR name,DWORD) {
    using Fn=HMODULE(WINAPI*)(LPCWSTR);
    auto f=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");return f(name);
}
#define VirtualProtectFromApp hostProtect
#define LoadPackagedLibrary hostPackaged
#include "../host/wxl_mapper.h"
#include "../host/mlp_probe.h"
#include "../host/play_runtime.h"
static FILE* hostEvents=nullptr;
static bool injectLogException=false;
static void handledLogException() {
    __try {RaiseException(0x40010006,0,0,nullptr);}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
static void logline(const char* s){if(injectLogException)handledLogException();puts(s);fflush(stdout);if(hostEvents){fprintf(hostEvents,"%s\n",s);fflush(hostEvents);}}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR args,int){
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);std::wstring root=path;root.resize(root.find_last_of(L"\\/"));
    bool game=std::string(args)=="game";
    injectLogException=std::string(args)=="observer-log";
    if(game) {
        hostEvents=_wfopen((root+L"\\host-game-events.log").c_str(),L"wb");
        if(!hostEvents)return 2;
        wchar_t gameRoot[32768];DWORD n=GetEnvironmentVariableW(L"PRUN_GAME_ROOT",gameRoot,32768);
        if(!n||n>=32768)return 2;
        root.assign(gameRoot,n);
        logline("OFF-DEVICE Wine diagnostic only; no console or Steam client success claim");
    }
    if(!play_runtime::start(root,game?L"Megabonk.exe":std::string(args)=="missing"?L"play-missing.exe":std::string(args)=="static-steam"?L"play-steam.exe":L"play-guest.exe",game?L"-force-d3d11":play_runtime::wide(args),logline))return 1;
    auto r=play_runtime::state;if(r->worker)WaitForSingleObject(r->worker,game?10000:2000);
    auto last=r->snapshot();char summary[8192];snprintf(summary,sizeof summary,"PLAY-TEST executed=%d finished=%d success=%d code=%lu last=%s",int(r->executed),int(r->finished),r->success,r->code,last.c_str());
    logline(summary);
    std::string mode=args;
    bool ok=r->executed;
    if(game)ok=ok&&r->finished;
    else if(mode=="observer-log")ok=ok&&r->finished&&r->success&&r->code==42&&r->firstChanceCount.load()>0&&r->firstChanceCode.load()==0x40010006&&r->firstChanceAddress.load()!=0;
    else if(mode=="steam-forward")ok=ok&&r->finished&&!r->success&&last.find("steam_api64.dll!SteamAPI_Init")!=std::string::npos&&last.find("caller=0x")!=std::string::npos;
    else if(mode=="steam-api"||mode=="steam-client")ok=ok&&r->finished&&!r->success&&last.find(mode=="steam-api"?"steam_api64.dll!LoadLibrary (real client prerequisite item 36)":"steamclient64.dll!LoadLibrary (real client prerequisite item 36)")!=std::string::npos&&last.find("caller=0x")!=std::string::npos;
    else if(mode=="missing")ok=!r->executed&&r->finished&&!r->success&&!r->worker&&r->missing.empty()&&last.find("mapping failed: play-missing.exe; guest initialization blocked")!=std::string::npos&&last.find("play-missing.dll!DeliberatelyMissingPlayImport")!=std::string::npos;
    else if(mode=="static-steam")ok=!r->executed&&r->finished&&!r->success&&!r->worker&&r->missing.empty()&&last.find("steam_api64.dll!SteamAPI_Init")!=std::string::npos;
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
