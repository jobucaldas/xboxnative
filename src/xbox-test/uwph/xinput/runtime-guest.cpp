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
    return 42;
}
// Mapper requires relocatable images; retain one actual base relocation.
extern "C" void* input_relocation=reinterpret_cast<void*>(&input_entry);
