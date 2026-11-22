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
// A query's answer is not instant on a real host: poll with FLUSH until it
// leaves S_FALSE, bounded so a query that never completes fails the test
// instead of hanging it.
static HRESULT drain(IDirect3DQuery9* query,void* data,DWORD size) {
    HRESULT hr=S_FALSE;
    for(unsigned i=0;i<3000&&hr==S_FALSE;++i) {
        hr=query->GetData(data,size,D3DGETDATA_FLUSH);
        if(hr==S_FALSE)Sleep(1);
    }
    return hr;
}
static ULONG refsOf(IUnknown* object) { object->AddRef();return object->Release(); }

// State-block fixtures. Two vs_2_0 shaders over a float3 POSITION in clip
// space (`mov oPos, v0` and `add oPos, v0, c0`, so a vertex constant moves the
// geometry) and two ps_2_0 shaders that write a constant (`mov oC0, c0` / c1).
static const DWORD passVs[]={0xFFFE0200,0x0200001F,0x80000000,0x900F0000,
                             0x02000001,0xC00F0000,0x90E40000,0x0000FFFF};
static const DWORD offsetVs[]={0xFFFE0200,0x0200001F,0x80000000,0x900F0000,
                               0x03000002,0xC00F0000,0x90E40000,0xA0E40000,0x0000FFFF};
