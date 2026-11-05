// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the real SWC caller with an owned logging-only guest, not graphics.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
static std::vector<std::string> messages,otherMessages;
static void test_log(const char* text) { messages.emplace_back(text); puts(text); }
static void other_log(const char* text) { otherMessages.emplace_back(text); }
static void logf(const char* format,...) {
    char text[512]; va_list args; va_start(args,format);
    vsnprintf(text,sizeof text,format,args); va_end(args); test_log(text);
}
// Only the unavailable CoreWindow/display setup is substituted. The production
// SWC run, mapper, imports, page checks and guest entry execute unchanged.
enum class CoreProcessEventsOption { ProcessAllIfPresent };
struct CoreWindow {
    static CoreWindow GetForCurrentThread() { return {}; }
    CoreWindow Dispatcher() { return {}; }
    void ProcessEvents(CoreProcessEventsOption) {}
};
static unsigned initCalls;
static void display_init(IUnknown*,void(*)(),LONG,LONG) { ++initCalls; }
static IUnknown* display_window(HWND) { return nullptr; }
static HMODULE fake_package(LPCWSTR,DWORD) { return (HMODULE)1; }
static FARPROC fake_proc(HMODULE,const char* name) {
    if (!strcmp(name,"uwph_display_init")) return (FARPROC)&display_init;
    if (!strcmp(name,"uwph_corewindow")) return (FARPROC)&display_window;
    return nullptr;
}
#define LoadPackagedLibrary fake_package
#define GetProcAddress fake_proc
#include "../swc_dxgi.h"
#undef GetProcAddress
#undef LoadPackagedLibrary
int main(int argc,char** argv) {
    if (argc!=3) return 2;
    wchar_t path[1024];
    if (!MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,1024)) return 2;
    bool nested=!strcmp(argv[2],"nested");
    {
        wxl::LogScope prior(nested?other_log:nullptr);
        swc::run(nullptr,path);
        if (wxl::logger!=(nested?other_log:nullptr)) return 1;
    }
    if (wxl::logger || initCalls!=1 || !otherMessages.empty()) return 1;
    if (std::count(messages.begin(),messages.end(),"SWC mapped-entry logging fixture")!=1 ||
        std::count(messages.begin(),messages.end(),"SWC RESULT entry=42 failures=0")!=1) return 1;
    for (const auto& message:messages) if (message.find("FAIL")!=std::string::npos) return 1;
    puts("SWC-LOGGING-PASS: real mapped-entry caller, isolated sink and restored prior scope; no graphics claim");
    return 0;
}
