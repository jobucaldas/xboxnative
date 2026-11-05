// Single-CoreWindow host for a separate mingw test PE. Logs LocalState/w32u.log.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11_1.h>
#include <xinput.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Storage.h>

#include "gamepad.h"

using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;

// ---------------------------------------------------------------- logging
static FILE* g_log = nullptr;
static LARGE_INTEGER g_t0, g_freq;
static double now_s() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart); }
static void logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    OutputDebugStringA(buf); OutputDebugStringA("\n");
    if (g_log) { fprintf(g_log, "[%8.3f] %s\n", now_s(), buf); fflush(g_log); }
}
static void open_log() {
    QueryPerformanceFrequency(&g_freq); QueryPerformanceCounter(&g_t0);
    try {
        std::wstring dir = Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
        g_log = _wfopen((dir + L"\\w32u.log").c_str(), L"wb");
    } catch (...) {}
}

static void test_log(const char* s) { logf("%s", s); }
static CoreWindow g_window{nullptr};
static void pump_events() { g_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent); }
static HMODULE driver;
static HRESULT (*present_guest)();
static void (*event_sink)(UINT,WPARAM,LPARAM);
static void (*pad_sink)(DWORD,const XINPUT_GAMEPAD*);
static void poll_pads() {
    using namespace Windows::Gaming::Input;
    auto pads=Gamepad::Gamepads();
    for(DWORD i=0;i<4;++i) {
        if(i>=pads.Size()){pad_sink(i,nullptr);continue;}
        auto p=convert_pad(pads.GetAt(i).GetCurrentReading());pad_sink(i,&p);
    }
}
static void guest_pump(){pump_events();if(pad_sink)poll_pads();}
struct PadRead { DWORD (WINAPI* get)(DWORD,XINPUT_STATE*); XINPUT_STATE state; DWORD status; };
static DWORD WINAPI read_pad_worker(void* arg) {
    auto p=static_cast<PadRead*>(arg);p->status=p->get(3,&p->state);return 0;
}
static bool test_pads() {
    using namespace Windows::Gaming::Input;
    auto get=(DWORD(WINAPI*)(DWORD,XINPUT_STATE*))GetProcAddress(driver,"XInputGetState");
    if(!get)return false;
    // Deliberately combine dissimilar WGI/XInput bit positions and signed extrema.
    GamepadReading r{};r.Buttons=GamepadButtons::A|GamepadButtons::Menu|GamepadButtons::DPadLeft|GamepadButtons::RightThumbstick;
    r.LeftTrigger=1;r.RightTrigger=0.5;r.LeftThumbstickX=-1;r.LeftThumbstickY=1;r.RightThumbstickX=-0.5;r.RightThumbstickY=0.5;
    auto p=convert_pad(r);
    if(p.wButtons!=(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_RIGHT_THUMB)||p.bLeftTrigger!=255||p.bRightTrigger!=127||p.sThumbLX!=-32768||p.sThumbLY!=32767||p.sThumbRX!=-16384||p.sThumbRY!=16383)return false;
    r.LeftTrigger=-1;r.RightTrigger=2;r.LeftThumbstickX=-2;r.LeftThumbstickY=2;r.RightThumbstickX=NAN;r.RightThumbstickY=INFINITY;
    auto clamped=convert_pad(r);
    if(clamped.bLeftTrigger||clamped.bRightTrigger!=255||clamped.sThumbLX!=-32768||clamped.sThumbLY!=32767||clamped.sThumbRX||clamped.sThumbRY)return false;
    XINPUT_STATE state{},unchanged{};
    pad_sink(3,nullptr);
    if(get(3,&state)!=ERROR_DEVICE_NOT_CONNECTED||get(4,&state)!=ERROR_BAD_ARGUMENTS||get(3,nullptr)!=ERROR_BAD_ARGUMENTS)return false;
    pad_sink(3,&p);if(get(3,&state)!=ERROR_SUCCESS)return false;
    pad_sink(3,&p);if(get(3,&unchanged)!=ERROR_SUCCESS||unchanged.dwPacketNumber!=state.dwPacketNumber)return false;
    p.bLeftTrigger=0;pad_sink(3,&p);
    if(get(3,&unchanged)!=ERROR_SUCCESS||unchanged.dwPacketNumber!=state.dwPacketNumber+1)return false;
    PadRead read{get,{},ERROR_GEN_FAILURE};
    HANDLE worker=CreateThread(nullptr,0,read_pad_worker,&read,0,nullptr);
    if(!worker)return false;
    WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
    if(read.status!=ERROR_SUCCESS||read.state.dwPacketNumber!=unchanged.dwPacketNumber||memcmp(&read.state.Gamepad,&p,sizeof(p)))return false;
    pad_sink(3,nullptr);if(get(3,&state)!=ERROR_DEVICE_NOT_CONNECTED)return false;
    pad_sink(3,&p);if(get(3,&state)!=ERROR_SUCCESS||state.dwPacketNumber!=unchanged.dwPacketNumber+1)return false;
    pad_sink(3,nullptr);
    logf("PASS synthetic WGI conversion, clamp, XInput packets/reconnect and worker polling (no physical input proof)");
    return true;
}
static void run_window() {
    driver=LoadPackagedLibrary(L"uwph-user32.dll",0);
    auto guest=LoadPackagedLibrary(L"window-test.dll",0);
    if(!driver||!guest){logf("FAIL load driver/test err=%lu",GetLastError());return;}
    auto init=(void(*)(::IUnknown*,void(*)(),LONG,LONG))GetProcAddress(driver,"uwph_display_init");
    event_sink=(void(*)(UINT,WPARAM,LPARAM))GetProcAddress(driver,"uwph_display_event");
    pad_sink=(void(*)(DWORD,const XINPUT_GAMEPAD*))GetProcAddress(driver,"uwph_display_pad");
    auto run=(int(*)(void(*)(const char*)))GetProcAddress(guest,"run_window_test");
    auto resolve=(FARPROC(*)(LPCWSTR,LPCSTR))GetProcAddress(driver,"uwph_resolve_import");
    if(!resolve){logf("FAIL resolver missing");return;}
    struct Check {const wchar_t* dll;const char* symbol;};
    const Check checks[]={{L"d3d11.dll","D3D11CreateDevice"},{L"dxgi.dll","CreateDXGIFactory1"},
      {L"ws2_32.dll","WSAStartup"},{L"bcrypt.dll","BCryptGenRandom"},{L"ole32.dll","CoCreateGuid"},
      {L"version.dll","GetFileVersionInfoSizeW"}};
    for(auto c:checks){if(!resolve(c.dll,c.symbol)){logf("FAIL native route %ls err=%lu",c.dll,GetLastError());return;}logf("PASS native route %ls!%s",c.dll,c.symbol);}
    for(auto c:checks){
      auto first=resolve(c.dll,c.symbol);
      if(resolve(c.dll,"uwph_deliberately_absent_export")||GetLastError()!=ERROR_PROC_NOT_FOUND||resolve(c.dll,c.symbol)!=first){logf("FAIL native module cache %ls",c.dll);return;}
    }
    logf("PASS native cache retains exports across failed lookups");
    for(auto name:{L"user32.dll",L"win32u.dll",L"kernel32.dll",L"ntdll.dll"}){
      if(resolve(name,"missing")||GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED){logf("FAIL builtin boundary %ls",name);return;}
      logf("PASS builtin boundary %ls: native fallback denied",name);
    }
    if(!init||!event_sink||!pad_sink||!run){logf("FAIL exports");return;}
    auto b=g_window.Bounds();init((::IUnknown*)get_abi(g_window),guest_pump,(LONG)b.Width,(LONG)b.Height);
    if(!test_pads()){logf("W32U RESULT failures=1 (synthetic pad checks)");return;}
    logf("W32U RESULT failures=%d",run(test_log));
    present_guest=(HRESULT(*)())GetProcAddress(guest,"present_window_test");
}

