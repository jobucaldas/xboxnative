// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contract test for Play's D3D10 shim. It loads the shim the way
// Play's loader does (by name, from its own directory) and drives the D3D10
// entry points a game uses first. Wine's d3d11.dll stands in for the console's
// device, so this proves the shim's own conversions and object model, not Xbox
// execution. No console is contacted and nothing here is a game.
#include <windows.h>
#include <d3d10.h>
#include <d3d10_1.h>
#include <d3d10misc.h>
#include <d3d11.h>
// The shim's own file-scope definitions, for the same reason: no CRT here.
extern "C" void* memset(void* d,int c,size_t n) {
    auto p=(unsigned char*)d;while(n--)*p++=(unsigned char)c;return d;
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

using CreateDevice=HRESULT(WINAPI*)(IDXGIAdapter*,D3D10_DRIVER_TYPE,HMODULE,UINT,UINT,ID3D10Device**);
using CreateDevice1=HRESULT(WINAPI*)(IDXGIAdapter*,D3D10_DRIVER_TYPE,HMODULE,UINT,D3D10_FEATURE_LEVEL1,UINT,ID3D10Device1**);
using CreateDeviceAndSwapChain=HRESULT(WINAPI*)(IDXGIAdapter*,D3D10_DRIVER_TYPE,HMODULE,UINT,UINT,
    DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D10Device**);
using CreateBlob=HRESULT(WINAPI*)(SIZE_T,ID3D10Blob**);
using CompileShader=HRESULT(WINAPI*)(const char*,SIZE_T,const char*,const D3D10_SHADER_MACRO*,ID3D10Include*,
    const char*,const char*,UINT,ID3D10Blob**,ID3D10Blob**);
using ReflectShader=HRESULT(WINAPI*)(const void*,SIZE_T,ID3D10ShaderReflection**);
using CoreCreateDevice=HRESULT(WINAPI*)(IDXGIFactory*,IDXGIAdapter*,UINT,D3D_FEATURE_LEVEL,ID3D10Device**);
using EnableAll=HRESULT(WINAPI*)(D3D10_STATE_BLOCK_MASK*);
using CreateStateBlock=HRESULT(WINAPI*)(ID3D10Device*,D3D10_STATE_BLOCK_MASK*,ID3D10StateBlock**);
using GetSetting=WINBOOL(WINAPI*)(D3D10_STATE_BLOCK_MASK*,D3D10_DEVICE_STATE_TYPES,UINT);
using EnableCapture=HRESULT(WINAPI*)(D3D10_STATE_BLOCK_MASK*,D3D10_DEVICE_STATE_TYPES,UINT,UINT);

static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) { return DefWindowProcW(h,m,w,l); }

// The shim is loaded by full path from this executable's own directory, so the
// test never picks up a host d3d10.dll.
static HMODULE loadShim() {
    wchar_t path[1024];DWORD n=GetModuleFileNameW(nullptr,path,1024);
    if(!n||n>=1024)return nullptr;
    while(n&&path[n-1]!=L'\\'&&path[n-1]!=L'/')--n;
    const wchar_t name[]=L"d3d10.dll";
    if(n+sizeof name/sizeof name[0]>=1024)return nullptr;
    for(unsigned i=0;i<sizeof name/sizeof name[0];++i)path[n+i]=name[i];
    return LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
}
// The same SM4 blobs the console's d3d10-triangle fixture draws with, so the
// off-device draw path and the console fixture cannot drift apart.
#include "../../app/tests/d3d10_bytecode.h"

using DisassembleShader=HRESULT(WINAPI*)(const void*,SIZE_T,WINBOOL,const char*,ID3D10Blob**);
using CreateDeviceAndSwapChain1=HRESULT(WINAPI*)(IDXGIAdapter*,D3D10_DRIVER_TYPE,HMODULE,UINT,D3D10_FEATURE_LEVEL1,UINT,
    DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D10Device1**);
// The FX10 types live in a header this test does not need; the ABI is pointers.
using CreateEffect=HRESULT(WINAPI*)(void*,SIZE_T,UINT,ID3D10Device*,void*,void**);
using CreateEffectPool=HRESULT(WINAPI*)(void*,SIZE_T,UINT,ID3D10Device*,void**);
using CompileEffect=HRESULT(WINAPI*)(void*,SIZE_T,const char*,const D3D10_SHADER_MACRO*,ID3D10Include*,UINT,UINT,
    ID3D10Blob**,ID3D10Blob**);
static struct {
    CreateDevice createDevice;CreateDeviceAndSwapChain createDeviceAndSwapChain;
    CreateDeviceAndSwapChain1 createDeviceAndSwapChain1;CompileShader compileShader;ReflectShader reflectShader;
    DisassembleShader disassembleShader;CoreCreateDevice coreCreateDevice;HRESULT(WINAPI* coreRegisterLayers)(void);
    CreateEffect createEffect;CreateEffectPool createEffectPool;CompileEffect compileEffect;
} ex;

// ------------------------------------------------------------ shim log capture
// The shim reports each deliberate refusal and fallback through
// OutputDebugStringA. With no debugger that raises DBG_PRINTEXCEPTION_C, which a
// vectored handler sees first, so the test asserts on the lines themselves
// instead of trusting that a code path ran.
static char logText[16384];static unsigned logUsed=0;
static LONG CALLBACK captureDebugString(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record=info->ExceptionRecord;
    if(record->ExceptionCode==0x40010006UL&&record->NumberParameters>=2) {
        const char* text=(const char*)record->ExceptionInformation[1];
        while(text&&*text&&logUsed+2<sizeof logText)logText[logUsed++]=*text++;
        logText[logUsed++]='\n';logText[logUsed]=0;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static bool logged(const char* needle) {
    for(unsigned i=0;i<logUsed;++i) {
        unsigned k=0;while(needle[k]&&i+k<logUsed&&logText[i+k]==needle[k])++k;
        if(!needle[k])return true;
    }
    return false;
}
#define CHECK_LOG(text) do { ++checks; if(!logged(text)){++failures;out("FAIL missing shim log: " text "\n");} } while(0)
#define RELEASE(p) do { if(p){(p)->Release();(p)=nullptr;} } while(0)

static ULONG refs(IUnknown* object) { object->AddRef();return object->Release(); }
static bool isRed(const unsigned char p[4]) { return p[0]>0x80&&p[1]<0x40&&p[2]<0x40; }
static bool isGreen(const unsigned char p[4]) { return p[1]>0x80&&p[0]<0x40&&p[2]<0x40; }
static bool isBlue(const unsigned char p[4]) { return p[2]>0xc0&&p[0]<0x40&&p[1]<0x40; }
static bool isClear(const unsigned char p[4]) { return p[0]<0x20&&p[1]<0x20&&p[2]<0x20; }

// A 64x64 RGBA8 render target and its staging twin: draw, copy, map, read.
struct Target {
    ID3D10Texture2D* texture=nullptr;ID3D10Texture2D* staging=nullptr;ID3D10RenderTargetView* view=nullptr;
    bool make(ID3D10Device* device) {
        D3D10_TEXTURE2D_DESC desc{};
        desc.Width=64;desc.Height=64;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.Usage=D3D10_USAGE_DEFAULT;desc.BindFlags=D3D10_BIND_RENDER_TARGET;
        CHECK_HR(device->CreateTexture2D(&desc,nullptr,&texture));
        desc.BindFlags=0;desc.Usage=D3D10_USAGE_STAGING;desc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
        CHECK_HR(device->CreateTexture2D(&desc,nullptr,&staging));
        if(texture)CHECK_HR(device->CreateRenderTargetView(texture,nullptr,&view));
        return texture&&staging&&view;
    }
    void clear(ID3D10Device* device) { const FLOAT black[4]={0,0,0,1};device->ClearRenderTargetView(view,black); }
    // Copy the target into the staging texture and answer one pixel of it.
    bool pixel(ID3D10Device* device,unsigned x,unsigned y,unsigned char texel[4]) {
        device->CopyResource(staging,texture);
        D3D10_MAPPED_TEXTURE2D mapped{};
        HRESULT hr=staging->Map(0,D3D10_MAP_READ,0,&mapped);
        CHECK_HR(hr);
        if(FAILED(hr)||!mapped.pData)return false;
        const unsigned char* p=(const unsigned char*)mapped.pData+y*mapped.RowPitch+x*4;
        texel[0]=p[0];texel[1]=p[1];texel[2]=p[2];texel[3]=p[3];
        staging->Unmap(0);
        return true;
    }
    void release() { RELEASE(view);RELEASE(staging);RELEASE(texture); }
};
static ID3D10Buffer* makeBuffer(ID3D10Device* device,const void* data,UINT bytes,UINT bind) {
    D3D10_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D10_USAGE_IMMUTABLE;desc.BindFlags=bind;
    D3D10_SUBRESOURCE_DATA init{};init.pSysMem=data;
    ID3D10Buffer* buffer=nullptr;
    CHECK_HR(device->CreateBuffer(&desc,&init,&buffer));
    return buffer;
}
// Two triangles in one buffer (red, then green), a point, and the layout they share.
static const float redGreen[6][6]={
    {-0.75f,0.75f,1,0,0,1},{0.75f,0.75f,1,0,0,1},{0.0f,-0.75f,1,0,0,1},
    {-0.75f,0.75f,0,1,0,1},{0.75f,0.75f,0,1,0,1},{0.0f,-0.75f,0,1,0,1}};
static const float pointVertex[6]={0.0f,0.0f,0,1,0,1};
static const D3D10_INPUT_ELEMENT_DESC layoutElements[]={
    {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D10_INPUT_PER_VERTEX_DATA,0},
    {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D10_INPUT_PER_VERTEX_DATA,0}};
static const UINT vertexStride=sizeof redGreen[0];
static const unsigned char garbageBytes[16]={1,2,3,4};   // not DXBC

// The pipeline every draw and query test shares: vs_4_0/ps_4_0/gs_4_0 from the
// console fixture, an input layout, and a red triangle (vertices 0..2) in a
// buffer that also holds a green one (vertices 3..5).
struct Pipeline {
    Target target;
    ID3D10VertexShader* vs=nullptr;ID3D10PixelShader* ps=nullptr;ID3D10GeometryShader* gs=nullptr;
    ID3D10InputLayout* layout=nullptr;ID3D10Buffer* triangles=nullptr;
    bool make(ID3D10Device* device) {
        if(!target.make(device))return false;
        CHECK_HR(device->CreateVertexShader(d3d10_vs,sizeof d3d10_vs,&vs));
        CHECK_HR(device->CreatePixelShader(d3d10_ps,sizeof d3d10_ps,&ps));
        CHECK_HR(device->CreateGeometryShader(d3d10_gs,sizeof d3d10_gs,&gs));
        CHECK_HR(device->CreateInputLayout(layoutElements,2,d3d10_vs,sizeof d3d10_vs,&layout));
        triangles=makeBuffer(device,redGreen,sizeof redGreen,D3D10_BIND_VERTEX_BUFFER);
        return vs&&ps&&gs&&layout&&triangles;
    }
    void bind(ID3D10Device* device) {
        D3D10_VIEWPORT viewport{0,0,64,64,0.0f,1.0f};
        device->RSSetViewports(1,&viewport);
        const UINT offset=0;
        device->IASetInputLayout(layout);
        device->IASetVertexBuffers(0,1,&triangles,&vertexStride,&offset);
        device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(vs);device->PSSetShader(ps);
        device->OMSetRenderTargets(1,&target.view,nullptr);
    }
    void release(ID3D10Device* device) {
        device->ClearState();   // drop the shim's binding references before the objects go
        RELEASE(triangles);RELEASE(layout);RELEASE(gs);RELEASE(ps);RELEASE(vs);target.release();
    }
};

// ----------------------------------------------------- device creation contracts
// Wine's d3d11 does not record the flags a device was created with and always
// answers 0. A host that does record them must answer the translated value.
static void checkNativeFlags(ID3D11Device* native,UINT expected) {
    const UINT got=native->GetCreationFlags();
    if(!got){out("INFO host d3d11 reports no creation flags; the translation is not observable here\n");return;}
    CHECK(got==expected);
}
static void testDeviceCreation(ID3D10Device* device) {
    // A NULL device pointer is refused, not written through after the device
    // was already created.
    CHECK_HR_IS(ex.createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,nullptr),E_POINTER);
    ID3D10Device* failed=(ID3D10Device*)(uintptr_t)1;
    CHECK_HR_IS(ex.createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION+1,&failed),E_INVALIDARG);
    CHECK(failed==nullptr);   // a failed creation leaves the guest's pointer NULL

    // Both legacy software driver requests take the logged WARP fallback.
    CHECK_LOG("no reference rasterizer on this platform; using WARP");
    CHECK_LOG("no software rasterizer module on this platform; using WARP");

    // The D3D10 creation flags are translated, not forwarded: ALLOW_NULL_FROM_MAP
    // and STRICT_VALIDATION have no D3D11 twin (logged, dropped), and DEBUGGABLE is
    // numbered differently in the two SDKs.
    ID3D10Device* flagged=nullptr;
    const UINT guestFlags=D3D10_CREATE_DEVICE_SINGLETHREADED|D3D10_CREATE_DEVICE_BGRA_SUPPORT|
        D3D10_CREATE_DEVICE_ALLOW_NULL_FROM_MAP|D3D10_CREATE_DEVICE_STRICT_VALIDATION;
    CHECK_HR(ex.createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,guestFlags,D3D10_SDK_VERSION,&flagged));
    if(flagged) {
        CHECK(flagged->GetCreationFlags()==guestFlags);   // the guest sees what it asked for
        ID3D11Device* native=nullptr;
        CHECK_HR(flagged->QueryInterface(__uuidof(ID3D11Device),(void**)&native));
        if(native) {
            checkNativeFlags(native,D3D11_CREATE_DEVICE_SINGLETHREADED|D3D11_CREATE_DEVICE_BGRA_SUPPORT);
            native->Release();
        }
        ID3D10Multithread* multithread=nullptr;
        CHECK_HR(flagged->QueryInterface(__uuidof(ID3D10Multithread),(void**)&multithread));
        if(multithread){CHECK(!multithread->GetMultithreadProtected());multithread->Release();}
        flagged->Release();
    }
    CHECK_LOG("ALLOW_NULL_FROM_MAP has no D3D11 equivalent");
    CHECK_LOG("STRICT_VALIDATION has no D3D11 equivalent");
    ID3D10Device* debuggable=nullptr;
    if(SUCCEEDED(ex.createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,D3D10_CREATE_DEVICE_DEBUGGABLE,
        D3D10_SDK_VERSION,&debuggable))&&debuggable) {
        ID3D11Device* native=nullptr;
        if(SUCCEEDED(debuggable->QueryInterface(__uuidof(ID3D11Device),(void**)&native))&&native) {
            checkNativeFlags(native,D3D11_CREATE_DEVICE_DEBUGGABLE);
            native->Release();
        }
        debuggable->Release();
    }

    // D3D10CoreCreateDevice: the same wrapped device, with the feature level and
    // flags the caller asked for and an explicit adapter honoured.
    CHECK_HR_IS(ex.coreCreateDevice(nullptr,nullptr,0,D3D_FEATURE_LEVEL_10_0,nullptr),E_POINTER);
    ID3D10Device* core=nullptr;
    CHECK_HR(ex.coreCreateDevice(nullptr,nullptr,D3D10_CREATE_DEVICE_SINGLETHREADED,D3D_FEATURE_LEVEL_10_1,&core));
    if(core) {
        ID3D10Device1* core1=nullptr;
        CHECK_HR(core->QueryInterface(__uuidof(ID3D10Device1),(void**)&core1));
        if(core1){CHECK(core1->GetFeatureLevel()==D3D10_FEATURE_LEVEL_10_1);core1->Release();}
        CHECK(core->GetCreationFlags()==D3D10_CREATE_DEVICE_SINGLETHREADED);
        core->Release();
    }
    // A level the D3D10 device cannot be (11_0) is answered with the 10_0 floor.
    core=nullptr;
    CHECK_HR(ex.coreCreateDevice(nullptr,nullptr,0,D3D_FEATURE_LEVEL_11_0,&core));
    if(core) {
        ID3D10Device1* core1=nullptr;
        if(SUCCEEDED(core->QueryInterface(__uuidof(ID3D10Device1),(void**)&core1))&&core1) {
            CHECK(core1->GetFeatureLevel()==D3D10_FEATURE_LEVEL_10_0);
            core1->Release();
        }
        core->Release();
    }
    IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;IDXGIFactory* factory=nullptr;
    CHECK_HR(device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi));
    if(dxgi)CHECK_HR(dxgi->GetAdapter(&adapter));
    if(adapter)CHECK_HR(adapter->GetParent(__uuidof(IDXGIFactory),(void**)&factory));
    if(adapter&&factory) {
        core=nullptr;
        CHECK_HR(ex.coreCreateDevice(factory,adapter,0,D3D_FEATURE_LEVEL_10_0,&core));
        RELEASE(core);
    }
    RELEASE(factory);RELEASE(adapter);RELEASE(dxgi);
    CHECK_HR(ex.coreRegisterLayers());

    // The D3D10.1 swapchain entry point refuses exactly what the D3D10 one does.
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"d3d10-shim-test-1";
    if(RegisterClassW(&cls)) {
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"d3d10 shim",WS_OVERLAPPEDWINDOW,
            0,0,160,120,nullptr,nullptr,cls.hInstance,nullptr);
        CHECK(window!=nullptr);
        if(window) {
            DXGI_SWAP_CHAIN_DESC chain{};
            chain.BufferDesc.Width=160;chain.BufferDesc.Height=120;chain.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            chain.SampleDesc.Count=1;chain.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;chain.BufferCount=2;
            chain.OutputWindow=window;chain.Windowed=FALSE;chain.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
            ID3D10Device1* refusedDevice=nullptr;IDXGISwapChain* refusedSwapchain=nullptr;
            CHECK_HR_IS(ex.createDeviceAndSwapChain1(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,
                D3D10_FEATURE_LEVEL_10_1,D3D10_1_SDK_VERSION,&chain,&refusedSwapchain,&refusedDevice),DXGI_ERROR_UNSUPPORTED);
            CHECK(!refusedDevice&&!refusedSwapchain);
            chain.Windowed=TRUE;chain.SampleDesc.Count=4;
            CHECK_HR_IS(ex.createDeviceAndSwapChain1(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,
                D3D10_FEATURE_LEVEL_10_1,D3D10_1_SDK_VERSION,&chain,&refusedSwapchain,&refusedDevice),DXGI_ERROR_UNSUPPORTED);
            CHECK(!refusedDevice&&!refusedSwapchain);
            chain.SampleDesc.Count=1;
            ID3D10Device1* chained=nullptr;IDXGISwapChain* swapchain=nullptr;
            CHECK_HR(ex.createDeviceAndSwapChain1(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,
                D3D10_FEATURE_LEVEL_10_1,D3D10_1_SDK_VERSION,&chain,&swapchain,&chained));
            if(chained)CHECK(chained->GetFeatureLevel()==D3D10_FEATURE_LEVEL_10_1);
            RELEASE(swapchain);RELEASE(chained);
            DestroyWindow(window);
        }
    }
    CHECK_LOG("exclusive fullscreen refused");
    CHECK_LOG("multisampled swapchain refused");
}

