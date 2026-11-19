// SPDX-License-Identifier: GPL-3.0-or-later
// i686 D3D9 fixture: clear, draw, present, and read back a pixel in Play's PE32 core.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>

typedef struct D3DXImageInfoX {
    UINT Width, Height, Depth, MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    UINT ImageFileFormat;
} D3DXImageInfoX;
typedef HRESULT (WINAPI *D3DXCreateTextureFn)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**);
typedef HRESULT (WINAPI *D3DXCreateTextureFromMemoryFn)(IDirect3DDevice9*,const void*,UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,DWORD,DWORD,D3DCOLOR,D3DXImageInfoX*,PALETTEENTRY*,IDirect3DTexture9**);
typedef HRESULT (WINAPI *D3DXGetImageInfoFn)(const void*,UINT,D3DXImageInfoX*);
typedef HRESULT (WINAPI *D3DXLoadSurfaceFn)(IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,DWORD,D3DCOLOR);

static const BYTE dds_1x1[] = {
    68,68,83,32,124,0,0,0,7,16,8,0,1,0,0,0,
    1,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,32,0,0,0,
    65,0,0,0,0,0,0,0,32,0,0,0,0,0,255,0,
    0,255,0,0,255,0,0,0,0,0,0,255,0,16,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    51,34,17,255,
};

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
    struct { D3DDEVICE_CREATION_PARAMETERS params; DWORD guard; } creation={0};
    creation.guard=0x12345678;
    if(FAILED(IDirect3DDevice9_GetCreationParameters(device,&creation.params))||creation.guard!=0x12345678||
       creation.params.hFocusWindow!=(HWND)1||creation.params.DeviceType!=D3DDEVTYPE_HAL||
       creation.params.BehaviorFlags!=D3DCREATE_HARDWARE_VERTEXPROCESSING)ExitProcess(59);
    IDirect3DTexture9 *device_resource=0;
    if(FAILED(IDirect3DDevice9_CreateTexture(device,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&device_resource,0))||!device_resource)ExitProcess(62);
    /* D3DX-style: GetDevice while the original device is still held, then let it go. */
    IDirect3DDevice9 *peek=0;
    if(FAILED(IDirect3DTexture9_GetDevice(device_resource,&peek))||!peek)ExitProcess(63);
    IDirect3DDevice9_Release(peek);
    IDirect3DDevice9_Release(device);
    device=0;
    if(FAILED(IDirect3DTexture9_GetDevice(device_resource,&device))||!device)ExitProcess(60);
    creation.guard=0x12345678;
    if(FAILED(IDirect3DDevice9_GetCreationParameters(device,&creation.params))||creation.guard!=0x12345678||
       creation.params.hFocusWindow!=(HWND)1||creation.params.DeviceType!=D3DDEVTYPE_HAL||
       creation.params.BehaviorFlags!=D3DCREATE_HARDWARE_VERTEXPROCESSING)ExitProcess(61);
    IDirect3DTexture9_Release(device_resource);
    HMODULE d3dx=LoadLibraryA("d3dx9_43.dll");
    if(!d3dx)ExitProcess(41);
    union { FARPROC raw; D3DXCreateTextureFn fn; } create_texture_p={GetProcAddress(d3dx,"D3DXCreateTexture")};
    union { FARPROC raw; D3DXCreateTextureFromMemoryFn fn; } create_texture_memory_p={GetProcAddress(d3dx,"D3DXCreateTextureFromFileInMemoryEx")};
    union { FARPROC raw; D3DXGetImageInfoFn fn; } get_image_info_p={GetProcAddress(d3dx,"D3DXGetImageInfoFromFileInMemory")};
    union { FARPROC raw; D3DXLoadSurfaceFn fn; } load_surface_p={GetProcAddress(d3dx,"D3DXLoadSurfaceFromSurface")};
    D3DXCreateTextureFn create_texture=create_texture_p.fn;
    D3DXCreateTextureFromMemoryFn create_texture_memory=create_texture_memory_p.fn;
    D3DXGetImageInfoFn get_image_info=get_image_info_p.fn;
    D3DXLoadSurfaceFn load_surface=load_surface_p.fn;
    if(!create_texture||!create_texture_memory||!get_image_info||!load_surface)ExitProcess(42);
    IDirect3DTexture9 *created=0;
    if(FAILED(create_texture(device,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&created))||!created)ExitProcess(43);
    IDirect3DSurface9 *created_surface=0;
    if(FAILED(IDirect3DTexture9_GetSurfaceLevel(created,0,&created_surface))||!created_surface)ExitProcess(44);
    D3DXImageInfoX image_info={0};
    if(FAILED(get_image_info(dds_1x1,sizeof(dds_1x1),&image_info))||image_info.Width!=1||image_info.Height!=1)ExitProcess(45);
    IDirect3DTexture9 *loaded=0;
    HRESULT loaded_hr=create_texture_memory(device,dds_1x1,sizeof(dds_1x1),0xffffffffu,0xffffffffu,0xffffffffu,0,D3DFMT_UNKNOWN,D3DPOOL_MANAGED,1,1,0,&image_info,0,&loaded); if(FAILED(loaded_hr)||!loaded)ExitProcess(FAILED(loaded_hr)?(UINT)loaded_hr:46);
    IDirect3DSurface9 *loaded_surface=0;
    if(FAILED(IDirect3DTexture9_GetSurfaceLevel(loaded,0,&loaded_surface))||!loaded_surface)ExitProcess(47);
    if(FAILED(load_surface(created_surface,0,0,loaded_surface,0,0,1,0)))ExitProcess(48);
    D3DLOCKED_RECT d3dx_locked={0};
    if(FAILED(IDirect3DSurface9_LockRect(created_surface,&d3dx_locked,0,D3DLOCK_READONLY))||!d3dx_locked.pBits)ExitProcess(49);
    DWORD d3dx_pixel=*(const DWORD*)d3dx_locked.pBits;
    IDirect3DSurface9_UnlockRect(created_surface);
    if((d3dx_pixel&0x00ffffffu)!=0x00112233u)ExitProcess(50);
    volatile HRESULT unsupported_hr=IDirect3DSurface9_GetDC(created_surface,0);
    if(unsupported_hr==S_OK)ExitProcess(51);
    IDirect3DSurface9_Release(loaded_surface);IDirect3DTexture9_Release(loaded);
    IDirect3DSurface9_Release(created_surface);IDirect3DTexture9_Release(created);
    (void)IDirect3DDevice9_TestCooperativeLevel(device);
    if(FAILED(IDirect3DDevice9_Clear(device,0,0,D3DCLEAR_TARGET,0xFF123456,1.0f,0)))ExitProcess(13);
    IDirect3DTexture9 *texture=0;
    if(FAILED(IDirect3DDevice9_CreateTexture(device,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,0))||!texture)ExitProcess(24);
    D3DLOCKED_RECT texel={0};
    if(FAILED(IDirect3DTexture9_LockRect(texture,0,&texel,0,0))||!texel.pBits)ExitProcess(25);
    for(UINT y=0;y<2;++y)for(UINT x=0;x<2;++x)*(DWORD*)((BYTE*)texel.pBits+y*texel.Pitch+x*4)=0x80FF0000;
    if(FAILED(IDirect3DTexture9_UnlockRect(texture,0)))ExitProcess(26);
    if(FAILED(IDirect3DTexture9_LockRect(texture,0,&texel,0,D3DLOCK_READONLY))||!texel.pBits)ExitProcess(37);
    DWORD uploaded=*(const DWORD*)texel.pBits;
    if(FAILED(IDirect3DTexture9_UnlockRect(texture,0)))ExitProcess(38);
    if(uploaded!=0x80FF0000)ExitProcess(39);
    if(FAILED(IDirect3DDevice9_SetTexture(device,0,(IDirect3DBaseTexture9*)texture)))ExitProcess(27);
    if(FAILED(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_MINFILTER,D3DTEXF_POINT))||
       FAILED(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT)))ExitProcess(28);
    if(FAILED(IDirect3DDevice9_SetRenderState(device,D3DRS_ZENABLE,FALSE))||
       FAILED(IDirect3DDevice9_SetRenderState(device,D3DRS_LIGHTING,FALSE))||
       FAILED(IDirect3DDevice9_SetRenderState(device,D3DRS_ALPHABLENDENABLE,TRUE))||
       FAILED(IDirect3DDevice9_SetRenderState(device,D3DRS_SRCBLEND,D3DBLEND_SRCALPHA))||
       FAILED(IDirect3DDevice9_SetRenderState(device,D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA)))ExitProcess(29);
    struct Vertex { float x,y,z,rhw; DWORD color; float u,v; } vertices[4]={
        {8,8,0.5f,1.0f,0xFFFFFFFF,0,0},{56,8,0.5f,1.0f,0xFFFFFFFF,1,0},
        {56,56,0.5f,1.0f,0xFFFFFFFF,1,1},{8,56,0.5f,1.0f,0xFFFFFFFF,0,1}};
    WORD indices[6]={0,1,2,0,2,3};
    IDirect3DIndexBuffer9 *index_buffer=0;
    if(FAILED(IDirect3DDevice9_CreateIndexBuffer(device,sizeof(indices),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&index_buffer,0))||!index_buffer)ExitProcess(30);
    void *index_data=0;
    if(FAILED(IDirect3DIndexBuffer9_Lock(index_buffer,0,sizeof(indices),&index_data,0))||!index_data)ExitProcess(31);
    for(UINT i=0;i<6;++i)((WORD*)index_data)[i]=indices[i];
    if(FAILED(IDirect3DIndexBuffer9_Unlock(index_buffer)))ExitProcess(32);
    IDirect3DVertexBuffer9 *vertex_buffer=0;
    if(FAILED(IDirect3DDevice9_CreateVertexBuffer(device,sizeof(vertices),0,0,D3DPOOL_MANAGED,&vertex_buffer,0))||!vertex_buffer)ExitProcess(34);
    void *vertex_data=0;
    if(FAILED(IDirect3DVertexBuffer9_Lock(vertex_buffer,0,sizeof(vertices),&vertex_data,0))||!vertex_data)ExitProcess(35);
    for(UINT i=0;i<sizeof(vertices);++i)((BYTE*)vertex_data)[i]=((const BYTE*)vertices)[i];
    if(FAILED(IDirect3DVertexBuffer9_Unlock(vertex_buffer)))ExitProcess(36);
    D3DVIEWPORT9 viewport={0,0,64,64,0.0f,1.0f};
    if(FAILED(IDirect3DDevice9_SetIndices(device,index_buffer))||
       FAILED(IDirect3DDevice9_SetStreamSource(device,0,vertex_buffer,0,sizeof(vertices[0])))||
       FAILED(IDirect3DDevice9_SetFVF(device,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1))||
       FAILED(IDirect3DDevice9_SetViewport(device,&viewport))||
       FAILED(IDirect3DDevice9_SetNPatchMode(device,0.0f)))ExitProcess(33);
    if(FAILED(IDirect3DDevice9_BeginScene(device)))ExitProcess(15);
    HRESULT draw_hr=IDirect3DDevice9_DrawIndexedPrimitive(device,D3DPT_TRIANGLELIST,0,0,4,0,2);
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
    DWORD textured=*(const DWORD*)((const BYTE*)locked.pBits+20*locked.Pitch+40*4);
    DWORD clear=*(const DWORD*)((const BYTE*)locked.pBits+2*locked.Pitch+2*4);
    IDirect3DSurface9_UnlockRect(readback);
    if(clear!=0xFF123456)ExitProcess(40);
    if((textured&0x00FFFFFF)!=0x00891A2B)ExitProcess(23);
    if(FAILED(IDirect3DDevice9_Present(device,0,0,0,0)))ExitProcess(18);
    IDirect3DSurface9_Release(readback);IDirect3DSurface9_Release(back);
    IDirect3DVertexBuffer9_Release(vertex_buffer);IDirect3DIndexBuffer9_Release(index_buffer);IDirect3DTexture9_Release(texture);
    IDirect3DDevice9_Release(device);IDirect3D9_Release(d3d);FreeLibrary(d3dx);
    ExitProcess(0);
}
