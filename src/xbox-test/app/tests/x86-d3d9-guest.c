// SPDX-License-Identifier: GPL-3.0-or-later
// i686 D3D9 fixture: clear, draw, present, and read back a pixel in Play's PE32 core.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>

void WINAPI entry(void) {
    IDirect3D9 *d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d)ExitProcess(11);
    D3DPRESENT_PARAMETERS pp={0};
    pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.Windowed=TRUE;pp.hDeviceWindow=(HWND)1;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *device=0;
    HRESULT device_hr=IDirect3D9_CreateDevice(d3d,0,D3DDEVTYPE_HAL,(HWND)1,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device);
    if(FAILED(device_hr)||!device)ExitProcess((UINT)device_hr);
    (void)IDirect3DDevice9_GetAvailableTextureMem(device);
    if(FAILED(IDirect3DDevice9_Clear(device,0,0,D3DCLEAR_TARGET,0xFF123456,1.0f,0)))ExitProcess(13);
    struct Vertex { float x,y,z,rhw; DWORD color; } vertices[3]={
        {16,16,0.5f,1.0f,0xFFFF0000},{48,16,0.5f,1.0f,0xFFFF0000},{32,48,0.5f,1.0f,0xFFFF0000}};
    if(FAILED(IDirect3DDevice9_SetFVF(device,D3DFVF_XYZRHW|D3DFVF_DIFFUSE)))ExitProcess(14);
    if(FAILED(IDirect3DDevice9_BeginScene(device)))ExitProcess(15);
    HRESULT draw_hr=IDirect3DDevice9_DrawPrimitiveUP(device,D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]));
    if(FAILED(draw_hr))ExitProcess((UINT)draw_hr);
    if(FAILED(IDirect3DDevice9_EndScene(device)))ExitProcess(17);
    // The shim uses a DISCARD swap effect, so read back before Present invalidates the target.
    IDirect3DSurface9 *back=0,*readback=0;
    if(FAILED(IDirect3DDevice9_GetBackBuffer(device,0,0,D3DBACKBUFFER_TYPE_MONO,&back))||!back)ExitProcess(19);
    if(FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(device,64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,0))||!readback)ExitProcess(20);
    HRESULT readback_hr=IDirect3DDevice9_GetRenderTargetData(device,back,readback);
    if(FAILED(readback_hr))ExitProcess((UINT)readback_hr);
    D3DLOCKED_RECT locked={0};
    if(FAILED(IDirect3DSurface9_LockRect(readback,&locked,0,D3DLOCK_READONLY))||!locked.pBits)ExitProcess(22);
    DWORD pixel=*(const DWORD*)((const BYTE*)locked.pBits+locked.Pitch+4);
    IDirect3DSurface9_UnlockRect(readback);
    if(pixel!=0xFF123456)ExitProcess(23);
    if(FAILED(IDirect3DDevice9_Present(device,0,0,0,0)))ExitProcess(18);
    IDirect3DSurface9_Release(readback);IDirect3DSurface9_Release(back);
    IDirect3DDevice9_Release(device);IDirect3D9_Release(d3d);
    ExitProcess(0);
}