// ------------------------------------------------------------ deliberate refusals
static void testRefusals(ID3D10Device* device) {
    // FX10 stays an honest refusal: E_NOTIMPL, outputs zeroed, one log line each.
    void* effect=(void*)(uintptr_t)1;void* pool=(void*)(uintptr_t)1;
    ID3D10Blob* blob=(ID3D10Blob*)(uintptr_t)1;ID3D10Blob* errors=(ID3D10Blob*)(uintptr_t)1;
    CHECK_HR_IS(ex.createEffect(nullptr,0,0,device,nullptr,&effect),E_NOTIMPL);
    CHECK(effect==nullptr);
    CHECK_HR_IS(ex.createEffectPool(nullptr,0,0,device,&pool),E_NOTIMPL);
    CHECK(pool==nullptr);
    CHECK_HR_IS(ex.compileEffect(nullptr,0,nullptr,nullptr,nullptr,0,0,&blob,&errors),E_NOTIMPL);
    CHECK(blob==nullptr&&errors==nullptr);
    CHECK_LOG("FX10 runtime is not implemented; effects refused");
    CHECK_LOG("FX10 runtime is not implemented; effect pools refused");
    CHECK_LOG("FX10 runtime is not implemented; effect compilation refused");

    // Shared resources and performance counters are refused the same way.
    void* shared=(void*)(uintptr_t)1;
    CHECK_HR_IS(device->OpenSharedResource((HANDLE)(uintptr_t)4,__uuidof(ID3D10Texture2D),&shared),E_NOTIMPL);
    CHECK(shared==nullptr);
    CHECK_LOG("shared resources are not supported on this platform");
    D3D10_COUNTER_DESC counterDesc{D3D10_COUNTER_GPU_IDLE,0};
    ID3D10Counter* counter=(ID3D10Counter*)(uintptr_t)1;
    CHECK_HR_IS(device->CreateCounter(&counterDesc,&counter),E_NOTIMPL);
    CHECK(counter==nullptr);
    CHECK_LOG("performance counters are not supported on this platform");
    D3D10_COUNTER_INFO info{D3D10_COUNTER_GPU_IDLE,7,7};
    device->CheckCounterInfo(&info);
    CHECK(info.NumSimultaneousCounters==0&&info.NumDetectableParallelUnits==0);
    D3D10_COUNTER_TYPE type{};UINT active=0,nameLength=0,unitsLength=0,descriptionLength=0;
    CHECK_HR_IS(device->CheckCounter(&counterDesc,&type,&active,nullptr,&nameLength,nullptr,&unitsLength,nullptr,
        &descriptionLength),E_NOTIMPL);
}

