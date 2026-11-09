// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that starts a D3D9 game the way one does. It
// statically imports d3d9.dll, which the console does not have, so Play must
// serve it from the launcher's runtime directory (the shim in uwph/d3d9) and
// that shim's own d3d11.dll/dxgi.dll imports must reach Play's routes. Then it
// creates a device with a swap chain on a guest HWND, clears the back buffer and
// presents. Exit code 42 = every step succeeded; anything else names the step
// that failed. No rendering claim: the log is the evidence.
#include <windows.h>
#include <d3d9.h>

extern "C" {
int d3d9_anchor;
void* d3d9_relocation=&d3d9_anchor;

// One guest window, created through the same user32 record core a game uses:
// Play's factory validates it and maps it to the launcher's CoreWindow.
static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d9";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d9",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

DWORD d3d9_entry() {
    HWND window=guestWindow();
    if(!window)return 10;

    // The SDK version is checked by the runtime, as on Windows.
    if(Direct3DCreate9(D3D_SDK_VERSION+1))return 11;
    IDirect3D9Ex* wrong=nullptr;
    if(Direct3DCreate9Ex(D3D_SDK_VERSION+1,&wrong)!=D3DERR_INVALIDCALL)return 12;
    if(wrong)return 13;

    IDirect3D9Ex* d3d9=nullptr;
    if(FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3d9))||!d3d9)return 14;
    if(d3d9->GetAdapterCount()!=1)return 15;

    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth=640;present.BackBufferHeight=480;
    present.BackBufferFormat=D3DFMT_X8R8G8B8;present.BackBufferCount=1;
    present.MultiSampleType=D3DMULTISAMPLE_NONE;
    present.SwapEffect=D3DSWAPEFFECT_DISCARD;present.hDeviceWindow=window;present.Windowed=TRUE;
    present.EnableAutoDepthStencil=TRUE;present.AutoDepthStencilFormat=D3DFMT_D24S8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* device=nullptr;
    HRESULT hr=d3d9->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &present,nullptr,&device);
    if(FAILED(hr)||!device)return 16;

    // The swap chain's back buffer is the device's render target 0, as on
    // Windows; the auto depth-stencil surface exists too.
    IDirect3DSurface9* backBuffer=nullptr;
    if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer))||!backBuffer)return 17;
    IDirect3DSurface9* depth=nullptr;
    if(FAILED(device->GetDepthStencilSurface(&depth))||!depth)return 18;

    D3DVIEWPORT9 viewport{0,0,640,480,0.0f,1.0f};
    device->SetViewport(&viewport);
    if(FAILED(device->BeginScene()))return 19;
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,
        0xff204060,1.0f,0)))return 20;
    if(FAILED(device->EndScene()))return 21;

    // Three frames, as a game's first frames: the CoreWindow adapter presents
    // each one.
    for(unsigned i=0;i<3;++i) {
        hr=device->Present(nullptr,nullptr,nullptr,nullptr);
        if(hr==D3DERR_DEVICELOST)return 22;
    }

    // Everything the fixture made goes away before it returns: Wine never
    // finishes process exit while a D3D object outlives the test.
    depth->Release();backBuffer->Release();
    device->Release();
    d3d9->Release();
    DestroyWindow(window);
    return 42;
}
}
