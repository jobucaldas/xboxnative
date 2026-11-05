#pragma once
// Historical research tail, retained as an explicit allowlist, not a claim that
// the regex classification proves portability. Resolve real OS exports only.
#include <map>
#include <mutex>
namespace pe_tail {
struct Symbol {const char* dll;const char* name;};
static const Symbol symbols[]={
    {"kernel32.dll","Thread32First"},
    {"kernel32.dll","Thread32Next"},
    {"kernel32.dll","SetThreadAffinityMask"},
    {"kernel32.dll","CreateWaitableTimerA"},
    {"kernel32.dll","VerifyVersionInfoW"},
    {"kernel32.dll","GetComputerNameW"},
    {"kernel32.dll","CreateToolhelp32Snapshot"},
    {"kernel32.dll","GetSystemPowerStatus"},
    {"kernel32.dll","GetStartupInfoA"},
    {"kernel32.dll","SetDllDirectoryW"},
    {"user32.dll","UnregisterDeviceNotification"},
    {"user32.dll","PtInRect"},
    {"user32.dll","GetWindowLongA"},
    {"user32.dll","RegisterDeviceNotificationW"},
    {"user32.dll","ScreenToClient"},
    {"user32.dll","GetSystemMetrics"},
    {"user32.dll","OffsetRect"},
    {"user32.dll","CopyRect"},
    {"user32.dll","AllowSetForegroundWindow"},
    {"user32.dll","GetDesktopWindow"},
    {"user32.dll","GetParent"},
    {"user32.dll","IsIconic"},
    {"user32.dll","MonitorFromRect"},
    {"user32.dll","IsWindowVisible"},
    {"user32.dll","GetMonitorInfoA"},
    {"user32.dll","DefWindowProcW"},
    {"user32.dll","GetWindowLongPtrW"},
    {"user32.dll","ClientToScreen"},
    {"user32.dll","MonitorFromWindow"},
    {"user32.dll","GetMonitorInfoW"},
    {"shlwapi.dll","SHDeleteKeyW"},
    {"shlwapi.dll","PathCanonicalizeW"},
    {"setupapi.dll","SetupDiEnumDeviceInterfaces"},
    {"setupapi.dll","SetupDiGetClassDevsA"},
    {"setupapi.dll","SetupDiGetDeviceInterfaceDetailW"},
    {"setupapi.dll","SetupDiEnumDeviceInfo"},
    {"setupapi.dll","SetupDiDestroyDeviceInfoList"},
    {"gdi32.dll","ChoosePixelFormat"},
    {"gdi32.dll","SetPixelFormat"},
    {"gdi32.dll","SwapBuffers"},
    {"shell32.dll","SHGetKnownFolderPath"},
    {"shell32.dll","ShellExecuteW"},
    {"shell32.dll","SHGetFolderPathW"},
    {"shell32.dll","CommandLineToArgvW"},
    {"opengl32.dll","wglGetProcAddress"},
    {"opengl32.dll","wglDeleteContext"},
    {"opengl32.dll","wglMakeCurrent"},
    {"opengl32.dll","wglGetCurrentDC"},
    {"opengl32.dll","wglCreateContext"},
    {"opengl32.dll","wglGetCurrentContext"},
    {"winmm.dll","waveInGetDevCapsW"},
    {"winmm.dll","waveInGetNumDevs"},
    {"winmm.dll","waveOutGetPosition"},
    {"winmm.dll","waveOutUnprepareHeader"},
    {"winmm.dll","waveOutPrepareHeader"},
    {"winmm.dll","timeBeginPeriod"},
    {"winmm.dll","timeEndPeriod"},
    {"winmm.dll","waveInPrepareHeader"},
    {"winmm.dll","waveInStart"},
    {"winmm.dll","waveInUnprepareHeader"},
    {"winmm.dll","waveInAddBuffer"},
    {"winmm.dll","waveInGetDevCapsA"},
    {"winmm.dll","waveOutGetDevCapsW"},
    {"winmm.dll","waveOutGetDevCapsA"},
    {"winmm.dll","waveOutGetNumDevs"},
    {"winmm.dll","timeGetTime"},
    {"imm32.dll","ImmReleaseContext"},
    {"imm32.dll","ImmGetContext"},
    {"hid.dll","HidP_GetData"},
    {"hid.dll","HidP_MaxDataListLength"},
    {"hid.dll","HidD_GetSerialNumberString"},
    {"hid.dll","HidD_GetManufacturerString"},
    {"hid.dll","HidD_GetProductString"},
    {"hid.dll","HidD_FreePreparsedData"},
    {"hid.dll","HidD_GetPreparsedData"},
    {"hid.dll","HidD_GetHidGuid"},
    {"hid.dll","HidP_SetUsages"},
    {"hid.dll","HidP_SetUsageValue"},
    {"hid.dll","HidP_GetButtonCaps"},
    {"hid.dll","HidP_GetValueCaps"},
    {"hid.dll","HidD_GetAttributes"},
    {"hid.dll","HidP_GetCaps"},
};
inline uintptr_t resolve(const char* dll,const char* name) {
    // These historical PE_PURE labels are false portability claims. Native
    // HWND/IME/GL operations cannot consume our CoreWindow record handles.
    if(!strcmp(dll,"user32.dll")&&strcmp(name,"PtInRect")&&strcmp(name,"CopyRect")&&strcmp(name,"OffsetRect"))return 0;
    if(!strcmp(dll,"gdi32.dll")||!strcmp(dll,"imm32.dll")||!strcmp(dll,"opengl32.dll"))return 0;
    for(auto& s:symbols)if(!strcmp(dll,s.dll)&&!strcmp(name,s.name)) {
        static std::map<std::string,HMODULE> modules;static std::mutex mutex;std::lock_guard<std::mutex> lock(mutex);
        auto it=modules.find(dll);
        if(it==modules.end()) {std::string key=dll;std::wstring wide(key.begin(),key.end());it=modules.emplace(key,LoadLibraryExW(wide.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)).first;}
        return it->second?(uintptr_t)GetProcAddress(it->second,name):0;
    }
    return 0;
}
inline void discover(void (*log)(const char*)) {
    for(auto& s:symbols) {char line[256];auto p=resolve(s.dll,s.name);snprintf(line,sizeof line,"K32T TAIL %s!%s system-route=%u",s.dll,s.name,unsigned(!!p));log(line);}
}
}
