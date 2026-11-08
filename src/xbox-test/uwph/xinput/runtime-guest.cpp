#include <windows.h>
#include <xinput.h>
extern "C" DWORD WINAPI input_entry() {
    HMODULE module=LoadLibraryW(L"xinput1_4.dll");if(!module)return 1;
    if(GetModuleHandleW(L"XINPUT1_4.DLL")!=module)return 2;
    auto state=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(module,"XInputGetState"));
    if(!state)return 3;
    XINPUT_STATE value{};
    if(state(0,&value)!=0 || value.Gamepad.wButtons!=XINPUT_GAMEPAD_A || value.Gamepad.sThumbLY!=-32768)return 4;
    if(value.dwPacketNumber!=1 || state(1,&value)!=ERROR_DEVICE_NOT_CONNECTED)return 5;
    SetLastError(0);
    if(GetProcAddress(module,"XInputGetKeystroke") || GetLastError()!=ERROR_PROC_NOT_FOUND)return 6;
    HMODULE second=nullptr;
    if(!GetModuleHandleExW(0,L"xinput1_4.dll",&second) || second!=module)return 7;
    if(!FreeLibrary(second) || state(0,&value)!=0)return 8;
    WCHAR name[128];SetLastError(0);
    if(GetModuleFileNameW(module,name,128) || GetLastError()!=ERROR_NOT_SUPPORTED)return 9;
    if(!FreeLibrary(module))return 10;
    if(GetProcAddress(module,"XInputGetState") || GetLastError()!=ERROR_INVALID_HANDLE)return 11;
    module=LoadLibraryA("xinput1_3");if(!module)return 12;
    state=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(module,"XInputGetState"));
    if(!state || state(0,&value)!=0 || value.Gamepad.wButtons!=XINPUT_GAMEPAD_A)return 13;
    if(!FreeLibrary(module))return 14;
    // Name without extension, as InControl/Unity spell it.
    module=LoadLibraryA("xinput1_4");if(!module || GetModuleHandleA("xinput1_4")!=module || GetModuleHandleA("XINPUT1_4.DLL")!=module)return 15;
    if(!FreeLibrary(module))return 16;
    // Static import: resolved by the loader to Play's module, never the console's.
    value={};
    if(XInputGetState(0,&value)!=0 || value.Gamepad.wButtons!=XINPUT_GAMEPAD_A)return 17;
    // A Unity plugin, loaded the way Mono does (full path, mixed slashes), reaches
    // XInput through its own LoadLibrary and must get the same emulated pad.
    WCHAR directory[MAX_PATH];DWORD length=GetModuleFileNameW(nullptr,directory,MAX_PATH);
    if(!length || length>=MAX_PATH)return 18;
    while(length && directory[length-1]!=L'\\' && directory[length-1]!=L'/')--length;
    directory[length]=0;
    WCHAR path[MAX_PATH*2];lstrcpyW(path,directory);lstrcatW(path,L"Plugins/x86_64\\play-xinput-plugin.dll");
    HMODULE plugin=LoadLibraryW(path);if(!plugin)return 19;
    auto pluginState=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(plugin,"plugin_get_state"));
    value={};
    if(!pluginState || pluginState(0,&value)!=0 || value.Gamepad.wButtons!=XINPUT_GAMEPAD_A)return 20;
    if(pluginState(1,&value)!=ERROR_DEVICE_NOT_CONNECTED)return 21;
    if(!FreeLibrary(plugin))return 22;
    // Mono/Unity may pass search-path flags with the full path; they only steer lookup.
    plugin=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!plugin)return 23;
    if(!FreeLibrary(plugin))return 24;
    return 42;
}
// Mapper requires relocatable images; retain one actual base relocation.
extern "C" void* input_relocation=reinterpret_cast<void*>(&input_entry);