// ------------------------------------------------------- descriptor conversions
static void testDescriptors(ID3D10Device* device) {
    // Depth-stencil views over an array slice range: the D3D10 union is copied
    // whole, so FirstArraySlice and ArraySize survive the trip to D3D11.
    D3D10_TEXTURE2D_DESC arrayDesc{};
    arrayDesc.Width=16;arrayDesc.Height=16;arrayDesc.MipLevels=1;arrayDesc.ArraySize=4;
    arrayDesc.Format=DXGI_FORMAT_D32_FLOAT;arrayDesc.SampleDesc.Count=1;
    arrayDesc.Usage=D3D10_USAGE_DEFAULT;arrayDesc.BindFlags=D3D10_BIND_DEPTH_STENCIL;
    ID3D10Texture2D* depthArray=nullptr;
    CHECK_HR(device->CreateTexture2D(&arrayDesc,nullptr,&depthArray));
    if(depthArray) {
        D3D10_DEPTH_STENCIL_VIEW_DESC slice{};
        slice.Format=DXGI_FORMAT_D32_FLOAT;slice.ViewDimension=D3D10_DSV_DIMENSION_TEXTURE2DARRAY;
        slice.Texture2DArray.MipSlice=0;slice.Texture2DArray.FirstArraySlice=1;slice.Texture2DArray.ArraySize=2;
        ID3D10DepthStencilView* sliceView=nullptr;
        CHECK_HR(device->CreateDepthStencilView(depthArray,&slice,&sliceView));
        if(sliceView) {
            D3D10_DEPTH_STENCIL_VIEW_DESC got{};sliceView->GetDesc(&got);
            CHECK(got.ViewDimension==D3D10_DSV_DIMENSION_TEXTURE2DARRAY);
            CHECK(got.Texture2DArray.FirstArraySlice==1&&got.Texture2DArray.ArraySize==2);
            ID3D11DepthStencilView* native=nullptr;
            CHECK_HR(sliceView->QueryInterface(__uuidof(ID3D11DepthStencilView),(void**)&native));
            if(native) {
                D3D11_DEPTH_STENCIL_VIEW_DESC nativeDesc{};native->GetDesc(&nativeDesc);
                CHECK(nativeDesc.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY);
                CHECK(nativeDesc.Texture2DArray.FirstArraySlice==1&&nativeDesc.Texture2DArray.ArraySize==2);
                native->Release();
            }
            sliceView->Release();
        }
        // Created with no descriptor, a view answers the resolved default.
        ID3D10DepthStencilView* defaultView=nullptr;
        CHECK_HR(device->CreateDepthStencilView(depthArray,nullptr,&defaultView));
        if(defaultView) {
            D3D10_DEPTH_STENCIL_VIEW_DESC got{};defaultView->GetDesc(&got);
            CHECK(got.Format==DXGI_FORMAT_D32_FLOAT&&got.ViewDimension==D3D10_DSV_DIMENSION_TEXTURE2DARRAY);
            CHECK(got.Texture2DArray.ArraySize==4);
            defaultView->Release();
        }
        depthArray->Release();
    }
    D3D10_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width=16;colorDesc.Height=16;colorDesc.MipLevels=3;colorDesc.ArraySize=1;
    colorDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;colorDesc.SampleDesc.Count=1;
    colorDesc.Usage=D3D10_USAGE_DEFAULT;colorDesc.BindFlags=D3D10_BIND_RENDER_TARGET|D3D10_BIND_SHADER_RESOURCE;
    ID3D10Texture2D* color=nullptr;
    CHECK_HR(device->CreateTexture2D(&colorDesc,nullptr,&color));
    if(color) {
        ID3D10RenderTargetView* rtv=nullptr;
        CHECK_HR(device->CreateRenderTargetView(color,nullptr,&rtv));
        if(rtv) {
            D3D10_RENDER_TARGET_VIEW_DESC got{};rtv->GetDesc(&got);
            CHECK(got.Format==DXGI_FORMAT_R8G8B8A8_UNORM&&got.ViewDimension==D3D10_RTV_DIMENSION_TEXTURE2D);
            rtv->Release();
        }
        ID3D10ShaderResourceView* srv=nullptr;
        CHECK_HR(device->CreateShaderResourceView(color,nullptr,&srv));
        if(srv) {
            D3D10_SHADER_RESOURCE_VIEW_DESC got{};srv->GetDesc(&got);
            CHECK(got.Format==DXGI_FORMAT_R8G8B8A8_UNORM&&got.ViewDimension==D3D10_SRV_DIMENSION_TEXTURE2D);
            CHECK(got.Texture2D.MostDetailedMip==0&&got.Texture2D.MipLevels==3);
            srv->Release();
        }
        color->Release();
    }

    // Misc flags come back in D3D10's numbering: GENERATE_MIPS|TEXTURECUBE share
    // their bits, GDI_COMPATIBLE (D3D10 0x20, D3D11 0x200) does not.
    D3D10_TEXTURE2D_DESC cubeDesc{};
    cubeDesc.Width=8;cubeDesc.Height=8;cubeDesc.MipLevels=0;cubeDesc.ArraySize=6;cubeDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    cubeDesc.SampleDesc.Count=1;cubeDesc.Usage=D3D10_USAGE_DEFAULT;
    cubeDesc.BindFlags=D3D10_BIND_SHADER_RESOURCE|D3D10_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags=D3D10_RESOURCE_MISC_TEXTURECUBE|D3D10_RESOURCE_MISC_GENERATE_MIPS;
    ID3D10Texture2D* cube=nullptr;
    CHECK_HR(device->CreateTexture2D(&cubeDesc,nullptr,&cube));
    if(cube) {
        D3D10_TEXTURE2D_DESC got{};cube->GetDesc(&got);
        CHECK(got.MiscFlags==(D3D10_RESOURCE_MISC_TEXTURECUBE|D3D10_RESOURCE_MISC_GENERATE_MIPS));
        CHECK(got.MipLevels==4&&got.ArraySize==6);   // MipLevels 0 asks for the full chain
        cube->Release();
    }
    D3D10_TEXTURE2D_DESC gdiDesc{};
    gdiDesc.Width=16;gdiDesc.Height=16;gdiDesc.MipLevels=1;gdiDesc.ArraySize=1;gdiDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    gdiDesc.SampleDesc.Count=1;gdiDesc.Usage=D3D10_USAGE_DEFAULT;gdiDesc.BindFlags=D3D10_BIND_RENDER_TARGET;
    gdiDesc.MiscFlags=D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
    ID3D10Texture2D* gdi=nullptr;
    HRESULT gdiHr=device->CreateTexture2D(&gdiDesc,nullptr,&gdi);
    if(FAILED(gdiHr)) {
        // Wine's d3d11 refuses GDI-compatible surfaces; the conversion is still
        // covered by the forward direction in the cases above.
        outHex("INFO GDI_COMPATIBLE texture refused by the host d3d11 hr=",(unsigned long)gdiHr);
    } else if(gdi) {
        D3D10_TEXTURE2D_DESC got{};gdi->GetDesc(&got);
        CHECK(got.MiscFlags==D3D10_RESOURCE_MISC_GDI_COMPATIBLE);
        ID3D11Texture2D* native=nullptr;
        if(SUCCEEDED(gdi->QueryInterface(__uuidof(ID3D11Texture2D),(void**)&native))&&native) {
            D3D11_TEXTURE2D_DESC nativeDesc{};native->GetDesc(&nativeDesc);
            CHECK(nativeDesc.MiscFlags==D3D11_RESOURCE_MISC_GDI_COMPATIBLE);
            native->Release();
        }
        gdi->Release();
    }

    // 1D and 3D textures round-trip through their own descriptors.
    D3D10_TEXTURE1D_DESC d1{};d1.Width=16;d1.MipLevels=1;d1.ArraySize=2;d1.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d1.Usage=D3D10_USAGE_DEFAULT;d1.BindFlags=D3D10_BIND_SHADER_RESOURCE;
    ID3D10Texture1D* t1=nullptr;
    CHECK_HR(device->CreateTexture1D(&d1,nullptr,&t1));
    if(t1) {
        D3D10_TEXTURE1D_DESC got{};t1->GetDesc(&got);
        CHECK(got.Width==16&&got.ArraySize==2&&got.Format==DXGI_FORMAT_R8G8B8A8_UNORM&&got.BindFlags==D3D10_BIND_SHADER_RESOURCE);
        t1->Release();
    }
    D3D10_TEXTURE3D_DESC d3{};d3.Width=4;d3.Height=4;d3.Depth=4;d3.MipLevels=1;d3.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d3.Usage=D3D10_USAGE_DEFAULT;d3.BindFlags=D3D10_BIND_SHADER_RESOURCE;
    ID3D10Texture3D* t3=nullptr;
    CHECK_HR(device->CreateTexture3D(&d3,nullptr,&t3));
    if(t3) {
        D3D10_TEXTURE3D_DESC got{};t3->GetDesc(&got);
        CHECK(got.Width==4&&got.Height==4&&got.Depth==4&&got.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
        t3->Release();
    }

    // Invalid calls fail cleanly: the out pointer is always written, never left
    // holding the caller's garbage, and a NULL out pointer is refused.
    ID3D10Texture2D* none=(ID3D10Texture2D*)(uintptr_t)1;
    D3D10_TEXTURE2D_DESC bad=colorDesc;bad.Width=0;
    CHECK(FAILED(device->CreateTexture2D(&bad,nullptr,&none)));
    CHECK(none==nullptr);
    CHECK_HR_IS(device->CreateTexture2D(&colorDesc,nullptr,nullptr),E_POINTER);
    ID3D10Buffer* noBuffer=(ID3D10Buffer*)(uintptr_t)1;
    D3D10_BUFFER_DESC badBuffer{};
    CHECK(FAILED(device->CreateBuffer(&badBuffer,nullptr,&noBuffer)));
    CHECK(noBuffer==nullptr);
    CHECK_HR_IS(device->CreateBuffer(nullptr,nullptr,&noBuffer),E_INVALIDARG);
    ID3D10VertexShader* noVs=(ID3D10VertexShader*)(uintptr_t)1;
    CHECK(FAILED(device->CreateVertexShader(garbageBytes,sizeof garbageBytes,&noVs)));
    CHECK(noVs==nullptr);
    ID3D10ShaderResourceView* noView=(ID3D10ShaderResourceView*)(uintptr_t)1;
    CHECK_HR_IS(device->CreateShaderResourceView(nullptr,nullptr,&noView),E_INVALIDARG);
    CHECK(noView==nullptr);
    ID3D10InputLayout* noLayout=(ID3D10InputLayout*)(uintptr_t)1;
    CHECK_HR_IS(device->CreateInputLayout(nullptr,2,d3d10_vs,sizeof d3d10_vs,&noLayout),E_INVALIDARG);
    CHECK(noLayout==nullptr);
    D3D10_INPUT_ELEMENT_DESC many[33]{};
    CHECK_HR_IS(device->CreateInputLayout(many,33,d3d10_vs,sizeof d3d10_vs,&noLayout),E_INVALIDARG);
    // Stream-output declarations: a stride needs entries, entries need an array,
    // and a stride cannot be combined with any output slot but 0.
    ID3D10GeometryShader* noGs=(ID3D10GeometryShader*)(uintptr_t)1;
    D3D10_SO_DECLARATION_ENTRY slotOne[]={{"SV_POSITION",0,0,4,1}};
    D3D10_SO_DECLARATION_ENTRY slotZero[]={{"SV_POSITION",0,0,4,0},{"COLOR",0,0,4,0}};
    CHECK_HR_IS(device->CreateGeometryShaderWithStreamOutput(d3d10_gs,sizeof d3d10_gs,nullptr,0,32,&noGs),E_INVALIDARG);
    CHECK(noGs==nullptr);
    CHECK_HR_IS(device->CreateGeometryShaderWithStreamOutput(d3d10_gs,sizeof d3d10_gs,nullptr,2,32,&noGs),E_INVALIDARG);
    CHECK_HR_IS(device->CreateGeometryShaderWithStreamOutput(d3d10_gs,sizeof d3d10_gs,slotOne,1,16,&noGs),E_INVALIDARG);
    ID3D10GeometryShader* soGs=nullptr;
    CHECK_HR(device->CreateGeometryShaderWithStreamOutput(d3d10_gs,sizeof d3d10_gs,slotZero,2,32,&soGs));
    RELEASE(soGs);

    // GetDevice answers the creating device with a reference; private data and
    // the eviction priority forward to the D3D11 object.
    ID3D10Buffer* buffer=makeBuffer(device,redGreen,sizeof redGreen,D3D10_BIND_VERTEX_BUFFER);
    if(buffer) {
        ID3D10Device* owner=nullptr;const ULONG before=refs(device);
        buffer->GetDevice(&owner);
        CHECK(owner==device&&refs(device)==before+1);
        RELEASE(owner);
        const UINT value=0x1234;
        CHECK_HR(buffer->SetPrivateData(__uuidof(ID3D10Buffer),sizeof value,&value));
        UINT got=0,size=sizeof got;
        CHECK_HR(buffer->GetPrivateData(__uuidof(ID3D10Buffer),&size,&got));
        CHECK(got==value&&size==sizeof value);
        buffer->Release();
    }
}

// ----------------------------------------------------------------- draw path
// Reads one pixel of an RGBA8 texture's subresource through an 8x8 staging copy
// of its top-left size x size corner.
static bool readTexel(ID3D10Device* device,ID3D10Texture2D* source,UINT subresource,unsigned x,unsigned y,unsigned char texel[4],
                      UINT size=8) {
    D3D10_TEXTURE2D_DESC desc{};
    desc.Width=8;desc.Height=8;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.Usage=D3D10_USAGE_STAGING;desc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
    ID3D10Texture2D* staging=nullptr;
    CHECK_HR(device->CreateTexture2D(&desc,nullptr,&staging));
    if(!staging)return false;
    D3D10_BOX corner{0,0,0,size,size,1};
    device->CopySubresourceRegion(staging,0,0,0,0,source,subresource,&corner);
    D3D10_MAPPED_TEXTURE2D mapped{};
    HRESULT hr=staging->Map(0,D3D10_MAP_READ,0,&mapped);
    CHECK_HR(hr);
    bool ok=SUCCEEDED(hr)&&mapped.pData;
    if(ok) {
        const unsigned char* p=(const unsigned char*)mapped.pData+y*mapped.RowPitch+x*4;
        texel[0]=p[0];texel[1]=p[1];texel[2]=p[2];texel[3]=p[3];
        staging->Unmap(0);
    }
    staging->Release();
    return ok;
}
static bool sameTexel(const unsigned char a[4],unsigned r,unsigned g,unsigned b,unsigned alpha) {
    return a[0]==r&&a[1]==g&&a[2]==b&&a[3]==alpha;
}

static void testDrawPath(ID3D10Device* device) {
    Pipeline p;
    if(!p.make(device)){p.release(device);return;}
    p.bind(device);
    unsigned char px[4];

    // Bound state reads back as the very objects the guest bound, and reading it
    // leaves no reference behind.
    {
        const ULONG layoutRefs=refs(p.layout),vsRefs=refs(p.vs),psRefs=refs(p.ps),viewRefs=refs(p.target.view),
            bufferRefs=refs(p.triangles);
        ID3D10InputLayout* gotLayout=nullptr;device->IAGetInputLayout(&gotLayout);CHECK(gotLayout==p.layout);RELEASE(gotLayout);
        ID3D10VertexShader* gotVs=nullptr;device->VSGetShader(&gotVs);CHECK(gotVs==p.vs);RELEASE(gotVs);
        ID3D10PixelShader* gotPs=nullptr;device->PSGetShader(&gotPs);CHECK(gotPs==p.ps);RELEASE(gotPs);
        ID3D10GeometryShader* gotGs=(ID3D10GeometryShader*)(uintptr_t)1;
        device->GSGetShader(&gotGs);CHECK(gotGs==nullptr);   // no GS bound: NULL, not stale
        D3D10_PRIMITIVE_TOPOLOGY topology=D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED;
        device->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D10Buffer* gotBuffer=nullptr;UINT gotStride=0,gotOffset=99;
        device->IAGetVertexBuffers(0,1,&gotBuffer,&gotStride,&gotOffset);
        CHECK(gotBuffer==p.triangles&&gotStride==vertexStride&&gotOffset==0);RELEASE(gotBuffer);
        ID3D10RenderTargetView* gotView=nullptr;ID3D10DepthStencilView* gotDepth=(ID3D10DepthStencilView*)(uintptr_t)1;
        device->OMGetRenderTargets(1,&gotView,&gotDepth);CHECK(gotView==p.target.view&&gotDepth==nullptr);RELEASE(gotView);
        device->OMGetRenderTargets(1,&gotView,nullptr);CHECK(gotView==p.target.view);RELEASE(gotView);
        CHECK(refs(p.layout)==layoutRefs&&refs(p.vs)==vsRefs&&refs(p.ps)==psRefs&&refs(p.target.view)==viewRefs
            &&refs(p.triangles)==bufferRefs);
    }
    // D3D11 hands out a reference on the bound depth view even when the guest
    // passes no depth pointer; the shim must drop it again.
    {
        D3D10_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width=64;depthDesc.Height=64;depthDesc.MipLevels=1;depthDesc.ArraySize=1;
        depthDesc.Format=DXGI_FORMAT_D32_FLOAT;depthDesc.SampleDesc.Count=1;
        depthDesc.Usage=D3D10_USAGE_DEFAULT;depthDesc.BindFlags=D3D10_BIND_DEPTH_STENCIL;
        ID3D10Texture2D* depth=nullptr;ID3D10DepthStencilView* depthView=nullptr;ID3D11DepthStencilView* native=nullptr;
        CHECK_HR(device->CreateTexture2D(&depthDesc,nullptr,&depth));
        if(depth)CHECK_HR(device->CreateDepthStencilView(depth,nullptr,&depthView));
        if(depthView)CHECK_HR(depthView->QueryInterface(__uuidof(ID3D11DepthStencilView),(void**)&native));
        if(native) {
            device->OMSetRenderTargets(1,&p.target.view,depthView);
            const ULONG before=refs(native);
            ID3D10RenderTargetView* gotView=nullptr;
            device->OMGetRenderTargets(1,&gotView,nullptr);
            RELEASE(gotView);
            CHECK(refs(native)==before);
            ID3D10DepthStencilView* gotDepth=nullptr;
            device->OMGetRenderTargets(0,nullptr,&gotDepth);   // no colour targets asked for at all
            CHECK(gotDepth==depthView);RELEASE(gotDepth);
            CHECK(refs(native)==before);
            device->OMSetRenderTargets(1,&p.target.view,nullptr);
            native->Release();
        }
        RELEASE(depthView);RELEASE(depth);
    }

    // Draw: red triangle in the middle, the clear colour in the corner.
    p.target.clear(device);
    device->Draw(3,0);
    CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
    CHECK(p.target.pixel(device,4,4,px)&&isClear(px));
    // DrawInstanced: two instances of the same triangle cover the same pixels.
    p.target.clear(device);
    device->DrawInstanced(3,2,0,0);
    CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
    // StartVertexLocation selects the green triangle in the shared buffer.
    p.target.clear(device);
    device->Draw(3,3);
    CHECK(p.target.pixel(device,32,32,px)&&isGreen(px));

    // DrawIndexed: StartIndexLocation and BaseVertexLocation (a signed value)
    // each reach D3D11 unchanged.
    const UINT indices[6]={0,1,2,3,4,5};
    ID3D10Buffer* indexBuffer=makeBuffer(device,indices,sizeof indices,D3D10_BIND_INDEX_BUFFER);
    if(indexBuffer) {
        device->IASetIndexBuffer(indexBuffer,DXGI_FORMAT_R32_UINT,8);
        ID3D10Buffer* gotIndex=nullptr;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT offset=0;
        device->IAGetIndexBuffer(&gotIndex,&format,&offset);
        CHECK(gotIndex==indexBuffer&&format==DXGI_FORMAT_R32_UINT&&offset==8);   // byte offset 8 = index 2
        RELEASE(gotIndex);
        device->IASetIndexBuffer(indexBuffer,DXGI_FORMAT_R32_UINT,0);
        p.target.clear(device);device->DrawIndexed(3,0,0);
        CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
        p.target.clear(device);device->DrawIndexed(3,3,0);   // indices 3,4,5
        CHECK(p.target.pixel(device,32,32,px)&&isGreen(px));
        p.target.clear(device);device->DrawIndexed(3,0,3);   // base +3: vertices 3,4,5
        CHECK(p.target.pixel(device,32,32,px)&&isGreen(px));
        p.target.clear(device);device->DrawIndexed(3,3,-3);   // indices 3,4,5, base -3: vertices 0,1,2
        CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
        p.target.clear(device);device->DrawIndexedInstanced(3,2,3,-3,0);
        CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
        device->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
        ID3D10Buffer* unbound=(ID3D10Buffer*)(uintptr_t)1;
        device->IAGetIndexBuffer(&unbound,&format,&offset);
        CHECK(unbound==nullptr);
        indexBuffer->Release();
    }

    // Geometry shader: one point expands to a green triangle.
    ID3D10Buffer* point=makeBuffer(device,pointVertex,sizeof pointVertex,D3D10_BIND_VERTEX_BUFFER);
    if(point) {
        const UINT zero=0;
        device->IASetVertexBuffers(0,1,&point,&vertexStride,&zero);
        device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
        device->GSSetShader(p.gs);
        ID3D10GeometryShader* gotGs=nullptr;device->GSGetShader(&gotGs);CHECK(gotGs==p.gs);RELEASE(gotGs);
        p.target.clear(device);
        device->Draw(1,0);
        CHECK(p.target.pixel(device,32,32,px)&&isGreen(px));
        CHECK(p.target.pixel(device,4,4,px)&&isClear(px));

        // Stream-out: the same GS declared with an output, captured through a
        // SO_STATISTICS query and a staging copy of the SO buffer.
        D3D10_SO_DECLARATION_ENTRY declaration[]={{"SV_POSITION",0,0,4,0},{"COLOR",0,0,4,0}};
        ID3D10GeometryShader* soShader=nullptr;
        CHECK_HR(device->CreateGeometryShaderWithStreamOutput(d3d10_gs,sizeof d3d10_gs,declaration,2,32,&soShader));
        D3D10_BUFFER_DESC soDesc{};soDesc.ByteWidth=3*32;soDesc.Usage=D3D10_USAGE_DEFAULT;soDesc.BindFlags=D3D10_BIND_STREAM_OUTPUT;
        D3D10_BUFFER_DESC stageDesc{};stageDesc.ByteWidth=3*32;stageDesc.Usage=D3D10_USAGE_STAGING;
        stageDesc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
        ID3D10Buffer* soTarget=nullptr;ID3D10Buffer* soStaging=nullptr;
        CHECK_HR(device->CreateBuffer(&soDesc,nullptr,&soTarget));
        CHECK_HR(device->CreateBuffer(&stageDesc,nullptr,&soStaging));
        D3D10_QUERY_DESC statsDesc{D3D10_QUERY_SO_STATISTICS,D3D10_QUERY_MISC_FLAG(0)};
        ID3D10Query* stats=nullptr;
        CHECK_HR(device->CreateQuery(&statsDesc,&stats));
        if(soShader&&soTarget&&soStaging&&stats) {
            const UINT soOffset=0;
            device->SOSetTargets(1,&soTarget,&soOffset);
            ID3D10Buffer* gotSo=nullptr;UINT gotOffset=99;
            device->SOGetTargets(1,&gotSo,&gotOffset);
            CHECK(gotSo==soTarget&&gotOffset==0);RELEASE(gotSo);
            device->GSSetShader(soShader);
            stats->Begin();device->Draw(1,0);stats->End();
            device->GSSetShader(nullptr);
            device->SOSetTargets(0,nullptr,nullptr);
            D3D10_QUERY_DATA_SO_STATISTICS soStats{};
            HRESULT hr=S_FALSE;
            for(unsigned i=0;i<3000&&hr==S_FALSE;++i){hr=stats->GetData(&soStats,sizeof soStats,0);if(hr==S_FALSE)Sleep(1);}
            CHECK_HR_IS(hr,S_OK);
            CHECK(soStats.NumPrimitivesWritten==1&&soStats.PrimitivesStorageNeeded==1);
            device->CopyResource(soStaging,soTarget);
            void* mapped=nullptr;
            CHECK_HR(soStaging->Map(D3D10_MAP_READ,0,&mapped));
            if(mapped) {
                // POSITION then COLOR for each of the three expanded vertices.
                static const float expected[3][8]={
                    {0.0f,0.4f,0.0f,1.0f,0.0f,1.0f,0.0f,1.0f},
                    {0.4f,-0.4f,0.0f,1.0f,0.0f,1.0f,0.0f,1.0f},
                    {-0.4f,-0.4f,0.0f,1.0f,0.0f,1.0f,0.0f,1.0f}};
                const float* written=(const float*)mapped;
                bool same=true;
                for(unsigned v=0;v<3;++v)for(unsigned c=0;c<8;++c)if(written[v*8+c]!=expected[v][c])same=false;
                CHECK(same);
                soStaging->Unmap();
            }
        }
        RELEASE(stats);RELEASE(soStaging);RELEASE(soTarget);RELEASE(soShader);
        device->GSSetShader(nullptr);
        device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        point->Release();
    }
    p.bind(device);

    // UpdateSubresource, CopySubresourceRegion and CopyResource on default textures.
    D3D10_TEXTURE2D_DESC desc{};
    desc.Width=8;desc.Height=8;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.Usage=D3D10_USAGE_DEFAULT;desc.BindFlags=D3D10_BIND_SHADER_RESOURCE;
    ID3D10Texture2D *source=nullptr,*dest=nullptr,*whole=nullptr;
    CHECK_HR(device->CreateTexture2D(&desc,nullptr,&source));
    CHECK_HR(device->CreateTexture2D(&desc,nullptr,&dest));
    CHECK_HR(device->CreateTexture2D(&desc,nullptr,&whole));
    if(source&&dest&&whole) {
        unsigned char pattern[8*8*4],zero[8*8*4]{};
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned char* t=pattern+(y*8+x)*4;t[0]=(unsigned char)(x*16);t[1]=(unsigned char)(y*16);t[2]=0xaa;t[3]=0xff;
        }
        device->UpdateSubresource(source,0,nullptr,pattern,8*4,0);
        device->UpdateSubresource(dest,0,nullptr,zero,8*4,0);
        // A 4x2 region at (2,3) lands at (4,2); nothing outside it changes.
        D3D10_BOX box{2,3,0,6,5,1};
        device->CopySubresourceRegion(dest,0,4,2,0,source,0,&box);
        CHECK(readTexel(device,dest,0,4,2,px)&&sameTexel(px,2*16,3*16,0xaa,0xff));
        CHECK(readTexel(device,dest,0,7,3,px)&&sameTexel(px,5*16,4*16,0xaa,0xff));
        CHECK(readTexel(device,dest,0,3,2,px)&&sameTexel(px,0,0,0,0));
        CHECK(readTexel(device,dest,0,4,4,px)&&sameTexel(px,0,0,0,0));
        // A boxed UpdateSubresource writes only its box.
        const unsigned char white[2*1*4]={255,255,255,255,255,255,255,255};
        D3D10_BOX patch{0,6,0,2,7,1};
        device->UpdateSubresource(dest,0,&patch,white,2*4,0);
        CHECK(readTexel(device,dest,0,1,6,px)&&sameTexel(px,255,255,255,255));
        CHECK(readTexel(device,dest,0,2,6,px)&&sameTexel(px,0,0,0,0));
        device->CopyResource(whole,source);
        CHECK(readTexel(device,whole,0,7,7,px)&&sameTexel(px,7*16,7*16,0xaa,0xff));
        CHECK(readTexel(device,whole,0,0,0,px)&&sameTexel(px,0,0,0xaa,0xff));
    }
    RELEASE(whole);RELEASE(dest);RELEASE(source);

    // A cubemap with GenerateMips: only mip 0 is written, so a blue mip 1 can
    // only come from the generation. A second view that starts at mip 1 must
    // leave mip 1 alone (red, written by hand) and derive mip 2 from it.
    D3D10_TEXTURE2D_DESC cubeDesc{};
    cubeDesc.Width=16;cubeDesc.Height=16;cubeDesc.MipLevels=3;cubeDesc.ArraySize=6;cubeDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    cubeDesc.SampleDesc.Count=1;cubeDesc.Usage=D3D10_USAGE_DEFAULT;
    cubeDesc.BindFlags=D3D10_BIND_SHADER_RESOURCE|D3D10_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags=D3D10_RESOURCE_MISC_TEXTURECUBE|D3D10_RESOURCE_MISC_GENERATE_MIPS;
    ID3D10Texture2D* cube=nullptr;
    CHECK_HR(device->CreateTexture2D(&cubeDesc,nullptr,&cube));
    ID3D10Device1* device1=nullptr;
    CHECK_HR(device->QueryInterface(__uuidof(ID3D10Device1),(void**)&device1));
    if(cube&&device1) {
        static unsigned char face[16*16*4],red[8*8*4];
        for(unsigned i=0;i<16*16;++i){face[i*4+0]=0;face[i*4+1]=0;face[i*4+2]=0xff;face[i*4+3]=0xff;}
        for(unsigned i=0;i<8*8;++i){red[i*4+0]=0xff;red[i*4+1]=0;red[i*4+2]=0;red[i*4+3]=0xff;}
        for(unsigned f=0;f<6;++f)device->UpdateSubresource(cube,D3D10CalcSubresource(0,f,3),nullptr,face,16*4,0);
        D3D10_SHADER_RESOURCE_VIEW_DESC1 all{};
        all.Format=DXGI_FORMAT_R8G8B8A8_UNORM;all.ViewDimension=D3D10_SRV_DIMENSION_TEXTURECUBE;
        all.TextureCube.MostDetailedMip=0;all.TextureCube.MipLevels=3;
        ID3D10ShaderResourceView1* allView=nullptr;
        CHECK_HR(device1->CreateShaderResourceView1(cube,&all,&allView));
        if(allView) {
            device->GenerateMips(allView);
            for(unsigned f=0;f<6;f+=5) {   // first and last face
                CHECK(readTexel(device,cube,D3D10CalcSubresource(1,f,3),0,0,px)&&isBlue(px));
                CHECK(readTexel(device,cube,D3D10CalcSubresource(1,f,3),7,7,px)&&isBlue(px));
                CHECK(readTexel(device,cube,D3D10CalcSubresource(0,f,3),3,3,px)&&isBlue(px));   // the source is untouched
            }
            allView->Release();
        }
        for(unsigned f=0;f<6;++f)device->UpdateSubresource(cube,D3D10CalcSubresource(1,f,3),nullptr,red,8*4,0);
        D3D10_SHADER_RESOURCE_VIEW_DESC1 tail=all;
        tail.TextureCube.MostDetailedMip=1;tail.TextureCube.MipLevels=2;
        ID3D10ShaderResourceView1* tailView=nullptr;
        CHECK_HR(device1->CreateShaderResourceView1(cube,&tail,&tailView));
        if(tailView) {
            device->GenerateMips(tailView);
            for(unsigned f=0;f<6;f+=5) {
                CHECK(readTexel(device,cube,D3D10CalcSubresource(1,f,3),0,0,px)&&isRed(px));   // outside the view's source: kept
                CHECK(readTexel(device,cube,D3D10CalcSubresource(2,f,3),0,0,px,4)&&isRed(px));   // derived from mip 1, not mip 0
            }
            tailView->Release();
        }
    }
    RELEASE(device1);RELEASE(cube);

    // ResolveSubresource from a 4x multisampled target. Wine's GL-backed d3d11
    // may not offer 4x MSAA; then there is nothing to resolve and that is said.
    UINT qualityLevels=0;
    CHECK_HR(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,4,&qualityLevels));
    if(!qualityLevels) {
        out("INFO 4x MSAA unsupported by the host d3d11; ResolveSubresource not exercised\n");
    } else {
        D3D10_TEXTURE2D_DESC ms{};
        ms.Width=64;ms.Height=64;ms.MipLevels=1;ms.ArraySize=1;ms.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        ms.SampleDesc.Count=4;ms.Usage=D3D10_USAGE_DEFAULT;ms.BindFlags=D3D10_BIND_RENDER_TARGET;
        ID3D10Texture2D* msTarget=nullptr;ID3D10RenderTargetView* msView=nullptr;
        CHECK_HR(device->CreateTexture2D(&ms,nullptr,&msTarget));
        if(msTarget)CHECK_HR(device->CreateRenderTargetView(msTarget,nullptr,&msView));
        D3D10_TEXTURE2D_DESC single=ms;single.SampleDesc.Count=1;
        ID3D10Texture2D* resolved=nullptr;
        CHECK_HR(device->CreateTexture2D(&single,nullptr,&resolved));
        if(msView&&resolved) {
            D3D10_TEXTURE2D_DESC got{};msTarget->GetDesc(&got);
            CHECK(got.SampleDesc.Count==4);
            const FLOAT colour[4]={0.75f,0.25f,0.5f,1.0f};
            device->ClearRenderTargetView(msView,colour);
            device->ResolveSubresource(resolved,0,msTarget,0,DXGI_FORMAT_R8G8B8A8_UNORM);
            // The clear colour must cross the resolve untouched.
            Target readback;readback.texture=resolved;
            D3D10_TEXTURE2D_DESC stageDesc=single;stageDesc.BindFlags=0;stageDesc.Usage=D3D10_USAGE_STAGING;
            stageDesc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
            CHECK_HR(device->CreateTexture2D(&stageDesc,nullptr,&readback.staging));
            if(readback.staging&&readback.pixel(device,32,32,px)) {
                CHECK(px[0]>0xb0&&px[0]<=0xc0&&px[1]>0x30&&px[1]<0x50&&px[2]>0x70&&px[2]<0x90&&px[3]==0xff);
            }
            // And a draw into the multisampled target resolves to the drawn pixels.
            const FLOAT black[4]={0,0,0,1};
            device->ClearRenderTargetView(msView,black);
            device->OMSetRenderTargets(1,&msView,nullptr);
            device->Draw(3,0);
            device->ResolveSubresource(resolved,0,msTarget,0,DXGI_FORMAT_R8G8B8A8_UNORM);
            CHECK(readback.staging&&readback.pixel(device,32,32,px)&&isRed(px));
            CHECK(readback.staging&&readback.pixel(device,4,4,px)&&isClear(px));
            RELEASE(readback.staging);
            device->OMSetRenderTargets(1,&p.target.view,nullptr);
        }
        RELEASE(resolved);RELEASE(msView);RELEASE(msTarget);
    }
    p.release(device);
}

