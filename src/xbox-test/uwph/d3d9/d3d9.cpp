// SPDX-License-Identifier: GPL-3.0-or-later
// Play's D3D9 shim: d3d9.dll on the console's D3D11.
//
// Built by build.sh as a CRT-free mingw PE DLL and mapped by Play's loader, so
// its d3d11.dll/dxgi.dll imports bind to Play's routes exactly as the D3D10
// shim's do (see ../d3dshim/base.h and research/D3D9-PLAN.md).
//
// This is milestone 1 of the D3D9 work: the API object, the device, and a swap
// chain on the CoreWindow adapter that clears and presents. It is the front-end
// half of option B in the decision record; the fixed-function and
// shader-translation half is not started, so drawing reports D3DERR_INVALIDCALL
// rather than pretending. Every method of IDirect3DDevice9Ex and IDirect3D9Ex is
// present, because the guest's calls land on fixed vtable slots.
//
// Deliberate gaps, reported through the launch log rather than faked:
//   * Shader creation (CreateVertexShader/CreatePixelShader) and the draw calls
//     return D3DERR_INVALIDCALL: D3D9 bytecode is not translated to DXBC yet.
//   * Fixed-function transforms, lighting and texture stages are stored and
//     read back but not applied (they need the same translation).
//   * ID3DXEffect, ID3DXFont and the D3DX helpers live in d3dx9_*.dll, which
//     Play does not ship; a game that ships its own copy gets its own copy.
//   * Exclusive fullscreen is refused by the CoreWindow adapter, as for D3D10.
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../d3dshim/base.h"

