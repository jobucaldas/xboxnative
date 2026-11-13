// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contract test for Play's D3D9 shim. It loads the shim the way
// Play's loader does (by name, from its own directory) and drives the entry
// points a game uses first. Wine's d3d11.dll stands in for the console's
// device, so this proves the shim's own conversions and object model, not Xbox
// execution. No console is contacted and nothing here is a game.
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include "texture_desc.h"
// The shim's own file-scope definitions, for the same reason: no CRT here.
extern "C" void* memset(void* d,int c,size_t n) {
    auto p=(unsigned char*)d;while(n--)*p++=(unsigned char)c;return d;
}
extern "C" void* memcpy(void* d,const void* s,size_t n) {
    auto p=(unsigned char*)d;auto q=(const unsigned char*)s;
    while(n--)*p++=*q++;return d;
}
extern "C" int strcmp(const char* a,const char* b) {
    while(*a&&*a==*b){++a;++b;}return (unsigned char)*a-(unsigned char)*b;
}
// mingw emits this stack-probe helper for large frames; the CRT supplies it.
extern "C" void ___chkstk_ms(void) {}
// No C runtime: the test is linked /nodefaultlib like the shim, so output goes
// to stderr through WriteFile and the entry point is named explicitly.
static void out(const char* text) {
    DWORD written=0;DWORD length=0;while(text[length])++length;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),text,length,&written,nullptr);
}
static void outNum(const char* prefix,unsigned long long value) {
    char digits[24];unsigned n=0;
    do{digits[n++]=char('0'+value%10);value/=10;}while(value);
    char line[64];unsigned used=0;
    while(*prefix&&used<sizeof line-2)line[used++]=*prefix++;
    while(n)line[used++]=digits[--n];
    line[used++]='\n';
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),line,used,nullptr,nullptr);
}
static void outHex(const char* prefix,unsigned long long value) {
    char digits[16];unsigned n=0;
    do{digits[n++]="0123456789abcdef"[value&15];value>>=4;}while(value);
    char line[96];unsigned used=0;
    while(*prefix&&used<sizeof line-8)line[used++]=*prefix++;
    line[used++]='0';line[used++]='x';
    while(n)line[used++]=digits[--n];
    line[used++]='\n';
    WriteFile(GetStdHandle(STD_ERROR_HANDLE),line,used,nullptr,nullptr);
}
static unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;out("FAIL line " #x);outNum("  at line ",__LINE__);} } while(0)
#define CHECK_HR(call) do { HRESULT hr_=(call); ++checks; if(FAILED(hr_)){++failures; \
    out("FAIL " #call);outHex("  hr=",(unsigned long)hr_);} } while(0)
#define CHECK_HR_IS(call,expected) do { HRESULT hr_=(call); ++checks; if(hr_!=(expected)){++failures; \
    out("FAIL " #call);outHex("  hr=",(unsigned long)hr_);outHex("  expected=",(unsigned long)(expected));} } while(0)

using Create9=IDirect3D9*(WINAPI*)(UINT);
using Create9Ex=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);

constexpr D3D11_TEXTURE2D_DESC managedTexture2x2Descriptor() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=2;desc.Height=2;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    d3d9_detail::applyManagedTextureBacking(desc,D3DPOOL_MANAGED);
    return desc;
}
constexpr D3D11_TEXTURE2D_DESC managedTexture2x2=managedTexture2x2Descriptor();
static_assert(managedTexture2x2.Width==2&&managedTexture2x2.Height==2&&
    managedTexture2x2.MipLevels==1&&managedTexture2x2.ArraySize==1&&
    managedTexture2x2.Format==DXGI_FORMAT_B8G8R8A8_UNORM&&
    managedTexture2x2.SampleDesc.Count==1&&managedTexture2x2.SampleDesc.Quality==0&&
    managedTexture2x2.Usage==D3D11_USAGE_DEFAULT&&
    managedTexture2x2.BindFlags==D3D11_BIND_SHADER_RESOURCE&&
    managedTexture2x2.CPUAccessFlags==0&&managedTexture2x2.MiscFlags==0,
    "managed 2x2 D3D9 texture must use the tested DEFAULT SRV descriptor");

static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) { return DefWindowProcW(h,m,w,l); }

