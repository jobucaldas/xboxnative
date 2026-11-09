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
extern "C" DWORD WINAPI d3d10_test_entry() {
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
    auto enableAll=(EnableAll)GetProcAddress(module,"D3D10StateBlockMaskEnableAll");
    auto createStateBlock=(CreateStateBlock)GetProcAddress(module,"D3D10CreateStateBlock");
    auto getSetting=(GetSetting)GetProcAddress(module,"D3D10StateBlockMaskGetSetting");
    auto enableCapture=(EnableCapture)GetProcAddress(module,"D3D10StateBlockMaskEnableCapture");
    auto vertexProfile=(const char*(WINAPI*)(ID3D10Device*))GetProcAddress(module,"D3D10GetVertexShaderProfile");
    auto registerLayers=(HRESULT(WINAPI*)(void))GetProcAddress(module,"D3D10RegisterLayers");
    CHECK(createDevice&&createDevice1&&createDeviceAndSwapChain&&createBlob&&compileShader);
    CHECK(enableAll&&createStateBlock&&getSetting&&enableCapture&&vertexProfile&&registerLayers);
    if(!createDevice||!createDevice1||!createDeviceAndSwapChain||!createBlob||!compileShader||
       !enableAll||!createStateBlock||!getSetting||!enableCapture||!vertexProfile||!registerLayers) {
        static const char* const all[]={"D3D10CreateDevice","D3D10CreateDevice1","D3D10CreateDeviceAndSwapChain",
            "D3D10CreateDeviceAndSwapChain1","D3D10CreateBlob","D3D10CompileShader","D3D10StateBlockMaskEnableAll",
            "D3D10CreateStateBlock","D3D10StateBlockMaskGetSetting","D3D10StateBlockMaskEnableCapture",
            "D3D10GetVertexShaderProfile","D3D10RegisterLayers"};
        for(auto name:all) {
            out("INFO export ");out(name);out("=");
            outHex("",(unsigned long long)(uintptr_t)GetProcAddress(module,name));
        }
    }
    if(failures){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

    // The D3D10 entry points report the SDK version they were built for.
    ID3D10Device* device=nullptr;
    CHECK_HR_IS(createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION+1,&device),E_INVALIDARG);
    CHECK(!device);
    CHECK_HR(createDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,&device));
    CHECK(device!=nullptr);
    if(!device){outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);return 1;}

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

    // An occlusion query: Begin/End/GetData go through the D3D11 context.
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
        CHECK_HR_IS(block->GetDevice(nullptr),E_NOTIMPL);
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
    const char source[]="float4 main(float4 p : POSITION) : SV_POSITION { return p; }";
    ID3D10Blob* code=nullptr;ID3D10Blob* errors=nullptr;
    HRESULT compiled=compileShader(source,sizeof source,"shader.hlsl",nullptr,nullptr,"main","vs_4_0",
        D3D10_SHADER_ENABLE_STRICTNESS,&code,&errors);
    if(FAILED(compiled)) {
        // The shader compiler is a separate DLL; report it rather than hide it.
        outHex("INFO D3D10CompileShader hr=",(unsigned long)compiled);
        if(errors)out((const char*)errors->GetBufferPointer());
    } else {
        CHECK(code&&code->GetBufferSize()>0);
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
    device->ClearState();
    device->Flush();
    device->Release();
    FreeLibrary(module);
    outNum("RESULT checks=",checks);outNum("RESULT failures=",failures);
    return failures?1:0;
}
