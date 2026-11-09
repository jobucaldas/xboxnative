// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that starts a D3D10 game the way one does. It
// statically imports d3d10.dll, which the console does not have, so Play must
// serve it from the launcher's runtime directory (the shim in uwph/d3d10) and
// that shim's own d3d11.dll/dxgi.dll imports must reach Play's routes. Then it
// creates a device with a swap chain on a guest HWND, clears the back buffer
// and presents. Exit code 42 = every step succeeded; anything else names the
// step that failed. No rendering claim: the log is the evidence.
#include <windows.h>
#include <d3d10.h>
#include <d3d10_1.h>
#include <dxgi.h>

extern "C" {
int d3d10_anchor;
void* d3d10_relocation=&d3d10_anchor;

// One guest window, created through the same user32 record core a game uses:
// Play's factory validates it and maps it to the launcher's CoreWindow.
static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d10";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d10",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

DWORD d3d10_entry() {
    HWND window=guestWindow();
    if(!window)return 10;

    // The SDK version is checked by the runtime, as on Windows.
    ID3D10Device* probe=nullptr;
    if(D3D10CreateDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION+1,&probe)!=E_INVALIDARG)return 11;
    if(probe)return 12;

    DXGI_SWAP_CHAIN_DESC chain{};
    chain.BufferDesc.Width=640;chain.BufferDesc.Height=480;
    chain.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    chain.SampleDesc.Count=1;chain.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    chain.BufferCount=2;chain.OutputWindow=window;chain.Windowed=TRUE;
    chain.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ID3D10Device* device=nullptr;IDXGISwapChain* swapchain=nullptr;
    HRESULT hr=D3D10CreateDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,
        D3D10_SDK_VERSION,&chain,&swapchain,&device);
    if(FAILED(hr)||!device||!swapchain)return 13;

    // The device reports the level a D3D10 game asks for, and the D3D10.1
    // interface is reachable through it.
    ID3D10Device1* one=nullptr;
    if(FAILED(device->QueryInterface(__uuidof(ID3D10Device1),(void**)&one))||!one)return 14;
    if(one->GetFeatureLevel()<D3D10_FEATURE_LEVEL_10_0)return 15;

    ID3D10Texture2D* backBuffer=nullptr;
    if(FAILED(swapchain->GetBuffer(0,__uuidof(ID3D10Texture2D),(void**)&backBuffer))||!backBuffer)return 16;
    ID3D10RenderTargetView* view=nullptr;
    if(FAILED(device->CreateRenderTargetView(backBuffer,nullptr,&view))||!view)return 17;

    D3D10_VIEWPORT viewport{0,0,640,480,0.0f,1.0f};
    device->RSSetViewports(1,&viewport);
    const FLOAT clear[4]={0.1f,0.2f,0.3f,1.0f};
    device->ClearRenderTargetView(view,clear);
    device->OMSetRenderTargets(1,&view,nullptr);

    // Three frames, as a game's first frames: the CoreWindow adapter presents
    // each one.
    for(unsigned i=0;i<3;++i) {
        hr=swapchain->Present(0,0);
        if(hr==DXGI_ERROR_DEVICE_REMOVED)return 18;
    }

    device->OMSetRenderTargets(0,nullptr,nullptr);
    view->Release();backBuffer->Release();
    // Everything the fixture made goes away before it returns: Wine never
    // finishes process exit while a D3D object outlives the test.
    swapchain->Release();
    one->Release();
    device->Release();
    DestroyWindow(window);
    return 42;
}
}
