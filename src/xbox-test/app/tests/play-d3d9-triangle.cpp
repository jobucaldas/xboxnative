// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that draws with D3D9 the way a game's first frame
// does. It statically imports d3d9.dll, so Play must serve the shim from the
// launcher's runtime directory; the shim's own d3d11.dll/dxgi.dll imports must
// reach Play's routes, and its shader translator must be served beside it.
//
// The draw is fixed-function: a D3DFVF_XYZRHW|D3DFVF_DIFFUSE triangle with no
// shaders bound, which the shim turns into vs_3_0/ps_3_0 and translates. The
// result is read back through a SYSTEMMEM surface and checked, so a draw that
// silently did nothing fails here rather than looking like a black screen.
//
// Exit code 42 = the triangle rendered where it should be; anything else names
// the step that failed. No console is contacted.
#include <windows.h>
#include <d3d9.h>

extern "C" {
int d3d9_triangle_anchor;
void* d3d9_triangle_relocation=&d3d9_triangle_anchor;

static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d9-triangle";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d9-triangle",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

// D3DFVF_XYZRHW|D3DFVF_DIFFUSE: four floats then a D3DCOLOR.
struct Vertex { float x,y,z,w; DWORD colour; };

DWORD d3d9_triangle_entry() {
    HWND window=guestWindow();
    if(!window)return 10;

    IDirect3D9Ex* d3d9=nullptr;
    if(FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3d9))||!d3d9)return 11;

    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth=640;present.BackBufferHeight=480;
    present.BackBufferFormat=D3DFMT_X8R8G8B8;present.BackBufferCount=1;
    present.MultiSampleType=D3DMULTISAMPLE_NONE;
    present.SwapEffect=D3DSWAPEFFECT_DISCARD;present.hDeviceWindow=window;present.Windowed=TRUE;
    present.EnableAutoDepthStencil=TRUE;present.AutoDepthStencilFormat=D3DFMT_D24S8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* device=nullptr;
    if(FAILED(d3d9->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &present,nullptr,&device))||!device)return 12;

    // An offscreen target and a CPU-readable surface to copy it into: the same
    // pair a game uses for a screenshot, and the only way to see the draw.
    const UINT size=64;
    IDirect3DSurface9* target=nullptr;
    if(FAILED(device->CreateRenderTarget(size,size,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
        &target,nullptr))||!target)return 13;
    IDirect3DSurface9* staging=nullptr;
    if(FAILED(device->CreateOffscreenPlainSurface(size,size,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
        &staging,nullptr))||!staging)return 14;

    IDirect3DVertexBuffer9* buffer=nullptr;
    if(FAILED(device->CreateVertexBuffer(3*sizeof(Vertex),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
        &buffer,nullptr))||!buffer)return 15;
    Vertex* vertices=nullptr;
    if(FAILED(buffer->Lock(0,0,reinterpret_cast<void**>(&vertices),0))||!vertices)return 16;
    // Screen-space vertices over the left of the target, all red.
    vertices[0]=Vertex{ 8.0f, 8.0f,0.0f,1.0f,0xffff0000};
    vertices[1]=Vertex{56.0f, 8.0f,0.0f,1.0f,0xffff0000};
    vertices[2]=Vertex{32.0f,56.0f,0.0f,1.0f,0xffff0000};
    if(FAILED(buffer->Unlock()))return 17;

    if(FAILED(device->SetStreamSource(0,buffer,0,sizeof(Vertex))))return 18;
    if(FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)))return 19;
    device->SetRenderState(D3DRS_LIGHTING,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    if(FAILED(device->SetRenderTarget(0,target)))return 20;
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 21;
    D3DVIEWPORT9 viewport{0,0,size,size,0.0f,1.0f};
    if(FAILED(device->SetViewport(&viewport)))return 22;

    if(FAILED(device->BeginScene()))return 23;
    // One triangle: D3D9's count is primitives, not vertices.
    if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 24;
    if(FAILED(device->EndScene()))return 25;
    if(FAILED(device->GetRenderTargetData(target,staging)))return 26;

    D3DLOCKED_RECT locked{};
    if(FAILED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))||!locked.pBits)return 27;
    const unsigned char* inside=static_cast<const unsigned char*>(locked.pBits)
        +32*locked.Pitch+32*4;
    const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
        +4*locked.Pitch+4*4;
    const bool insideRed=inside[2]>0x80&&inside[0]<0x40&&inside[1]<0x40;
    const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
    staging->UnlockRect();
    if(!insideRed||!outsideBlack)return 28;

    // The draw reached the swap chain's back buffer too, as a game's does: the
    // fixture presents one frame with the triangle in it.
    IDirect3DSurface9* backBuffer=nullptr;
    if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer))||!backBuffer)return 30;
    if(FAILED(device->SetRenderTarget(0,backBuffer)))return 31;
    D3DVIEWPORT9 full{0,0,640,480,0.0f,1.0f};
    device->SetViewport(&full);
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 32;
    if(FAILED(device->BeginScene()))return 33;
    if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 34;
    if(FAILED(device->EndScene()))return 35;
    const HRESULT presented=device->Present(nullptr,nullptr,nullptr,nullptr);
    if(presented==D3DERR_DEVICELOST)return 36;

    backBuffer->Release();
    staging->Release();target->Release();buffer->Release();
    device->Release();d3d9->Release();
    DestroyWindow(window);
    return 42;
}
}