static const DWORD constantPs0[]={0xFFFF0200,0x02000001,0x800F0800,0xA0E40000,0x0000FFFF};
static const DWORD constantPs1[]={0xFFFF0200,0x02000001,0x800F0800,0xA0E40001,0x0000FFFF};
struct ColourVertex { float x,y,z,w; DWORD colour; };
// A pre-transformed (XYZRHW) triangle covering the middle of a 64x64 target.
static IDirect3DVertexBuffer9* colourTriangle(IDirect3DDevice9Ex* device,DWORD colour) {
    IDirect3DVertexBuffer9* buffer=nullptr;
    if(FAILED(device->CreateVertexBuffer(3*sizeof(ColourVertex),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
            &buffer,nullptr))||!buffer)return nullptr;
    ColourVertex* v=nullptr;
    if(SUCCEEDED(buffer->Lock(0,0,reinterpret_cast<void**>(&v),0))&&v) {
        v[0]={ 8.0f, 8.0f,0.0f,1.0f,colour};
        v[1]={56.0f, 8.0f,0.0f,1.0f,colour};
        v[2]={32.0f,56.0f,0.0f,1.0f,colour};
    }
    buffer->Unlock();
    return buffer;
}
// The same coverage in clip space, as three floats a vertex shader reads.
static IDirect3DVertexBuffer9* clipTriangle(IDirect3DDevice9Ex* device) {
    IDirect3DVertexBuffer9* buffer=nullptr;
    if(FAILED(device->CreateVertexBuffer(3*3*sizeof(float),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
            &buffer,nullptr))||!buffer)return nullptr;
    float* v=nullptr;
    if(SUCCEEDED(buffer->Lock(0,0,reinterpret_cast<void**>(&v),0))&&v) {
        const float data[9]={-0.75f,-0.75f,0.5f, 0.75f,-0.75f,0.5f, 0.0f,0.75f,0.5f};
        for(unsigned i=0;i<9;++i)v[i]=data[i];
    }
    buffer->Unlock();
    return buffer;
}
// Clears `target`, draws one triangle with whatever the device now holds, and
// returns the pixel at (x,y) as 0x00RRGGBB, or 0xFFFFFFFF when a call failed.
static DWORD drawAndRead(IDirect3DDevice9Ex* device,IDirect3DSurface9* target,IDirect3DSurface9* staging,
                         unsigned x,unsigned y) {
    if(FAILED(device->SetRenderTarget(0,target)))return 0xFFFFFFFFu;
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 0xFFFFFFFFu;
    device->BeginScene();
    const HRESULT drawn=device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1);
    device->EndScene();
    if(FAILED(drawn)||FAILED(device->GetRenderTargetData(target,staging)))return 0xFFFFFFFFu;
    D3DLOCKED_RECT locked{};
    if(FAILED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))||!locked.pBits)return 0xFFFFFFFFu;
    const unsigned char* p=static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch+x*4;
    const DWORD rgb=DWORD(p[2])<<16|DWORD(p[1])<<8|p[0];
    staging->UnlockRect();
    return rgb;
}
static bool sameMatrix(const D3DMATRIX& a,const D3DMATRIX& b) {
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(a.m[r][c]!=b.m[r][c])return false;
    return true;
}
static bool sameViewport(const D3DVIEWPORT9& a,const D3DVIEWPORT9& b) {
    return a.X==b.X&&a.Y==b.Y&&a.Width==b.Width&&a.Height==b.Height&&a.MinZ==b.MinZ&&a.MaxZ==b.MaxZ;
}
static bool sameRect(const RECT& a,const RECT& b) {
    return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;
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
    CHECK_HR(ex->CheckDeviceType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_UNKNOWN,TRUE));
    CHECK_HR_IS(ex->CheckDeviceType(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DFMT_UNKNOWN,FALSE),D3DERR_NOTAVAILABLE);
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
    present.BackBufferFormat=D3DFMT_UNKNOWN;present.BackBufferCount=1;
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
    CHECK(present.BackBufferFormat==D3DFMT_X8R8G8B8);
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

    // Queries. D3D9 lets a game ask first whether a query type exists: a null
    // result pointer is that probe and must create nothing.
    const ULONG deviceRefs=refsOf(device);
    {
        const D3DQUERYTYPE supported[]={D3DQUERYTYPE_EVENT,D3DQUERYTYPE_OCCLUSION,D3DQUERYTYPE_TIMESTAMP,
            D3DQUERYTYPE_TIMESTAMPDISJOINT,D3DQUERYTYPE_TIMESTAMPFREQ};
        for(D3DQUERYTYPE type:supported) {
            HRESULT hr=device->CreateQuery(type,nullptr);
            if(type==D3DQUERYTYPE_EVENT||type==D3DQUERYTYPE_OCCLUSION)CHECK_HR_IS(hr,D3D_OK);
            else {
                // Timestamps need host support; a host that refuses them must say
                // so with NOTAVAILABLE, never with a crash or another code.
                if(hr!=D3D_OK)outHex("INFO host refuses timestamp query type=",type);
                CHECK(hr==D3D_OK||hr==D3DERR_NOTAVAILABLE);
            }
        }
        CHECK(refsOf(device)==deviceRefs);   // the probes created nothing
        const D3DQUERYTYPE unsupported[]={D3DQUERYTYPE_VCACHE,D3DQUERYTYPE_RESOURCEMANAGER,
            D3DQUERYTYPE_VERTEXSTATS,D3DQUERYTYPE_PIPELINETIMINGS,D3DQUERYTYPE_BANDWIDTHTIMINGS,
            D3DQUERYTYPE(7),D3DQUERYTYPE(99)};
        for(D3DQUERYTYPE type:unsupported) {
            CHECK_HR_IS(device->CreateQuery(type,nullptr),D3DERR_NOTAVAILABLE);
            IDirect3DQuery9* none=reinterpret_cast<IDirect3DQuery9*>(1);
            CHECK_HR_IS(device->CreateQuery(type,&none),D3DERR_NOTAVAILABLE);
            CHECK(none==nullptr);
        }
    }

    // EVENT: signalled once the GPU has reached the END, and D3D9 calls an event
    // that was never issued signalled too.
    {
        IDirect3DQuery9* event=nullptr;
        CHECK_HR(device->CreateQuery(D3DQUERYTYPE_EVENT,&event));
        CHECK(event!=nullptr);
        if(event) {
            CHECK(event->GetType()==D3DQUERYTYPE_EVENT);
            CHECK(event->GetDataSize()==sizeof(BOOL));
            CHECK(refsOf(device)==deviceRefs+1);   // the query holds its device
            IDirect3DDevice9* queryDevice=nullptr;
            CHECK_HR(event->GetDevice(&queryDevice));
            CHECK(queryDevice==static_cast<IDirect3DDevice9*>(device));
            CHECK(refsOf(device)==deviceRefs+2);   // GetDevice adds its own
            if(queryDevice)queryDevice->Release();
            CHECK_HR_IS(event->GetDevice(nullptr),D3DERR_INVALIDCALL);
            void* unknown=nullptr;
            CHECK_HR(event->QueryInterface(__uuidof(IDirect3DQuery9),&unknown));
            if(unknown)static_cast<IUnknown*>(unknown)->Release();
            CHECK_HR_IS(event->QueryInterface(__uuidof(IDirect3DDevice9),&unknown),E_NOINTERFACE);

            BOOL signalled=FALSE;
            CHECK_HR_IS(event->GetData(&signalled,sizeof signalled,0),S_OK);
            CHECK(signalled==TRUE);
            // Only an END exists for an event; the other flag combinations do not.
            CHECK_HR_IS(event->Issue(D3DISSUE_BEGIN),D3DERR_INVALIDCALL);
            CHECK_HR_IS(event->Issue(0),D3DERR_INVALIDCALL);
            CHECK_HR_IS(event->Issue(D3DISSUE_BEGIN|D3DISSUE_END),D3DERR_INVALIDCALL);
            CHECK_HR_IS(event->Issue(4),D3DERR_INVALIDCALL);
            CHECK_HR_IS(event->Issue(0x80000000u),D3DERR_INVALIDCALL);
            CHECK_HR(event->Issue(D3DISSUE_END));
            signalled=FALSE;
            CHECK_HR_IS(drain(event,&signalled,sizeof signalled),S_OK);
            CHECK(signalled==TRUE);
            // A readiness probe takes no buffer, and a size without a buffer is refused.
            CHECK_HR_IS(event->GetData(nullptr,0,D3DGETDATA_FLUSH),S_OK);
            CHECK_HR_IS(event->GetData(&signalled,0,D3DGETDATA_FLUSH),S_OK);
            CHECK_HR_IS(event->GetData(nullptr,sizeof(BOOL),0),D3DERR_INVALIDCALL);
            CHECK_HR_IS(event->GetData(&signalled,sizeof signalled,2),D3DERR_INVALIDCALL);
            CHECK_HR_IS(event->GetData(&signalled,sizeof signalled,0x80),D3DERR_INVALIDCALL);
            // The copy is capped at both the caller's size and the data's size.
            unsigned char bytes[8];
            for(unsigned i=0;i<8;++i)bytes[i]=0xaa;
            CHECK_HR_IS(event->GetData(bytes,2,D3DGETDATA_FLUSH),S_OK);
            CHECK(bytes[0]==1&&bytes[1]==0&&bytes[2]==0xaa);
            for(unsigned i=0;i<8;++i)bytes[i]=0xaa;
            CHECK_HR_IS(event->GetData(bytes,8,D3DGETDATA_FLUSH),S_OK);
            CHECK(bytes[0]==1&&bytes[3]==0&&bytes[4]==0xaa&&bytes[7]==0xaa);
            event->Release();
            CHECK(refsOf(device)==deviceRefs);
        }
    }

    // OCCLUSION counts the pixels that pass between BEGIN and END: a visible
    // triangle counts, and so does nothing when the draw misses the target, so a
    // query that always answers one value cannot pass both.
    {
        IDirect3DSurface9* target=nullptr;
        CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
            &target,nullptr));
        IDirect3DQuery9* occlusion=nullptr;
        CHECK_HR(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&occlusion));
        if(target&&occlusion) {
            CHECK(occlusion->GetType()==D3DQUERYTYPE_OCCLUSION);
            CHECK(occlusion->GetDataSize()==sizeof(DWORD));
            struct Vertex { float x,y,z,w; DWORD colour; };
            const Vertex visible[3]={
                { 8.0f, 8.0f,0.0f,1.0f,0xffff0000},
                {56.0f, 8.0f,0.0f,1.0f,0xffff0000},
                {32.0f,56.0f,0.0f,1.0f,0xffff0000},
            };
            Vertex outside[3];
            for(unsigned i=0;i<3;++i){outside[i]=visible[i];outside[i].x+=200.0f;}   // off the 64x64 target
            CHECK_HR(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
            CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetRenderTarget(0,target));
            D3DVIEWPORT9 full{0,0,64,64,0.0f,1.0f};
            CHECK_HR(device->SetViewport(&full));
            CHECK_HR(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0));

            DWORD pixels=0xdeadbeef;
            CHECK_HR(device->BeginScene());
            CHECK_HR(occlusion->Issue(D3DISSUE_BEGIN));
            // An open interval has no answer yet, and never hangs the caller.
            CHECK_HR_IS(occlusion->GetData(&pixels,sizeof pixels,0),S_FALSE);
            CHECK_HR(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,visible,sizeof(Vertex)));
            CHECK_HR(occlusion->Issue(D3DISSUE_END));
            CHECK_HR(device->EndScene());
            CHECK_HR_IS(drain(occlusion,&pixels,sizeof pixels),S_OK);
            outNum("INFO occlusion visible pixels=",pixels);
            // The triangle covers about 1152 pixels.
            CHECK(pixels>=900&&pixels<=1400);

            // The same query object restarts on a new BEGIN.
            pixels=0xdeadbeef;
            CHECK_HR(device->BeginScene());
            CHECK_HR(occlusion->Issue(D3DISSUE_BEGIN));
            CHECK_HR(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,outside,sizeof(Vertex)));
            CHECK_HR(occlusion->Issue(D3DISSUE_END));
            CHECK_HR(device->EndScene());
            CHECK_HR_IS(drain(occlusion,&pixels,sizeof pixels),S_OK);
            CHECK(pixels==0);

            // A bracket with no draw, and an END with no BEGIN, both count nothing.
            pixels=0xdeadbeef;
            CHECK_HR(occlusion->Issue(D3DISSUE_BEGIN));
            CHECK_HR(occlusion->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(occlusion,&pixels,sizeof pixels),S_OK);
            CHECK(pixels==0);
            pixels=0xdeadbeef;
            CHECK_HR(occlusion->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(occlusion,&pixels,sizeof pixels),S_OK);
            CHECK(pixels==0);
            // A second BEGIN before the END restarts the count rather than failing.
            CHECK_HR(occlusion->Issue(D3DISSUE_BEGIN));
            CHECK_HR(occlusion->Issue(D3DISSUE_BEGIN));
            CHECK_HR(occlusion->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(occlusion,&pixels,sizeof pixels),S_OK);
            CHECK_HR_IS(occlusion->Issue(0),D3DERR_INVALIDCALL);
            CHECK_HR_IS(occlusion->GetData(&pixels,sizeof pixels,0x10),D3DERR_INVALIDCALL);
            CHECK_HR(device->SetRenderTarget(0,backBuffer));
        }
        if(occlusion)occlusion->Release();
        if(target)target->Release();
    }

    // TIMESTAMP, TIMESTAMPDISJOINT and TIMESTAMPFREQ. A host without timestamp
    // support refuses the creation (reported above); everything else is asserted.
    {
        IDirect3DQuery9* stamp=nullptr;
        IDirect3DQuery9* later=nullptr;
        if(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,nullptr)==D3D_OK) {
            CHECK_HR(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&stamp));
            CHECK_HR(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&later));
        }
        if(stamp&&later) {
            CHECK(stamp->GetType()==D3DQUERYTYPE_TIMESTAMP);
            CHECK(stamp->GetDataSize()==sizeof(UINT64));
            CHECK_HR_IS(stamp->Issue(D3DISSUE_BEGIN),D3DERR_INVALIDCALL);
            UINT64 first=0,second=0;
            CHECK_HR(stamp->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(stamp,&first,sizeof first),S_OK);
            CHECK_HR(later->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(later,&second,sizeof second),S_OK);
            outHex("INFO timestamps first=",first);outHex("INFO timestamps second=",second);
            CHECK(first!=0&&second>=first);   // the clock only moves forward
            // A buffer smaller than the data gets the leading bytes only.
            unsigned char bytes[8];
            for(unsigned i=0;i<8;++i)bytes[i]=0xaa;
            CHECK_HR_IS(stamp->GetData(bytes,4,D3DGETDATA_FLUSH),S_OK);
            CHECK(bytes[4]==0xaa&&bytes[7]==0xaa);
        }
        if(later)later->Release();
        if(stamp)stamp->Release();

        IDirect3DQuery9* disjoint=nullptr;
        IDirect3DQuery9* frequency=nullptr;
        if(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,nullptr)==D3D_OK)
            CHECK_HR(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        if(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,nullptr)==D3D_OK)
            CHECK_HR(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
        if(disjoint) {
            CHECK(disjoint->GetType()==D3DQUERYTYPE_TIMESTAMPDISJOINT);
            CHECK(disjoint->GetDataSize()==sizeof(BOOL));
            BOOL broken=0xdeadbeef;
            CHECK_HR(disjoint->Issue(D3DISSUE_BEGIN));
            CHECK_HR(disjoint->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(disjoint,&broken,sizeof broken),S_OK);
            CHECK(broken==FALSE);   // nothing disturbed the clock; also proves it is not the frequency field
            // END alone is an empty interval, as for the other bracketed type.
            broken=0xdeadbeef;
            CHECK_HR(disjoint->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(disjoint,&broken,sizeof broken),S_OK);
            CHECK(broken==FALSE);
            CHECK_HR_IS(disjoint->Issue(0),D3DERR_INVALIDCALL);
        }
        if(frequency) {
            CHECK(frequency->GetType()==D3DQUERYTYPE_TIMESTAMPFREQ);
            CHECK(frequency->GetDataSize()==sizeof(UINT64));
            // D3D9 has no BEGIN for it; the shim brackets the D3D11 query itself.
            CHECK_HR_IS(frequency->Issue(D3DISSUE_BEGIN),D3DERR_INVALIDCALL);
            UINT64 hertz=0;
            CHECK_HR(frequency->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(frequency,&hertz,sizeof hertz),S_OK);
            outHex("INFO timestamp frequency=",hertz);
            CHECK(hertz>1);
            // Asking again answers the same clock.
            UINT64 again=0;
            CHECK_HR(frequency->Issue(D3DISSUE_END));
            CHECK_HR_IS(drain(frequency,&again,sizeof again),S_OK);
            CHECK(again==hertz);
        }
        if(frequency)frequency->Release();
        if(disjoint)disjoint->Release();
    }

    // State blocks. Every section proves a restore twice: the Get* calls read
    // the state back, and a draw whose pixel depends on it reads it again.
    {
        const ULONG blockBase=refsOf(device);
        IDirect3DSurface9* target=nullptr;
        IDirect3DSurface9* staging=nullptr;
        CHECK_HR(device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        CHECK_HR(device->CreateOffscreenPlainSurface(64,64,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        IDirect3DVertexBuffer9* red=colourTriangle(device,0xffff0000);
        IDirect3DVertexBuffer9* blue=colourTriangle(device,0xff0000ff);
        IDirect3DVertexBuffer9* clip=clipTriangle(device);
        IDirect3DTexture9* texA=nullptr;
        IDirect3DTexture9* texB=nullptr;
        CHECK_HR(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texA,nullptr));
        CHECK_HR(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texB,nullptr));
        IDirect3DIndexBuffer9* ibA=nullptr;
        IDirect3DIndexBuffer9* ibB=nullptr;
        CHECK_HR(device->CreateIndexBuffer(12,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ibA,nullptr));
        CHECK_HR(device->CreateIndexBuffer(12,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ibB,nullptr));
        IDirect3DVertexShader9* vsPass=nullptr;
        IDirect3DVertexShader9* vsOffset=nullptr;
        IDirect3DPixelShader9* psC0=nullptr;
        IDirect3DPixelShader9* psC1=nullptr;
        CHECK_HR(device->CreateVertexShader(passVs,&vsPass));
        CHECK_HR(device->CreateVertexShader(offsetVs,&vsOffset));
        CHECK_HR(device->CreatePixelShader(constantPs0,&psC0));
        CHECK_HR(device->CreatePixelShader(constantPs1,&psC1));
        const D3DVERTEXELEMENT9 positionOnly[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        IDirect3DVertexDeclaration9* declA=nullptr;
        CHECK_HR(device->CreateVertexDeclaration(positionOnly,&declA));
        const bool ready=target&&staging&&red&&blue&&clip&&texA&&texB&&ibA&&ibB&&vsPass&&vsOffset
            &&psC0&&psC1&&declA;
        IDirect3DBaseTexture9* baseA=texA;
        IDirect3DBaseTexture9* baseB=texB;
        const D3DVIEWPORT9 vpA{0,0,64,64,0.0f,1.0f};
        const D3DVIEWPORT9 vpB{0,0,32,32,0.25f,0.75f};
        const RECT scissorA{0,0,64,64};
        const RECT scissorB{0,0,16,16};
        D3DMATRIX worldA{};worldA._11=worldA._22=worldA._33=worldA._44=1.0f;
        D3DMATRIX worldB=worldA;worldB._11=2.0f;worldB._41=3.0f;
        D3DMATRIX viewA=worldA;
        D3DMATRIX viewB=worldA;viewB._42=5.0f;
        D3DMATRIX projectionA=worldA;
        D3DMATRIX projectionB=worldA;projectionB._33=0.5f;
        D3DMATERIAL9 materialA{};materialA.Diffuse.r=1.0f;materialA.Diffuse.a=1.0f;
        D3DMATERIAL9 materialB{};materialB.Diffuse.g=1.0f;materialB.Diffuse.a=0.5f;
        const DWORD fvf0=D3DFVF_XYZRHW|D3DFVF_DIFFUSE;
        const DWORD fvf1=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1;
        const DWORD fvf2=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX2;
        const DWORD fvf3=D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX3;
        const unsigned stride=sizeof(ColourVertex);
        // Read-backs. Each returns what the device holds without keeping a
        // reference, which is safe because the caller holds the object too.
        auto rs=[&](D3DRENDERSTATETYPE s){DWORD v=0xdeadbeef;device->GetRenderState(s,&v);return v;};
        auto ts=[&](DWORD stage,D3DTEXTURESTAGESTATETYPE t){DWORD v=0xdeadbeef;
            device->GetTextureStageState(stage,t,&v);return v;};
        auto ss=[&](DWORD sampler,D3DSAMPLERSTATETYPE t){DWORD v=0xdeadbeef;
            device->GetSamplerState(sampler,t,&v);return v;};
        auto texOf=[&](DWORD stage){IDirect3DBaseTexture9* t=nullptr;device->GetTexture(stage,&t);
            if(t)t->Release();return t;};
        auto streamOf=[&](UINT s,UINT* offset,UINT* strideOut){IDirect3DVertexBuffer9* b=nullptr;
            *offset=0xdead;*strideOut=0xdead;device->GetStreamSource(s,&b,offset,strideOut);
            if(b)b->Release();return b;};
        auto indicesOf=[&](){IDirect3DIndexBuffer9* b=nullptr;device->GetIndices(&b);if(b)b->Release();return b;};
        auto vsOf=[&](){IDirect3DVertexShader9* s=nullptr;device->GetVertexShader(&s);if(s)s->Release();return s;};
        auto psOf=[&](){IDirect3DPixelShader9* s=nullptr;device->GetPixelShader(&s);if(s)s->Release();return s;};
        auto declOf=[&](){IDirect3DVertexDeclaration9* d=nullptr;device->GetVertexDeclaration(&d);
            if(d)d->Release();return d;};
        auto fvfOf=[&](){DWORD v=0xdeadbeef;device->GetFVF(&v);return v;};
        auto vsConst=[&](UINT reg){float v[4]={-1,-1,-1,-1};device->GetVertexShaderConstantF(reg,v,1);return v[0];};
        auto psConst=[&](UINT reg){float v[4]={-1,-1,-1,-1};device->GetPixelShaderConstantF(reg,v,1);return v[0];};
        auto viewportOf=[&](){D3DVIEWPORT9 v{};device->GetViewport(&v);return v;};
        auto scissorOf=[&](){RECT r{};device->GetScissorRect(&r);return r;};
        auto transformOf=[&](D3DTRANSFORMSTATETYPE t){D3DMATRIX m{};device->GetTransform(t,&m);return m;};
        auto diffuseOf=[&](){D3DMATERIAL9 m{};device->GetMaterial(&m);return m;};
        const D3DRENDERSTATETYPE unnamed=D3DRENDERSTATETYPE(250);   // in no half: D3DSBT_ALL only
        if(ready) {
            // ------------------------------------------------ D3DSBT_ALL
            // A known state A, a block that holds it, then a state B that
            // changes every kind of state the block covers.
            const float vc20A[4]={1,2,3,4},vc20B[4]={9,8,7,6};
            const float pc7A[4]={5,6,7,8},pc7B[4]={4,3,2,1};
            CHECK_HR(device->SetVertexShader(nullptr));
            CHECK_HR(device->SetPixelShader(nullptr));
            CHECK_HR(device->SetVertexDeclaration(nullptr));
            CHECK_HR(device->SetFVF(fvf0));
            CHECK_HR(device->SetStreamSource(0,red,0,stride));
            CHECK_HR(device->SetStreamSource(1,nullptr,0,0));
            CHECK_HR(device->SetIndices(ibA));
            CHECK_HR(device->SetTexture(0,texA));
            CHECK_HR(device->SetTexture(3,nullptr));
            CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_COLORWRITEENABLE,0xF));
            CHECK_HR(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_TEXTUREFACTOR,0xff112233));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,0x10));
            CHECK_HR(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));
            CHECK_HR(device->SetRenderState(unnamed,1));
            CHECK_HR(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));
            CHECK_HR(device->SetSamplerState(5,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            CHECK_HR(device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));
            CHECK_HR(device->SetTextureStageState(2,D3DTSS_ALPHAARG2,D3DTA_CURRENT));
            CHECK_HR(device->SetVertexShaderConstantF(20,vc20A,1));
            CHECK_HR(device->SetPixelShaderConstantF(7,pc7A,1));
            CHECK_HR(device->SetViewport(&vpA));
            CHECK_HR(device->SetScissorRect(&scissorA));
            CHECK_HR(device->SetTransform(D3DTS_WORLD,&worldA));
            CHECK_HR(device->SetTransform(D3DTS_VIEW,&viewA));
            CHECK_HR(device->SetTransform(D3DTS_PROJECTION,&projectionA));
            CHECK_HR(device->SetMaterial(&materialA));
            CHECK(drawAndRead(device,target,staging,32,32)==0xff0000);   // red: the draw works at all
            IDirect3DStateBlock9* all=nullptr;
            CHECK_HR(device->CreateStateBlock(D3DSBT_ALL,&all));
            CHECK(all!=nullptr);
            if(all) {
                // B, first the part a draw shows: a different stream, only the
                // red channel written, a scissor that excludes the middle, a
                // smaller viewport. Any one left behind by Apply would show.
                CHECK_HR(device->SetStreamSource(0,blue,0,stride));
                CHECK_HR(device->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED));
                CHECK_HR(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
                CHECK_HR(device->SetScissorRect(&scissorB));
                CHECK_HR(device->SetViewport(&vpB));
                CHECK(drawAndRead(device,target,staging,32,32)==0x000000);
                // ...then everything else the block covers.
                CHECK_HR(device->SetFVF(fvf1));
                CHECK_HR(device->SetVertexDeclaration(declA));
                CHECK_HR(device->SetStreamSource(0,blue,4,16));
                CHECK_HR(device->SetStreamSource(1,red,8,20));
                CHECK_HR(device->SetIndices(ibB));
                CHECK_HR(device->SetTexture(0,texB));
                CHECK_HR(device->SetTexture(3,texA));
                CHECK_HR(device->SetVertexShader(vsPass));
                CHECK_HR(device->SetPixelShader(psC0));
                CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,TRUE));
                CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));
                CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,TRUE));
                CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
                CHECK_HR(device->SetRenderState(D3DRS_TEXTUREFACTOR,0xff445566));
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,0x77));
                CHECK_HR(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME));
                CHECK_HR(device->SetRenderState(unnamed,2));
                CHECK_HR(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));
                CHECK_HR(device->SetSamplerState(5,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,1));
                CHECK_HR(device->SetTextureStageState(2,D3DTSS_ALPHAARG2,D3DTA_TFACTOR));
                CHECK_HR(device->SetVertexShaderConstantF(20,vc20B,1));
                CHECK_HR(device->SetPixelShaderConstantF(7,pc7B,1));
                CHECK_HR(device->SetTransform(D3DTS_WORLD,&worldB));
                CHECK_HR(device->SetTransform(D3DTS_VIEW,&viewB));
                CHECK_HR(device->SetTransform(D3DTS_PROJECTION,&projectionB));
                CHECK_HR(device->SetMaterial(&materialB));
                // B really is B before the block is applied.
                CHECK(fvfOf()==fvf1&&declOf()==declA&&vsOf()==vsPass&&psOf()==psC0);
                CHECK_HR(all->Apply());
                UINT offset=0,strideBack=0;
                CHECK(streamOf(0,&offset,&strideBack)==red&&offset==0&&strideBack==stride);
                CHECK(streamOf(1,&offset,&strideBack)==nullptr&&offset==0&&strideBack==0);
                CHECK(indicesOf()==ibA);
                CHECK(texOf(0)==baseA&&texOf(3)==nullptr);
                CHECK(fvfOf()==fvf0&&declOf()==nullptr);
                CHECK(vsOf()==nullptr&&psOf()==nullptr);
                CHECK(rs(D3DRS_LIGHTING)==FALSE&&rs(D3DRS_CULLMODE)==D3DCULL_NONE&&rs(D3DRS_ZENABLE)==FALSE);
                CHECK(rs(D3DRS_ALPHABLENDENABLE)==FALSE&&rs(D3DRS_COLORWRITEENABLE)==0xF);
                CHECK(rs(D3DRS_SCISSORTESTENABLE)==FALSE&&rs(D3DRS_TEXTUREFACTOR)==0xff112233);
                CHECK(rs(D3DRS_ALPHAREF)==0x10&&rs(D3DRS_FILLMODE)==D3DFILL_SOLID&&rs(unnamed)==1);
                CHECK(ss(0,D3DSAMP_MINFILTER)==D3DTEXF_POINT&&ss(5,D3DSAMP_ADDRESSU)==D3DTADDRESS_WRAP);
                CHECK(ts(0,D3DTSS_COLOROP)==D3DTOP_SELECTARG1&&ts(0,D3DTSS_TEXCOORDINDEX)==0);
                CHECK(ts(2,D3DTSS_ALPHAARG2)==D3DTA_CURRENT);
                CHECK(vsConst(20)==1.0f&&psConst(7)==5.0f);
                CHECK(sameViewport(viewportOf(),vpA)&&sameRect(scissorOf(),scissorA));
                CHECK(sameMatrix(transformOf(D3DTS_WORLD),worldA)&&sameMatrix(transformOf(D3DTS_VIEW),viewA));
                CHECK(sameMatrix(transformOf(D3DTS_PROJECTION),projectionA));
                CHECK(diffuseOf().Diffuse.r==1.0f&&diffuseOf().Diffuse.g==0.0f&&diffuseOf().Diffuse.a==1.0f);
                CHECK(drawAndRead(device,target,staging,32,32)==0xff0000);   // red again
                // Apply is repeatable and leaves the device as it found it.
                CHECK_HR(all->Apply());
                CHECK(texOf(0)==baseA&&indicesOf()==ibA&&fvfOf()==fvf0);
                all->Release();
            }

            // ------------------------------ D3DSBT_PIXELSTATE / D3DSBT_VERTEXSTATE
            // Two halves of the device's state that are set independently; each
            // block must put back its own half and nothing of the other, and
            // what neither covers (streams, indices, viewport, ...) never moves.
            const DWORD dwF0=fvf0,dwF1=fvf1;
            auto setPixel=[&](unsigned w){
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,w?0x99:0x20));
                CHECK_HR(device->SetRenderState(D3DRS_TEXTUREFACTOR,w?0xff0a0b0c:0xff010203));
                CHECK_HR(device->SetRenderState(D3DRS_ZFUNC,w?D3DCMP_GREATER:D3DCMP_LESS));
                CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,w?TRUE:FALSE));
                CHECK_HR(device->SetSamplerState(0,D3DSAMP_ADDRESSU,w?D3DTADDRESS_CLAMP:D3DTADDRESS_MIRROR));
                CHECK_HR(device->SetSamplerState(2,D3DSAMP_MAGFILTER,w?D3DTEXF_LINEAR:D3DTEXF_POINT));
                CHECK_HR(device->SetTextureStageState(0,D3DTSS_COLOROP,w?D3DTOP_SUBTRACT:D3DTOP_ADD));
                CHECK_HR(device->SetPixelShader(w?psC1:psC0));
                const float c3[4]={w?9.0f:1.0f,0,0,0},c200[4]={w?4.0f:3.0f,0,0,0};
                CHECK_HR(device->SetPixelShaderConstantF(3,c3,1));
                CHECK_HR(device->SetPixelShaderConstantF(200,c200,1));
            };
            auto checkPixel=[&](unsigned w){
                CHECK(rs(D3DRS_ALPHAREF)==(w?0x99u:0x20u));
                CHECK(rs(D3DRS_TEXTUREFACTOR)==(w?0xff0a0b0cu:0xff010203u));
                CHECK(rs(D3DRS_ZFUNC)==DWORD(w?D3DCMP_GREATER:D3DCMP_LESS));
                CHECK(rs(D3DRS_ALPHABLENDENABLE)==DWORD(w?TRUE:FALSE));
                CHECK(ss(0,D3DSAMP_ADDRESSU)==DWORD(w?D3DTADDRESS_CLAMP:D3DTADDRESS_MIRROR));
                CHECK(ss(2,D3DSAMP_MAGFILTER)==DWORD(w?D3DTEXF_LINEAR:D3DTEXF_POINT));
                CHECK(ts(0,D3DTSS_COLOROP)==DWORD(w?D3DTOP_SUBTRACT:D3DTOP_ADD));
                CHECK(psOf()==(w?psC1:psC0));
                CHECK(psConst(3)==(w?9.0f:1.0f)&&psConst(200)==(w?4.0f:3.0f));
            };
            auto setVertex=[&](unsigned w){
                CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,w?D3DCULL_CCW:D3DCULL_CW));
                CHECK_HR(device->SetRenderState(D3DRS_LIGHTING,w?TRUE:FALSE));
                CHECK_HR(device->SetRenderState(D3DRS_AMBIENT,w?0xff808080:0xff112233));
                CHECK_HR(device->SetVertexShader(w?vsOffset:vsPass));
                const float c3[4]={w?8.0f:2.0f,0,0,0},c250[4]={w?6.0f:5.0f,0,0,0};
                CHECK_HR(device->SetVertexShaderConstantF(3,c3,1));
                CHECK_HR(device->SetVertexShaderConstantF(250,c250,1));
                // State 0 is a declaration over an FVF; state 1 is the FVF alone.
                CHECK_HR(device->SetFVF(w?dwF1:dwF0));
                if(!w)CHECK_HR(device->SetVertexDeclaration(declA));
            };
            auto checkVertex=[&](unsigned w){
                CHECK(rs(D3DRS_CULLMODE)==DWORD(w?D3DCULL_CCW:D3DCULL_CW));
                CHECK(rs(D3DRS_LIGHTING)==DWORD(w?TRUE:FALSE));
                CHECK(rs(D3DRS_AMBIENT)==(w?0xff808080u:0xff112233u));
                CHECK(vsOf()==(w?vsOffset:vsPass));
                CHECK(vsConst(3)==(w?8.0f:2.0f)&&vsConst(250)==(w?6.0f:5.0f));
                CHECK(fvfOf()==(w?dwF1:dwF0)&&declOf()==(w?nullptr:declA));
            };
            // Listed under both halves: either block puts it back.
            auto setShared=[&](unsigned w){
                CHECK_HR(device->SetRenderState(D3DRS_SHADEMODE,w?D3DSHADE_FLAT:D3DSHADE_GOURAUD));
                CHECK_HR(device->SetRenderState(D3DRS_FOGCOLOR,w?0xff00ff00:0xffff0000));
                CHECK_HR(device->SetTextureStageState(1,D3DTSS_TEXCOORDINDEX,w?3:2));
                CHECK_HR(device->SetTextureStageState(1,D3DTSS_TEXTURETRANSFORMFLAGS,
                    w?D3DTTFF_COUNT3:D3DTTFF_COUNT2));
            };
            auto checkShared=[&](unsigned w){
                CHECK(rs(D3DRS_SHADEMODE)==DWORD(w?D3DSHADE_FLAT:D3DSHADE_GOURAUD));
                CHECK(rs(D3DRS_FOGCOLOR)==(w?0xff00ff00u:0xffff0000u));
                CHECK(ts(1,D3DTSS_TEXCOORDINDEX)==(w?3u:2u));
                CHECK(ts(1,D3DTSS_TEXTURETRANSFORMFLAGS)==DWORD(w?D3DTTFF_COUNT3:D3DTTFF_COUNT2));
            };
            // In neither half: only D3DSBT_ALL covers these (including bound textures).
            auto setOther=[&](unsigned w){
                CHECK_HR(device->SetTexture(0,w?texB:texA));
                CHECK_HR(device->SetTexture(4,w?texA:nullptr));
                CHECK_HR(device->SetStreamSource(0,w?blue:red,w?4:0,stride));
                CHECK_HR(device->SetIndices(w?ibB:ibA));
                CHECK_HR(device->SetViewport(w?&vpB:&vpA));
                CHECK_HR(device->SetScissorRect(w?&scissorB:&scissorA));
                CHECK_HR(device->SetTransform(D3DTS_WORLD,w?&worldB:&worldA));
                CHECK_HR(device->SetMaterial(w?&materialB:&materialA));
                CHECK_HR(device->SetRenderState(unnamed,w?2:1));
            };
            auto checkOther=[&](unsigned w){
                UINT offset=0,strideBack=0;
                CHECK(texOf(0)==(w?baseB:baseA));
                CHECK(texOf(4)==(w?baseA:nullptr));
                CHECK(streamOf(0,&offset,&strideBack)==(w?blue:red)&&offset==(w?4u:0u)&&strideBack==stride);
                CHECK(indicesOf()==(w?ibB:ibA));
                CHECK(sameViewport(viewportOf(),w?vpB:vpA)&&sameRect(scissorOf(),w?scissorB:scissorA));
                CHECK(sameMatrix(transformOf(D3DTS_WORLD),w?worldB:worldA));
                CHECK(diffuseOf().Diffuse.g==(w?1.0f:0.0f));
                CHECK(rs(unnamed)==(w?2u:1u));
            };
            setPixel(0);setVertex(0);setShared(0);setOther(0);
            IDirect3DStateBlock9* pixelBlock=nullptr;
            IDirect3DStateBlock9* vertexBlock=nullptr;
            CHECK_HR(device->CreateStateBlock(D3DSBT_PIXELSTATE,&pixelBlock));
            CHECK_HR(device->CreateStateBlock(D3DSBT_VERTEXSTATE,&vertexBlock));
            if(pixelBlock&&vertexBlock) {
                setPixel(1);setVertex(1);setShared(1);setOther(1);
                CHECK_HR(pixelBlock->Apply());
                checkPixel(0);checkVertex(1);checkShared(0);checkOther(1);
                CHECK_HR(vertexBlock->Apply());
                checkPixel(0);checkVertex(0);checkShared(0);checkOther(1);
                // The other order, and the shared states from the other side.
                setPixel(1);setVertex(1);setShared(1);
                CHECK_HR(vertexBlock->Apply());
                checkPixel(1);checkVertex(0);checkShared(0);checkOther(1);
                setShared(1);
                CHECK_HR(pixelBlock->Apply());
                checkPixel(0);checkVertex(0);checkShared(0);checkOther(1);
                // Capture re-reads only this block's half: the pixel block takes
                // pixel state 1 while the vertex state is 0 and changes after.
                setPixel(1);setVertex(0);setShared(1);
                CHECK_HR(pixelBlock->Capture());
                setPixel(0);setVertex(1);setShared(0);
                CHECK_HR(pixelBlock->Apply());
                checkPixel(1);checkVertex(1);checkShared(1);checkOther(1);
            }
            if(pixelBlock)pixelBlock->Release();
            if(vertexBlock)vertexBlock->Release();

            // --------------------------- shader state through a real draw
            // vs: add oPos,v0,c0 (c0.x moves the triangle off screen); ps: the
            // colour in c0. Both halves are proven by the pixel they produce.
            CHECK_HR(device->SetFVF(0));
            CHECK_HR(device->SetVertexDeclaration(declA));
            CHECK_HR(device->SetStreamSource(0,clip,0,3*sizeof(float)));
            CHECK_HR(device->SetStreamSource(1,nullptr,0,0));
            CHECK_HR(device->SetViewport(&vpA));
            CHECK_HR(device->SetScissorRect(&scissorA));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
            CHECK_HR(device->SetRenderState(D3DRS_ZENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            CHECK_HR(device->SetRenderState(D3DRS_COLORWRITEENABLE,0xF));
            CHECK_HR(device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            CHECK_HR(device->SetVertexShader(vsOffset));
            CHECK_HR(device->SetPixelShader(psC0));
            const float zero[4]={0,0,0,0},away[4]={5,0,0,0};
            const float redC[4]={1,0,0,1},greenC[4]={0,1,0,1},blueC[4]={0,0,1,1};
            CHECK_HR(device->SetVertexShaderConstantF(0,zero,1));
            CHECK_HR(device->SetPixelShaderConstantF(0,redC,1));
            CHECK(drawAndRead(device,target,staging,32,32)==0xff0000);
            IDirect3DStateBlock9* shaderPixel=nullptr;
            IDirect3DStateBlock9* shaderVertex=nullptr;
            CHECK_HR(device->CreateStateBlock(D3DSBT_PIXELSTATE,&shaderPixel));
            CHECK_HR(device->CreateStateBlock(D3DSBT_VERTEXSTATE,&shaderVertex));
            if(shaderPixel&&shaderVertex) {
                CHECK_HR(device->SetPixelShaderConstantF(0,greenC,1));
                CHECK(drawAndRead(device,target,staging,32,32)==0x00ff00);
                CHECK_HR(device->SetVertexShaderConstantF(0,away,1));
                CHECK(drawAndRead(device,target,staging,32,32)==0x000000);   // moved off the target
                // The pixel block restores the colour; the geometry stays away.
                CHECK_HR(shaderPixel->Apply());
                CHECK(drawAndRead(device,target,staging,32,32)==0x000000);
                // The vertex block brings the geometry back, in the restored red.
                CHECK_HR(shaderVertex->Apply());
                CHECK(drawAndRead(device,target,staging,32,32)==0xff0000);
                // The vertex block leaves the pixel colour alone.
                CHECK_HR(device->SetPixelShaderConstantF(0,greenC,1));
                CHECK_HR(shaderVertex->Apply());
                CHECK(drawAndRead(device,target,staging,32,32)==0x00ff00);
                CHECK_HR(shaderPixel->Apply());
                CHECK(drawAndRead(device,target,staging,32,32)==0xff0000);
                // Another pixel shader reading c1: the block puts shader c0 back.
                CHECK_HR(device->SetPixelShader(psC1));
                CHECK_HR(device->SetPixelShaderConstantF(1,blueC,1));
                CHECK(drawAndRead(device,target,staging,32,32)==0x0000ff);
                CHECK_HR(shaderPixel->Apply());
                CHECK(psOf()==psC0);
                CHECK(drawAndRead(device,target,staging,32,32)==0xff0000);
                // Capture takes the colour now, not the one the block was made with.
                CHECK_HR(device->SetPixelShaderConstantF(0,blueC,1));
                CHECK_HR(shaderPixel->Capture());
                CHECK_HR(device->SetPixelShaderConstantF(0,greenC,1));
                CHECK(drawAndRead(device,target,staging,32,32)==0x00ff00);
                CHECK_HR(shaderPixel->Apply());
                CHECK(drawAndRead(device,target,staging,32,32)==0x0000ff);
                // And the vertex block, captured with the geometry away.
                CHECK_HR(device->SetVertexShaderConstantF(0,away,1));
                CHECK_HR(shaderVertex->Capture());
                CHECK_HR(device->SetVertexShaderConstantF(0,zero,1));
                CHECK(drawAndRead(device,target,staging,32,32)==0x0000ff);
                CHECK_HR(shaderVertex->Apply());
                CHECK(drawAndRead(device,target,staging,32,32)==0x000000);
            }
            if(shaderPixel)shaderPixel->Release();
            if(shaderVertex)shaderVertex->Release();

            // --------------------------------- BeginStateBlock / EndStateBlock
            CHECK_HR(device->SetVertexShader(nullptr));
            CHECK_HR(device->SetPixelShader(nullptr));
            CHECK_HR(device->SetFVF(fvf0));
            CHECK_HR(device->SetVertexDeclaration(declA));
            CHECK_HR(device->SetStreamSource(0,red,0,stride));
            CHECK_HR(device->SetStreamSource(1,nullptr,0,0));
            CHECK_HR(device->SetIndices(ibA));
            CHECK_HR(device->SetTexture(0,texA));
            CHECK_HR(device->SetTexture(1,nullptr));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,5));
            CHECK_HR(device->SetRenderState(D3DRS_TEXTUREFACTOR,0xff000005));
            CHECK_HR(device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS));
            CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));
            CHECK_HR(device->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            CHECK_HR(device->SetSamplerState(2,D3DSAMP_MINFILTER,D3DTEXF_POINT));
            const float vc10A[4]={1,1,1,1},vc10B[4]={9,9,9,9},pc4A[4]={4,4,4,4};
            CHECK_HR(device->SetVertexShaderConstantF(10,vc10A,1));
            CHECK_HR(device->SetPixelShaderConstantF(4,pc4A,1));
            CHECK_HR(device->SetViewport(&vpA));
            CHECK_HR(device->SetScissorRect(&scissorA));
            CHECK_HR(device->SetTransform(D3DTS_WORLD,&worldA));
            CHECK_HR(device->SetTransform(D3DTS_VIEW,&viewA));
            CHECK_HR(device->SetMaterial(&materialA));
            const ULONG texARefs=refsOf(texA),texBRefs=refsOf(texB),blueRefs=refsOf(blue);
            IDirect3DStateBlock9* recorded=nullptr;
            CHECK_HR(device->BeginStateBlock());
            CHECK(refsOf(device)==blockBase);   // a recording holds no reference on the device
            CHECK_HR_IS(device->BeginStateBlock(),D3DERR_INVALIDCALL);
            IDirect3DStateBlock9* refused=reinterpret_cast<IDirect3DStateBlock9*>(device);
            CHECK_HR_IS(device->CreateStateBlock(D3DSBT_ALL,&refused),D3DERR_INVALIDCALL);
            CHECK(refused==nullptr);
            // Arguments are still validated while recording.
            CHECK_HR_IS(device->SetRenderState(D3DRENDERSTATETYPE(256),1),D3DERR_INVALIDCALL);
            CHECK_HR_IS(device->SetTexture(16,nullptr),D3DERR_INVALIDCALL);
            CHECK_HR_IS(device->SetStreamSource(16,nullptr,0,0),D3DERR_INVALIDCALL);
            CHECK_HR_IS(device->SetViewport(nullptr),D3DERR_INVALIDCALL);
            CHECK_HR_IS(device->SetVertexShaderConstantF(255,vc10B,2),D3DERR_INVALIDCALL);
            CHECK_HR_IS(device->SetTransform(D3DTS_WORLD,nullptr),D3DERR_INVALIDCALL);
            // The recorded calls.
            CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,77));
            CHECK_HR(device->SetRenderState(D3DRS_TEXTUREFACTOR,0xff00ff00));
            CHECK_HR(device->SetTexture(0,texA));   // recorded twice: the second wins
            CHECK_HR(device->SetTexture(0,texB));
            CHECK_HR(device->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
            CHECK_HR(device->SetVertexShaderConstantF(10,vc10B,1));
            CHECK_HR(device->SetStreamSource(1,blue,4,20));
            CHECK_HR(device->SetViewport(&vpB));
            CHECK_HR(device->SetScissorRect(&scissorB));
            CHECK_HR(device->SetTransform(D3DTS_WORLD,&worldB));
            D3DMATRIX shift=worldA;shift._41=2.0f;
            CHECK_HR(device->MultiplyTransform(D3DTS_VIEW,&shift));
            CHECK_HR(device->SetMaterial(&materialB));
            CHECK_HR(device->SetFVF(fvf1));
            // Nothing above reached the device.
            CHECK(rs(D3DRS_ALPHAREF)==5&&rs(D3DRS_TEXTUREFACTOR)==0xff000005);
            CHECK(texOf(0)==baseA&&ss(1,D3DSAMP_MAGFILTER)==D3DTEXF_POINT&&vsConst(10)==1.0f);
            UINT recOffset=0,recStride=0;
            CHECK(streamOf(1,&recOffset,&recStride)==nullptr);
            CHECK(sameViewport(viewportOf(),vpA)&&sameRect(scissorOf(),scissorA));
            CHECK(sameMatrix(transformOf(D3DTS_WORLD),worldA)&&sameMatrix(transformOf(D3DTS_VIEW),viewA));
            CHECK(diffuseOf().Diffuse.r==1.0f&&fvfOf()==fvf0&&declOf()==declA);
            // The block holds what it recorded: texB and blue, and not the
            // texA it recorded first and replaced.
            CHECK(refsOf(texB)==texBRefs+1&&refsOf(blue)==blueRefs+1&&refsOf(texA)==texARefs);
            // A null result pointer is refused and recording carries on.
            CHECK_HR_IS(device->EndStateBlock(nullptr),D3DERR_INVALIDCALL);
            CHECK_HR_IS(device->BeginStateBlock(),D3DERR_INVALIDCALL);
            CHECK_HR(device->EndStateBlock(&recorded));
            CHECK(recorded!=nullptr);
            CHECK(refsOf(device)==blockBase+1);
            IDirect3DStateBlock9* again=recorded;
            CHECK_HR_IS(device->EndStateBlock(&again),D3DERR_INVALIDCALL);
            CHECK(again==nullptr);
            CHECK_HR_IS(device->EndStateBlock(nullptr),D3DERR_INVALIDCALL);
            if(recorded) {
                // States the block did not record, set differently on the device.
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_NEVER));
                CHECK_HR(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
                CHECK_HR(device->SetSamplerState(2,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));
                CHECK_HR(device->SetTexture(1,texB));
                CHECK_HR(device->SetStreamSource(0,blue,0,stride));
                const float pc4B[4]={6,6,6,6};
                CHECK_HR(device->SetPixelShaderConstantF(4,pc4B,1));
                CHECK_HR(recorded->Apply());
                CHECK(rs(D3DRS_ALPHAREF)==77&&rs(D3DRS_TEXTUREFACTOR)==0xff00ff00);
                CHECK(texOf(0)==baseB&&ss(1,D3DSAMP_MAGFILTER)==D3DTEXF_LINEAR&&vsConst(10)==9.0f);
                CHECK(streamOf(1,&recOffset,&recStride)==blue&&recOffset==4&&recStride==20);
                CHECK(sameViewport(viewportOf(),vpB)&&sameRect(scissorOf(),scissorB));
                CHECK(sameMatrix(transformOf(D3DTS_WORLD),worldB));
                CHECK(transformOf(D3DTS_VIEW)._41==2.0f&&transformOf(D3DTS_VIEW)._11==1.0f);   // viewA x shift
                CHECK(diffuseOf().Diffuse.g==1.0f&&fvfOf()==fvf1&&declOf()==nullptr);
                // ...and nothing it did not.
                CHECK(rs(D3DRS_ALPHAFUNC)==D3DCMP_NEVER&&rs(D3DRS_CULLMODE)==D3DCULL_NONE);
                CHECK(ss(2,D3DSAMP_MINFILTER)==D3DTEXF_LINEAR&&texOf(1)==baseB&&psConst(4)==6.0f);
                CHECK(streamOf(0,&recOffset,&recStride)==blue);
                CHECK(sameMatrix(transformOf(D3DTS_PROJECTION),projectionA)&&indicesOf()==ibA);
                // Capture re-reads the recorded subset only.
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,1));
                CHECK_HR(device->SetTexture(0,texA));
                const float vc10C[4]={2,2,2,2};
                CHECK_HR(device->SetVertexShaderConstantF(10,vc10C,1));
                CHECK_HR(device->SetStreamSource(1,red,0,16));
                CHECK_HR(device->SetViewport(&vpA));
                CHECK_HR(device->SetFVF(fvf2));
                CHECK_HR(device->SetTransform(D3DTS_WORLD,&worldA));
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS));
                CHECK_HR(device->SetTexture(1,nullptr));
                CHECK_HR(device->SetStreamSource(0,red,0,stride));
                CHECK_HR(recorded->Capture());
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAREF,2));
                CHECK_HR(device->SetTexture(0,nullptr));
                CHECK_HR(device->SetVertexShaderConstantF(10,vc10B,1));
                CHECK_HR(device->SetStreamSource(1,nullptr,0,0));
                CHECK_HR(device->SetViewport(&vpB));
                CHECK_HR(device->SetFVF(fvf3));
                CHECK_HR(device->SetTransform(D3DTS_WORLD,&worldB));
                CHECK_HR(device->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));
                CHECK_HR(device->SetTexture(1,texA));
                CHECK_HR(device->SetStreamSource(0,nullptr,0,0));
                CHECK_HR(recorded->Apply());
                CHECK(rs(D3DRS_ALPHAREF)==1&&texOf(0)==baseA&&vsConst(10)==2.0f);
                CHECK(streamOf(1,&recOffset,&recStride)==red&&recOffset==0&&recStride==16);
                CHECK(sameViewport(viewportOf(),vpA)&&fvfOf()==fvf2&&sameMatrix(transformOf(D3DTS_WORLD),worldA));
                CHECK(rs(D3DRS_ALPHAFUNC)==D3DCMP_GREATER&&texOf(1)==baseA);
                CHECK(streamOf(0,&recOffset,&recStride)==nullptr);
                recorded->Release();
            }
            CHECK(refsOf(device)==blockBase);

            // --------------------------------------- lifetime and error paths
            IDirect3DStateBlock9* probe=nullptr;
            CHECK_HR(device->CreateStateBlock(D3DSBT_PIXELSTATE,&probe));
            if(probe) {
                CHECK(refsOf(device)==blockBase+1);   // the block holds its device
                IDirect3DDevice9* got=nullptr;
                CHECK_HR(probe->GetDevice(&got));
                CHECK(got==static_cast<IDirect3DDevice9*>(device));
                CHECK(refsOf(device)==blockBase+2);
                if(got)got->Release();
                CHECK_HR_IS(probe->GetDevice(nullptr),D3DERR_INVALIDCALL);
                IUnknown* unknown=nullptr;
                IDirect3DStateBlock9* same=nullptr;
                void* wrong=reinterpret_cast<void*>(probe);
                CHECK_HR(probe->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&unknown)));
                CHECK_HR(probe->QueryInterface(__uuidof(IDirect3DStateBlock9),reinterpret_cast<void**>(&same)));
                CHECK(same==probe);
                CHECK_HR_IS(probe->QueryInterface(__uuidof(IDirect3DDevice9),&wrong),E_NOINTERFACE);
                CHECK(wrong==nullptr);
                CHECK_HR_IS(probe->QueryInterface(__uuidof(IUnknown),nullptr),E_POINTER);
                if(unknown)unknown->Release();
                if(same)same->Release();
                // While the device records, a finished block can be neither
                // applied nor captured.
                CHECK_HR(device->BeginStateBlock());
                CHECK_HR_IS(probe->Apply(),D3DERR_INVALIDCALL);
                CHECK_HR_IS(probe->Capture(),D3DERR_INVALIDCALL);
                IDirect3DStateBlock9* abandoned=nullptr;
                CHECK_HR(device->EndStateBlock(&abandoned));
                if(abandoned)abandoned->Release();
                CHECK_HR(probe->Apply());
                probe->Release();
            }
            CHECK(refsOf(device)==blockBase);
            IDirect3DStateBlock9* rejected=nullptr;
            CHECK_HR_IS(device->CreateStateBlock(D3DSBT_ALL,nullptr),D3DERR_INVALIDCALL);
            const D3DSTATEBLOCKTYPE badTypes[]={D3DSTATEBLOCKTYPE(0),D3DSTATEBLOCKTYPE(4),
                D3DSTATEBLOCKTYPE(99),D3DSTATEBLOCKTYPE(0x7fffffff)};
            for(D3DSTATEBLOCKTYPE type:badTypes) {
                rejected=reinterpret_cast<IDirect3DStateBlock9*>(device);   // must be reset to null
                CHECK_HR_IS(device->CreateStateBlock(type,&rejected),D3DERR_INVALIDCALL);
                CHECK(rejected==nullptr);
            }
            rejected=reinterpret_cast<IDirect3DStateBlock9*>(device);
            CHECK_HR_IS(device->EndStateBlock(&rejected),D3DERR_INVALIDCALL);   // never began
            CHECK(rejected==nullptr);

            // A block holds what it captured with a reference of its own, so the
            // objects outlive both the guest's release and the device unbinding
            // them, and a block that outlives its device binding still works.
            {
                IDirect3DTexture9* life=nullptr;
                IDirect3DVertexBuffer9* lifeVb=colourTriangle(device,0xff00ff00);
                IDirect3DIndexBuffer9* lifeIb=nullptr;
                IDirect3DVertexShader9* lifeVs=nullptr;
                IDirect3DPixelShader9* lifePs=nullptr;
                IDirect3DVertexDeclaration9* lifeDecl=nullptr;
                CHECK_HR(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&life,nullptr));
                CHECK_HR(device->CreateIndexBuffer(12,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&lifeIb,nullptr));
                CHECK_HR(device->CreateVertexShader(passVs,&lifeVs));
                CHECK_HR(device->CreatePixelShader(constantPs0,&lifePs));
                CHECK_HR(device->CreateVertexDeclaration(positionOnly,&lifeDecl));
                if(life&&lifeVb&&lifeIb&&lifeVs&&lifePs&&lifeDecl) {
                    CHECK_HR(device->SetTexture(6,life));
                    CHECK_HR(device->SetStreamSource(2,lifeVb,0,stride));
                    CHECK_HR(device->SetIndices(lifeIb));
                    CHECK_HR(device->SetVertexShader(lifeVs));
                    CHECK_HR(device->SetPixelShader(lifePs));
                    CHECK_HR(device->SetVertexDeclaration(lifeDecl));
                    IUnknown* held[]={life,lifeVb,lifeIb,lifeVs,lifePs,lifeDecl};
                    for(IUnknown* object:held)CHECK(refsOf(object)==2);   // the guest and the device
                    IDirect3DStateBlock9* keeper=nullptr;
                    CHECK_HR(device->CreateStateBlock(D3DSBT_ALL,&keeper));
                    if(keeper) {
                        for(IUnknown* object:held)CHECK(refsOf(object)==3);   // and the block
                        CHECK_HR(device->SetTexture(6,nullptr));
                        CHECK_HR(device->SetStreamSource(2,nullptr,0,0));
                        CHECK_HR(device->SetIndices(nullptr));
                        CHECK_HR(device->SetVertexShader(nullptr));
                        CHECK_HR(device->SetPixelShader(nullptr));
                        CHECK_HR(device->SetVertexDeclaration(nullptr));
                        for(IUnknown* object:held)CHECK(refsOf(object)==2);
                        life->Release();lifeVb->Release();lifeIb->Release();
                        lifeVs->Release();lifePs->Release();lifeDecl->Release();
                        // Only the block is left holding them: still alive.
                        for(IUnknown* object:held)CHECK(refsOf(object)==1);
                        CHECK_HR(keeper->Apply());
                        for(IUnknown* object:held)CHECK(refsOf(object)==2);
                        CHECK(texOf(6)==static_cast<IDirect3DBaseTexture9*>(life)&&indicesOf()==lifeIb);
                        CHECK(life->GetLevelCount()==1);   // a live object, not a dangling one
                        keeper->Release();
                        for(IUnknown* object:held)CHECK(refsOf(object)==1);   // the device's alone
                    } else {
                        life->Release();lifeVb->Release();lifeIb->Release();
                        lifeVs->Release();lifePs->Release();lifeDecl->Release();
                    }
                    CHECK_HR(device->SetTexture(6,nullptr));
                    CHECK_HR(device->SetStreamSource(2,nullptr,0,0));
                    CHECK_HR(device->SetIndices(nullptr));
                    CHECK_HR(device->SetVertexShader(nullptr));
                    CHECK_HR(device->SetPixelShader(nullptr));
                    CHECK_HR(device->SetVertexDeclaration(nullptr));
                }
            }
            // Capture drops the reference to what it replaces, D3DSBT_PIXELSTATE
            // does not capture bound textures, and a recording that is abandoned
            // along with its block drops what it held.
            {
                IDirect3DTexture9* swapped=nullptr;
                CHECK_HR(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&swapped,nullptr));
                if(swapped) {
                    CHECK_HR(device->SetTexture(7,swapped));
                    IDirect3DStateBlock9* pixelNoTex=nullptr;
                    CHECK_HR(device->CreateStateBlock(D3DSBT_PIXELSTATE,&pixelNoTex));
                    CHECK(refsOf(swapped)==2);   // guest and device only; pixel block does not hold textures
                    if(pixelNoTex)pixelNoTex->Release();
                    IDirect3DStateBlock9* holder=nullptr;
                    CHECK_HR(device->CreateStateBlock(D3DSBT_ALL,&holder));
                    CHECK(refsOf(swapped)==3);   // guest, device, block
                    CHECK_HR(device->SetTexture(7,nullptr));
                    if(holder) {
                        CHECK_HR(holder->Capture());   // now holds the empty slot
                        CHECK(refsOf(swapped)==1);
                        holder->Release();
                    }
                    CHECK_HR(device->BeginStateBlock());
                    CHECK_HR(device->SetTexture(7,swapped));
                    CHECK(refsOf(swapped)==2);   // the recording holds it
                    // Integer wrap-around on constant register range must be rejected (recording and live).
                    const float dummy[4]={1,2,3,4};
                    CHECK_HR_IS(device->SetVertexShaderConstantF(0xffffffffu,dummy,2),D3DERR_INVALIDCALL);
                    CHECK_HR_IS(device->SetPixelShaderConstantF(0xffffffffu,dummy,2),D3DERR_INVALIDCALL);
                    IDirect3DStateBlock9* made=nullptr;
                    CHECK_HR(device->EndStateBlock(&made));
                    CHECK(refsOf(swapped)==2);
                    if(made)made->Release();
                    CHECK(refsOf(swapped)==1);
                    CHECK_HR_IS(device->SetVertexShaderConstantF(0xffffffffu,dummy,2),D3DERR_INVALIDCALL);
                    CHECK_HR_IS(device->SetPixelShaderConstantF(0xffffffffu,dummy,2),D3DERR_INVALIDCALL);
                    swapped->Release();
                }
            }
            // Leave the device the way the later sections expect it.
            CHECK_HR(device->SetTexture(0,nullptr));
            CHECK_HR(device->SetTexture(1,nullptr));
            CHECK_HR(device->SetTexture(3,nullptr));
            CHECK_HR(device->SetTexture(4,nullptr));
            CHECK_HR(device->SetStreamSource(0,nullptr,0,0));
            CHECK_HR(device->SetStreamSource(1,nullptr,0,0));
            CHECK_HR(device->SetIndices(nullptr));
            CHECK_HR(device->SetVertexShader(nullptr));
            CHECK_HR(device->SetPixelShader(nullptr));
            CHECK_HR(device->SetVertexDeclaration(nullptr));
            CHECK_HR(device->SetRenderTarget(0,backBuffer));
        }
        if(declA)declA->Release();
        if(psC1)psC1->Release();
        if(psC0)psC0->Release();
        if(vsOffset)vsOffset->Release();
        if(vsPass)vsPass->Release();
        if(ibB)ibB->Release();
        if(ibA)ibA->Release();
        if(texB)texB->Release();
        if(texA)texA->Release();
        if(clip)clip->Release();
        if(blue)blue->Release();
        if(red)red->Release();
        if(staging)staging->Release();
        if(target)target->Release();
        CHECK(refsOf(device)==blockBase);
    }

    // A query outlives a Reset: it holds the device and its context, which Reset
    // keeps. An open state-block recording is dropped by Reset.
    IDirect3DQuery9* survivor=nullptr;
    CHECK_HR(device->CreateQuery(D3DQUERYTYPE_EVENT,&survivor));
    CHECK(refsOf(device)==deviceRefs+1);
    CHECK_HR(device->BeginStateBlock());

    // Reset rebuilds the swap chain, as D3D9 documents.
    CHECK_HR(device->Reset(&present));
    {
        IDirect3DStateBlock9* afterResetBlock=nullptr;
        CHECK_HR_IS(device->EndStateBlock(&afterResetBlock),D3DERR_INVALIDCALL);
        CHECK(afterResetBlock==nullptr);
    }
    if(survivor) {
        BOOL signalled=FALSE;
        CHECK_HR(survivor->Issue(D3DISSUE_END));
        CHECK_HR_IS(drain(survivor,&signalled,sizeof signalled),S_OK);
        CHECK(signalled==TRUE);
        survivor->Release();
    }
    CHECK(refsOf(device)==deviceRefs);
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
