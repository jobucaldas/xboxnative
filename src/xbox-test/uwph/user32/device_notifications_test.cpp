#include <windows.h>
#include <dbt.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstdint>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
int main() {
    HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
    auto reg=reinterpret_cast<HDEVNOTIFY(WINAPI*)(HANDLE,void*,DWORD)>(GetProcAddress(module,"RegisterDeviceNotificationW"));
    auto unreg=reinterpret_cast<BOOL(WINAPI*)(HDEVNOTIFY)>(GetProcAddress(module,"UnregisterDeviceNotification"));
    CHECK(reg && unreg);
    // Compare invalid recipients and flags against real host Wine USER32.
    unsigned count=0;
    for (auto recipient : {uintptr_t(0),uintptr_t(1),uintptr_t(0x12345678)})
        for (DWORD flags : {0u,2u,4u,8u,0xffffffffu}) {
            SetLastError(0xdead); auto expected=RegisterDeviceNotificationW((HANDLE)recipient,nullptr,flags); DWORD error=GetLastError();
            SetLastError(0xdead); CHECK(reg((HANDLE)recipient,nullptr,flags)==expected); CHECK(GetLastError()==error); ++count;
        }
    auto registerClass=reinterpret_cast<ATOM(WINAPI*)(const WNDCLASSEXW*)>(GetProcAddress(module,"RegisterClassExW"));
    auto createWindow=reinterpret_cast<decltype(&CreateWindowExW)>(GetProcAddress(module,"CreateWindowExW"));
    auto destroyWindow=reinterpret_cast<decltype(&DestroyWindow)>(GetProcAddress(module,"DestroyWindow"));
    auto defaultProc=reinterpret_cast<WNDPROC>(GetProcAddress(module,"DefWindowProcW"));
    CHECK(registerClass && createWindow && destroyWindow && defaultProc);
    WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.lpfnWndProc=defaultProc; cls.lpszClassName=L"notify-contract";
    CHECK(registerClass(&cls));
    HWND window=createWindow(0,cls.lpszClassName,L"",0,0,0,100,100,nullptr,nullptr,nullptr,nullptr); CHECK(window);
    DEV_BROADCAST_DEVICEINTERFACE_W iface{}; iface.dbcc_size=sizeof(iface); iface.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;
    for (DWORD flags : {0u,4u}) {
        SetLastError(0xdead); CHECK(!reg(window,&iface,flags)); CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);
    }
    CHECK(destroyWindow(window));
    SetLastError(0xdead); CHECK(!reg(window,&iface,0)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    // Service recipients avoid guest HWND validation. Supported filter shapes
    // must fail explicitly until a notification delivery backend is installed.
    for (DWORD type : {0u,1u,2u,3u,4u,5u,6u,7u,0xffffffffu}) {
        DEV_BROADCAST_HDR filter{sizeof(filter),type,0};
        SetLastError(0xdead); CHECK(!reg((HANDLE)1,&filter,1));
        CHECK(GetLastError()==((type==5 || type==6)?ERROR_CALL_NOT_IMPLEMENTED:ERROR_INVALID_DATA)); ++count;
    }
    SetLastError(0xdead); CHECK(!reg((HANDLE)1,nullptr,5)); CHECK(GetLastError()==ERROR_CALL_NOT_IMPLEMENTED);
    SetLastError(0xdead); CHECK(!UnregisterDeviceNotification(nullptr)); DWORD error=GetLastError();
    SetLastError(0xdead); CHECK(!unreg(nullptr)); CHECK(GetLastError()==error);
    for (auto token : {uintptr_t(1),uintptr_t(0x12345678),UINTPTR_MAX}) {
        SetLastError(0xdead); CHECK(!unreg((HDEVNOTIFY)token)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    }
    std::printf("PASS: %u notification validation cases, explicit unsupported registration, safe invalid unregister\n",count);
}