namespace {
// ------------------------------------------------------------------ formats
// D3D9 formats that map one-to-one onto DXGI. D3D9's numeric values are not
// DXGI's, so this is a switch rather than a cast; anything absent from the
// table is a format the console's D3D11 cannot sample or render as.
DXGI_FORMAT dxgiFormat(D3DFORMAT format) {
    switch(format) {
    case D3DFMT_A8R8G8B8:return DXGI_FORMAT_B8G8R8A8_UNORM;
    // D3D9's X formats ignore alpha, so the alpha channel is the right target;
    // the X variants (B8G8R8X8_UNORM) are not valid flip-model swap-chain
    // formats, and the CoreWindow adapter only creates flip-model chains.
    case D3DFMT_X8R8G8B8:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_A8B8G8R8:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_X8B8G8R8:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_R5G6B5:return DXGI_FORMAT_B5G6R5_UNORM;
    case D3DFMT_A1R5G5B5:return DXGI_FORMAT_B5G5R5A1_UNORM;
    case D3DFMT_X1R5G5B5:return DXGI_FORMAT_B5G5R5A1_UNORM;
    case D3DFMT_A4R4G4B4:return DXGI_FORMAT_B4G4R4A4_UNORM;
    case D3DFMT_A2B10G10R10:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DFMT_A2R10G10B10:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DFMT_G16R16:return DXGI_FORMAT_R16G16_UNORM;
    case D3DFMT_A16B16G16R16:return DXGI_FORMAT_R16G16B16A16_UNORM;
    case D3DFMT_A8:return DXGI_FORMAT_A8_UNORM;
    case D3DFMT_L8:return DXGI_FORMAT_R8_UNORM;
    case D3DFMT_A8L8:return DXGI_FORMAT_R8G8_UNORM;
    case D3DFMT_L16:return DXGI_FORMAT_R16_UNORM;
    case D3DFMT_D16_LOCKABLE:return DXGI_FORMAT_D16_UNORM;
    case D3DFMT_D16:return DXGI_FORMAT_D16_UNORM;
    case D3DFMT_D32:return DXGI_FORMAT_D32_FLOAT;
    case D3DFMT_D32F_LOCKABLE:return DXGI_FORMAT_D32_FLOAT;
    case D3DFMT_D24S8:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24X8:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24X4S4:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24FS8:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_DXT1:return DXGI_FORMAT_BC1_UNORM;
    case D3DFMT_DXT2:return DXGI_FORMAT_BC2_UNORM;
    case D3DFMT_DXT3:return DXGI_FORMAT_BC2_UNORM;
    case D3DFMT_DXT4:return DXGI_FORMAT_BC3_UNORM;
    case D3DFMT_DXT5:return DXGI_FORMAT_BC3_UNORM;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
bool depthFormat(D3DFORMAT format) {
    switch(format) {
    case D3DFMT_D16_LOCKABLE:case D3DFMT_D16:case D3DFMT_D32:case D3DFMT_D32F_LOCKABLE:
    case D3DFMT_D24S8:case D3DFMT_D24X8:case D3DFMT_D24X4S4:case D3DFMT_D24FS8:
        return true;
    default:return false;
    }
}
// D3DPOOL decides the D3D11 usage: DEFAULT is GPU-only, SYSTEMMEM and SCRATCH
// are CPU-visible, MANAGED has no D3D11 equivalent and is served as a dynamic
// CPU-writable resource (D3D11 keeps a copy, which is the observable behaviour).
D3D11_USAGE usageFor(D3DPOOL pool,DWORD d3d9Usage) {
    if(d3d9Usage&D3DUSAGE_DYNAMIC)return D3D11_USAGE_DYNAMIC;
    switch(pool) {
    case D3DPOOL_SYSTEMMEM:case D3DPOOL_SCRATCH:return D3D11_USAGE_STAGING;
    case D3DPOOL_MANAGED:return D3D11_USAGE_DYNAMIC;
    default:return D3D11_USAGE_DEFAULT;
    }
}
UINT cpuAccessFor(D3DPOOL pool,DWORD d3d9Usage) {
    if(d3d9Usage&D3DUSAGE_DYNAMIC)return D3D11_CPU_ACCESS_WRITE;
    switch(pool) {
    case D3DPOOL_SYSTEMMEM:case D3DPOOL_SCRATCH:return D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
    case D3DPOOL_MANAGED:return D3D11_CPU_ACCESS_WRITE;
    default:return 0;
    }
}
// D3D9 render states and sampler states are not D3D11 pipeline state, so they
// are stored and read back; the translation that would consume them is the
// unfinished half of the shim.
constexpr unsigned renderStateCount=256;
constexpr unsigned textureStageCount=16;
constexpr unsigned streamCount=16;
constexpr unsigned samplerStateCount=16;
constexpr unsigned maxVertexConst=256;
constexpr unsigned maxPixelConst=224;

void colorF(float out[4],D3DCOLOR color) {
    out[0]=float((color>>16)&0xff)/255.0f;
    out[1]=float((color>>8)&0xff)/255.0f;
    out[2]=float(color&0xff)/255.0f;
    out[3]=float((color>>24)&0xff)/255.0f;
}
void copyText(char* out,unsigned size,const char* text) {
    unsigned i=0;
    for(;i+1<size&&text[i];++i)out[i]=text[i];
    out[i]=0;
}
// The capability table is the same whether a guest asks the factory or a
// device, so both build it here.
void fillCaps(D3DCAPS9* caps,D3DDEVTYPE type,UINT adapter) {
    *caps=D3DCAPS9{};
    caps->DeviceType=type;
    caps->AdapterOrdinal=adapter;
    caps->Caps=D3DCAPS_READ_SCANLINE;
    caps->PresentationIntervals=D3DPRESENT_INTERVAL_IMMEDIATE|D3DPRESENT_INTERVAL_ONE;
    caps->DevCaps=D3DDEVCAPS_DRAWPRIMTLVERTEX|D3DDEVCAPS_HWTRANSFORMANDLIGHT;
    caps->MaxTextureWidth=caps->MaxTextureHeight=16384;
    caps->MaxVolumeExtent=2048;
    caps->MaxTextureRepeat=8192;
    caps->MaxTextureAspectRatio=8192;
    caps->MaxAnisotropy=16;
    caps->MaxVertexW=1e10f;
    caps->MaxTextureBlendStages=8;
    caps->MaxSimultaneousTextures=8;
    caps->MaxStreams=streamCount;
    caps->MaxStreamStride=255;
    caps->MaxPrimitiveCount=0x555555;
    caps->MaxVertexIndex=0xffffff;
    caps->MaxVertexShaderConst=maxVertexConst;
    caps->MaxUserClipPlanes=6;
    caps->MaxVertexBlendMatrices=4;
    caps->NumSimultaneousRTs=4;
    caps->MaxVertexShader30InstructionSlots=32768;
    caps->MaxPixelShader30InstructionSlots=32768;
    // The D3D11 device behind this shim is shader model 4+, which covers D3D9's
    // 3.0. Reporting it is the hardware truth; the shim's own translation of
    // D3D9 bytecode is what is missing, and Create*Shader says so.
    caps->VertexShaderVersion=D3DVS_VERSION(3,0);
    caps->PixelShaderVersion=D3DPS_VERSION(3,0);
    caps->PixelShader1xMaxValue=8.0f;
    caps->MaxPointSize=256.0f;
}
}

namespace {
// ------------------------------------------------------------------ surfaces
// A D3D9 surface is a 2D texture plus, when it can be rendered to, the views a
// D3D11 context needs. The back buffer comes from the swap chain, so the same
// wrapper serves both.
struct Surface : d3dshim::Base, IDirect3DSurface9 {
    Surface() { self=static_cast<IDirect3DSurface9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DSurface9),&__uuidof(IDirect3DResource9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    // The D3D11 texture is `inner`, so ~Base releases the one reference the
    // surface owns; `texture` is only an alias for the typed calls.
    ID3D11Texture2D* texture=nullptr;
    ID3D11RenderTargetView* rtv=nullptr;     // when the surface is a render target
    ID3D11DepthStencilView* dsv=nullptr;     // when the surface is depth-stencil
    D3DSURFACE_DESC desc{};
    bool cpuVisible=false;                   // SYSTEMMEM, SCRATCH or DYNAMIC
    bool locked=false;
    // D3D9's surfaces and swap chains hold their device weakly: the device owns
    // them, not the other way round. Base::owner would be a strong reference, so
    // the device's own implicit back buffer would keep the device alive forever.
    // The device's guest-visible interface, not a Device*, because Device is
    // defined below this wrapper.
    IDirect3DDevice9* device=nullptr;
    ~Surface() {
        if(locked&&context)context->Unmap(texture,0);
        if(rtv)rtv->Release();
        if(dsv)dsv->Release();
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,const void* data,DWORD size,DWORD) override {
        return inner?inner->SetPrivateData(id,size,data):D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,void* data,DWORD* size) override {
        UINT bytes=size?*size:0;
        HRESULT hr=inner?inner->GetPrivateData(id,&bytes,data):D3DERR_INVALIDCALL;
        if(size)*size=bytes;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID id) override {
        return inner?inner->SetPrivateData(id,0,nullptr):D3DERR_INVALIDCALL;
    }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID,void** container) override {
        if(container)*container=nullptr;
        return D3DERR_INVALIDCALL;   // surfaces made here have no parent texture
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=desc;
        return D3D_OK;
    }
    // The back buffer and render targets live in D3DPOOL_DEFAULT, which D3D9
    // does not allow to be locked; the same call fails on Windows. A SYSTEMMEM,
    // SCRATCH or dynamic surface is CPU-visible and does map.
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* out,const RECT*,DWORD) override {
        if(!out)return D3DERR_INVALIDCALL;
        out->pBits=nullptr;out->Pitch=0;
        if(!cpuVisible||!context||!texture) {
            d3dshim::noteOnce("D3D9 shim: LockRect on a default-pool surface refused, as on Windows");
            return D3DERR_INVALIDCALL;
        }
        if(locked)return D3DERR_INVALIDCALL;   // D3D9 allows one lock at a time
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // A staging texture can only be read, a dynamic one only written.
        D3D11_MAP mode=desc.Usage&D3DUSAGE_DYNAMIC?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_READ_WRITE;
        HRESULT hr=context->Map(texture,0,mode,0,&mapped);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        locked=true;
        out->pBits=mapped.pData;
        out->Pitch=INT(mapped.RowPitch);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect() override {
        if(!locked)return D3DERR_INVALIDCALL;
        context->Unmap(texture,0);
        locked=false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDC(HDC* dc) override { if(dc)*dc=nullptr;return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return D3DERR_INVALIDCALL; }
};
}

namespace {
// ---------------------------------------------------------------- swap chain
struct SwapChain : d3dshim::Base, IDirect3DSwapChain9 {
    SwapChain() { self=static_cast<IDirect3DSwapChain9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DSwapChain9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    IDXGISwapChain* chain=nullptr;
    Surface* backBuffer=nullptr;          // the wrapper handed to the guest
    D3DPRESENT_PARAMETERS present{};
    IDirect3DDevice9* device=nullptr;     // weak, as for Surface
    ~SwapChain() {
        if(backBuffer)backBuffer->Release();
        if(chain)chain->Release();
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT*,const RECT*,HWND,const RGNDATA*,DWORD) override {
        if(!chain)return D3DERR_INVALIDCALL;
        UINT interval=present.PresentationInterval==D3DPRESENT_INTERVAL_IMMEDIATE?0:1;
        HRESULT hr=chain->Present(interval,0);
        if(hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET)return D3DERR_DEVICELOST;
        return SUCCEEDED(hr)?D3D_OK:D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9*) override {
        return D3DERR_INVALIDCALL;   // the console has no front buffer to read back
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index,D3DBACKBUFFER_TYPE,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!backBuffer||index!=0)return D3DERR_INVALIDCALL;
        backBuffer->AddRef();
        *out=backBuffer;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS* status) override {
        if(!status)return D3DERR_INVALIDCALL;
        status->InVBlank=FALSE;status->ScanLine=0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* mode) override {
        if(!mode)return D3DERR_INVALIDCALL;
        mode->Width=present.BackBufferWidth;mode->Height=present.BackBufferHeight;
        mode->RefreshRate=0;mode->Format=present.BackBufferFormat;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=present;
        return D3D_OK;
    }
};
}

namespace {
// --------------------------------------------------------------------- device
struct Device : d3dshim::Base, IDirect3DDevice9Ex {
    Device() { self=static_cast<IDirect3DDevice9Ex*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DDevice9),&__uuidof(IDirect3DDevice9Ex)); }
    ~Device() {
        // The context holds references on what was bound; drop ours first so
        // nothing outlives the device (Wine's present path never finishes
        // process exit while a D3D object outlives its device).
        if(ID3D11DeviceContext* immediate=context())immediate->ClearState();
        renderTarget.clear();depthStencil.clear();
        for(unsigned i=0;i<textureStageCount;++i)if(textures[i]){textures[i]->Release();textures[i]=nullptr;}
        for(unsigned i=0;i<streamCount;++i)if(streams[i]){streams[i]->Release();streams[i]=nullptr;}
        if(indices)indices->Release();
        if(declaration)declaration->Release();
        if(vertexShader)vertexShader->Release();
        if(pixelShader)pixelShader->Release();
        if(swapChain)swapChain->Release();
        if(parent)parent->Release();
        host.destroy();
    }
    D3DSHIM_REF
    D3DSHIM_QI
    d3dshim::Host host;
    SwapChain* swapChain=nullptr;
    d3dshim::Held<1> renderTarget;      // what Clear() clears
    d3dshim::Held<1> depthStencil;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DPRESENT_PARAMETERS present{};
    DWORD renderStates[renderStateCount]{};
    DWORD textureStageStates[textureStageCount][32]{};
    DWORD samplerStates[samplerStateCount][16]{};
    IDirect3DBaseTexture9* textures[textureStageCount]{};
    IDirect3DVertexBuffer9* streams[streamCount]{};
    UINT streamOffsets[streamCount]{};
    UINT streamStrides[streamCount]{};
    IDirect3DIndexBuffer9* indices=nullptr;
    IDirect3DVertexDeclaration9* declaration=nullptr;
    DWORD fvf=0;
    IDirect3DVertexShader9* vertexShader=nullptr;
    IDirect3DPixelShader9* pixelShader=nullptr;
    float vertexConstF[maxVertexConst][4]{};
    float pixelConstF[maxPixelConst][4]{};
    D3DVIEWPORT9 viewport{0,0,0,0,0.0f,1.0f};
    RECT scissor{};
    IDirect3D9Ex* parent=nullptr;       // the factory that made this device
    D3DDEVTYPE deviceType=D3DDEVTYPE_HAL;

    ID3D11Device* device() const { return host.device; }
    ID3D11DeviceContext* context() const { return host.context; }

    // ------------------------------------------------------------ device info
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 512u*1024u*1024u; }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=parent;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps) override {
        if(!caps)return D3DERR_INVALIDCALL;
        fillCaps(caps,deviceType,creation.AdapterOrdinal);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT index,D3DDISPLAYMODE* mode) override {
        if(!mode||index!=0)return D3DERR_INVALIDCALL;
        mode->Width=present.BackBufferWidth;mode->Height=present.BackBufferHeight;
        mode->RefreshRate=0;mode->Format=present.BackBufferFormat;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=creation;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT,UINT,IDirect3DSurface9*) override { return D3DERR_INVALIDCALL; }
    void STDMETHODCALLTYPE SetCursorPosition(int,int,DWORD) override {}
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL show) override { return show; }

    // ------------------------------------------------------------ swap chains
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: additional swap chains are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index,IDirect3DSwapChain9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(index!=0||!swapChain)return D3DERR_INVALIDCALL;
        swapChain->AddRef();
        *out=swapChain;
        return D3D_OK;
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 1; }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* parameters) override {
        if(!parameters)return D3DERR_INVALIDCALL;
        return rebuildSwapChain(parameters);
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND override,const RGNDATA* dirty) override {
        if(!swapChain)return D3DERR_INVALIDCALL;
        return swapChain->Present(src,dst,override,dirty,0);
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT chainIndex,UINT backIndex,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(chainIndex!=0||!swapChain)return D3DERR_INVALIDCALL;
        return swapChain->GetBackBuffer(backIndex,type,out);
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT chainIndex,D3DRASTER_STATUS* status) override {
        if(chainIndex!=0||!swapChain)return D3DERR_INVALIDCALL;
        return swapChain->GetRasterStatus(status);
    }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetGammaRamp(UINT,DWORD,const D3DGAMMARAMP*) override {}
    void STDMETHODCALLTYPE GetGammaRamp(UINT,D3DGAMMARAMP* ramp) override {
        if(ramp)for(unsigned i=0;i<256;++i){ramp->red[i]=WORD(i*257);ramp->green[i]=WORD(i*257);ramp->blue[i]=WORD(i*257);}
    }

    // -------------------------------------------------------------- resources
    HRESULT createTexture2D(UINT width,UINT height,UINT levels,DWORD usage,D3DFORMAT format,
                            D3DPOOL pool,ID3D11Texture2D** out) {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        DXGI_FORMAT dxgi=dxgiFormat(format);
        if(dxgi==DXGI_FORMAT_UNKNOWN) {
            d3dshim::noteOnce("D3D9 shim: texture format has no DXGI equivalent");
            return D3DERR_INVALIDCALL;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;desc.MipLevels=levels?levels:1;desc.ArraySize=1;
        desc.Format=dxgi;desc.SampleDesc.Count=1;
        desc.Usage=usageFor(pool,usage);
        desc.BindFlags=0;
        if(usage&D3DUSAGE_RENDERTARGET)desc.BindFlags|=D3D11_BIND_RENDER_TARGET;
        if(usage&D3DUSAGE_DEPTHSTENCIL)desc.BindFlags|=D3D11_BIND_DEPTH_STENCIL;
        if(!desc.BindFlags&&desc.Usage==D3D11_USAGE_DEFAULT)desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=cpuAccessFor(pool,usage);
        // D3D11 refuses a dynamic resource with no bind flag and a staging
        // resource with one, so the two pools get exactly what they allow.
        if(desc.Usage==D3D11_USAGE_DYNAMIC&&!desc.BindFlags)desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(desc.Usage==D3D11_USAGE_STAGING)desc.BindFlags=0;
        return device()->CreateTexture2D(&desc,nullptr,out);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,
                                            IDirect3DTexture9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: IDirect3DTexture9 is not implemented yet; use a render target or surface");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,
                                                  IDirect3DVolumeTexture9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: volume textures are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,
                                                IDirect3DCubeTexture9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: cube textures are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT,DWORD,DWORD,D3DPOOL,
                                                 IDirect3DVertexBuffer9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: vertex buffers are not implemented yet");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT,DWORD,D3DFORMAT,D3DPOOL,
                                                IDirect3DIndexBuffer9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: index buffers are not implemented yet");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width,UINT height,D3DFORMAT format,
                                                 D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                 IDirect3DSurface9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        return makeSurface(width,height,format,samples,quality,true,false,out);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width,UINT height,D3DFORMAT format,
                                                        D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                        IDirect3DSurface9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        return makeSurface(width,height,format,samples,quality,false,true,out);
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*) override {
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9*,IDirect3DBaseTexture9*) override {
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9*,IDirect3DSurface9*) override {
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT,IDirect3DSurface9*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,
                                          D3DTEXTUREFILTERTYPE) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9*,const RECT*,D3DCOLOR) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,
                                                          IDirect3DSurface9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        return makeSurface(width,height,format,D3DMULTISAMPLE_NONE,0,false,false,out,pool);
    }

    // ------------------------------------------------------------ render state
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index,IDirect3DSurface9* surface) override {
        if(index!=0)return D3DERR_INVALIDCALL;
        if(surface) {
            auto wrapper=static_cast<Surface*>(surface);
            if(!wrapper->rtv)return D3DERR_INVALIDCALL;
            renderTarget.set(0,surface);
            context()->OMSetRenderTargets(1,&wrapper->rtv,nullptr);
        } else {
            renderTarget.set(0,nullptr);
            context()->OMSetRenderTargets(0,nullptr,nullptr);
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(index!=0)return D3DERR_INVALIDCALL;
        *out=static_cast<IDirect3DSurface9*>(renderTarget.objects[0]);
        if(*out)(*out)->AddRef();
        return *out?D3D_OK:D3DERR_INVALIDCALL;
    }
    ID3D11RenderTargetView* currentTarget() const {
        auto surface=static_cast<IDirect3DSurface9*>(renderTarget.objects[0]);
        return surface?static_cast<Surface*>(surface)->rtv:nullptr;
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* surface) override {
        ID3D11DepthStencilView* view=nullptr;
        if(surface) {
            view=static_cast<Surface*>(surface)->dsv;
            if(!view)return D3DERR_INVALIDCALL;
        }
        depthStencil.set(0,surface);
        ID3D11RenderTargetView* rt=currentTarget();
        context()->OMSetRenderTargets(rt?1:0,rt?&rt:nullptr,view);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=static_cast<IDirect3DSurface9*>(depthStencil.objects[0]);
        if(*out)(*out)->AddRef();
        return *out?D3D_OK:D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Clear(DWORD rectCount,const D3DRECT*,DWORD flags,D3DCOLOR color,float z,DWORD stencil) override {
        if(rectCount)d3dshim::noteOnce("D3D9 shim: Clear with rectangles clears the whole target");
        if(flags&D3DCLEAR_TARGET) {
            ID3D11RenderTargetView* view=currentTarget();
            if(!view&&swapChain&&swapChain->backBuffer)view=swapChain->backBuffer->rtv;
            if(view) {
                float rgba[4];colorF(rgba,color);
                context()->ClearRenderTargetView(view,rgba);
            }
        }
        if((flags&D3DCLEAR_ZBUFFER)||(flags&D3DCLEAR_STENCIL)) {
            auto surface=static_cast<IDirect3DSurface9*>(depthStencil.objects[0]);
            ID3D11DepthStencilView* view=surface?static_cast<Surface*>(surface)->dsv:nullptr;
            if(view) {
                UINT clearFlags=0;
                if(flags&D3DCLEAR_ZBUFFER)clearFlags|=D3D11_CLEAR_DEPTH;
                if(flags&D3DCLEAR_STENCIL)clearFlags|=D3D11_CLEAR_STENCIL;
                context()->ClearDepthStencilView(view,clearFlags,z,UINT8(stencil));
            }
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) override {
        d3dshim::noteOnce("D3D9 shim: fixed-function transforms are stored only; drawing is not implemented");
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE,D3DMATRIX* matrix) override {
        if(matrix) {
            *matrix=D3DMATRIX{};
            matrix->_11=matrix->_22=matrix->_33=matrix->_44=1.0f;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* value) override {
        if(!value)return D3DERR_INVALIDCALL;
        viewport=*value;
        D3D11_VIEWPORT out{};
        out.TopLeftX=float(value->X);out.TopLeftY=float(value->Y);
        out.Width=float(value->Width);out.Height=float(value->Height);
        out.MinDepth=value->MinZ;out.MaxDepth=value->MaxZ;
        context()->RSSetViewports(1,&out);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=viewport;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* material) override {
        if(material)*material=D3DMATERIAL9{};
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD,const D3DLIGHT9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD,D3DLIGHT9* light) override {
        if(light)*light=D3DLIGHT9{};
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD,WINBOOL) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD,WINBOOL* enabled) override {
        if(enabled)*enabled=FALSE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD,const float*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD,float* plane) override {
        if(plane)for(unsigned i=0;i<4;++i)plane[i]=0.0f;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state,DWORD value) override {
        if(unsigned(state)<renderStateCount)renderStates[state]=value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state,DWORD* value) override {
        if(!value||unsigned(state)>=renderStateCount)return D3DERR_INVALIDCALL;
        *value=renderStates[state];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: state blocks are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** out) override {
        if(out)*out=nullptr;
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* status) override {
        if(status) {
            status->ClipUnion=0;
            status->ClipIntersection=D3DCS_ALL;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage,IDirect3DBaseTexture9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(stage>=textureStageCount)return D3DERR_INVALIDCALL;
        *out=textures[stage];
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* texture) override {
        if(stage>=textureStageCount)return D3DERR_INVALIDCALL;
        if(textures[stage]==texture)return D3D_OK;
        if(texture)texture->AddRef();
        if(textures[stage])textures[stage]->Release();
        textures[stage]=texture;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE type,DWORD* value) override {
        if(!value||stage>=textureStageCount||unsigned(type)>=32)return D3DERR_INVALIDCALL;
        *value=textureStageStates[stage][type];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE type,DWORD value) override {
        if(stage>=textureStageCount||unsigned(type)>=32)return D3DERR_INVALIDCALL;
        textureStageStates[stage][type]=value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE type,DWORD* value) override {
        if(!value||sampler>=samplerStateCount||unsigned(type)>=16)return D3DERR_INVALIDCALL;
        *value=samplerStates[sampler][type];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE type,DWORD value) override {
        if(sampler>=samplerStateCount||unsigned(type)>=16)return D3DERR_INVALIDCALL;
        samplerStates[sampler][type]=value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override {
        if(passes)*passes=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT,const PALETTEENTRY*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT,PALETTEENTRY*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* number) override {
        if(number)*number=0;
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* rect) override {
        if(!rect)return D3DERR_INVALIDCALL;
        scissor=*rect;
        D3D11_RECT out{rect->left,rect->top,rect->right,rect->bottom};
        context()->RSSetScissorRects(1,&out);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* rect) override {
        if(!rect)return D3DERR_INVALIDCALL;
        *rect=scissor;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL) override { return D3D_OK; }
    WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return D3D_OK; }
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0.0f; }

    // ----------------------------------------------------------------- drawing
    // Every draw path needs D3D9 bytecode or fixed-function state translated to
    // D3D11, which this milestone does not do. Reporting the API's own failure
    // code is what a device without the capability returns.
    HRESULT noDraw() {
        d3dshim::noteOnce("D3D9 shim: drawing needs D3D9 shader translation, which is not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT) override { return noDraw(); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT) override { return noDraw(); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT) override { return noDraw(); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,
                                                     D3DFORMAT,const void*,UINT) override { return noDraw(); }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT,UINT,UINT,IDirect3DVertexBuffer9*,
                                              IDirect3DVertexDeclaration9*,DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9*,IDirect3DVertexDeclaration9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: vertex declarations are not implemented yet");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* value) override {
        if(value)value->AddRef();
        if(declaration)declaration->Release();
        declaration=value;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=declaration;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD value) override { fvf=value;return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* value) override {
        if(!value)return D3DERR_INVALIDCALL;
        *value=fvf;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD*,IDirect3DVertexShader9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: D3D9 vertex shader bytecode translation is not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* shader) override {
        if(shader)shader->AddRef();
        if(vertexShader)vertexShader->Release();
        vertexShader=shader;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=vertexShader;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start,const float* data,UINT count) override {
        if(!data||start+count>maxVertexConst)return D3DERR_INVALIDCALL;
        memcpy(vertexConstF[start],data,count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start,float* data,UINT count) override {
        if(!data||start+count>maxVertexConst)return D3DERR_INVALIDCALL;
        memcpy(data,vertexConstF[start],count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT,const int*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT,int*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT,const WINBOOL*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT,WINBOOL*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride) override {
        if(stream>=streamCount)return D3DERR_INVALIDCALL;
        if(buffer)buffer->AddRef();
        if(streams[stream])streams[stream]->Release();
        streams[stream]=buffer;
        streamOffsets[stream]=offset;streamStrides[stream]=stride;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream,IDirect3DVertexBuffer9** buffer,UINT* offset,UINT* stride) override {
        if(stream>=streamCount)return D3DERR_INVALIDCALL;
        if(buffer){*buffer=streams[stream];if(*buffer)(*buffer)->AddRef();}
        if(offset)*offset=streamOffsets[stream];
        if(stride)*stride=streamStrides[stream];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT,UINT* divider) override {
        if(divider)*divider=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* buffer) override {
        if(buffer)buffer->AddRef();
        if(indices)indices->Release();
        indices=buffer;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=indices;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: D3D9 pixel shader bytecode translation is not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader) override {
        if(shader)shader->AddRef();
        if(pixelShader)pixelShader->Release();
        pixelShader=shader;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=pixelShader;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start,const float* data,UINT count) override {
        if(!data||start+count>maxPixelConst)return D3DERR_INVALIDCALL;
        memcpy(pixelConstF[start],data,count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT start,float* data,UINT count) override {
        if(!data||start+count>maxPixelConst)return D3DERR_INVALIDCALL;
        memcpy(data,pixelConstF[start],count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT,const int*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT,int*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT,const WINBOOL*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT,WINBOOL*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT,const float*,const D3DRECTPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT,const float*,const D3DTRIPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE,IDirect3DQuery9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: queries are not implemented");
        return D3DERR_INVALIDCALL;
    }

    // ------------------------------------------------------------ IDirect3D9Ex
    HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT,UINT,float*,float*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9*,IDirect3DSurface9*,IDirect3DVertexBuffer9*,
                                           UINT,IDirect3DVertexBuffer9*,D3DCOMPOSERECTSOP,INT,INT) override {
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE PresentEx(const RECT* src,const RECT* dst,HWND override,const RGNDATA* dirty,DWORD) override {
        return Present(src,dst,override,dirty);
    }
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* priority) override {
        if(priority)*priority=0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9**,UINT32) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override {
        if(latency)*latency=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT width,UINT height,D3DFORMAT format,
                                                   D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                   IDirect3DSurface9** out,HANDLE*,DWORD) override {
        return CreateRenderTarget(width,height,format,samples,quality,FALSE,out,nullptr);
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,
                                                            IDirect3DSurface9** out,HANDLE*,DWORD) override {
        return CreateOffscreenPlainSurface(width,height,format,pool,out,nullptr);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT width,UINT height,D3DFORMAT format,
                                                          D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                          IDirect3DSurface9** out,HANDLE*,DWORD) override {
        return CreateDepthStencilSurface(width,height,format,samples,quality,FALSE,out,nullptr);
    }
    HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS* parameters,D3DDISPLAYMODEEX*) override {
        if(!parameters)return D3DERR_INVALIDCALL;
        return rebuildSwapChain(parameters);
    }
    HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT index,D3DDISPLAYMODEEX* mode,D3DDISPLAYROTATION* rotation) override {
        if(!mode||index!=0)return D3DERR_INVALIDCALL;
        mode->Size=sizeof(*mode);
        mode->Width=present.BackBufferWidth;mode->Height=present.BackBufferHeight;
        mode->RefreshRate=0;mode->Format=present.BackBufferFormat;
        mode->ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
        if(rotation)*rotation=D3DDISPLAYROTATION_IDENTITY;
        return D3D_OK;
    }

    // ---------------------------------------------------------------- helpers
    HRESULT makeSurface(UINT width,UINT height,D3DFORMAT format,D3DMULTISAMPLE_TYPE samples,DWORD quality,
                        bool renderTarget,bool depthStencil,IDirect3DSurface9** out,
                        D3DPOOL pool=D3DPOOL_DEFAULT) {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        DWORD usage=renderTarget?D3DUSAGE_RENDERTARGET:(depthStencil?D3DUSAGE_DEPTHSTENCIL:0);
        ID3D11Texture2D* texture=nullptr;
        HRESULT hr=createTexture2D(width,height,1,usage,format,pool,&texture);
        if(FAILED(hr))return hr;
        auto surface=d3dshim::create<Surface>();
        if(!surface){texture->Release();return E_OUTOFMEMORY;}
        surface->texture=texture;
        surface->inner=texture;
        surface->device=static_cast<IDirect3DDevice9*>(self);
        surface->context=context();
        surface->cpuVisible=usageFor(pool,usage)!=D3D11_USAGE_DEFAULT;
        surface->desc.Format=format;
        surface->desc.Type=D3DRTYPE_SURFACE;
        surface->desc.Usage=usage;
        surface->desc.Pool=pool;
        surface->desc.MultiSampleType=samples;
        surface->desc.MultiSampleQuality=quality;
        surface->desc.Width=width;
        surface->desc.Height=height;
        if(renderTarget) {
            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format=dxgiFormat(format);
            view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            hr=device()->CreateRenderTargetView(texture,&view,&surface->rtv);
            if(FAILED(hr)){d3dshim::destroy(surface);return hr;}
        }
        if(depthStencil) {
            D3D11_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format=dxgiFormat(format);
            view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            hr=device()->CreateDepthStencilView(texture,&view,&surface->dsv);
            if(FAILED(hr)){d3dshim::destroy(surface);return hr;}
        }
        *out=surface;
        return D3D_OK;
    }
    // The swap chain is rebuilt on Reset: D3D9 requires the guest to release its
    // default-pool resources first, and the CoreWindow adapter has no
    // ResizeBuffers path of its own, so a fresh chain is the honest
    // implementation.
    HRESULT rebuildSwapChain(D3DPRESENT_PARAMETERS* parameters) {
        if(swapChain){swapChain->Release();swapChain=nullptr;}
        renderTarget.clear();depthStencil.clear();
        HRESULT hr=createSwapChain(parameters);
        if(FAILED(hr))return hr;
        present=*parameters;
        return D3D_OK;
    }
    HRESULT createSwapChain(D3DPRESENT_PARAMETERS* parameters) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width=parameters->BackBufferWidth;
        desc.BufferDesc.Height=parameters->BackBufferHeight;
        desc.BufferDesc.Format=dxgiFormat(parameters->BackBufferFormat);
        desc.BufferDesc.RefreshRate.Numerator=0;
        desc.BufferDesc.RefreshRate.Denominator=1;
        desc.SampleDesc.Count=parameters->MultiSampleType?UINT(parameters->MultiSampleType):1;
        desc.SampleDesc.Quality=parameters->MultiSampleQuality;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=parameters->BackBufferCount?parameters->BackBufferCount:1;
        desc.OutputWindow=parameters->hDeviceWindow?parameters->hDeviceWindow:creation.hFocusWindow;
        desc.Windowed=TRUE;   // the CoreWindow adapter has no exclusive mode
        desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        if(desc.BufferDesc.Format==DXGI_FORMAT_UNKNOWN) {
            d3dshim::noteOnce("D3D9 shim: back buffer format has no DXGI equivalent");
            return D3DERR_INVALIDCALL;
        }
        // The device's own adapter factory, exactly as the D3D10 shim does it:
        // Play redirects this factory's CreateSwapChain for a guest window to
        // the validated CoreWindow adapter.
        IDXGIDevice* dxgi=nullptr;
        IDXGIAdapter* adapter=nullptr;
        IDXGIFactory* local=nullptr;
        HRESULT hr=device()->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi));
        if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
        if(dxgi)dxgi->Release();
        if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(&local));
        if(adapter)adapter->Release();
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        IDXGISwapChain* chain=nullptr;
        hr=d3dshim::createSwapChain(local,device(),&desc,&chain);
        local->Release();
        if(FAILED(hr)) {
            d3dshim::Line line("D3D9 shim: CreateSwapChain failed hr=");
            line.hex((unsigned long)hr);line.emit();
            return D3DERR_INVALIDCALL;
        }
        auto wrapper=d3dshim::create<SwapChain>();
        if(!wrapper){chain->Release();return E_OUTOFMEMORY;}
        wrapper->chain=chain;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->context=context();
        wrapper->present=*parameters;
        // The back buffer is the device's first render target, as on Windows.
        ID3D11Texture2D* texture=nullptr;
        hr=chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
        if(SUCCEEDED(hr)&&texture) {
            auto surface=d3dshim::create<Surface>();
            if(surface) {
                surface->texture=texture;
                surface->inner=texture;
                surface->device=static_cast<IDirect3DDevice9*>(self);
                surface->context=context();
                surface->desc.Format=parameters->BackBufferFormat;
                surface->desc.Type=D3DRTYPE_SURFACE;
                surface->desc.Usage=D3DUSAGE_RENDERTARGET;
                surface->desc.Pool=D3DPOOL_DEFAULT;
                surface->desc.MultiSampleType=parameters->MultiSampleType;
                surface->desc.MultiSampleQuality=parameters->MultiSampleQuality;
                D3D11_TEXTURE2D_DESC got{};
                texture->GetDesc(&got);
                surface->desc.Width=got.Width;
                surface->desc.Height=got.Height;
                D3D11_RENDER_TARGET_VIEW_DESC view{};
                view.Format=got.Format;
                view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
                if(SUCCEEDED(device()->CreateRenderTargetView(texture,&view,&surface->rtv))) {
                    wrapper->backBuffer=surface;
                    renderTarget.set(0,surface);
                    context()->OMSetRenderTargets(1,&surface->rtv,nullptr);
                } else {
                    d3dshim::destroy(surface);
                }
            } else {
                texture->Release();
            }
        }
        swapChain=wrapper;
        // EnableAutoDepthStencil makes the device create and bind a depth
        // surface of its own, as D3D9 documents. It is not a guest-held object,
        // so the device owns the one reference.
        if(parameters->EnableAutoDepthStencil) {
            IDirect3DSurface9* autoDepth=nullptr;
            if(SUCCEEDED(makeSurface(desc.BufferDesc.Width,desc.BufferDesc.Height,
                    parameters->AutoDepthStencilFormat,D3DMULTISAMPLE_NONE,0,false,true,&autoDepth))) {
                depthStencil.set(0,autoDepth);
                autoDepth->Release();   // the Held slot owns it now
                ID3D11RenderTargetView* rt=currentTarget();
                context()->OMSetRenderTargets(rt?1:0,rt?&rt:nullptr,static_cast<Surface*>(autoDepth)->dsv);
            }
        }
        return D3D_OK;
    }
};
}

