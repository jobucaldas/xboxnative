// VRX spike (BACKLOG uwph-vrx-spike): does AppContainer's real kernel32 allow W^X-violating code
// execution, and does the documented VirtualAllocFromApp/VirtualProtectFromApp path work instead?
// Minimal CoreWindow+D3D11 UWP app (no XAML), runs the 3 test cases once at boot, logs results to
// LocalState\vrx.log, then idles rendering a heartbeat frame. Own package identity (Joao.XboxSteam.Vrx)
// so it never touches the Grok.XboxTest launcher package other lanes are using on the same console.
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
        g_log = _wfopen((dir + L"\\vrx.log").c_str(), L"wb");
    } catch (...) {}
}

// ---------------------------------------------------------------- the 3 test cases
// "mov eax, 42; ret" (x64): B8 2A 00 00 00 C3
static const unsigned char kStub[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
typedef int (*FnI)();

// one SEH frame per case: alloc -> write code -> make executable -> call through a function pointer.
// Allocation and protection failures are logged with GetLastError; execution faults use SEH.
static void case_a_rwx_alloc_exec() {
    const char* name = "caseA VirtualAlloc(PAGE_EXECUTE_READWRITE)+exec";
    LPVOID p = nullptr;
    __try {
        p = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!p) { logf("%s: ALLOC_FAILED err=%lu", name, GetLastError()); return; }
        memcpy(p, kStub, sizeof(kStub));
        FlushInstructionCache(GetCurrentProcess(), p, sizeof(kStub));
        FnI fn = (FnI)p;
        int val = fn();
        logf("%s: OK value=%d", name, val);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("%s: EXCEPTION code=0x%08lx", name, (unsigned long)GetExceptionCode());
    }
    VirtualFree(p, 0, MEM_RELEASE);
}
static void case_b_rw_then_protect_rx() {
    const char* name = "caseB VirtualAlloc(RW)+VirtualProtect(RX)+exec";
    LPVOID p = nullptr;
    __try {
        p = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!p) { logf("%s: ALLOC_FAILED err=%lu", name, GetLastError()); return; }
        memcpy(p, kStub, sizeof(kStub));
        DWORD oldProt = 0;
        if (!VirtualProtect(p, 4096, PAGE_EXECUTE_READ, &oldProt)) {
            logf("%s: PROTECT_FAILED err=%lu", name, GetLastError());
        } else {
            FlushInstructionCache(GetCurrentProcess(), p, sizeof(kStub));
            FnI fn = (FnI)p;
            int val = fn();
            logf("%s: OK value=%d", name, val);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("%s: EXCEPTION code=0x%08lx", name, (unsigned long)GetExceptionCode());
    }
    VirtualFree(p, 0, MEM_RELEASE);
}
static void case_c_fromapp_rw_then_protect_rx() {
    const char* name = "caseC VirtualAllocFromApp(RW)+VirtualProtectFromApp(RX)+exec";
    LPVOID p = nullptr;
    __try {
        p = VirtualAllocFromApp(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!p) { logf("%s: ALLOC_FAILED err=%lu", name, GetLastError()); return; }
        memcpy(p, kStub, sizeof(kStub));
        DWORD oldProt = 0;
        if (!VirtualProtectFromApp(p, 4096, PAGE_EXECUTE_READ, &oldProt)) {
            logf("%s: PROTECT_FAILED err=%lu", name, GetLastError());
        } else {
            FlushInstructionCache(GetCurrentProcess(), p, sizeof(kStub));
            FnI fn = (FnI)p;
            int val = fn();
            logf("%s: OK value=%d", name, val);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logf("%s: EXCEPTION code=0x%08lx", name, (unsigned long)GetExceptionCode());
    }
    VirtualFree(p, 0, MEM_RELEASE);
}
static void run_vrx_cases() {
    logf("vrx: running 3 cases (AppContainer RWX / RW+VirtualProtect / VirtualAllocFromApp+VirtualProtectFromApp)");
    case_a_rwx_alloc_exec();
    case_b_rw_then_protect_rx();
    case_c_fromapp_rw_then_protect_rx();
    logf("vrx: all cases done");
}

// ---------------------------------------------------------------- minimal CoreWindow + D3D11 (heartbeat only)
struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    CoreWindow m_window{nullptr};
    bool m_closed = false;
    com_ptr<ID3D11Device> m_dev;
    com_ptr<ID3D11DeviceContext> m_ctx;
    com_ptr<ID3D11DeviceContext1> m_ctx1;
    com_ptr<IDXGISwapChain1> m_sc;
    com_ptr<ID3D11RenderTargetView> m_rtv;
    uint64_t m_frame = 0;

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

    bool InitD3D() {
        D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls, 2, D3D11_SDK_VERSION, m_dev.put(), &got, m_ctx.put());
        logf("D3D11CreateDevice hr=0x%08lx fl=0x%x", (unsigned long)hr, (unsigned)got);
        if (FAILED(hr)) return false;
        m_ctx1 = m_ctx.try_as<ID3D11DeviceContext1>();
        auto dxgiDev = m_dev.as<IDXGIDevice>();
        com_ptr<IDXGIAdapter> ad; dxgiDev->GetAdapter(ad.put());
        com_ptr<IDXGIFactory2> fac; hr = ad->GetParent(__uuidof(IDXGIFactory2), fac.put_void());
        if (FAILED(hr)) return false;
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = 1920; sd.Height = 1080; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = fac->CreateSwapChainForCoreWindow(m_dev.get(), (::IUnknown*)winrt::get_abi(m_window), &sd, nullptr, m_sc.put());
        logf("CreateSwapChainForCoreWindow hr=0x%08lx", (unsigned long)hr);
        if (FAILED(hr)) return false;
        com_ptr<ID3D11Texture2D> bb; hr = m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), bb.put_void());
        if (FAILED(hr)) return false;
        hr = m_dev->CreateRenderTargetView(bb.get(), nullptr, m_rtv.put());
        return SUCCEEDED(hr);
    }
    void Draw() {
        const float bg[4] = {0.05f, 0.20f, 0.55f, 1.0f};
        ID3D11RenderTargetView* rtvs[] = {m_rtv.get()};
        m_ctx->OMSetRenderTargets(1, rtvs, nullptr);
        m_ctx->ClearRenderTargetView(m_rtv.get(), bg);
        if (m_ctx1) { const float white[4] = {1, 1, 1, 1}; D3D11_RECT r{LONG((m_frame * 8) % 1800), 20, LONG((m_frame * 8) % 1800) + 120, 50}; m_ctx1->ClearView(m_rtv.get(), white, &r, 1); }
    }
    void Run() {
        logf("Run: entering"); m_window.Activate();
        run_vrx_cases();
        if (!InitD3D()) { logf("D3D init FAILED; idling"); while (!m_closed) { m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending); Sleep(50); } return; }
        double lastBeat = 0;
        while (!m_closed) {
            m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            Draw();
            HRESULT hr = m_sc->Present(1, 0);
            if (FAILED(hr)) { logf("Present hr=0x%08lx", (unsigned long)hr); Sleep(100); }
            m_frame++;
            double t = now_s();
            if (m_frame == 1 || t - lastBeat >= 10.0) { logf("frame=%llu t=%.1f", (unsigned long long)m_frame, t); lastBeat = t; }
        }
    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    init_apartment(apartment_type::multi_threaded);
    CoreApplication::Run(make<App>());
    return 0;
}
