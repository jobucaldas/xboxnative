// Link the real launcher path into the off-device runner without copying its
// PE32 routing or adapter callbacks. wWinMain stays uncalled; WinMain belongs to
// play-runner.cpp and drives this bridge with an owned fixture or game.
#include <windows.h>
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
#define PLAY_SLOT0_PROVIDER
#include "../main.cpp"

extern "C" int play_runner_pe32(const wchar_t* stateRoot,const wchar_t* gameRoot,const wchar_t* exe,unsigned long* exitCode) {
    if(!stateRoot||!gameRoot||!exe||!*exe||!exitCode)return 2;
    *exitCode=0;
    g_dir=stateRoot;
    g_lpf=_wfopen((g_dir+L"\\launcher-game-events.log").c_str(),L"wb");
    if(!g_lpf)return 2;
    regb::init(g_dir,lp_sink);
    LpCtx context;
    context.root=gameRoot;context.installRoot=gameRoot;context.cwd=gameRoot;
    wchar_t appid[32];DWORD appidLength=GetEnvironmentVariableW(L"PRUN_GAME_APPID",appid,32);
    if(appidLength&&appidLength<32)context.appid=uint32_t(_wtoi(appid));
    context.names.push_back(exe);context.gen=1;
    std::wstring path=std::wstring(gameRoot)+L"\\"+exe;
    lp_run_pe32(context,path,exe);
    const bool finished=g_launch.pe32Finished.load();
    if(finished)*exitCode=(unsigned long)g_launch.pe32ExitCode.load();
    fclose(g_lpf);g_lpf=nullptr;
    return finished?0:1;
}