namespace {
// -------------------------------------------------------------------- factory
struct Factory : d3dshim::Base, IDirect3D9Ex {
    Factory() { self=static_cast<IDirect3D9Ex*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3D9),&__uuidof(IDirect3D9Ex)); }
    ~Factory() {
        if(factory)factory->Release();
    }
    D3DSHIM_REF
    D3DSHIM_QI
    IDXGIFactory* factory=nullptr;
    HRESULT init() {
        if(factory)return S_OK;
        return d3dshim::factory(&factory);
    }
    // The console exposes one adapter; the CoreWindow adapter backs it.
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter,DWORD,D3DADAPTER_IDENTIFIER9* identifier) override {
        if(!identifier||adapter!=0)return D3DERR_INVALIDCALL;
        *identifier=D3DADAPTER_IDENTIFIER9{};
        copyText(identifier->Driver,sizeof identifier->Driver,"Play D3D9 shim");
        copyText(identifier->Description,sizeof identifier->Description,"Play D3D9 shim");
        copyText(identifier->DeviceName,sizeof identifier->DeviceName,"Console CoreWindow adapter");
        identifier->DriverVersion.QuadPart=0x00010000;
        identifier->VendorId=0x1414;   // Microsoft
        identifier->DeviceId=0x0001;
        identifier->WHQLLevel=1;
        return D3D_OK;
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter,D3DFORMAT format) override {
        if(adapter!=0)return 0;
        return dxgiFormat(format)==DXGI_FORMAT_UNKNOWN?0:1;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter,D3DFORMAT format,UINT index,D3DDISPLAYMODE* mode) override {
        if(adapter!=0||index!=0||!mode)return D3DERR_INVALIDCALL;
        if(dxgiFormat(format)==DXGI_FORMAT_UNKNOWN)return D3DERR_INVALIDCALL;
        mode->Width=1920;mode->Height=1080;mode->RefreshRate=60;mode->Format=format;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter,D3DDISPLAYMODE* mode) override {
        if(adapter!=0||!mode)return D3DERR_INVALIDCALL;
        mode->Width=1920;mode->Height=1080;mode->RefreshRate=60;mode->Format=D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter,D3DDEVTYPE type,D3DFORMAT displayFormat,
                                              D3DFORMAT backBufferFormat,WINBOOL) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(dxgiFormat(displayFormat)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        if(dxgiFormat(backBufferFormat)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter,D3DDEVTYPE type,D3DFORMAT,DWORD usage,
                                                D3DRESOURCETYPE,D3DFORMAT format) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(usage&D3DUSAGE_RENDERTARGET)return D3DERR_NOTAVAILABLE;   // no view can be made yet
        return dxgiFormat(format)==DXGI_FORMAT_UNKNOWN?D3DERR_NOTAVAILABLE:D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter,D3DDEVTYPE type,D3DFORMAT format,WINBOOL,
                                                         D3DMULTISAMPLE_TYPE samples,DWORD* quality) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(dxgiFormat(format)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        // The CoreWindow adapter has no resolve layer, so only single-sample is
        // offered; the swap chain refuses anything else too.
        if(samples!=D3DMULTISAMPLE_NONE)return D3DERR_NOTAVAILABLE;
        if(quality)*quality=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter,D3DDEVTYPE type,D3DFORMAT,D3DFORMAT,
                                                     D3DFORMAT dsFormat) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        return depthFormat(dsFormat)?D3D_OK:D3DERR_NOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter,D3DDEVTYPE type,D3DFORMAT src,
                                                          D3DFORMAT dst) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(dxgiFormat(src)==DXGI_FORMAT_UNKNOWN||dxgiFormat(dst)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter,D3DDEVTYPE type,D3DCAPS9* caps) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL||!caps)return D3DERR_INVALIDCALL;
        fillCaps(caps,type,adapter);
        return D3D_OK;
    }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return nullptr; }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
                                           D3DPRESENT_PARAMETERS* parameters,IDirect3DDevice9** out) override {
        return createDevice(adapter,type,focus,flags,parameters,reinterpret_cast<void**>(out));
    }

    // ------------------------------------------------------------ IDirect3D9Ex
    UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT adapter,const D3DDISPLAYMODEFILTER* filter) override {
        return GetAdapterModeCount(adapter,filter?filter->Format:D3DFMT_X8R8G8B8);
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT adapter,const D3DDISPLAYMODEFILTER* filter,UINT index,
                                                 D3DDISPLAYMODEEX* mode) override {
        if(!mode)return D3DERR_INVALIDCALL;
        D3DDISPLAYMODE plain{};
        HRESULT hr=EnumAdapterModes(adapter,filter?filter->Format:D3DFMT_X8R8G8B8,index,&plain);
        if(FAILED(hr))return hr;
        mode->Size=sizeof(*mode);mode->Width=plain.Width;mode->Height=plain.Height;
        mode->RefreshRate=plain.RefreshRate;mode->Format=plain.Format;
        mode->ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT adapter,D3DDISPLAYMODEEX* mode,
                                                      D3DDISPLAYROTATION* rotation) override {
        if(!mode)return D3DERR_INVALIDCALL;
        D3DDISPLAYMODE plain{};
        HRESULT hr=GetAdapterDisplayMode(adapter,&plain);
        if(FAILED(hr))return hr;
        mode->Size=sizeof(*mode);mode->Width=plain.Width;mode->Height=plain.Height;
        mode->RefreshRate=plain.RefreshRate;mode->Format=plain.Format;
        mode->ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
        if(rotation)*rotation=D3DDISPLAYROTATION_IDENTITY;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
                                             D3DPRESENT_PARAMETERS* parameters,D3DDISPLAYMODEEX*,
                                             IDirect3DDevice9Ex** out) override {
        return createDevice(adapter,type,focus,flags,parameters,reinterpret_cast<void**>(out));
    }
    HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT adapter,LUID* luid) override {
        if(!luid||adapter!=0)return D3DERR_INVALIDCALL;
        if(FAILED(init()))return D3DERR_NOTAVAILABLE;
        IDXGIAdapter* dxgiAdapter=nullptr;
        if(FAILED(factory->EnumAdapters(adapter,&dxgiAdapter))||!dxgiAdapter)return D3DERR_NOTAVAILABLE;
        DXGI_ADAPTER_DESC desc{};
        HRESULT hr=dxgiAdapter->GetDesc(&desc);
        dxgiAdapter->Release();
        if(FAILED(hr))return D3DERR_NOTAVAILABLE;
        *luid=desc.AdapterLuid;
        return D3D_OK;
    }

    HRESULT createDevice(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
                         D3DPRESENT_PARAMETERS* parameters,void** out) {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(adapter!=0||!parameters)return D3DERR_INVALIDCALL;
        if(type!=D3DDEVTYPE_HAL) {
            d3dshim::noteOnce("D3D9 shim: only D3DDEVTYPE_HAL is available; REF and NULLREF are refused");
            return D3DERR_INVALIDCALL;
        }
        if(FAILED(init()))return D3DERR_NOTAVAILABLE;
        auto device=d3dshim::create<Device>();
        if(!device)return E_OUTOFMEMORY;
        static const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,D3D_FEATURE_LEVEL_9_3};
        HRESULT hr=device->host.create(nullptr,D3D_DRIVER_TYPE_HARDWARE,levels,4,0);
        if(FAILED(hr)){d3dshim::destroy(device);return D3DERR_NOTAVAILABLE;}
        device->creation.AdapterOrdinal=adapter;
        device->creation.DeviceType=type;
        device->creation.hFocusWindow=focus;
        device->creation.BehaviorFlags=flags;
        device->deviceType=type;
        device->parent=this;
        AddRef();   // the device holds the factory it came from
        hr=device->createSwapChain(parameters);
        if(FAILED(hr)) {
            d3dshim::destroy(device);   // releases the factory reference above
            return hr;
        }
        device->present=*parameters;
        *out=device;
        return D3D_OK;
    }
};
}