// ------------------------------------------- queries, timestamps and predication
static bool pollQuery(ID3D10Asynchronous* query,void* data,UINT size) {
    for(unsigned i=0;i<3000;++i) {
        HRESULT hr=query->GetData(data,size,0);
        if(hr==S_OK)return true;
        if(FAILED(hr)){outHex("  GetData hr=",(unsigned long)hr);return false;}
        Sleep(1);
    }
    out("  GetData never answered\n");
    return false;
}
static void testQueries(ID3D10Device* device) {
    Pipeline p;
    if(!p.make(device)){p.release(device);return;}
    p.bind(device);
    unsigned char px[4];

    // Every query type is created at the type it names and sizes its data the
    // D3D10 way (D3D11's pipeline statistics are larger; the shim narrows them).
    static const struct { D3D10_QUERY type;UINT size; } types[]={
        {D3D10_QUERY_EVENT,sizeof(BOOL)},{D3D10_QUERY_OCCLUSION,sizeof(UINT64)},{D3D10_QUERY_TIMESTAMP,sizeof(UINT64)},
        {D3D10_QUERY_TIMESTAMP_DISJOINT,sizeof(D3D10_QUERY_DATA_TIMESTAMP_DISJOINT)},
        {D3D10_QUERY_PIPELINE_STATISTICS,sizeof(D3D10_QUERY_DATA_PIPELINE_STATISTICS)},
        {D3D10_QUERY_SO_STATISTICS,sizeof(D3D10_QUERY_DATA_SO_STATISTICS)}};
    for(const auto& t:types) {
        D3D10_QUERY_DESC desc{t.type,D3D10_QUERY_MISC_FLAG(0)};
        ID3D10Query* query=nullptr;
        HRESULT hr=device->CreateQuery(&desc,&query);
        if(FAILED(hr)) {
            // Wine's wined3d only offers what the GL driver exposes.
            outHex("INFO CreateQuery refused by the host d3d11, type=",(unsigned long)t.type);
            continue;
        }
        D3D10_QUERY_DESC got{};query->GetDesc(&got);
        CHECK(got.Query==t.type);
        CHECK(query->GetDataSize()==t.size);
        query->Release();
    }
    D3D10_QUERY_DESC invalid{D3D10_QUERY(99),D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query* none=(ID3D10Query*)(uintptr_t)1;
    CHECK(FAILED(device->CreateQuery(&invalid,&none)));
    CHECK(none==nullptr);
    ID3D10Predicate* nonePredicate=(ID3D10Predicate*)(uintptr_t)1;
    D3D10_QUERY_DESC notPredicate{D3D10_QUERY_TIMESTAMP,D3D10_QUERY_MISC_FLAG(0)};
    CHECK(FAILED(device->CreatePredicate(&notPredicate,&nonePredicate)));
    CHECK(nonePredicate==nullptr);

    // Event and occlusion queries around a draw.
    D3D10_QUERY_DESC eventDesc{D3D10_QUERY_EVENT,D3D10_QUERY_MISC_FLAG(0)};
    D3D10_QUERY_DESC occlusionDesc{D3D10_QUERY_OCCLUSION,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query *eventQuery=nullptr,*occlusion=nullptr,*empty=nullptr;
    CHECK_HR(device->CreateQuery(&eventDesc,&eventQuery));
    CHECK_HR(device->CreateQuery(&occlusionDesc,&occlusion));
    CHECK_HR(device->CreateQuery(&occlusionDesc,&empty));
    if(eventQuery&&occlusion&&empty) {
        empty->Begin();empty->End();
        occlusion->Begin();p.target.clear(device);device->Draw(3,0);occlusion->End();
        eventQuery->End();
        BOOL done=FALSE;UINT64 samples=0,none64=99;
        CHECK(pollQuery(eventQuery,&done,sizeof done)&&done);
        CHECK(pollQuery(occlusion,&samples,sizeof samples)&&samples>0);
        CHECK(pollQuery(empty,&none64,sizeof none64)&&none64==0);
    }
    RELEASE(empty);RELEASE(occlusion);RELEASE(eventQuery);

    // Timestamps: a disjoint bracket reports the frequency, and two timestamps
    // taken inside it do not run backwards.
    D3D10_QUERY_DESC disjointDesc{D3D10_QUERY_TIMESTAMP_DISJOINT,D3D10_QUERY_MISC_FLAG(0)};
    D3D10_QUERY_DESC stampDesc{D3D10_QUERY_TIMESTAMP,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query *disjoint=nullptr,*first=nullptr,*second=nullptr;
    HRESULT disjointHr=device->CreateQuery(&disjointDesc,&disjoint);
    HRESULT stampHr=device->CreateQuery(&stampDesc,&first);
    if(SUCCEEDED(stampHr))stampHr=device->CreateQuery(&stampDesc,&second);
    if(FAILED(disjointHr)||FAILED(stampHr)) {
        outHex("INFO timestamp queries refused by the host d3d11 hr=",(unsigned long)(FAILED(disjointHr)?disjointHr:stampHr));
    } else {
        disjoint->Begin();first->End();p.target.clear(device);device->Draw(3,0);second->End();disjoint->End();
        D3D10_QUERY_DATA_TIMESTAMP_DISJOINT bracket{};UINT64 t1=0,t2=0;
        CHECK(pollQuery(disjoint,&bracket,sizeof bracket)&&bracket.Frequency>0);
        CHECK(pollQuery(first,&t1,sizeof t1)&&pollQuery(second,&t2,sizeof t2));
        if(!bracket.Disjoint)CHECK(t2>=t1);
    }
    RELEASE(second);RELEASE(first);RELEASE(disjoint);

    // Pipeline statistics: the D3D10 structure is a prefix of D3D11's, and the
    // call that asks for it by its D3D10 size must be answered.
    D3D10_QUERY_DESC statsDesc{D3D10_QUERY_PIPELINE_STATISTICS,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query* stats=nullptr;
    HRESULT statsHr=device->CreateQuery(&statsDesc,&stats);
    if(FAILED(statsHr)) {
        outHex("INFO pipeline statistics refused by the host d3d11 hr=",(unsigned long)statsHr);
    } else {
        stats->Begin();p.target.clear(device);device->Draw(3,0);stats->End();
        D3D10_QUERY_DATA_PIPELINE_STATISTICS data{};
        CHECK(pollQuery(stats,&data,sizeof data));
        CHECK(data.IAVertices==3&&data.IAPrimitives==1);
        CHECK(stats->GetData(&data,sizeof data+8,0)==E_INVALIDARG);   // not the D3D10 size
    }
    RELEASE(stats);

    // Predication: the predicate is the object the guest created, GetPredication
    // returns it with its value, and a draw is skipped when the predicate's
    // result equals the value (the occlusion predicate is TRUE when samples passed).
    D3D10_QUERY_DESC predicateDesc{D3D10_QUERY_OCCLUSION_PREDICATE,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Predicate* predicate=nullptr;
    HRESULT predicateHr=device->CreatePredicate(&predicateDesc,&predicate);
    if(FAILED(predicateHr)) {
        outHex("INFO predicates refused by the host d3d11 hr=",(unsigned long)predicateHr);
    } else {
        ID3D10Query* asQuery=nullptr;ID3D10Asynchronous* asAsync=nullptr;ID3D10DeviceChild* asChild=nullptr;
        CHECK_HR(predicate->QueryInterface(__uuidof(ID3D10Query),(void**)&asQuery));
        CHECK_HR(predicate->QueryInterface(__uuidof(ID3D10Asynchronous),(void**)&asAsync));
        CHECK_HR(predicate->QueryInterface(__uuidof(ID3D10DeviceChild),(void**)&asChild));
        CHECK(static_cast<IUnknown*>(asQuery)==static_cast<IUnknown*>(predicate));
        RELEASE(asQuery);RELEASE(asAsync);RELEASE(asChild);
        D3D10_QUERY_DESC got{};predicate->GetDesc(&got);
        CHECK(got.Query==D3D10_QUERY_OCCLUSION_PREDICATE);
        // A native D3D11 answers a BOOL (4 bytes); Wine's occlusion-backed
        // predicate answers a UINT64. The shim forwards the host's size.
        const UINT dataSize=predicate->GetDataSize();
        CHECK(dataSize==sizeof(BOOL)||dataSize==sizeof(UINT64));
        ID3D10Predicate* current=(ID3D10Predicate*)(uintptr_t)1;BOOL value=TRUE;
        device->GetPredication(&current,&value);
        CHECK(current==nullptr);   // nothing set yet
        predicate->Begin();p.target.clear(device);device->Draw(3,0);predicate->End();
        UINT64 result=0;
        CHECK(pollQuery(predicate,&result,dataSize)&&(DWORD)result!=0);   // samples passed
        device->SetPredication(predicate,TRUE);
        current=nullptr;value=FALSE;
        device->GetPredication(&current,&value);
        CHECK(current==predicate&&value==TRUE);RELEASE(current);
        // The native context holds the very predicate behind the wrapper, with the value.
        ID3D11Device* native=nullptr;ID3D11DeviceContext* nativeContext=nullptr;ID3D11Predicate* nativePredicate=nullptr;
        CHECK_HR(device->QueryInterface(__uuidof(ID3D11Device),(void**)&native));
        if(native)native->GetImmediateContext(&nativeContext);
        CHECK_HR(predicate->QueryInterface(__uuidof(ID3D11Predicate),(void**)&nativePredicate));
        if(nativeContext&&nativePredicate) {
            ID3D11Predicate* set=nullptr;BOOL setValue=FALSE;
            nativeContext->GetPredication(&set,&setValue);
            CHECK(set==nativePredicate&&setValue==TRUE);RELEASE(set);
        }
        // A draw is skipped when the predicate's result equals the value. Wine's
        // d3d11 does not do conditional rendering on every driver; when it draws
        // either way, that is a host limit and the polarity is not asserted.
        p.target.clear(device);device->Draw(3,0);
        const bool drawnWhenTrue=p.target.pixel(device,32,32,px)&&isRed(px);
        device->SetPredication(predicate,FALSE);
        p.target.clear(device);device->Draw(3,0);
        const bool drawnWhenFalse=p.target.pixel(device,32,32,px)&&isRed(px);
        if(drawnWhenTrue==drawnWhenFalse)out("INFO host d3d11 does not gate draws on predicates; polarity not asserted\n");
        else CHECK(!drawnWhenTrue&&drawnWhenFalse);   // result TRUE: skipped for TRUE, drawn for FALSE
        device->SetPredication(nullptr,FALSE);
        current=(ID3D10Predicate*)(uintptr_t)1;
        device->GetPredication(&current,&value);
        CHECK(current==nullptr);
        if(nativeContext) {
            ID3D11Predicate* set=(ID3D11Predicate*)(uintptr_t)1;BOOL setValue=TRUE;
            nativeContext->GetPredication(&set,&setValue);
            CHECK(set==nullptr);
        }
        p.target.clear(device);device->Draw(3,0);
        CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
        RELEASE(nativePredicate);RELEASE(nativeContext);RELEASE(native);
        predicate->Release();
    }
    p.release(device);
}

// ------------------------------------------- shader tools: compile, reflect, disassemble
static void testShaderTools(ID3D10Device* device) {
    // A failed compile answers the compiler's messages, owned by the guest, and
    // a NULL shader.
    static const char broken[]="float4 main( : SV_POSITION { return 0 }";
    ID3D10Blob* shader=(ID3D10Blob*)(uintptr_t)1;ID3D10Blob* errors=nullptr;
    HRESULT hr=ex.compileShader(broken,sizeof broken-1,"broken.hlsl",nullptr,nullptr,"main","vs_4_0",0,&shader,&errors);
    if(hr==E_FAIL&&!errors) {
        out("INFO D3D10CompileShader unavailable (no d3dcompiler_47); shader tools not exercised\n");
        return;
    }
    CHECK(FAILED(hr));
    CHECK(shader==nullptr);
    CHECK(errors!=nullptr);
    if(errors) {
        CHECK(errors->GetBufferSize()>0&&errors->GetBufferPointer()!=nullptr);
        CHECK(refs(errors)==1);   // the guest's reference is the only one left
        errors->Release();
    }
    // With no errors block requested the messages are released exactly once.
    shader=(ID3D10Blob*)(uintptr_t)1;
    CHECK(FAILED(ex.compileShader(broken,sizeof broken-1,"broken.hlsl",nullptr,nullptr,"main","vs_4_0",0,&shader,nullptr)));
    CHECK(shader==nullptr);

    // A PS with a texture, a sampler and a constant buffer: the reflection
    // descriptors carry the bindings, and the compiled bytes disassemble.
    static const char source[]="Texture2D tex : register(t3); SamplerState samp : register(s2);\n"
        "cbuffer C : register(b1) { float4 tint; };\n"
        "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return tex.Sample(samp,uv)*tint; }\n";
    ID3D10Blob* code=nullptr;errors=nullptr;
    CHECK_HR(ex.compileShader(source,sizeof source-1,"ps.hlsl",nullptr,nullptr,"main","ps_4_0",0,&code,&errors));
    RELEASE(errors);
    if(code) {
        ID3D10ShaderReflection* reflection=nullptr;
        CHECK_HR(ex.reflectShader(code->GetBufferPointer(),code->GetBufferSize(),&reflection));
        if(reflection) {
            D3D10_SHADER_DESC shaderDesc{};
            CHECK_HR(reflection->GetDesc(&shaderDesc));
            CHECK(shaderDesc.ConstantBuffers==1&&shaderDesc.BoundResources==3&&shaderDesc.InputParameters==2);
            bool sawTexture=false,sawSampler=false,sawBuffer=false;
            for(UINT i=0;i<shaderDesc.BoundResources;++i) {
                D3D10_SHADER_INPUT_BIND_DESC bind{};
                CHECK_HR(reflection->GetResourceBindingDesc(i,&bind));
                if(!bind.Name)continue;
                if(!strcmp(bind.Name,"tex")) {
                    sawTexture=true;
                    CHECK(bind.Type==D3D10_SIT_TEXTURE&&bind.BindPoint==3&&bind.BindCount==1);
                    CHECK(bind.Dimension==D3D10_SRV_DIMENSION_TEXTURE2D);
                } else if(!strcmp(bind.Name,"samp")) {
                    sawSampler=true;CHECK(bind.Type==D3D10_SIT_SAMPLER&&bind.BindPoint==2);
                } else if(!strcmp(bind.Name,"C")) {
                    sawBuffer=true;CHECK(bind.Type==D3D10_SIT_CBUFFER&&bind.BindPoint==1);
                }
            }
            CHECK(sawTexture&&sawSampler&&sawBuffer);
            ID3D10ShaderReflectionConstantBuffer* byName=reflection->GetConstantBufferByName("C");
            CHECK(byName!=nullptr);
            if(byName) {
                D3D10_SHADER_BUFFER_DESC bufferDesc{};
                CHECK_HR(byName->GetDesc(&bufferDesc));
                CHECK(bufferDesc.Variables==1&&bufferDesc.Size==16&&bufferDesc.Name&&!strcmp(bufferDesc.Name,"C"));
                ID3D10ShaderReflectionVariable* tint=byName->GetVariableByName("tint");
                CHECK(tint!=nullptr&&tint==byName->GetVariableByIndex(0));   // one wrapper per object
                if(tint) {
                    D3D10_SHADER_VARIABLE_DESC variable{};
                    CHECK_HR(tint->GetDesc(&variable));
                    CHECK(variable.Name&&!strcmp(variable.Name,"tint")&&variable.StartOffset==0&&variable.Size==16);
                }
            }
            D3D10_SIGNATURE_PARAMETER_DESC input{};
            CHECK_HR(reflection->GetInputParameterDesc(1,&input));
            CHECK(input.SemanticName&&!strcmp(input.SemanticName,"TEXCOORD")&&input.SemanticIndex==0);
            CHECK(FAILED(reflection->GetInputParameterDesc(9,&input)));
            reflection->Release();
        }
        ID3D10Blob* listing=(ID3D10Blob*)(uintptr_t)1;
        HRESULT disassembled=ex.disassembleShader(code->GetBufferPointer(),code->GetBufferSize(),FALSE,"shim test",&listing);
        if(FAILED(disassembled)) {
            // Wine's d3dcompiler_47 only disassembles what vkd3d's text target can print.
            outHex("INFO D3D10DisassembleShader refused by the host compiler hr=",(unsigned long)disassembled);
            CHECK(listing==nullptr);
        } else {
            CHECK(listing!=nullptr);
            if(listing) {
                const char* text=(const char*)listing->GetBufferPointer();
                bool sawProfile=false;
                for(SIZE_T i=0;text&&i+4<=listing->GetBufferSize();++i)
                    if(text[i]=='p'&&text[i+1]=='s'&&text[i+2]=='_'&&text[i+3]=='4')sawProfile=true;
                CHECK(sawProfile);
                listing->Release();
            }
        }
        ID3D10Blob* nothing=(ID3D10Blob*)(uintptr_t)1;
        CHECK_HR_IS(ex.disassembleShader(code->GetBufferPointer(),code->GetBufferSize(),FALSE,nullptr,nullptr),E_POINTER);
        CHECK(FAILED(ex.disassembleShader(garbageBytes,sizeof garbageBytes,FALSE,nullptr,&nothing)));
        CHECK(nothing==nullptr);
        code->Release();
    }

    // The GS reflection carries the geometry-stage fields past the common prefix.
    ID3D10ShaderReflection* gsReflection=nullptr;
    CHECK_HR(ex.reflectShader(d3d10_gs,sizeof d3d10_gs,&gsReflection));
    if(gsReflection) {
        D3D10_SHADER_DESC gsDesc{};
        CHECK_HR(gsReflection->GetDesc(&gsDesc));
        // GSOutputTopology is not asserted: Wine's reflection leaves it 0.
        CHECK(gsDesc.GSMaxOutputVertexCount==3);
        gsReflection->Release();
    }

    // A compiled VS and PS draw: the shim's by-name compiler produces bytecode
    // the shim's own device accepts and runs.
    Pipeline p;
    if(p.make(device)) {
        static const char vsSource[]="struct Vin { float4 pos:POSITION; float4 col:COLOR; };\n"
            "struct Vout { float4 pos:SV_Position; float4 col:COLOR; };\n"
            "Vout main(Vin i) { Vout o; o.pos=i.pos; o.col=i.col; return o; }\n";
        static const char psSource[]="struct P { float4 pos:SV_Position; float4 col:COLOR; };\n"
            "float4 main(P i):SV_Target { return i.col; }\n";
        ID3D10Blob *vsBlob=nullptr,*psBlob=nullptr;
        CHECK_HR(ex.compileShader(vsSource,sizeof vsSource-1,nullptr,nullptr,nullptr,"main","vs_4_0",0,&vsBlob,nullptr));
        CHECK_HR(ex.compileShader(psSource,sizeof psSource-1,nullptr,nullptr,nullptr,"main","ps_4_0",0,&psBlob,nullptr));
        if(vsBlob&&psBlob) {
            const unsigned char* bits=(const unsigned char*)vsBlob->GetBufferPointer();
            CHECK(vsBlob->GetBufferSize()>4&&bits[0]=='D'&&bits[1]=='X'&&bits[2]=='B'&&bits[3]=='C');
            ID3D10VertexShader* vs=nullptr;ID3D10PixelShader* ps=nullptr;ID3D10InputLayout* layout=nullptr;
            CHECK_HR(device->CreateVertexShader(bits,vsBlob->GetBufferSize(),&vs));
            CHECK_HR(device->CreatePixelShader(psBlob->GetBufferPointer(),psBlob->GetBufferSize(),&ps));
            // The layout comes from the compiled VS's own input signature.
            CHECK_HR(device->CreateInputLayout(layoutElements,2,bits,vsBlob->GetBufferSize(),&layout));
            p.bind(device);
            if(vs&&ps&&layout) {
                device->IASetInputLayout(layout);device->VSSetShader(vs);device->PSSetShader(ps);
                unsigned char px[4];
                p.target.clear(device);
                device->Draw(3,0);
                CHECK(p.target.pixel(device,32,32,px)&&isRed(px));
                CHECK(p.target.pixel(device,4,4,px)&&isClear(px));
            }
            RELEASE(layout);RELEASE(ps);RELEASE(vs);
        }
        RELEASE(psBlob);RELEASE(vsBlob);
    }
    p.release(device);
}

// -------------------------------------- objects the shim did not create
// A buffer made through the forwarded ID3D11Device and bound with the native
// context comes back through the D3D10 getters as a wrapper that still knows
// its device and context.
static void testAdoptedObjects(ID3D10Device* device) {
    ID3D11Device* native=nullptr;ID3D11DeviceContext* context=nullptr;
    CHECK_HR(device->QueryInterface(__uuidof(ID3D11Device),(void**)&native));
    if(!native)return;
    native->GetImmediateContext(&context);
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* buffer=nullptr;
    CHECK_HR(native->CreateBuffer(&desc,nullptr,&buffer));
    if(context&&buffer) {
        const UINT stride=16,offset=4;
        context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        ID3D10Buffer* adopted=nullptr;UINT gotStride=0,gotOffset=0;
        device->IAGetVertexBuffers(0,1,&adopted,&gotStride,&gotOffset);
        CHECK(adopted!=nullptr&&gotStride==16&&gotOffset==4);
        if(adopted) {
            void* mapped=nullptr;
            CHECK_HR(adopted->Map(D3D10_MAP_WRITE_DISCARD,0,&mapped));   // needs the wrapper's context
            CHECK(mapped!=nullptr);
            if(mapped)adopted->Unmap();
            ID3D10Device* owner=nullptr;
            adopted->GetDevice(&owner);
            CHECK(owner==device);   // not NULL
            RELEASE(owner);
            D3D10_BUFFER_DESC got{};adopted->GetDesc(&got);
            CHECK(got.ByteWidth==64&&got.Usage==D3D10_USAGE_DYNAMIC&&got.BindFlags==D3D10_BIND_VERTEX_BUFFER);
            // A second readback is the same wrapper, not a new one.
            ID3D10Buffer* again=nullptr;UINT againStride=0,againOffset=0;
            device->IAGetVertexBuffers(0,1,&again,&againStride,&againOffset);
            CHECK(again==adopted);RELEASE(again);
            adopted->Release();
        }
        ID3D11Buffer* none[1]={nullptr};const UINT zero=0;
        context->IASetVertexBuffers(0,1,none,&zero,&zero);
    }
    RELEASE(buffer);RELEASE(context);native->Release();
}

// ------------------------------------------------------ state block capture
// Capture reads the viewports and the scissor rects separately: one viewport
// bound must not shrink the scissor query to one rect.
static void testStateBlockScissors(ID3D10Device* device,CreateStateBlock createStateBlock,EnableAll enableAll) {
    D3D10_VIEWPORT viewport{0,0,64,64,0.0f,1.0f};
    device->RSSetViewports(1,&viewport);
    D3D10_RECT rects[2]={{1,2,30,31},{5,6,40,41}};
    device->RSSetScissorRects(2,rects);
    D3D10_STATE_BLOCK_MASK mask{};
    CHECK_HR(enableAll(&mask));
    ID3D10StateBlock* block=nullptr;
    CHECK_HR(createStateBlock(device,&mask,&block));
    if(block) {
        CHECK_HR(block->Capture());
        device->ClearState();
        CHECK_HR(block->Apply());
        D3D10_RECT got[2]{};UINT count=2;
        device->RSGetScissorRects(&count,got);
        CHECK(count>=2);
        CHECK(got[0].left==1&&got[0].top==2&&got[0].right==30&&got[0].bottom==31);
        CHECK(got[1].left==5&&got[1].top==6&&got[1].right==40&&got[1].bottom==41);
        block->Release();
    }
    device->ClearState();
}

// ------------------------------- state block release, device and counts
// ReleaseAllDeviceObjects drops the captured pipeline objects but keeps the
// device; Capture/Apply restore exactly the viewport and scissor counts bound.
static void testStateBlockRelease(ID3D10Device* device,CreateStateBlock createStateBlock,EnableAll enableAll) {
    D3D10_TEXTURE2D_DESC td{};td.Width=16;td.Height=16;td.MipLevels=1;td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D10_USAGE_DEFAULT;
    td.BindFlags=D3D10_BIND_RENDER_TARGET;
    ID3D10Texture2D* texture=nullptr;ID3D10RenderTargetView* rtv=nullptr;
    CHECK_HR(device->CreateTexture2D(&td,nullptr,&texture));
    if(texture)CHECK_HR(device->CreateRenderTargetView(texture,nullptr,&rtv));
    D3D10_BUFFER_DESC bd{};bd.ByteWidth=64;bd.Usage=D3D10_USAGE_DEFAULT;bd.BindFlags=D3D10_BIND_VERTEX_BUFFER;
    ID3D10Buffer* vb=nullptr;
    CHECK_HR(device->CreateBuffer(&bd,nullptr,&vb));
    D3D10_BUFFER_DESC cbd{};cbd.ByteWidth=64;cbd.Usage=D3D10_USAGE_DEFAULT;cbd.BindFlags=D3D10_BIND_CONSTANT_BUFFER;
    ID3D10Buffer* cb=nullptr;
    CHECK_HR(device->CreateBuffer(&cbd,nullptr,&cb));
    D3D10_SAMPLER_DESC sd{};sd.Filter=D3D10_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=sd.AddressV=sd.AddressW=D3D10_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc=D3D10_COMPARISON_NEVER;sd.MaxLOD=D3D10_FLOAT32_MAX;
    ID3D10SamplerState* sampler=nullptr;
    CHECK_HR(device->CreateSamplerState(&sd,&sampler));
    D3D10_RASTERIZER_DESC rd{};rd.FillMode=D3D10_FILL_SOLID;rd.CullMode=D3D10_CULL_NONE;rd.DepthClipEnable=TRUE;
    ID3D10RasterizerState* raster=nullptr;
    CHECK_HR(device->CreateRasterizerState(&rd,&raster));
    if(!rtv||!vb||!cb||!sampler||!raster) {
        RELEASE(raster);RELEASE(sampler);RELEASE(cb);RELEASE(vb);RELEASE(rtv);RELEASE(texture);
        return;
    }
    const UINT stride=16,offset=0;
    device->OMSetRenderTargets(1,&rtv,nullptr);
    device->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    device->PSSetConstantBuffers(0,1,&cb);
    device->PSSetSamplers(0,1,&sampler);
    device->RSSetState(raster);
    D3D10_VIEWPORT viewports[2]={{0,0,16,16,0.0f,1.0f},{1,2,8,8,0.0f,1.0f}};
    D3D10_RECT rects[2]={{1,2,3,4},{5,6,7,8}};
    device->RSSetViewports(2,viewports);
    device->RSSetScissorRects(2,rects);
    const ULONG rtvBase=refs(rtv),vbBase=refs(vb),cbBase=refs(cb),samplerBase=refs(sampler),rasterBase=refs(raster);

    D3D10_STATE_BLOCK_MASK mask{};
    CHECK_HR(enableAll(&mask));
    const ULONG deviceBase=refs(device);
    ID3D10StateBlock* block=nullptr;
    CHECK_HR(createStateBlock(device,&mask,&block));
    if(block) {
        CHECK(refs(device)==deviceBase+1);
        ID3D10Device* got=nullptr;
        CHECK_HR(block->GetDevice(&got));
        CHECK(got==device&&refs(device)==deviceBase+2);
        RELEASE(got);
        CHECK_HR_IS(block->GetDevice(nullptr),E_INVALIDARG);

        // Apply before Capture starts with 0 viewports and 0 scissor rects.
        CHECK_HR(block->Apply());
        UINT vpCount=99,rectCount=99;
        device->RSGetViewports(&vpCount,nullptr);
        device->RSGetScissorRects(&rectCount,nullptr);
        CHECK(vpCount==0&&rectCount==0);
        // Rebind pipeline state and test 2, 1, 0, and 16 viewport/scissor counts.
        device->OMSetRenderTargets(1,&rtv,nullptr);
        device->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        device->PSSetConstantBuffers(0,1,&cb);
        device->PSSetSamplers(0,1,&sampler);
        device->RSSetState(raster);
        device->RSSetViewports(2,viewports);
        device->RSSetScissorRects(2,rects);

        CHECK_HR(block->Capture());
        CHECK(refs(rtv)>rtvBase&&refs(vb)>vbBase&&refs(cb)>cbBase&&refs(sampler)>samplerBase&&refs(raster)>rasterBase);
        // Two viewports and two rects are captured, not all sixteen.
        device->ClearState();
        CHECK_HR(block->Apply());
        vpCount=rectCount=0;
        device->RSGetViewports(&vpCount,nullptr);
        device->RSGetScissorRects(&rectCount,nullptr);
        CHECK(vpCount==2&&rectCount==2);
        D3D10_VIEWPORT gotViewports[16]{};D3D10_RECT gotRects[16]{};
        vpCount=rectCount=2;
        device->RSGetViewports(&vpCount,gotViewports);
        device->RSGetScissorRects(&rectCount,gotRects);
        CHECK(gotViewports[1].TopLeftX==1&&gotViewports[1].TopLeftY==2&&gotViewports[1].Width==8);
        CHECK(gotRects[1].left==5&&gotRects[1].bottom==8);
        // One viewport and one rect round-trip as one each.
        device->RSSetViewports(1,viewports);
        device->RSSetScissorRects(1,rects);
        CHECK_HR(block->Capture());
        device->ClearState();
        CHECK_HR(block->Apply());
        vpCount=rectCount=0;
        device->RSGetViewports(&vpCount,nullptr);
        device->RSGetScissorRects(&rectCount,nullptr);
        CHECK(vpCount==1&&rectCount==1);
        // Zero viewports and zero rects round-trip as zero each.
        device->RSSetViewports(0,nullptr);
        device->RSSetScissorRects(0,nullptr);
        CHECK_HR(block->Capture());
        device->RSSetViewports(2,viewports);
        device->RSSetScissorRects(2,rects);
        CHECK_HR(block->Apply());
        vpCount=rectCount=99;
        device->RSGetViewports(&vpCount,nullptr);
        device->RSGetScissorRects(&rectCount,nullptr);
        CHECK(vpCount==0&&rectCount==0);
        // All sixteen viewports and rects round-trip cleanly.
        D3D10_VIEWPORT allVps[16]{};D3D10_RECT allRects[16]{};
        for(int i=0;i<16;++i) {
            allVps[i]={i,i*2,4,4,0.0f,1.0f};
            allRects[i]={i,i+1,i+10,i+20};
        }
        device->RSSetViewports(16,allVps);
        device->RSSetScissorRects(16,allRects);
        CHECK_HR(block->Capture());
        device->ClearState();
        CHECK_HR(block->Apply());
        vpCount=rectCount=0;
        device->RSGetViewports(&vpCount,nullptr);
        device->RSGetScissorRects(&rectCount,nullptr);
        CHECK(vpCount==16&&rectCount==16);
        device->RSGetViewports(&vpCount,gotViewports);
        device->RSGetScissorRects(&rectCount,gotRects);
        CHECK(gotViewports[15].TopLeftX==15&&gotViewports[15].TopLeftY==30);
        CHECK(gotRects[15].left==15&&gotRects[15].bottom==35);

        // Releasing drops the pipeline references now, keeps the device, and
        // is idempotent. The latest capture still holds references, so drop
        // them before measuring the guest-and-pipeline baseline.
        CHECK_HR(block->ReleaseAllDeviceObjects());
        device->ClearState();
        device->RSSetViewports(2,viewports);
        device->RSSetScissorRects(2,rects);
        device->OMSetRenderTargets(1,&rtv,nullptr);
        device->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        device->PSSetConstantBuffers(0,1,&cb);
        device->PSSetSamplers(0,1,&sampler);
        device->RSSetState(raster);
        const ULONG rtvBound=refs(rtv),vbBound=refs(vb),cbBound=refs(cb),samplerBound=refs(sampler),rasterBound=refs(raster);
        CHECK_HR(block->Capture());
        CHECK(refs(rtv)>rtvBound&&refs(vb)>vbBound&&refs(cb)>cbBound);
        CHECK_HR(block->ReleaseAllDeviceObjects());
        CHECK(refs(rtv)==rtvBound&&refs(vb)==vbBound&&refs(cb)==cbBound&&refs(sampler)==samplerBound&&refs(raster)==rasterBound);
        CHECK_HR(block->ReleaseAllDeviceObjects());
        CHECK(refs(rtv)==rtvBound&&refs(vb)==vbBound&&refs(cb)==cbBound);
        CHECK(refs(device)==deviceBase+1);
        CHECK_HR(block->GetDevice(&got));
        CHECK(got==device);
        RELEASE(got);
        // Apply after the release binds nulls for the released slots; Capture
        // after it works normally.
        CHECK_HR(block->Apply());
        ID3D10RenderTargetView* boundRtv=nullptr;
        device->OMGetRenderTargets(1,&boundRtv,nullptr);
        CHECK(boundRtv==nullptr);
        RELEASE(boundRtv);
        device->OMSetRenderTargets(1,&rtv,nullptr);
        CHECK_HR(block->Capture());
        CHECK(refs(rtv)>rtvBound);
        device->ClearState();
        CHECK_HR(block->Apply());
        device->OMGetRenderTargets(1,&boundRtv,nullptr);
        CHECK(boundRtv==rtv);
        RELEASE(boundRtv);

        block->Release();
        CHECK(refs(device)==deviceBase);
        CHECK(refs(rtv)==rtvBound);
    }
    device->ClearState();
    RELEASE(raster);RELEASE(sampler);RELEASE(cb);RELEASE(vb);RELEASE(rtv);RELEASE(texture);
}

extern "C" DWORD WINAPI d3d10_test_entry() {
    AddVectoredExceptionHandler(1,captureDebugString);
    HMODULE module=loadShim();
    if(!module) {
        outHex("SKIP-DLL d3d10.dll not loadable error=",GetLastError());
        return 2;
    }
    {wchar_t loaded[1024];DWORD m=GetModuleFileNameW(module,loaded,1024);
     out("INFO loaded ");if(m&&m<1024){char narrow[1024];unsigned k=0;
        for(DWORD i=0;i<m&&k+1<sizeof narrow;++i)narrow[k++]=char(loaded[i]);narrow[k]=0;out(narrow);}
     out("\n");}
    auto createDevice=(CreateDevice)GetProcAddress(module,"D3D10CreateDevice");
    auto createDevice1=(CreateDevice1)GetProcAddress(module,"D3D10CreateDevice1");
    auto createDeviceAndSwapChain=(CreateDeviceAndSwapChain)GetProcAddress(module,"D3D10CreateDeviceAndSwapChain");
    auto createBlob=(CreateBlob)GetProcAddress(module,"D3D10CreateBlob");
    auto compileShader=(CompileShader)GetProcAddress(module,"D3D10CompileShader");
    auto reflectShader=(ReflectShader)GetProcAddress(module,"D3D10ReflectShader");
    auto coreCreateDevice=(CoreCreateDevice)GetProcAddress(module,"D3D10CoreCreateDevice");
    auto enableAll=(EnableAll)GetProcAddress(module,"D3D10StateBlockMaskEnableAll");
    auto createStateBlock=(CreateStateBlock)GetProcAddress(module,"D3D10CreateStateBlock");
    auto getSetting=(GetSetting)GetProcAddress(module,"D3D10StateBlockMaskGetSetting");
    auto enableCapture=(EnableCapture)GetProcAddress(module,"D3D10StateBlockMaskEnableCapture");
    auto vertexProfile=(const char*(WINAPI*)(ID3D10Device*))GetProcAddress(module,"D3D10GetVertexShaderProfile");
    auto registerLayers=(HRESULT(WINAPI*)(void))GetProcAddress(module,"D3D10RegisterLayers");
    CHECK(createDevice&&createDevice1&&createDeviceAndSwapChain&&createBlob&&compileShader&&reflectShader);
    CHECK(coreCreateDevice&&enableAll&&createStateBlock&&getSetting&&enableCapture&&vertexProfile&&registerLayers);
    if(!createDevice||!createDevice1||!createDeviceAndSwapChain||!createBlob||!compileShader||!reflectShader||
       !coreCreateDevice||!enableAll||!createStateBlock||!getSetting||!enableCapture||!vertexProfile||!registerLayers) {
        static const char* const all[]={"D3D10CreateDevice","D3D10CreateDevice1","D3D10CreateDeviceAndSwapChain",
            "D3D10CreateDeviceAndSwapChain1","D3D10CreateBlob","D3D10CompileShader","D3D10ReflectShader",
            "D3D10CoreCreateDevice","D3D10StateBlockMaskEnableAll",
            "D3D10CreateStateBlock","D3D10StateBlockMaskGetSetting","D3D10StateBlockMaskEnableCapture",
            "D3D10GetVertexShaderProfile","D3D10RegisterLayers"};
        for(auto name:all) {
            out("INFO export ");out(name);out("=");
            outHex("",(unsigned long long)(uintptr_t)GetProcAddress(module,name));
        }
    }
    ex.createDevice=createDevice;ex.createDeviceAndSwapChain=createDeviceAndSwapChain;
    ex.compileShader=compileShader;ex.reflectShader=reflectShader;ex.coreCreateDevice=coreCreateDevice;
    ex.createDeviceAndSwapChain1=(CreateDeviceAndSwapChain1)GetProcAddress(module,"D3D10CreateDeviceAndSwapChain1");
    ex.disassembleShader=(DisassembleShader)GetProcAddress(module,"D3D10DisassembleShader");
    ex.coreRegisterLayers=(HRESULT(WINAPI*)(void))GetProcAddress(module,"D3D10CoreRegisterLayers");
    ex.createEffect=(CreateEffect)GetProcAddress(module,"D3D10CreateEffectFromMemory");
    ex.createEffectPool=(CreateEffectPool)GetProcAddress(module,"D3D10CreateEffectPoolFromMemory");
    ex.compileEffect=(CompileEffect)GetProcAddress(module,"D3D10CompileEffectFromMemory");
    CHECK(ex.createDeviceAndSwapChain1&&ex.disassembleShader&&ex.coreRegisterLayers);
    CHECK(ex.createEffect&&ex.createEffectPool&&ex.compileEffect);
    if(failures){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

    // The D3D10 entry points report the SDK version they were built for.
    ID3D10Device* device=nullptr;
    CHECK_HR_IS(createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION+1,&device),E_INVALIDARG);
    CHECK(!device);
    CHECK_HR(createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,&device));
    CHECK(device!=nullptr);
    if(!device){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

    // The console has neither rasterizer module; both legacy software driver
    // requests deliberately take the logged WARP fallback instead.
    ID3D10Device* reference=nullptr;
    CHECK_HR(createDevice(nullptr,D3D10_DRIVER_TYPE_REFERENCE,nullptr,0,D3D10_SDK_VERSION,&reference));
    CHECK(reference!=nullptr);
    if(reference)reference->Release();
    ID3D10Device* software=nullptr;
    CHECK_HR(createDevice(nullptr,D3D10_DRIVER_TYPE_SOFTWARE,nullptr,0,D3D10_SDK_VERSION,&software));
    CHECK(software!=nullptr);
    if(software)software->Release();

    // D3D10CoreCreateDevice is the internal entry point used by d3d10core.dll;
    // it must produce the same wrapped device instead of being a stub.
    ID3D10Device* core=nullptr;
    CHECK_HR(coreCreateDevice(nullptr,nullptr,0,D3D_FEATURE_LEVEL_10_0,&core));
    CHECK(core!=nullptr);
    if(core)core->Release();

    // A D3D10 device runs D3D10 shaders, so the shim asks for 10_0 first.
    ID3D10Device1* one=nullptr;
    CHECK_HR(device->QueryInterface(__uuidof(ID3D10Device1),(void**)&one));
    CHECK(one&&one->GetFeatureLevel()==D3D10_FEATURE_LEVEL_10_0);
    if(one)one->Release();
    CHECK(device->GetCreationFlags()==0);
    // Wine's d3d10 device forwards unknown interfaces to its D3D11 device, and
    // so does the shim: a guest that asks for ID3D11Device gets it.
    IUnknown* forwarded=nullptr;
    CHECK_HR(device->QueryInterface(__uuidof(ID3D11Device),(void**)&forwarded));
    CHECK(forwarded!=nullptr);
    if(forwarded)forwarded->Release();
    CHECK_HR_IS(device->QueryInterface(__uuidof(ID3D10StateBlock),(void**)&forwarded),E_NOINTERFACE);

    // ID3D10Multithread is a separate object, created on first request.
    ID3D10Multithread* multithread=nullptr;
    CHECK_HR(device->QueryInterface(__uuidof(ID3D10Multithread),(void**)&multithread));
    CHECK(multithread&&multithread->GetMultithreadProtected());
    if(multithread) {
        multithread->Enter();multithread->Leave();
        ID3D10Multithread* again=nullptr;
        CHECK_HR(device->QueryInterface(__uuidof(ID3D10Multithread),(void**)&again));
        CHECK(again==multithread);
        if(again)again->Release();
        multithread->Release();
    }

    // A vertex buffer: created, mapped, unmapped, and read back as the same
    // D3D10 object through the context.
    D3D10_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth=64;bufferDesc.Usage=D3D10_USAGE_DYNAMIC;bufferDesc.BindFlags=D3D10_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags=D3D10_CPU_ACCESS_WRITE;
    ID3D10Buffer* buffer=nullptr;
    CHECK_HR(device->CreateBuffer(&bufferDesc,nullptr,&buffer));
    if(buffer) {
        void* mapped=nullptr;
        CHECK_HR(buffer->Map(D3D10_MAP_WRITE_DISCARD,0,&mapped));
        CHECK(mapped!=nullptr);
        buffer->Unmap();
        D3D10_BUFFER_DESC got{};
        buffer->GetDesc(&got);
        CHECK(got.ByteWidth==64&&got.BindFlags==D3D10_BIND_VERTEX_BUFFER);
        ID3D10Resource* resource=nullptr;
        buffer->QueryInterface(__uuidof(ID3D10Resource),(void**)&resource);
        D3D10_RESOURCE_DIMENSION dimension=D3D10_RESOURCE_DIMENSION_UNKNOWN;
        if(resource){resource->GetType(&dimension);resource->Release();}
        CHECK(dimension==D3D10_RESOURCE_DIMENSION_BUFFER);
        ID3D10Device* owner=nullptr;
        buffer->GetDevice(&owner);
        CHECK(owner==device);
        if(owner)owner->Release();
        // D3D11 dereferences the stride array for every bound vertex buffer, so
        // a NULL stride is not a valid call even though D3D10's declaration
        // does not say so.
        const UINT setStride=16,setOffset=0;
        device->IASetVertexBuffers(0,1,&buffer,&setStride,&setOffset);
        ID3D10Buffer* readback=nullptr;
        UINT stride=0,offset=0;
        device->IAGetVertexBuffers(0,1,&readback,&stride,&offset);
        // The readback is the same wrapper the guest already holds.
        CHECK(readback==buffer);
        CHECK(stride==16&&offset==0);
        if(readback)readback->Release();
        // The buffer holds the device alive; Wine never finishes process exit
        // while a D3D object outlives the test, so unbind and release it. D3D11
        // dereferences the stride array for every slot, so unbinding passes a
        // NULL buffer entry rather than a NULL list.
        ID3D10Buffer* none[1]={nullptr};
        const UINT zero=0;
        device->IASetVertexBuffers(0,1,none,&zero,&zero);
        buffer->Release();
    }

    // A render target: the clear path a game's first frame takes.
    D3D10_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width=64;textureDesc.Height=64;textureDesc.MipLevels=1;textureDesc.ArraySize=1;
    textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDesc.SampleDesc.Count=1;
    textureDesc.Usage=D3D10_USAGE_DEFAULT;textureDesc.BindFlags=D3D10_BIND_RENDER_TARGET;
    ID3D10Texture2D* texture=nullptr;
    CHECK_HR(device->CreateTexture2D(&textureDesc,nullptr,&texture));
    ID3D10RenderTargetView* target=nullptr;
    if(texture) {
        CHECK_HR(device->CreateRenderTargetView(texture,nullptr,&target));
        D3D10_TEXTURE2D_DESC got{};
        texture->GetDesc(&got);
        CHECK(got.Width==64&&got.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    if(target) {
        ID3D10Resource* resource=nullptr;
        target->GetResource(&resource);
        CHECK(resource==static_cast<ID3D10Resource*>(texture));
        if(resource)resource->Release();
        device->OMSetRenderTargets(1,&target,nullptr);
        const FLOAT color[4]={0.25f,0.5f,0.75f,1.0f};
        device->ClearRenderTargetView(target,color);
        device->OMSetRenderTargets(0,nullptr,nullptr);
        target->Release();
    }
    if(texture)texture->Release();

    // A depth buffer and its view, then a viewport and a scissor rect.
    D3D10_TEXTURE2D_DESC depthDesc=textureDesc;
    depthDesc.Format=DXGI_FORMAT_D32_FLOAT;depthDesc.BindFlags=D3D10_BIND_DEPTH_STENCIL;
    ID3D10Texture2D* depth=nullptr;
    CHECK_HR(device->CreateTexture2D(&depthDesc,nullptr,&depth));
    ID3D10DepthStencilView* depthView=nullptr;
    if(depth) {
        CHECK_HR(device->CreateDepthStencilView(depth,nullptr,&depthView));
        if(depthView) {
            device->ClearDepthStencilView(depthView,D3D10_CLEAR_DEPTH,1.0f,0);
            depthView->Release();
        }
        depth->Release();
    }
    D3D10_VIEWPORT viewport{0,0,64,64,0.0f,1.0f};
    device->RSSetViewports(1,&viewport);
    UINT viewports=1;D3D10_VIEWPORT readbackViewport{};
    device->RSGetViewports(&viewports,&readbackViewport);
    CHECK(viewports==1&&readbackViewport.Width==64&&readbackViewport.MaxDepth==1.0f);
    D3D10_RECT scissor{0,0,32,32};
    device->RSSetScissorRects(1,&scissor);

    // State objects: the descriptors the shim stores for GetDesc.
    D3D10_BLEND_DESC blend{};
    blend.BlendEnable[0]=TRUE;blend.SrcBlend=D3D10_BLEND_SRC_ALPHA;blend.DestBlend=D3D10_BLEND_INV_SRC_ALPHA;
    blend.BlendOp=D3D10_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D10_BLEND_ONE;blend.DestBlendAlpha=D3D10_BLEND_ZERO;
    blend.BlendOpAlpha=D3D10_BLEND_OP_ADD;blend.RenderTargetWriteMask[0]=D3D10_COLOR_WRITE_ENABLE_ALL;
    ID3D10BlendState* blendState=nullptr;
    CHECK_HR(device->CreateBlendState(&blend,&blendState));
    if(blendState) {
        D3D10_BLEND_DESC got{};
        blendState->GetDesc(&got);
        CHECK(got.BlendEnable[0]&&got.SrcBlend==D3D10_BLEND_SRC_ALPHA);
        ID3D10BlendState1* widened=nullptr;
        CHECK_HR(blendState->QueryInterface(__uuidof(ID3D10BlendState1),(void**)&widened));
        if(widened) {
            D3D10_BLEND_DESC1 got1{};
            widened->GetDesc1(&got1);
            CHECK(got1.RenderTarget[0].BlendEnable&&got1.RenderTarget[0].SrcBlend==D3D10_BLEND_SRC_ALPHA);
            widened->Release();
        }
        device->OMSetBlendState(blendState,nullptr,0xffffffff);
        blendState->Release();
    }
    D3D10_DEPTH_STENCIL_DESC depthState{};
    depthState.DepthEnable=TRUE;depthState.DepthWriteMask=D3D10_DEPTH_WRITE_MASK_ALL;
    depthState.DepthFunc=D3D10_COMPARISON_LESS;
    ID3D10DepthStencilState* depthStencilState=nullptr;
    CHECK_HR(device->CreateDepthStencilState(&depthState,&depthStencilState));
    if(depthStencilState) {
        D3D10_DEPTH_STENCIL_DESC got{};
        depthStencilState->GetDesc(&got);
        CHECK(got.DepthEnable&&got.DepthFunc==D3D10_COMPARISON_LESS);
        device->OMSetDepthStencilState(depthStencilState,0);
        depthStencilState->Release();
    }
    D3D10_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode=D3D10_FILL_SOLID;rasterizer.CullMode=D3D10_CULL_BACK;
    rasterizer.FrontCounterClockwise=FALSE;rasterizer.DepthClipEnable=TRUE;
    ID3D10RasterizerState* rasterizerState=nullptr;
    CHECK_HR(device->CreateRasterizerState(&rasterizer,&rasterizerState));
    if(rasterizerState) {
        D3D10_RASTERIZER_DESC got{};
        rasterizerState->GetDesc(&got);
        CHECK(got.FillMode==D3D10_FILL_SOLID&&got.CullMode==D3D10_CULL_BACK);
        device->RSSetState(rasterizerState);
        rasterizerState->Release();
    }
    D3D10_SAMPLER_DESC sampler{};
    sampler.Filter=D3D10_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV=D3D10_TEXTURE_ADDRESS_CLAMP;sampler.AddressW=D3D10_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc=D3D10_COMPARISON_NEVER;sampler.MaxLOD=D3D10_FLOAT32_MAX;
    ID3D10SamplerState* samplerState=nullptr;
    CHECK_HR(device->CreateSamplerState(&sampler,&samplerState));
    if(samplerState) {
        D3D10_SAMPLER_DESC got{};
        samplerState->GetDesc(&got);
        CHECK(got.Filter==D3D10_FILTER_MIN_MAG_MIP_LINEAR);
        samplerState->Release();
    }

    // Query enums and Begin/End/GetData translate directly to D3D11.
    D3D10_QUERY_DESC queryDesc{D3D10_QUERY_OCCLUSION,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query* query=nullptr;
    CHECK_HR(device->CreateQuery(&queryDesc,&query));
    if(query) {
        D3D10_QUERY_DESC got{};
        query->GetDesc(&got);
        CHECK(got.Query==D3D10_QUERY_OCCLUSION);
        query->Begin();
        query->End();
        UINT64 samples=0;
        // Not a FLAG_NONE query: a blocking read is legal and must answer.
        CHECK_HR(query->GetData(&samples,sizeof samples,0));
        query->Release();
    }
    D3D10_QUERY_DESC soQueryDesc{D3D10_QUERY_SO_STATISTICS,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query* soQuery=nullptr;
    CHECK_HR(device->CreateQuery(&soQueryDesc,&soQuery));
    if(soQuery) {
        ID3D11Query* nativeQuery=nullptr;
        CHECK_HR(soQuery->QueryInterface(__uuidof(ID3D11Query),(void**)&nativeQuery));
        if(nativeQuery) {
            D3D11_QUERY_DESC nativeDesc{};nativeQuery->GetDesc(&nativeDesc);
            CHECK(nativeDesc.Query==D3D11_QUERY_SO_STATISTICS);
            nativeQuery->Release();
        }
        D3D10_QUERY_DATA_SO_STATISTICS stats{};
        soQuery->Begin();soQuery->End();
        CHECK_HR(soQuery->GetData(&stats,sizeof stats,0));
        soQuery->Release();
    }
    CHECK(device->GetExceptionMode()==0);
    CHECK_HR_IS(device->SetExceptionMode(0),E_NOTIMPL);

    // Format support is the device's answer, not the shim's.
    UINT support=0;
    CHECK_HR(device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,&support));
    CHECK(support&D3D10_FORMAT_SUPPORT_TEXTURE2D);

    // State blocks: Wine's mask arithmetic and per-field capture.
    D3D10_STATE_BLOCK_MASK mask{};
    CHECK_HR(enableAll(&mask));
    CHECK(getSetting(&mask,D3D10_DST_RS_VIEWPORTS,0));
    CHECK_HR(enableCapture(&mask,D3D10_DST_RS_VIEWPORTS,0,1));
    ID3D10StateBlock* block=nullptr;
    CHECK_HR(createStateBlock(device,&mask,&block));
    if(block) {
        CHECK_HR(block->Capture());
        CHECK_HR(block->Apply());
        CHECK_HR_IS(block->GetDevice(nullptr),E_INVALIDARG);
        block->Release();
    }

    // Blobs and the shader-tool entry points the D3D10 API shares with D3D11.
    ID3D10Blob* blob=nullptr;
    CHECK_HR(createBlob(16,&blob));
    if(blob) {
        CHECK(blob->GetBufferSize()==16&&blob->GetBufferPointer()!=nullptr);
        blob->Release();
    }
    CHECK(vertexProfile&&!strcmp(vertexProfile(device),"vs_4_0"));
    CHECK_HR_IS(registerLayers(),E_NOTIMPL);
    const char source[]="cbuffer Globals : register(b0) { float4 tint; } "
        "float4 main(float4 p : POSITION) : SV_POSITION { return p + tint; }";
    ID3D10Blob* code=nullptr;ID3D10Blob* errors=nullptr;
    HRESULT compiled=compileShader(source,sizeof source-1,"shader.hlsl",nullptr,nullptr,"main","vs_4_0",
        D3D10_SHADER_ENABLE_STRICTNESS,&code,&errors);
    if(FAILED(compiled)) {
        // The shader compiler is a separate DLL; report it rather than hide it.
        outHex("INFO D3D10CompileShader hr=",(unsigned long)compiled);
        if(errors)out((const char*)errors->GetBufferPointer());
    } else {
        CHECK(code&&code->GetBufferSize()>0);
        ID3D10ShaderReflection* reflection=nullptr;
        CHECK_HR(reflectShader(code->GetBufferPointer(),code->GetBufferSize(),&reflection));
        CHECK(reflection!=nullptr);
        if(reflection) {
            D3D10_SHADER_DESC shaderDesc{};
            CHECK_HR(reflection->GetDesc(&shaderDesc));
            CHECK(shaderDesc.ConstantBuffers==1&&shaderDesc.InputParameters==1&&shaderDesc.OutputParameters==1);
            D3D10_SIGNATURE_PARAMETER_DESC inputDesc{},outputDesc{};
            CHECK_HR(reflection->GetInputParameterDesc(0,&inputDesc));
            CHECK_HR(reflection->GetOutputParameterDesc(0,&outputDesc));
            CHECK(inputDesc.SemanticName&&outputDesc.SemanticName&&inputDesc.SemanticIndex==0&&outputDesc.SemanticIndex==0);
            ID3D10ShaderReflectionConstantBuffer* constants=reflection->GetConstantBufferByIndex(0);
            CHECK(constants!=nullptr);
            if(constants) {
                D3D10_SHADER_BUFFER_DESC bufferDesc{};
                CHECK_HR(constants->GetDesc(&bufferDesc));
                CHECK(bufferDesc.Variables==1);
                ID3D10ShaderReflectionVariable* variable=constants->GetVariableByIndex(0);
                CHECK(variable!=nullptr);
                if(variable) {
                    D3D10_SHADER_VARIABLE_DESC variableDesc{};
                    CHECK_HR(variable->GetDesc(&variableDesc));
                    ID3D10ShaderReflectionType* type=variable->GetType();
                    CHECK(type!=nullptr);
                    if(type) {
                        D3D10_SHADER_TYPE_DESC typeDesc{};
                        CHECK_HR(type->GetDesc(&typeDesc));
                        CHECK(typeDesc.Rows==1&&typeDesc.Columns==4);
                    }
                }
            }
            reflection->Release();
        }
    }
    if(code)code->Release();
    if(errors)errors->Release();

    // The D3D10.1 entry point creates the same object at the level asked for.
    ID3D10Device1* device1=nullptr;
    CHECK_HR(createDevice1(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,
        D3D10_FEATURE_LEVEL_10_1,D3D10_1_SDK_VERSION,&device1));
    if(device1) {
        CHECK(device1->GetFeatureLevel()==D3D10_FEATURE_LEVEL_10_1);
        device1->Release();
    }

    // A swapchain on a real window: the shim reaches it through the device's
    // own adapter factory, which is where Play redirects a guest window.
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"d3d10-shim-test";
    if(RegisterClassW(&cls)) {
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"d3d10 shim",WS_OVERLAPPEDWINDOW,
            0,0,320,240,nullptr,nullptr,cls.hInstance,nullptr);
        CHECK(window!=nullptr);
        if(window) {
            DXGI_SWAP_CHAIN_DESC chain{};
            chain.BufferDesc.Width=320;chain.BufferDesc.Height=240;
            chain.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            chain.SampleDesc.Count=1;chain.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
            chain.BufferCount=2;chain.OutputWindow=window;chain.Windowed=TRUE;
            chain.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
            ID3D10Device* refusedDevice=nullptr;IDXGISwapChain* refusedSwapchain=nullptr;
            chain.Windowed=FALSE;
            CHECK_HR_IS(createDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,
                &chain,&refusedSwapchain,&refusedDevice),DXGI_ERROR_UNSUPPORTED);
            CHECK(!refusedDevice&&!refusedSwapchain);
            chain.Windowed=TRUE;chain.SampleDesc.Count=4;
            CHECK_HR_IS(createDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,
                &chain,&refusedSwapchain,&refusedDevice),DXGI_ERROR_UNSUPPORTED);
            CHECK(!refusedDevice&&!refusedSwapchain);
            chain.SampleDesc.Count=1;
            ID3D10Device* chained=nullptr;IDXGISwapChain* swapchain=nullptr;
            CHECK_HR(createDeviceAndSwapChain(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,
                &chain,&swapchain,&chained));
            if(swapchain&&chained) {
                ID3D10Texture2D* backBuffer=nullptr;
                CHECK_HR(swapchain->GetBuffer(0,__uuidof(ID3D10Texture2D),(void**)&backBuffer));
                if(backBuffer) {
                    ID3D10RenderTargetView* view=nullptr;
                    CHECK_HR(chained->CreateRenderTargetView(backBuffer,nullptr,&view));
                    if(view) {
                        const FLOAT clear[4]={0.0f,0.0f,0.0f,1.0f};
                        chained->ClearRenderTargetView(view,clear);
                        chained->OMSetRenderTargets(1,&view,nullptr);
                        HRESULT presented=swapchain->Present(0,0);
                        // Wine's windowed flip model presents; a device-lost or
                        // occluded answer is still an answer, a crash is not.
                        outHex("INFO Present hr=",(unsigned long)presented);
                        CHECK(presented==S_OK||presented==DXGI_STATUS_OCCLUDED||presented==DXGI_ERROR_DEVICE_REMOVED);
                        view->Release();
                    }
                    backBuffer->Release();
                }
                swapchain->Release();
            }
            if(chained)chained->Release();
            DestroyWindow(window);
        }
    }
    // Rebinding a prefix of constant-buffer slots keeps the later slots bound
    // (and referenced); only OM/SO target lists replace the whole set.
    {
        D3D10_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=16;cbDesc.Usage=D3D10_USAGE_DEFAULT;cbDesc.BindFlags=D3D10_BIND_CONSTANT_BUFFER;
        ID3D10Buffer *first=nullptr,*second=nullptr,*third=nullptr;
        CHECK_HR(device->CreateBuffer(&cbDesc,nullptr,&first));
        CHECK_HR(device->CreateBuffer(&cbDesc,nullptr,&second));
        CHECK_HR(device->CreateBuffer(&cbDesc,nullptr,&third));
        if(first&&second&&third) {
            ID3D10Buffer* pair[2]={first,second};
            device->VSSetConstantBuffers(0,2,pair);
            ULONG bound=second->AddRef();second->Release();
            device->VSSetConstantBuffers(0,1,&third);
            ULONG after=second->AddRef();second->Release();
            CHECK(after==bound);
            ID3D10Buffer* got=nullptr;
            device->VSGetConstantBuffers(1,1,&got);
            CHECK(got==second);
            if(got)got->Release();
        }
        device->ClearState();
        if(first)first->Release();
        if(second)second->Release();
        if(third)third->Release();
    }
    device->ClearState();
    out("INFO group device-creation\n");testDeviceCreation(device);
    out("INFO group refusals\n");testRefusals(device);
    out("INFO group descriptors\n");testDescriptors(device);
    out("INFO group draw-path\n");testDrawPath(device);
    out("INFO group queries\n");testQueries(device);
    out("INFO group shader-tools\n");testShaderTools(device);
    out("INFO group adopted-objects\n");testAdoptedObjects(device);
    out("INFO group state-block-scissors\n");testStateBlockScissors(device,createStateBlock,enableAll);
    out("INFO group state-block-release\n");testStateBlockRelease(device,createStateBlock,enableAll);
    out("INFO groups done\n");
    device->ClearState();
    device->Flush();
    device->Release();
    FreeLibrary(module);
    outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);
    return failures?1:0;
}