// CoreWindow event source; all drawing is performed by the mingw test PE.
struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{nullptr};
    bool m_closed = false;

    IFrameworkView CreateView() { return *this; }
    void Initialize(CoreApplicationView const& v) {
        open_log();
        logf("boot: Initialize (pid=%lu)", GetCurrentProcessId());
        v.Activated([](auto&&, IActivatedEventArgs const& a) {
            logf("Activated kind=%d (activation args ignored)", (int)a.Kind());
            CoreWindow::GetForCurrentThread().Activate();
        });
    }
    void SetWindow(CoreWindow const& w) {
        m_window = w; g_window=w;
        w.SizeChanged([](auto&&, WindowSizeChangedEventArgs const& a){if(event_sink)event_sink(WM_SIZE,SIZE_RESTORED,MAKELPARAM((LONG)a.Size().Width,(LONG)a.Size().Height));});
        w.Activated([](auto&&, WindowActivatedEventArgs const& a){if(event_sink)event_sink(WM_ACTIVATE,a.WindowActivationState()==CoreWindowActivationState::Deactivated?WA_INACTIVE:WA_ACTIVE,0);});
        w.KeyDown([](auto&&, KeyEventArgs const& a){if(event_sink)event_sink(WM_KEYDOWN,(WPARAM)a.VirtualKey(),0);});
        w.KeyUp([](auto&&, KeyEventArgs const& a){if(event_sink)event_sink(WM_KEYUP,(WPARAM)a.VirtualKey(),0);});
        w.PointerMoved([](auto&&, PointerEventArgs const& a){auto p=a.CurrentPoint().Position();if(event_sink)event_sink(WM_MOUSEMOVE,0,MAKELPARAM((LONG)p.X,(LONG)p.Y));});
        w.PointerPressed([](auto&&, auto&&){if(event_sink)event_sink(WM_LBUTTONDOWN,MK_LBUTTON,0);});
        w.PointerReleased([](auto&&, auto&&){if(event_sink)event_sink(WM_LBUTTONUP,0,0);});
        w.Closed([this](auto&&, auto&&) { m_closed = true; logf("window closed"); });
        logf("SetWindow bounds=%.0fx%.0f", w.Bounds().Width, w.Bounds().Height);
    }
    void Load(hstring const&) {}
    void Uninitialize() { logf("Uninitialize"); }

    void Run() {
        logf("Run: entering"); m_window.Activate();
        run_window();
        // Keep the last guest green frame available for Device Portal capture.
        while(!m_closed){guest_pump();if(present_guest){auto hr=present_guest();if(FAILED(hr)){logf("hold Present failed 0x%08lx",hr);present_guest=nullptr;}}else Sleep(16);}
        return;

    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment(apartment_type::multi_threaded);
    CoreApplication::Run(make<App>());
    return 0;
}