extern "C" {
// ------------------------------------------------------------------- d3d9.dll
__declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    if(sdk!=D3D_SDK_VERSION) {
        d3dshim::noteOnce("D3D9 shim: Direct3DCreate9 refused an SDK version other than D3D_SDK_VERSION");
        return nullptr;
    }
    auto factory=d3dshim::create<Factory>();
    if(!factory)return nullptr;
    if(FAILED(factory->init())){d3dshim::destroy(factory);return nullptr;}
    return static_cast<IDirect3D9*>(static_cast<IDirect3D9Ex*>(factory));
}
__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(sdk!=D3D_SDK_VERSION)return D3DERR_INVALIDCALL;
    auto factory=d3dshim::create<Factory>();
    if(!factory)return E_OUTOFMEMORY;
    if(FAILED(factory->init())){d3dshim::destroy(factory);return D3DERR_NOTAVAILABLE;}
    *out=factory;
    return D3D_OK;
}
// The D3DPERF family is the PIX instrumentation API; a release runtime answers
// without recording, which is what Wine's does too.
__declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus() { return 0; }
__declspec(dllexport) WINBOOL WINAPI D3DPERF_QueryRepeatFrame() { return FALSE; }
__declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD) {}
__declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR,const wchar_t*) { return 0; }
__declspec(dllexport) int WINAPI D3DPERF_EndEvent() { return 0; }
__declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR,const wchar_t*) {}
__declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR,const wchar_t*) {}
__declspec(dllexport) void WINAPI DebugSetLevel(DWORD) {}
__declspec(dllexport) void WINAPI DebugSetMute() {}
__declspec(dllexport) void* WINAPI Direct3DShaderValidatorCreate9() { return nullptr; }
}
