// Stands in for a Unity plugin such as XInputDotNet's XInputInterface64.dll: it
// imports only kernel32 and reaches XInput by LoadLibrary at run time, trying
// xinput1_4, 1_3 then 9_1_0 as the original does.
#include <windows.h>
#include <xinput.h>
extern "C" __declspec(dllexport) DWORD WINAPI plugin_get_state(DWORD index,XINPUT_STATE* state) {
    const char* names[]={"xinput1_4.dll","xinput1_3.dll","xinput9_1_0.dll"};
    HMODULE module=nullptr;
    for(auto name:names)if((module=LoadLibraryA(name)))break;
    if(!module)return 0xfffffff0;
    auto get=reinterpret_cast<DWORD(WINAPI*)(DWORD,XINPUT_STATE*)>(GetProcAddress(module,"XInputGetState"));
    return get?get(index,state):0xfffffff1;
}
extern "C" BOOL WINAPI plugin_entry(HINSTANCE,DWORD,LPVOID) {return TRUE;}
// The mapper requires a real base relocation.
extern "C" __declspec(dllexport) void* plugin_relocation=reinterpret_cast<void*>(&plugin_entry);
