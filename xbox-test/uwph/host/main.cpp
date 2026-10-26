// UWPH kernel primitive harness: separate host/test PE DLLs inside a UWP process.
// Logs LocalState/k32.log and keeps a CoreWindow+D3D heartbeat alive.
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11_1.h>
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
        g_log = _wfopen((dir + L"\\k32.log").c_str(), L"wb");
    } catch (...) {}
}

#include "cs32_probe.h"
#include "backend.h"
#include "wxl_mapper.h"
#include "mlp_probe.h"
#ifdef WXL_TLS_LANE
#include "tls_test_runner.h"
extern "C" __declspec(dllimport) DWORD carrier_anchor_a();
extern "C" __declspec(dllimport) DWORD carrier_anchor_b();
#endif
static void test_log(const char* s) { logf("%s", s); }
static void run_k32() {
    HMODULE backend = LoadPackagedLibrary(L"uwph-host.dll", 0);
    HMODULE guest = LoadPackagedLibrary(L"kernel-test.dll", 0);
    if (!backend || !guest) { logf("K32 LOAD FAILED err=%lu", GetLastError()); return; }
    auto handle = (uint64_t*)GetProcAddress(backend, "__wine_unixlib_handle");
    auto dispatch = (Dispatch*)GetProcAddress(backend, "__wine_unix_call_dispatcher");
    using Run = int (*)(const wchar_t*, void (*)(const char*), uint64_t, Dispatch, Dispatch);
    auto wine10 = (Dispatch)GetProcAddress(backend, "uwph_wine10_unix_dispatcher");
    auto run = (Run)GetProcAddress(guest, "run_kernel_tests");
    if (!handle || !dispatch || !run || !wine10) { logf("K32 EXPORT FAILED err=%lu", GetLastError()); return; }
    logf("loaded separate host and test PE DLLs, ABI v1");
    auto dir = Windows::Storage::ApplicationData::Current().LocalFolder().Path();
    int failures = run(dir.c_str(), test_log, *handle, *dispatch, wine10);
    logf("K32 RESULT failures=%d", failures);
    FreeLibrary(guest); FreeLibrary(backend);
}

static void run_hid() {
    HMODULE backend = LoadPackagedLibrary(L"uwph-hid.dll", 0);
    HMODULE guest = LoadPackagedLibrary(L"hid-test.dll", 0);
    using Run = int (*)(HMODULE, void (*)(const char*));
    auto run = guest ? (Run)GetProcAddress(guest, "run_hid_tests") : nullptr;
    if (backend && run) logf("HID RESULT failures=%d", run(backend, test_log));
    else logf("HID LOAD/EXPORT FAILED err=%lu", GetLastError());
    if (guest) FreeLibrary(guest);
    if (backend) FreeLibrary(backend);
}

#include "swc_dxgi.h"

// ---------------------------------------------------------------- minimal CoreWindow + D3D11 (heartbeat only)
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
        m_window = w;
        w.Closed([this](auto&&, auto&&) { m_closed = true; logf("window closed"); });
        logf("SetWindow bounds=%.0fx%.0f", w.Bounds().Width, w.Bounds().Height);
    }
    void Load(hstring const&) {}
    void Uninitialize() { logf("Uninitialize"); }

    void Run() {
        logf("Run: entering"); m_window.Activate();
        auto probe_root = std::wstring(Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str());
        // Only the isolated Mlp package selects this nonexecuting probe mode.
        if (probe_root.find(L"Joao.XboxSteam.Mlp_") != std::wstring::npos) {
            HMODULE display=LoadPackagedLibrary(L"uwph-user32.dll",0);
            using Init=void (*)(IUnknown*,void(*)(),LONG,LONG);
            if(display) if(auto init=(Init)GetProcAddress(display,"uwph_display_init"))
                init((IUnknown*)winrt::get_abi(m_window), [] {
                    CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                },1920,1080);
            mlp::run(probe_root, test_log);
            while (!m_closed) m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            return;
        }
        if (probe_root.find(L"Joao.XboxSteam.Swc_") != std::wstring::npos) {
            swc::run((IUnknown*)winrt::get_abi(m_window), (probe_root+L"\\swc-test.exe").c_str());
            while (!m_closed) m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            return;
        }
        if (probe_root.find(L"Joao.XboxSteam.Cs32_") != std::wstring::npos) {
            cs32::run();
            while (!m_closed) m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
            return;
        }
        run_k32();
        auto local = Windows::Storage::ApplicationData::Current().LocalFolder().Path();
        wxl::run((std::wstring(local.c_str()) + L"\\wxl-test.exe").c_str(), test_log);
        run_hid();
#ifdef WXL_TLS_LANE
        bool lifecycle=wxl::run((std::wstring(local.c_str()) + L"\\lifecycle.dll").c_str(), test_log);
        logf("LIFECYCLE RESULT failures=%d",lifecycle?0:1);
        logf("WXL native TLS startup anchors A=%lu B=%lu",carrier_anchor_a(),carrier_anchor_b());
        HMODULE ca=LoadPackagedLibrary(L"tls-carrier-a.dll",0);
        logf("WXL carrier A module=%p error=%lu",ca,ca?0:GetLastError());
        HMODULE cb=LoadPackagedLibrary(L"tls-carrier-b.dll",0);
        logf("WXL carrier B module=%p error=%lu",cb,cb?0:GetLastError());
        bool tls=wxl::runTls(local.c_str(),test_log,ca,cb);
        logf("WXL TLS RESULT failures=%d",tls?0:1);
        if (cb) FreeLibrary(cb);
        if (ca) FreeLibrary(ca);
#endif
        HMODULE backend = LoadPackagedLibrary(L"uwph-host.dll", 0);
        HMODULE guest = LoadPackagedLibrary(L"d3dt-test.dll", 0);
        using SetTarget = void (*)(IUnknown*);
        using GetTarget = HRESULT (*)(IUnknown**);
        using Run = HRESULT (*)(GetTarget, void (*)(const char*), void (*)());
        auto set = backend ? (SetTarget)GetProcAddress(backend, "uwph_set_swapchain_target") : nullptr;
        auto get = backend ? (GetTarget)GetProcAddress(backend, "uwph_get_swapchain_target") : nullptr;
        auto run = guest ? (Run)GetProcAddress(guest, "run_d3dt") : nullptr;
        if (set && get && run) {
            set((IUnknown*)winrt::get_abi(m_window));
            HRESULT hr = run(get, test_log, [] {
                CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            });
            logf("D3DT RETURN hr=0x%08lx", (unsigned long)hr);
            set(nullptr);
        } else logf("D3DT LOAD/EXPORT FAILED err=%lu", GetLastError());
        // Keep guest device/swapchain alive so the last presented color remains visible.
        while (!m_closed) m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        if (guest) FreeLibrary(guest);
        if (backend) FreeLibrary(backend);

    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment(apartment_type::multi_threaded);
    CoreApplication::Run(make<App>());
    return 0;
}
