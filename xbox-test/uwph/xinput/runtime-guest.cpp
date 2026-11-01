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
    module=LoadLibraryW(L"dinput8.dll");if(!module)return 15;
    using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,void*);
    auto create=reinterpret_cast<Create>(GetProcAddress(module,"DirectInput8Create"));
    if(!create)return 16;
    const GUID input8a={0xbf798030,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};
    void* object=reinterpret_cast<void*>(1);
    if(create(GetModuleHandleW(nullptr),0x800,input8a,&object,nullptr)!=E_NOTIMPL || object)return 17;
    if(!FreeLibrary(module))return 18;
    UINT size=123;SetLastError(0);
    if(GetRawInputData(nullptr,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=UINT(-1) ||
        GetLastError()!=ERROR_INVALID_HANDLE || size!=123)return 19;
    return 42;
}
// Mapper requires relocatable images; retain one actual base relocation.
extern "C" void* input_relocation=reinterpret_cast<void*>(&input_entry);