// The shim is loaded by full path from this executable's own directory, so the
// test never picks up a host d3d9.dll.
static HMODULE loadShim() {
    wchar_t path[1024];DWORD n=GetModuleFileNameW(nullptr,path,1024);
    if(!n||n>=1024)return nullptr;
    while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;
    const wchar_t name[]=L"d3d9.dll";
    if(n+sizeof name/sizeof name[0]>=1024)return nullptr;
    for(unsigned i=0;i<sizeof name/sizeof name[0];++i)path[n+i]=name[i];
    return LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
}
extern "C" DWORD WINAPI d3d9_test_entry() {
    HMODULE module=loadShim();
    if(!module) {
        outHex("SKIP-DLL d3d9.dll not loadable error=",GetLastError());
        return 2;
    }
    {wchar_t loaded[1024];DWORD m=GetModuleFileNameW(module,loaded,1024);
     out("INFO loaded ");if(m&&m<1024){char narrow[1024];unsigned k=0;
        for(DWORD i=0;i<m&&k+1<sizeof narrow;++i)narrow[k++]=char(loaded[i]);narrow[k]=0;out(narrow);}
     out("\n");}
    auto create9=(Create9)GetProcAddress(module,"Direct3DCreate9");
    auto create9Ex=(Create9Ex)GetProcAddress(module,"Direct3DCreate9Ex");
    CHECK(create9!=nullptr);
    CHECK(create9Ex!=nullptr);
    if(!create9||!create9Ex){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

    // Regression: a D3D9 A8R8G8B8 managed 2x2 texture must be created as a
    // one-mip, sampleable DEFAULT texture, with no CPU access. LockRect uses the
    // shim's separate staging shadow; dynamic textures on the console UMD can
    // remove the device. This checks the descriptor helper the shim itself uses.
    D3D11_TEXTURE2D_DESC managed{};
    managed.Width=2;managed.Height=2;managed.MipLevels=1;managed.ArraySize=1;
    managed.Format=DXGI_FORMAT_B8G8R8A8_UNORM;managed.SampleDesc.Count=1;
    managed.Usage=D3D11_USAGE_DYNAMIC;managed.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    managed.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    d3d9_detail::applyManagedTextureBacking(managed,D3DPOOL_MANAGED);
    CHECK(managed.Width==2&&managed.Height==2&&managed.MipLevels==1&&managed.ArraySize==1);
    CHECK(managed.Format==DXGI_FORMAT_B8G8R8A8_UNORM&&managed.SampleDesc.Count==1&&managed.SampleDesc.Quality==0);
    CHECK(managed.Usage==D3D11_USAGE_DEFAULT&&managed.BindFlags==D3D11_BIND_SHADER_RESOURCE);
    CHECK(managed.CPUAccessFlags==0&&managed.MiscFlags==0);

    // The entry points report the SDK version they were built for.
    CHECK(create9(D3D_SDK_VERSION+1)==nullptr);
    IDirect3D9Ex* wrong=nullptr;
    CHECK_HR_IS(create9Ex(D3D_SDK_VERSION+1,&wrong),D3DERR_INVALIDCALL);
    CHECK(wrong==nullptr);

    IDirect3D9Ex* ex=nullptr;
    CHECK_HR(create9Ex(D3D_SDK_VERSION,&ex));
    CHECK(ex!=nullptr);
    if(!ex){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

    // IDirect3D9 and IDirect3D9Ex are the same object, as on Windows.
    IDirect3D9* plain=nullptr;
    CHECK_HR(ex->QueryInterface(__uuidof(IDirect3D9),(void**)&plain));
    CHECK(plain==static_cast<IDirect3D9*>(ex));
    if(plain)plain->Release();
    IDirect3D9* fromCreate9=create9(D3D_SDK_VERSION);
    CHECK(fromCreate9!=nullptr);
    if(fromCreate9) {
        CHECK(fromCreate9->GetAdapterCount()==1);
        fromCreate9->Release();
    }

    // Adapter enumeration: one adapter, the CoreWindow's output.
    CHECK(ex->GetAdapterCount()==1);
    D3DADAPTER_IDENTIFIER9 identifier{};
    CHECK_HR(ex->GetAdapterIdentifier(0,0,&identifier));
    CHECK(identifier.VendorId==0x1414);
    CHECK(!strcmp(identifier.DeviceName,"Console CoreWindow adapter"));
    CHECK_HR_IS(ex->GetAdapterIdentifier(1,0,&identifier),D3DERR_INVALIDCALL);
    D3DDISPLAYMODE mode{};
    CHECK_HR(ex->GetAdapterDisplayMode(0,&mode));
    CHECK(mode.Width==1920&&mode.Height==1080&&mode.Format==D3DFMT_X8R8G8B8);
    CHECK(ex->GetAdapterModeCount(0,D3DFMT_X8R8G8B8)==1);
    CHECK(ex->GetAdapterModeCount(0,D3DFMT_UNKNOWN)==0);
    CHECK_HR(ex->EnumAdapterModes(0,D3DFMT_X8R8G8B8,0,&mode));
    CHECK_HR_IS(ex->EnumAdapterModes(0,D3DFMT_X8R8G8B8,1,&mode),D3DERR_INVALIDCALL);
    LUID luid{};
    CHECK_HR(ex->GetAdapterLUID(0,&luid));

    // The capability and format queries a game runs before creating a device.
    D3DCAPS9 caps{};
    CHECK_HR(ex->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps));
    CHECK(caps.VertexShaderVersion==D3DVS_VERSION(3,0));
    CHECK(caps.PixelShaderVersion==D3DPS_VERSION(3,0));
    CHECK_HR_IS(ex->GetDeviceCaps(0,D3DDEVTYPE_REF,&caps),D3DERR_INVALIDCALL);
    CHECK_HR(ex->CheckDeviceType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_X8R8G8B8,TRUE));
    CHECK_HR_IS(ex->CheckDeviceType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_UNKNOWN,TRUE),D3DERR_NOTAVAILABLE);
    CHECK_HR(ex->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,D3DFMT_A8R8G8B8));
    CHECK_HR(ex->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DUSAGE_RENDERTARGET,
        D3DRTYPE_TEXTURE,D3DFMT_A8R8G8B8));
    DWORD quality=0;
    CHECK_HR(ex->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,TRUE,D3DMULTISAMPLE_NONE,&quality));
    CHECK(quality==1);
    CHECK_HR_IS(ex->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,TRUE,
        D3DMULTISAMPLE_4_SAMPLES,&quality),D3DERR_NOTAVAILABLE);
    CHECK_HR(ex->CheckDepthStencilMatch(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_X8R8G8B8,D3DFMT_D24S8));
    CHECK_HR_IS(ex->CheckDepthStencilMatch(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_X8R8G8B8,
        D3DFMT_A8R8G8B8),D3DERR_NOTAVAILABLE);
    CHECK_HR(ex->CheckDeviceFormatConversion(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_A8R8G8B8));

    // A device on a real window: this is the path a game takes first.
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"d3d9-shim-test";
    HWND window=nullptr;
    if(RegisterClassW(&cls))
        window=CreateWindowExW(0,cls.lpszClassName,L"d3d9 shim",WS_OVERLAPPEDWINDOW,
            0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
    CHECK(window!=nullptr);

    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth=320;present.BackBufferHeight=240;
    present.BackBufferFormat=D3DFMT_X8R8G8B8;present.BackBufferCount=1;
    present.MultiSampleType=D3DMULTISAMPLE_NONE;
    present.SwapEffect=D3DSWAPEFFECT_DISCARD;present.hDeviceWindow=window;present.Windowed=TRUE;
    present.EnableAutoDepthStencil=TRUE;present.AutoDepthStencilFormat=D3DFMT_D24S8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* device=nullptr;
    if(window) {
        CHECK_HR_IS(ex->CreateDevice(0,D3DDEVTYPE_REF,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &present,nullptr),D3DERR_INVALIDCALL);
        CHECK_HR(ex->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &present,nullptr,&device));
    }
    CHECK(device!=nullptr);
    if(!device){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

    // Device identity and caps.
    D3DDEVICE_CREATION_PARAMETERS creation{};
    CHECK_HR(device->GetCreationParameters(&creation));
    CHECK(creation.hFocusWindow==window&&creation.DeviceType==D3DDEVTYPE_HAL);
    CHECK_HR(device->GetDeviceCaps(&caps));
    CHECK(caps.VertexShaderVersion==D3DVS_VERSION(3,0));
    IDirect3D9* owner=nullptr;
    CHECK_HR(device->GetDirect3D(&owner));
    CHECK(owner==static_cast<IDirect3D9*>(ex));
    if(owner)owner->Release();
    CHECK_HR(device->TestCooperativeLevel());
    CHECK(device->GetNumberOfSwapChains()==1);

    // The swap chain and its back buffer, which is the device's first render
    // target exactly as on Windows.
    IDirect3DSwapChain9* chain=nullptr;
    CHECK_HR(device->GetSwapChain(0,&chain));
    CHECK(chain!=nullptr);
    if(chain) {
        D3DPRESENT_PARAMETERS got{};
        CHECK_HR(chain->GetPresentParameters(&got));
        CHECK(got.BackBufferWidth==320&&got.BackBufferFormat==D3DFMT_X8R8G8B8);
        IDirect3DDevice9* chainDevice=nullptr;
        CHECK_HR(chain->GetDevice(&chainDevice));
        CHECK(chainDevice==static_cast<IDirect3DDevice9*>(device));
        if(chainDevice)chainDevice->Release();
    }
    IDirect3DSurface9* backBuffer=nullptr;
    CHECK_HR(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer));
    CHECK(backBuffer!=nullptr);
    if(backBuffer) {
        D3DSURFACE_DESC desc{};
        CHECK_HR(backBuffer->GetDesc(&desc));
        CHECK(desc.Width==320&&desc.Height==240&&desc.Format==D3DFMT_X8R8G8B8);
        CHECK(desc.Usage&D3DUSAGE_RENDERTARGET);
        CHECK(backBuffer->GetType()==D3DRTYPE_SURFACE);
        IDirect3DDevice9* surfaceDevice=nullptr;
        CHECK_HR(backBuffer->GetDevice(&surfaceDevice));
        CHECK(surfaceDevice==static_cast<IDirect3DDevice9*>(device));
        if(surfaceDevice)surfaceDevice->Release();
        // A default-pool surface is not lockable, as on Windows.
        D3DLOCKED_RECT locked{};
        CHECK_HR_IS(backBuffer->LockRect(&locked,nullptr,0),D3DERR_INVALIDCALL);
        // The swap chain's back buffer is the device's render target 0.
        IDirect3DSurface9* current=nullptr;
        CHECK_HR(device->GetRenderTarget(0,&current));
        CHECK(current==backBuffer);
        if(current)current->Release();
    }

    // A depth-stencil surface, which the auto depth-stencil parameter created.
    IDirect3DSurface9* depth=nullptr;
    CHECK_HR(device->GetDepthStencilSurface(&depth));
    CHECK(depth!=nullptr);
    if(depth) {
        D3DSURFACE_DESC desc{};
        CHECK_HR(depth->GetDesc(&desc));
        CHECK(desc.Format==D3DFMT_D24S8);
        depth->Release();
    }

    // The frame path: BeginScene, clear, EndScene, Present.
    CHECK_HR(device->BeginScene());
    CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,
        0xff204060,1.0f,0));
    CHECK_HR(device->EndScene());
    HRESULT presented=device->Present(nullptr,nullptr,nullptr,nullptr);
    // Wine's windowed path presents; a lost or occluded device is still an
    // answer, a crash is not.
    outHex("INFO Present hr=",(unsigned long)presented);
    CHECK(presented==D3D_OK||presented==D3DERR_DEVICELOST);
    CHECK_HR(device->PresentEx(nullptr,nullptr,nullptr,nullptr,0));

    // Viewport and scissor rect reach the D3D11 context and read back.
    D3DVIEWPORT9 viewport{0,0,320,240,0.0f,1.0f};
    CHECK_HR(device->SetViewport(&viewport));
    D3DVIEWPORT9 readback{};
    CHECK_HR(device->GetViewport(&readback));
    CHECK(readback.Width==320&&readback.MaxZ==1.0f);
    RECT scissor{0,0,160,120};
    CHECK_HR(device->SetScissorRect(&scissor));
    RECT scissorBack{};
    CHECK_HR(device->GetScissorRect(&scissorBack));
    CHECK(scissorBack.right==160&&scissorBack.bottom==120);

    // Render states and sampler states are stored and read back; the
    // translation that consumes them is the unfinished half.
    CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE));
    DWORD value=0;
    CHECK_HR(device->GetRenderState(D3DRS_ZENABLE,&value));
    CHECK(value==D3DZB_TRUE);
    CHECK_HR(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));
    CHECK_HR(device->GetSamplerState(0,D3DSAMP_MINFILTER,&value));
    CHECK(value==D3DTEXF_LINEAR);
    CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE));
    CHECK_HR(device->GetTextureStageState(0,D3DTSS_COLOROP,&value));
    CHECK(value==D3DTOP_MODULATE);

    // Shader constants are stored and read back.
    const float constants[2][4]={{1,2,3,4},{5,6,7,8}};
    CHECK_HR(device->SetVertexShaderConstantF(0,&constants[0][0],2));
    float returned[2][4]{};
    CHECK_HR(device->GetVertexShaderConstantF(0,&returned[0][0],2));
    CHECK(returned[1][3]==8.0f);
    CHECK_HR(device->SetPixelShaderConstantF(4,&constants[0][0],1));
    float pixel[4]{};
    CHECK_HR(device->GetPixelShaderConstantF(4,pixel,1));
    CHECK(pixel[0]==1.0f&&pixel[3]==4.0f);

    // A render target the guest creates, set, cleared and unset.
    IDirect3DSurface9* target=nullptr;
    CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    CHECK(target!=nullptr);
    if(target) {
        CHECK_HR(device->SetRenderTarget(0,target));
        IDirect3DSurface9* current=nullptr;
        CHECK_HR(device->GetRenderTarget(0,&current));
        CHECK(current==target);
        if(current)current->Release();
        CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff00ff00,1.0f,0));
        // Back to the swap chain's back buffer.
        CHECK_HR(device->SetRenderTarget(0,backBuffer));
        target->Release();
    }

    // A depth-stencil surface the guest creates and binds.
    IDirect3DSurface9* ownDepth=nullptr;
    CHECK_HR(device->CreateDepthStencilSurface(64,64,D3DFMT_D16,D3DMULTISAMPLE_NONE,0,FALSE,&ownDepth,nullptr));
    CHECK(ownDepth!=nullptr);
    if(ownDepth) {
        CHECK_HR(device->SetDepthStencilSurface(ownDepth));
        CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,0.5f,0));
        ownDepth->Release();
    }

    // A SYSTEMMEM surface is CPU-visible and locks, unlike the back buffer.
    IDirect3DSurface9* staging=nullptr;
    CHECK_HR(device->CreateOffscreenPlainSurface(16,16,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,
        &staging,nullptr));
    if(staging) {
        D3DLOCKED_RECT locked{};
        CHECK_HR(staging->LockRect(&locked,nullptr,0));
        CHECK(locked.pBits!=nullptr&&locked.Pitch>=64);
        // A plain lock on a SYSTEMMEM surface is writable, which is how a game
        // fills an offscreen surface; the write must land in the texture.
        if(locked.pBits)*static_cast<unsigned*>(locked.pBits)=0xff00ff00u;
        CHECK_HR(staging->UnlockRect());
        CHECK_HR(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        if(locked.pBits)CHECK((*static_cast<const unsigned*>(locked.pBits)&0x00ffffffu)==0x0000ff00u);
        CHECK_HR(staging->UnlockRect());
        CHECK_HR_IS(staging->UnlockRect(),D3DERR_INVALIDCALL);   // not locked twice
        staging->Release();
    }

    // Drawing. The shim renders a triangle into an offscreen render target,
    // reads the pixels back through a SYSTEMMEM surface, and checks the colour
    // the fixed-function pipeline was asked for is where the triangle is and
    // nowhere else. This is the whole draw path: buffers, a vertex declaration,
    // the generated fixed-function shaders, the translator, the states.
    {
        IDirect3DSurface9* target=nullptr;
        CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
            &target,nullptr));
        IDirect3DSurface9* staging=nullptr;
        CHECK_HR(device->CreateOffscreenPlainSurface(64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
            &staging,nullptr));
        IDirect3DVertexBuffer9* buffer=nullptr;
        CHECK_HR(device->CreateVertexBuffer(3*sizeof(float)*5,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
            &buffer,nullptr));
        if(target&&staging&&buffer) {
            // Three vertices in clip space: a triangle over the left half of the
            // target. D3D9's XYZRHW vertices are already in screen space, so
            // the generated shader passes them through untransformed.
            // D3DFVF_XYZRHW|D3DFVF_DIFFUSE: four floats then a D3DCOLOR, so the
            // vertex is a struct rather than an array of floats (a D3DCOLOR
            // written as a float literal is a different bit pattern).
            struct Vertex { float x,y,z,w; DWORD colour; };
            Vertex* vertices=nullptr;
            CHECK_HR(buffer->Lock(0,0,reinterpret_cast<void**>(&vertices),0));
            if(vertices) {
                const Vertex data[3]={
                    { 8.0f, 8.0f,0.0f,1.0f,0xffff0000},   // red, top left
                    {56.0f, 8.0f,0.0f,1.0f,0xffff0000},   // red, top right
                    {32.0f,56.0f,0.0f,1.0f,0xffff0000},   // red, bottom middle
                };
                for(unsigned i=0;i<3;++i)vertices[i]=data[i];
            }
            CHECK_HR(buffer->Unlock());
            CHECK_HR(device->SetStreamSource(0,buffer,0,sizeof(Vertex)));
            CHECK_HR(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
            CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            // Stage 0 selects the vertex colour: no texture in this draw.
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetRenderTarget(0,target));
            CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0));
            D3DVIEWPORT9 full{0,0,64,64,0.0f,1.0f};
            CHECK_HR(device->SetViewport(&full));
            CHECK_HR(device->BeginScene());
            CHECK_HR(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));
            CHECK_HR(device->EndScene());
            CHECK_HR(device->GetRenderTargetData(target,staging));
            D3DLOCKED_RECT locked{};
            CHECK_HR(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
            if(locked.pBits) {
                // Inside the triangle: the vertex colour. Outside: the clear
                // colour. A draw that silently did nothing fails both.
                const unsigned char* inside=static_cast<const unsigned char*>(locked.pBits)
                    +32*locked.Pitch+32*4;
                const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
                    +4*locked.Pitch+4*4;
                outHex("INFO inside bgra=",unsigned(inside[0])|unsigned(inside[1])<<8
                    |unsigned(inside[2])<<16|unsigned(inside[3])<<24);
                for(unsigned y=0;y<64;y+=8) {
                    char row[80];unsigned used=0;
                    for(unsigned x=0;x<64;x+=8) {
                        const unsigned char* p=static_cast<const unsigned char*>(locked.pBits)
                            +y*locked.Pitch+x*4;
                        row[used++]=p[2]>0x80?'R':p[1]>0x80?'G':p[0]>0x80?'B':'.';
                    }
                    row[used++]='\n';row[used]=0;out(row);
                }
                CHECK(inside[2]>0x80&&inside[0]<0x40&&inside[1]<0x40);   // red
                CHECK(outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20); // black
            }
            CHECK_HR(staging->UnlockRect());
            CHECK_HR(device->SetRenderTarget(0,backBuffer));
        }
        if(buffer)buffer->Release();
        if(staging)staging->Release();
        if(target)target->Release();
    }

    // A texture, a sampler and a texture-stage operation: the shim uploads the
    // pixels, binds the view and the sampler, and the generated pixel shader
    // samples them.
    {
        IDirect3DTexture9* texture=nullptr;
        CHECK_HR(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
        if(texture) {
            D3DLOCKED_RECT locked{};
            CHECK_HR(texture->LockRect(0,&locked,nullptr,0));
            if(locked.pBits) {
                for(unsigned y=0;y<2;++y) {
                    unsigned* row=reinterpret_cast<unsigned*>(
                        static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch);
                    row[0]=0xff00ff00u;row[1]=0xff00ff00u;   // green
                }
            }
            CHECK_HR(texture->UnlockRect(0));
            CHECK_HR(device->SetTexture(0,texture));
            IDirect3DBaseTexture9* bound=nullptr;
            CHECK_HR(device->GetTexture(0,&bound));
            CHECK(bound==static_cast<IDirect3DBaseTexture9*>(texture));
            if(bound)bound->Release();
            CHECK_HR(device->SetTexture(0,nullptr));
            CHECK_HR(device->GetTexture(0,&bound));
            CHECK(bound==nullptr);
            texture->Release();
        }
        // A MANAGED texture with a mip chain is still lockable at every level,
        // as in D3D9: D3D11 cannot make it dynamic, so the shim locks through a
        // staging copy and copies the level back on unlock.
        IDirect3DTexture9* mipmapped=nullptr;
        CHECK_HR(device->CreateTexture(8,8,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&mipmapped,nullptr));
        if(mipmapped) {
            CHECK(mipmapped->GetLevelCount()==4);
            D3DLOCKED_RECT locked{};
            CHECK_HR(mipmapped->LockRect(0,&locked,nullptr,0));
            if(locked.pBits) {
                for(unsigned y=0;y<8;++y) {
                    unsigned* row=reinterpret_cast<unsigned*>(
                        static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch);
                    for(unsigned x=0;x<8;++x)row[x]=0xff0000ffu;   // blue
                }
            }
            CHECK_HR(mipmapped->UnlockRect(0));
            // The write reached the texture, not just the staging copy: read the
            // level back through a SYSTEMMEM surface.
            IDirect3DSurface9* staging=nullptr;
            CHECK_HR(device->CreateOffscreenPlainSurface(8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,
                &staging,nullptr));
            if(staging) {
                IDirect3DSurface9* level0=nullptr;
                CHECK_HR(mipmapped->GetSurfaceLevel(0,&level0));
                if(level0) {
                    CHECK_HR(device->GetRenderTargetData(level0,staging));
                    CHECK_HR(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
                    if(locked.pBits) {
                        const unsigned char* p=static_cast<const unsigned char*>(locked.pBits);
                        CHECK(p[0]>0x80&&p[1]<0x40&&p[2]<0x40);   // blue in BGRA
                    }
                    CHECK_HR(staging->UnlockRect());
                    level0->Release();
                }
                staging->Release();
            }
            mipmapped->Release();
        }
    }

    // A level surface's lifetime is its own: it holds its container texture and
    // the MANAGED staging shadow, so the guest may release the texture first,
    // lock through a surface that outlives it, forget an UnlockRect, or fetch
    // the same level twice. The write still has to reach what a draw samples.
    {
        IDirect3DTexture9* texture=nullptr;
        CHECK_HR(device->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
        IDirect3DSurface9* level0=nullptr;
        CHECK_HR(texture->GetSurfaceLevel(0,&level0));
        CHECK(level0!=nullptr);
        // The same level twice: two surfaces, each with its own reference.
        IDirect3DSurface9* level0b=nullptr;
        CHECK_HR(texture->GetSurfaceLevel(0,&level0b));
        CHECK(level0b!=nullptr&&level0b!=level0);
        if(level0b)level0b->Release();
        // The container is answerable, as D3D9 documents.
        IDirect3DTexture9* gotContainer=nullptr;
        CHECK_HR(level0->GetContainer(__uuidof(IDirect3DTexture9),
            reinterpret_cast<void**>(&gotContainer)));
        CHECK(gotContainer==texture);
        if(gotContainer)gotContainer->Release();
        // Release the texture first: the surface keeps the texture alive.
        texture->Release();
        texture=nullptr;
        if(level0) {
            D3DLOCKED_RECT locked{};
            CHECK_HR(level0->LockRect(&locked,nullptr,0));
            if(locked.pBits) {
                for(unsigned y=0;y<4;++y) {
                    unsigned* row=reinterpret_cast<unsigned*>(
                        static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch);
                    for(unsigned x=0;x<4;++x)row[x]=0xff00ff00u;   // green
                }
            }
            CHECK_HR(level0->UnlockRect());
            // A second lock of the same level reads back what the first wrote.
            CHECK_HR(level0->LockRect(&locked,nullptr,D3DLOCK_READONLY));
            if(locked.pBits) {
                const unsigned* p=static_cast<const unsigned*>(locked.pBits);
                CHECK(p[0]==0xff00ff00u);
            }
            CHECK_HR(level0->UnlockRect());
            level0->Release();
            level0=nullptr;
        }
        // A write through a level surface reaches what a draw samples: lock,
        // write a pattern, unlock, draw, read back pixels == pattern.
        CHECK_HR(device->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
        IDirect3DSurface9* target=nullptr;
        CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
            &target,nullptr));
        IDirect3DSurface9* staging=nullptr;
        CHECK_HR(device->CreateOffscreenPlainSurface(64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
            &staging,nullptr));
        if(texture&&target&&staging) {
            IDirect3DSurface9* level=nullptr;
            CHECK_HR(texture->GetSurfaceLevel(0,&level));
            if(level) {
                D3DLOCKED_RECT locked{};
                // A sub-rect lock: only the top-left 2x2 texels are written;
                // the shim maps the whole level, so the write goes at the start.
                RECT sub{0,0,2,2};
                CHECK_HR(level->LockRect(&locked,&sub,0));
                if(locked.pBits) {
                    unsigned* row=reinterpret_cast<unsigned*>(locked.pBits);
                    row[0]=0xff0000ffu;row[1]=0xff0000ffu;   // blue
                }
                CHECK_HR(level->UnlockRect());
                CHECK_HR(device->SetTexture(0,texture));
                CHECK_HR(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
                CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,FALSE));
                CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
                CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
                CHECK_HR(device->SetRenderTarget(0,target));
                CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0));
                D3DVIEWPORT9 full{0,0,64,64,0.0f,1.0f};
                CHECK_HR(device->SetViewport(&full));
                struct Vertex { float x,y,z,w; float u,v; };
                // One quad covering the target: the top half samples the
                // written sub-rect, the bottom half the untouched texels.
                const Vertex quad[6]={
                    { 0.0f, 0.0f,0.0f,1.0f,0.0f,0.0f},
                    {64.0f, 0.0f,0.0f,1.0f,0.25f,0.0f},
                    { 0.0f,64.0f,0.0f,1.0f,0.0f,1.0f},
                    {64.0f, 0.0f,0.0f,1.0f,0.25f,0.0f},
                    {64.0f,64.0f,0.0f,1.0f,0.25f,1.0f},
                    { 0.0f,64.0f,0.0f,1.0f,0.0f,1.0f},
                };
                CHECK_HR(device->BeginScene());
                CHECK_HR(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,quad,sizeof(Vertex)));
                CHECK_HR(device->EndScene());
                CHECK_HR(device->GetRenderTargetData(target,staging));
                CHECK_HR(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
                if(locked.pBits) {
                    // The quad's u spans one texel (0..0.25) and v the whole
                    // texture, so target (8,8) samples texel (0,0) — inside the
                    // written sub-rect — and (32,60) samples texel (2,2), which
                    // the sub-rect lock never touched.
                    const unsigned char* inside=static_cast<const unsigned char*>(locked.pBits)
                        +8*locked.Pitch+8*4;
                    const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
                        +60*locked.Pitch+32*4;
                    CHECK(inside[0]>0x80&&inside[1]<0x40&&inside[2]<0x40);   // blue
                    CHECK(outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20); // black
                }
                CHECK_HR(staging->UnlockRect());
                CHECK_HR(device->SetTexture(0,nullptr));
                CHECK_HR(device->SetRenderTarget(0,backBuffer));
                level->Release();
            }
        }
        // Released while locked: the destructor finishes the lock, so a later
        // read of the level still sees the write.
        if(texture) {
            IDirect3DSurface9* level=nullptr;
            CHECK_HR(texture->GetSurfaceLevel(0,&level));
            if(level) {
                D3DLOCKED_RECT locked{};
                CHECK_HR(level->LockRect(&locked,nullptr,0));
                if(locked.pBits) {
                    unsigned* row=reinterpret_cast<unsigned*>(locked.pBits);
                    row[0]=0xff0000ffu;   // blue, left locked on purpose
                }
                level->Release();   // no UnlockRect: ~Surface unmaps and copies up
            }
            IDirect3DSurface9* staging4=nullptr;
            CHECK_HR(device->CreateOffscreenPlainSurface(4,4,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,
                &staging4,nullptr));
            IDirect3DSurface9* level0c=nullptr;
            CHECK_HR(texture->GetSurfaceLevel(0,&level0c));
            if(staging4&&level0c) {
                CHECK_HR(device->GetRenderTargetData(level0c,staging4));
                D3DLOCKED_RECT locked{};
                CHECK_HR(staging4->LockRect(&locked,nullptr,D3DLOCK_READONLY));
                if(locked.pBits) {
                    const unsigned char* p=static_cast<const unsigned char*>(locked.pBits);
                    CHECK(p[0]>0x80&&p[1]<0x40&&p[2]<0x40);   // blue reached the texture
                }
                CHECK_HR(staging4->UnlockRect());
            }
            if(level0c)level0c->Release();
            if(staging4)staging4->Release();
            texture->Release();
            texture=nullptr;
        }
        if(staging)staging->Release();
        if(target)target->Release();
    }

    // A shader the guest compiles itself goes through the translator: this is
    // the same vs_1_1 the translator's own regression uses.
    {
        const DWORD tokens[]={
            0xFFFE0101,                                     // vs_1_1
            (0x1F)|(2<<24),0x00000000,0xE4000000,           // dcl_position v0
            (20)|(3<<24),0xC0000000,0xE4000000,0xA0000000,  // m4x4 oPos, v0, c0
            0x0000FFFF,                                     // end
        };
        IDirect3DVertexShader9* shader=nullptr;
        CHECK_HR(device->CreateVertexShader(tokens,&shader));
        CHECK(shader!=nullptr);
        if(shader) {
            UINT size=0;
            CHECK_HR(shader->GetFunction(nullptr,&size));
            CHECK(size==sizeof tokens);
            // A buffer too small for the bytecode is refused without a write:
            // the caller's own capacity is still what it passed in.
            DWORD shortBuffer[2]{};
            UINT shortSize=2;
            CHECK_HR_IS(shader->GetFunction(shortBuffer,&shortSize),D3DERR_INVALIDCALL);
            CHECK(shortSize==2);
            CHECK(shortBuffer[0]==0&&shortBuffer[1]==0);
            shader->Release();
        }
        // A blob with no END token is not a shader, and the shim says so.
        const DWORD truncated[]={0xFFFE0101,0x0000FFFF|(1<<24)};
        IDirect3DVertexShader9* bad=nullptr;
        CHECK_HR_IS(device->CreateVertexShader(truncated,&bad),D3DERR_INVALIDCALL);
        CHECK(bad==nullptr);
    }

    // Vertex declarations report their element count, terminator included, and
    // refuse a buffer that cannot hold them instead of overrunning it.
    {
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            D3DDECL_END(),
        };
        IDirect3DVertexDeclaration9* declaration=nullptr;
        CHECK_HR(device->CreateVertexDeclaration(elements,&declaration));
        if(declaration) {
            UINT count=0;
            CHECK_HR(declaration->GetDeclaration(nullptr,&count));
            CHECK(count==3);   // two elements and the terminator, not bytes
            D3DVERTEXELEMENT9 readBack[3]{};
            count=3;
            CHECK_HR(declaration->GetDeclaration(readBack,&count));
            CHECK(count==3);
            CHECK(readBack[1].Usage==D3DDECLUSAGE_TEXCOORD);
            // A capacity of two cannot hold three elements.
            D3DVERTEXELEMENT9 small[2]{};
            count=2;
            CHECK_HR_IS(declaration->GetDeclaration(small,&count),D3DERR_INVALIDCALL);
            declaration->Release();
        }
    }

    // An FVF with one-float texcoords decodes them as one float, not two: the
    // size field is a two-bit mask at bit 16, not a shift count.
    {
        CHECK_HR(device->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE1(0)));
        CHECK_HR(device->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE4(0)));
    }

    // A texture with Levels=0 asks for the full chain down to 1x1.
    {
        IDirect3DTexture9* chainTexture=nullptr;
        CHECK_HR(device->CreateTexture(8,8,0,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
            &chainTexture,nullptr));
        if(chainTexture) {
            CHECK(chainTexture->GetLevelCount()==4);   // 8, 4, 2, 1
            chainTexture->Release();
        }
    }

    // The UP draw path: the primitive count is converted to vertices, and the
    // data is copied from guest memory into a dynamic buffer.
    {
        struct Vertex { float x,y,z,w; DWORD colour; };
        const Vertex vertices[3]={
            { 8.0f, 8.0f,0.0f,1.0f,0xffff0000},
            {56.0f, 8.0f,0.0f,1.0f,0xffff0000},
            {32.0f,56.0f,0.0f,1.0f,0xffff0000},
        };
        CHECK_HR(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
        CHECK_HR(device->BeginScene());
        CHECK_HR(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex)));
        CHECK_HR(device->EndScene());
    }

    // An indexed UP draw whose indices start at a non-zero MinVertexIndex: the
    // array holds MinVertexIndex+NumVertices vertices, and the indices address
    // it from vertex 0. A shim that copies only NumVertices from the start, or
    // that adds MinVertexIndex again, reads the wrong vertices and draws the
    // wrong colour.
    {
        struct Vertex { float x,y,z,w; DWORD colour; };
        const Vertex vertices[5]={
            { 0.0f, 0.0f,0.0f,1.0f,0xff000000},   // black, unused
            { 0.0f, 0.0f,0.0f,1.0f,0xff000000},   // black, unused
            { 8.0f, 8.0f,0.0f,1.0f,0xff00ff00},   // green, top left
            {56.0f, 8.0f,0.0f,1.0f,0xff00ff00},   // green, top right
            {32.0f,56.0f,0.0f,1.0f,0xff00ff00},   // green, bottom middle
        };
        const unsigned short indices[3]={2,3,4};
        IDirect3DSurface9* target=nullptr;
        CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
            &target,nullptr));
        IDirect3DSurface9* staging=nullptr;
        CHECK_HR(device->CreateOffscreenPlainSurface(64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
            &staging,nullptr));
        if(target&&staging) {
            CHECK_HR(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
            CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetRenderTarget(0,target));
            CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0));
            D3DVIEWPORT9 full{0,0,64,64,0.0f,1.0f};
            CHECK_HR(device->SetViewport(&full));
            CHECK_HR(device->BeginScene());
            CHECK_HR(device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,2,3,1,indices,
                D3DFMT_INDEX16,vertices,sizeof(Vertex)));
            CHECK_HR(device->EndScene());
            CHECK_HR(device->GetRenderTargetData(target,staging));
            D3DLOCKED_RECT locked{};
            CHECK_HR(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
            if(locked.pBits) {
                const unsigned char* inside=static_cast<const unsigned char*>(locked.pBits)
                    +32*locked.Pitch+32*4;
                CHECK(inside[1]>0x80&&inside[0]<0x40&&inside[2]<0x40);   // green
            }
            CHECK_HR(staging->UnlockRect());
            CHECK_HR(device->SetRenderTarget(0,backBuffer));
        }
        if(staging)staging->Release();
        if(target)target->Release();
    }

    // The fixed-function texture-stage operations a 2D game sets, proven by
    // pixel readback. One texture, one diffuse colour, one triangle per case;
    // the expected pixel is written next to each case. The texture texel is
    // 0x80204080 (r=0x20 g=0x40 b=0x80 a=0x80), the diffuse 0xFF80C040
    // (r=0x80 g=0xC0 b=0x40), so every op lands on a distinct value.
    {
        IDirect3DSurface9* target=nullptr;
        CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
            &target,nullptr));
        IDirect3DSurface9* staging=nullptr;
        CHECK_HR(device->CreateOffscreenPlainSurface(64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
            &staging,nullptr));
        IDirect3DTexture9* texture=nullptr;
        CHECK_HR(device->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
        IDirect3DVertexBuffer9* buffer=nullptr;
        CHECK_HR(device->CreateVertexBuffer(3*(4*sizeof(float)+sizeof(DWORD)+2*sizeof(float)),
            D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&buffer,nullptr));
        if(target&&staging&&texture&&buffer) {
            D3DLOCKED_RECT texLocked{};
            CHECK_HR(texture->LockRect(0,&texLocked,nullptr,0));
            if(texLocked.pBits) {
                for(unsigned y=0;y<4;++y) {
                    unsigned* row=reinterpret_cast<unsigned*>(
                        static_cast<unsigned char*>(texLocked.pBits)+y*texLocked.Pitch);
                    for(unsigned x=0;x<4;++x)row[x]=0x80204080u;
                }
            }
            CHECK_HR(texture->UnlockRect(0));
            struct Vertex { float x,y,z,w; DWORD colour; float u,v; };
            Vertex* vertices=nullptr;
            CHECK_HR(buffer->Lock(0,0,reinterpret_cast<void**>(&vertices),0));
            if(vertices) {
                const Vertex data[3]={
                    { 8.0f, 8.0f,0.0f,1.0f,0xff80c040,0.5f,0.5f},
                    {56.0f, 8.0f,0.0f,1.0f,0xff80c040,0.5f,0.5f},
                    {32.0f,56.0f,0.0f,1.0f,0xff80c040,0.5f,0.5f},
                };
                for(unsigned i=0;i<3;++i)vertices[i]=data[i];
            }
            CHECK_HR(buffer->Unlock());
            CHECK_HR(device->SetStreamSource(0,buffer,0,sizeof(Vertex)));
            CHECK_HR(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1));
            CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetTexture(0,texture));
            struct Case {
                const char* name; DWORD op,arg1,arg2,tfactor;
                unsigned char r,g,b;   // expected centre pixel
            };
            // Expected values, from the texel (r .125 g .25 b .5 a .5) and the
            // diffuse (r .5 g .75 b .25): modulate .0625/.1875/.125, add
            // .625/1/.75, subtract diffuse-texture .375/.5/-0.25, and so on.
            const Case cases[]={
                {"modulate",        D3DTOP_MODULATE,   D3DTA_TEXTURE,D3DTA_DIFFUSE,0,         0x10,0x30,0x20},
                {"modulate2x",      D3DTOP_MODULATE2X, D3DTA_TEXTURE,D3DTA_DIFFUSE,0,         0x20,0x60,0x40},
                {"modulate4x",      D3DTOP_MODULATE4X, D3DTA_TEXTURE,D3DTA_DIFFUSE,0,         0x40,0xc0,0x80},
                {"add",             D3DTOP_ADD,        D3DTA_TEXTURE,D3DTA_DIFFUSE,0,         0xa0,0xff,0xc0},
                {"subtract",        D3DTOP_SUBTRACT,   D3DTA_DIFFUSE,D3DTA_TEXTURE,0,         0x60,0x80,0x00},
                {"tfactor",         D3DTOP_SELECTARG1, D3DTA_TFACTOR,D3DTA_DIFFUSE,0xff305070,0x30,0x50,0x70},
                {"complement",      D3DTOP_SELECTARG1, D3DTA_DIFFUSE|D3DTA_COMPLEMENT,D3DTA_DIFFUSE,0,0x7f,0x3f,0xbf},
                {"tfactor-modulate",D3DTOP_MODULATE,   D3DTA_TEXTURE,D3DTA_TFACTOR,0xff305070,0x06,0x14,0x38},
                {"alpha-replicate", D3DTOP_MODULATE,   D3DTA_TEXTURE|D3DTA_ALPHAREPLICATE,D3DTA_DIFFUSE,0,0x40,0x60,0x20},
            };
            for(const Case& c:cases) {
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,c.op));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG1,c.arg1));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG2,c.arg2));
                CHECK_HR(device->SetRenderState(D3DRS_TEXTUREFACTOR,c.tfactor));
                CHECK_HR(device->SetRenderTarget(0,target));
                CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0));
                D3DVIEWPORT9 full{0,0,64,64,0.0f,1.0f};
                CHECK_HR(device->SetViewport(&full));
                CHECK_HR(device->BeginScene());
                CHECK_HR(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));
                CHECK_HR(device->EndScene());
                CHECK_HR(device->GetRenderTargetData(target,staging));
                D3DLOCKED_RECT locked{};
                CHECK_HR(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
                if(locked.pBits) {
                    const unsigned char* p=static_cast<const unsigned char*>(locked.pBits)
                        +32*locked.Pitch+32*4;
                    // p is BGRA; c.r/g/b are RGB. Two counts of rounding.
                    out(c.name);out(" bgra=");
                    outHex("",unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16);
                    CHECK(p[2]>c.r-3&&p[2]<c.r+3);   // red
                    CHECK(p[1]>c.g-3&&p[1]<c.g+3);   // green
                    CHECK(p[0]>c.b-3&&p[0]<c.b+3);   // blue
                }
                CHECK_HR(staging->UnlockRect());
            }
            CHECK_HR(device->SetTexture(0,nullptr));
            CHECK_HR(device->SetRenderTarget(0,backBuffer));
        }
        if(buffer)buffer->Release();
        if(texture)texture->Release();
        if(staging)staging->Release();
        if(target)target->Release();
    }

    // Reset rebuilds the swap chain, as D3D9 documents.
    CHECK_HR(device->Reset(&present));
    IDirect3DSurface9* afterReset=nullptr;
    CHECK_HR(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&afterReset));
    CHECK(afterReset!=nullptr);
    if(afterReset)afterReset->Release();

    // Release everything the guest holds before the device: Wine never finishes
    // process exit while a D3D object outlives its device.
    if(backBuffer)backBuffer->Release();
    if(chain)chain->Release();
    device->Release();
    ex->Release();
    if(window)DestroyWindow(window);
    FreeLibrary(module);
    outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);
    return failures?1:0;
}
