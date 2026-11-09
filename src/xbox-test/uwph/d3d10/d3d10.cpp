// SPDX-License-Identifier: GPL-3.0-or-later
// Play's D3D10 shim: d3d10.dll, d3d10_1.dll and d3d10core.dll on the console's
// D3D11.
//
// Built by build.sh as a CRT-free mingw PE DLL and mapped by Play's loader, so
// its d3d11.dll/dxgi.dll imports bind to Play's routes: D3D11CreateDevice
// reaches Play's hook (guest adapter unwrapped, device adopted for guest-window
// swapchains) and CreateDXGIFactory1 reaches the validated CoreWindow factory.
// See ../d3dshim/base.h for why Wine's d3d10core cannot serve this platform.
//
// Every object is a thin proxy: the D3D10 interface the guest holds wraps one
// D3D11 object, and the descriptor conversions below are the only real work.
// They were checked member by member against the two SDK headers; the
// static_asserts make a header change that breaks one fail the build.
//
// Deliberate gaps, reported through the launch log rather than faked:
//   * The FX10 runtime (D3D10CreateEffectFromMemory, D3D10CreateEffectPool-
//     FromMemory, D3D10CompileEffectFromMemory) returns E_NOTIMPL. Wine's is a
//     345 KB parser welded to its own device; nothing here could run it.
//   * Performance counters (CreateCounter/CheckCounter) return E_NOTIMPL.
//   * OpenSharedResource returns E_NOTIMPL: the console exposes no shareable
//     cross-process surfaces.
//   * D3D10GetVersion, D3D10RegisterLayers, D3D10PreprocessShader,
//     D3D10DisassembleEffect and RevertToOldImplementation are export stubs, as
//     Wine's are.
//   * The three DLL names are one image with the union of their exports, so a
//     guest's d3d10_1.dll forwards resolve in the same module.
#include <windows.h>
#include <d3d10.h>
#include <d3d10_1.h>
#include <d3d10misc.h>
#include <d3d10effect.h>
#include <d3d10shader.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11shader.h>
#include <dxgi.h>
#include "../d3dshim/base.h"

namespace {
// ---------------------------------------------------------------- conversions
UINT miscFlags(UINT flags) {
    UINT out=flags&0x7;   // GENERATE_MIPS, SHARED and TEXTURECUBE share their bits
    if(flags&D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX) out|=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if(flags&D3D10_RESOURCE_MISC_GDI_COMPATIBLE) out|=D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    return out;
}
D3D11_BUFFER_DESC bufferDesc(const D3D10_BUFFER_DESC& in) {
    D3D11_BUFFER_DESC out{};
    out.ByteWidth=in.ByteWidth;out.Usage=D3D11_USAGE(in.Usage);out.BindFlags=in.BindFlags;
    out.CPUAccessFlags=in.CPUAccessFlags;out.MiscFlags=miscFlags(in.MiscFlags);
    out.StructureByteStride=0;   // D3D10 has no structured buffers
    return out;
}
D3D11_TEXTURE1D_DESC texture1DDesc(const D3D10_TEXTURE1D_DESC& in) {
    D3D11_TEXTURE1D_DESC out{};
    out.Width=in.Width;out.MipLevels=in.MipLevels;out.ArraySize=in.ArraySize;out.Format=in.Format;
    out.Usage=D3D11_USAGE(in.Usage);out.BindFlags=in.BindFlags;out.CPUAccessFlags=in.CPUAccessFlags;
    out.MiscFlags=miscFlags(in.MiscFlags);
    return out;
}
D3D11_TEXTURE2D_DESC texture2DDesc(const D3D10_TEXTURE2D_DESC& in) {
    D3D11_TEXTURE2D_DESC out{};
    out.Width=in.Width;out.Height=in.Height;out.MipLevels=in.MipLevels;out.ArraySize=in.ArraySize;
    out.Format=in.Format;out.SampleDesc=in.SampleDesc;out.Usage=D3D11_USAGE(in.Usage);
    out.BindFlags=in.BindFlags;out.CPUAccessFlags=in.CPUAccessFlags;out.MiscFlags=miscFlags(in.MiscFlags);
    return out;
}
D3D11_TEXTURE3D_DESC texture3DDesc(const D3D10_TEXTURE3D_DESC& in) {
    D3D11_TEXTURE3D_DESC out{};
    out.Width=in.Width;out.Height=in.Height;out.Depth=in.Depth;out.MipLevels=in.MipLevels;
    out.Format=in.Format;out.Usage=D3D11_USAGE(in.Usage);out.BindFlags=in.BindFlags;
    out.CPUAccessFlags=in.CPUAccessFlags;out.MiscFlags=miscFlags(in.MiscFlags);
    return out;
}
// The D3D11 descriptors add fields at the end (BufferEx, TextureCubeArray) and,
// for depth-stencil, a Flags word after ViewDimension. A zeroed destination
// plus the D3D10 bytes leaves every added field at its default.
template<class D,class S> inline D extended(const S& in) {
    static_assert(sizeof(D)>=sizeof(S),"D3D10 descriptor must fit the D3D11 one");
    D out{};memcpy(&out,&in,sizeof in);return out;
}
D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(const D3D10_DEPTH_STENCIL_VIEW_DESC& in) {
    D3D11_DEPTH_STENCIL_VIEW_DESC out{};
    out.Format=in.Format;out.ViewDimension=D3D11_DSV_DIMENSION(in.ViewDimension);out.Flags=0;
    // The two unions start at the same offset and every member is at most the
    // 12-byte Texture2DArray form; the D3D11 union only adds members after it.
    memcpy(&out.Texture1D,&in.Texture1D,sizeof in.Texture1D);
    return out;
}
D3D11_BLEND_DESC1 blendDesc1(const D3D10_BLEND_DESC1& in) {
    D3D11_BLEND_DESC1 out{};
    out.AlphaToCoverageEnable=in.AlphaToCoverageEnable;
    out.IndependentBlendEnable=in.IndependentBlendEnable;
    for(unsigned i=0;i<D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT;++i) {
        auto& rt=out.RenderTarget[i];auto& src=in.RenderTarget[i];
        rt.BlendEnable=src.BlendEnable;rt.LogicOpEnable=FALSE;
        rt.SrcBlend=D3D11_BLEND(src.SrcBlend);rt.DestBlend=D3D11_BLEND(src.DestBlend);
        rt.BlendOp=D3D11_BLEND_OP(src.BlendOp);rt.SrcBlendAlpha=D3D11_BLEND(src.SrcBlendAlpha);
        rt.DestBlendAlpha=D3D11_BLEND(src.DestBlendAlpha);rt.BlendOpAlpha=D3D11_BLEND_OP(src.BlendOpAlpha);
        rt.LogicOp=D3D11_LOGIC_OP(0);rt.RenderTargetWriteMask=src.RenderTargetWriteMask;
    }
    return out;
}
// D3D10_VIEWPORT holds integer corners; D3D11_VIEWPORT holds floats.
void viewport(D3D11_VIEWPORT& out,const D3D10_VIEWPORT& in) {
    out.TopLeftX=FLOAT(in.TopLeftX);out.TopLeftY=FLOAT(in.TopLeftY);
    out.Width=FLOAT(in.Width);out.Height=FLOAT(in.Height);
    out.MinDepth=in.MinDepth;out.MaxDepth=in.MaxDepth;
}
void viewport(D3D10_VIEWPORT& out,const D3D11_VIEWPORT& in) {
    out.TopLeftX=INT(in.TopLeftX);out.TopLeftY=INT(in.TopLeftY);
    out.Width=UINT(in.Width);out.Height=UINT(in.Height);
    out.MinDepth=in.MinDepth;out.MaxDepth=in.MaxDepth;
}
// D3D10_DRIVER_TYPE and D3D_DRIVER_TYPE number their members differently, so
// this is a switch rather than a cast.
D3D_DRIVER_TYPE driverType(D3D10_DRIVER_TYPE type) {
    switch(type) {
    case D3D10_DRIVER_TYPE_HARDWARE:return D3D_DRIVER_TYPE_HARDWARE;
    case D3D10_DRIVER_TYPE_WARP:return D3D_DRIVER_TYPE_WARP;
    case D3D10_DRIVER_TYPE_NULL:return D3D_DRIVER_TYPE_NULL;
    // The console has no reference rasterizer and no swrast module; WARP is the
    // software rasterizer the platform does have.
    case D3D10_DRIVER_TYPE_REFERENCE:
        d3dshim::noteOnce("D3D10 shim: no reference rasterizer on this platform; using WARP");
        return D3D_DRIVER_TYPE_WARP;
    case D3D10_DRIVER_TYPE_SOFTWARE:
        d3dshim::noteOnce("D3D10 shim: no software rasterizer module on this platform; using WARP");
        return D3D_DRIVER_TYPE_WARP;
    default:return D3D_DRIVER_TYPE_HARDWARE;
    }
}
UINT createFlags(UINT flags) {
    UINT out=0;
    if(flags&D3D10_CREATE_DEVICE_SINGLETHREADED)out|=D3D11_CREATE_DEVICE_SINGLETHREADED;
    if(flags&D3D10_CREATE_DEVICE_DEBUG)out|=D3D11_CREATE_DEVICE_DEBUG;
    if(flags&D3D10_CREATE_DEVICE_SWITCH_TO_REF)out|=D3D11_CREATE_DEVICE_SWITCH_TO_REF;
    if(flags&D3D10_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS)
        out|=D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    if(flags&D3D10_CREATE_DEVICE_BGRA_SUPPORT)out|=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if(flags&D3D10_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY)
        out|=D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY;
    // The two SDKs number this flag differently (D3D10 0x400, D3D11 0x40).
    if(flags&D3D10_CREATE_DEVICE_DEBUGGABLE)out|=D3D11_CREATE_DEVICE_DEBUGGABLE;
    // ALLOW_NULL_FROM_MAP and STRICT_VALIDATION have no D3D11 equivalent.
    if(flags&D3D10_CREATE_DEVICE_ALLOW_NULL_FROM_MAP)
        d3dshim::noteOnce("D3D10 shim: ALLOW_NULL_FROM_MAP has no D3D11 equivalent and is not applied");
    if(flags&D3D10_CREATE_DEVICE_STRICT_VALIDATION)
        d3dshim::noteOnce("D3D10 shim: STRICT_VALIDATION has no D3D11 equivalent and is not applied");
    return out;
}
D3D_FEATURE_LEVEL featureLevel(D3D10_FEATURE_LEVEL1 level) {
    switch(level) {
    case D3D10_FEATURE_LEVEL_10_1:return D3D_FEATURE_LEVEL_10_1;
    case D3D10_FEATURE_LEVEL_9_3:return D3D_FEATURE_LEVEL_9_3;
    case D3D10_FEATURE_LEVEL_9_2:return D3D_FEATURE_LEVEL_9_2;
    case D3D10_FEATURE_LEVEL_9_1:return D3D_FEATURE_LEVEL_9_1;
    default:return D3D_FEATURE_LEVEL_10_0;
    }
}
D3D10_FEATURE_LEVEL1 featureLevel1(D3D_FEATURE_LEVEL level) {
    switch(level) {
    case D3D_FEATURE_LEVEL_10_1:return D3D10_FEATURE_LEVEL_10_1;
    case D3D_FEATURE_LEVEL_9_3:return D3D10_FEATURE_LEVEL_9_3;
    case D3D_FEATURE_LEVEL_9_2:return D3D10_FEATURE_LEVEL_9_2;
    case D3D_FEATURE_LEVEL_9_1:return D3D10_FEATURE_LEVEL_9_1;
    default:return D3D10_FEATURE_LEVEL_10_0;
    }
}
// The D3D11 object behind a guest interface: our own wrapper answers baseId;
// anything else is a native D3D11 object the guest obtained from DXGI (a
// swapchain buffer), which D3D11 accepts unchanged. The reference taken by the
// probe is dropped again: the guest still owns the object it passed in.
template<class Inner> inline Inner* behind(IUnknown* object) {
    if(!object)return nullptr;
    if(auto inner=d3dshim::innerOf(object))return static_cast<Inner*>(inner);
    Inner* direct=nullptr;
    if(SUCCEEDED(object->QueryInterface(__uuidof(Inner),reinterpret_cast<void**>(&direct))))direct->Release();
    return direct;
}
// d3dcompiler is a system DLL on Windows 10; a game that ships its own copy is
// served that copy first by Play's loader. Never a hard dependency: the shim
// loads without it and only the shader-tool exports need it.
struct Compiler {
    HMODULE module=nullptr;bool tried=false;
    HMODULE get() {
        if(tried)return module;
        tried=true;
        module=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)d3dshim::noteOnce("D3D10 shim: d3dcompiler_47.dll unavailable; shader compilation/reflection refused");
        return module;
    }
    FARPROC proc(const char* name) {
        auto m=get();
        return m?GetProcAddress(m,name):nullptr;
    }
};
Compiler compiler;
}

namespace {
// A copy of the D3D10 prefix of a D3D11 descriptor (Get* readback).
template<class D,class S> inline D prefix(const S& in) {
    static_assert(sizeof(D)<=sizeof(S),"D3D10 descriptor must not exceed the D3D11 one");
    D out{};memcpy(&out,&in,sizeof out);return out;
}
// ------------------------------------------------------------- ID3D10Multithread
// D3D11's immediate context serializes its own calls unless the guest asked for
// SINGLETHREADED, so Enter/Leave have nothing to do; the honest answer to
// GetMultithreadProtected is whether that protection is in force.
struct Multithread : d3dshim::Base, ID3D10Multithread {
    Multithread() { self=static_cast<ID3D10Multithread*>(this); }
    D3DSHIM_REF
    D3DSHIM_QI
    void STDMETHODCALLTYPE Enter() override {}
    void STDMETHODCALLTYPE Leave() override {}
    WINBOOL STDMETHODCALLTYPE SetMultithreadProtected(WINBOOL protect) override {
        // D3D11 cannot switch this at runtime; report what is in force.
        protectedDevice=protect?TRUE:protectedDevice;return protectedDevice;
    }
    WINBOOL STDMETHODCALLTYPE GetMultithreadProtected() override { return protectedDevice; }
    WINBOOL protectedDevice=TRUE;
};

// --------------------------------------------------------------- shared bases
// Every child answers GetDevice with the shim device the guest created it from
// and forwards private data to the D3D11 object behind it.
template<class I> struct Child : d3dshim::Base, I {
    D3DSHIM_REF
    D3DSHIM_QI
    void STDMETHODCALLTYPE GetDevice(ID3D10Device** out) override {
        if(!out)return;
        *out=owner?static_cast<ID3D10Device*>(owner):nullptr;
        if(*out)(*out)->AddRef();
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT* size,void* data) override {
        return inner?inner->GetPrivateData(id,size,data):E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT size,const void* data) override {
        return inner?inner->SetPrivateData(id,size,data):E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown* data) override {
        return inner?inner->SetPrivateDataInterface(id,const_cast<IUnknown*>(data)):E_FAIL;
    }
};
template<class I> struct Resource : Child<I> {
    void STDMETHODCALLTYPE GetType(D3D10_RESOURCE_DIMENSION* type) override {
        if(!type)return;
        D3D11_RESOURCE_DIMENSION value=D3D11_RESOURCE_DIMENSION_UNKNOWN;
        if(this->inner)static_cast<ID3D11Resource*>(this->inner)->GetType(&value);
        *type=D3D10_RESOURCE_DIMENSION(value);
    }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT priority) override {
        if(this->inner)static_cast<ID3D11Resource*>(this->inner)->SetEvictionPriority(priority);
    }
    UINT STDMETHODCALLTYPE GetEvictionPriority() override {
        return this->inner?static_cast<ID3D11Resource*>(this->inner)->GetEvictionPriority():0;
    }
};
template<class I> struct View : Child<I> {
    void STDMETHODCALLTYPE GetResource(ID3D10Resource** out) override {
        if(!out)return;
        *out=nullptr;
        if(!this->inner)return;
        ID3D11Resource* resource=nullptr;
        static_cast<ID3D11View*>(this->inner)->GetResource(&resource);
        if(!resource)return;
        auto wrapper=d3dshim::wrap<ID3D10Resource>(resource);
        resource->Release();
        if(wrapper){wrapper->AddRef();*out=wrapper;}
    }
};
template<class I> struct Asynchronous : Child<I> {
    // D3D11 has no Begin/End/GetData on the asynchronous object: they are
    // context methods that take it, which is the same call the D3D10 runtime
    // makes.
    void STDMETHODCALLTYPE Begin() override {
        if(this->inner&&this->context)this->context->Begin(static_cast<ID3D11Asynchronous*>(this->inner));
    }
    void STDMETHODCALLTYPE End() override {
        if(this->inner&&this->context)this->context->End(static_cast<ID3D11Asynchronous*>(this->inner));
    }
    HRESULT STDMETHODCALLTYPE GetData(void* data,UINT size,UINT flags) override {
        if(!this->inner||!this->context)return E_FAIL;
        return this->context->GetData(static_cast<ID3D11Asynchronous*>(this->inner),data,size,flags);
    }
    UINT STDMETHODCALLTYPE GetDataSize() override {
        return this->inner?static_cast<ID3D11Asynchronous*>(this->inner)->GetDataSize():0;
    }
};

// ------------------------------------------------------------------- resources
struct Buffer : Resource<ID3D10Buffer> {
    Buffer() { self=static_cast<ID3D10Buffer*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10Buffer),&__uuidof(ID3D10Resource),&__uuidof(ID3D10DeviceChild)); }
    HRESULT STDMETHODCALLTYPE Map(D3D10_MAP type,UINT flags,void** data) override {
        if(!this->inner)return E_FAIL;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // D3D10's DO_NOT_WAIT shares D3D11_MAP_FLAG_DO_NOT_WAIT's value.
        HRESULT hr=context->Map(static_cast<ID3D11Resource*>(this->inner),0,D3D11_MAP(type),flags,&mapped);
        if(data)*data=mapped.pData;
        return hr;
    }
    void STDMETHODCALLTYPE Unmap() override {
        if(this->inner)context->Unmap(static_cast<ID3D11Resource*>(this->inner),0);
    }
    void STDMETHODCALLTYPE GetDesc(D3D10_BUFFER_DESC* desc) override {
        if(!desc)return;
        D3D11_BUFFER_DESC got{};
        if(this->inner)static_cast<ID3D11Buffer*>(this->inner)->GetDesc(&got);
        *desc=D3D10_BUFFER_DESC{};desc->ByteWidth=got.ByteWidth;desc->Usage=D3D10_USAGE(got.Usage);
        desc->BindFlags=got.BindFlags;desc->CPUAccessFlags=got.CPUAccessFlags;desc->MiscFlags=got.MiscFlags;
    }
};
struct Texture1D : Resource<ID3D10Texture1D> {
    Texture1D() { self=static_cast<ID3D10Texture1D*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10Texture1D),&__uuidof(ID3D10Resource),&__uuidof(ID3D10DeviceChild)); }
    HRESULT STDMETHODCALLTYPE Map(UINT subresource,D3D10_MAP type,UINT flags,void** data) override {
        if(!this->inner)return E_FAIL;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr=context->Map(static_cast<ID3D11Resource*>(this->inner),subresource,D3D11_MAP(type),flags,&mapped);
        if(data)*data=mapped.pData;
        return hr;
    }
    void STDMETHODCALLTYPE Unmap(UINT subresource) override {
        if(this->inner)context->Unmap(static_cast<ID3D11Resource*>(this->inner),subresource);
    }
    void STDMETHODCALLTYPE GetDesc(D3D10_TEXTURE1D_DESC* desc) override {
        if(!desc)return;
        D3D11_TEXTURE1D_DESC got{};
        if(this->inner)static_cast<ID3D11Texture1D*>(this->inner)->GetDesc(&got);
        *desc=D3D10_TEXTURE1D_DESC{};desc->Width=got.Width;desc->MipLevels=got.MipLevels;
        desc->ArraySize=got.ArraySize;desc->Format=got.Format;desc->Usage=D3D10_USAGE(got.Usage);
        desc->BindFlags=got.BindFlags;desc->CPUAccessFlags=got.CPUAccessFlags;desc->MiscFlags=got.MiscFlags;
    }
};
struct Texture2D : Resource<ID3D10Texture2D> {
    Texture2D() { self=static_cast<ID3D10Texture2D*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10Texture2D),&__uuidof(ID3D10Resource),&__uuidof(ID3D10DeviceChild)); }
    HRESULT STDMETHODCALLTYPE Map(UINT subresource,D3D10_MAP type,UINT flags,D3D10_MAPPED_TEXTURE2D* mapped) override {
        if(!this->inner)return E_FAIL;
        D3D11_MAPPED_SUBRESOURCE got{};
        HRESULT hr=context->Map(static_cast<ID3D11Resource*>(this->inner),subresource,D3D11_MAP(type),flags,&got);
        if(mapped){mapped->pData=got.pData;mapped->RowPitch=got.RowPitch;}
        return hr;
    }
    void STDMETHODCALLTYPE Unmap(UINT subresource) override {
        if(this->inner)context->Unmap(static_cast<ID3D11Resource*>(this->inner),subresource);
    }
    void STDMETHODCALLTYPE GetDesc(D3D10_TEXTURE2D_DESC* desc) override {
        if(!desc)return;
        D3D11_TEXTURE2D_DESC got{};
        if(this->inner)static_cast<ID3D11Texture2D*>(this->inner)->GetDesc(&got);
        *desc=D3D10_TEXTURE2D_DESC{};desc->Width=got.Width;desc->Height=got.Height;desc->MipLevels=got.MipLevels;
        desc->ArraySize=got.ArraySize;desc->Format=got.Format;desc->SampleDesc=got.SampleDesc;
        desc->Usage=D3D10_USAGE(got.Usage);desc->BindFlags=got.BindFlags;
        desc->CPUAccessFlags=got.CPUAccessFlags;desc->MiscFlags=got.MiscFlags;
    }
};
struct Texture3D : Resource<ID3D10Texture3D> {
    Texture3D() { self=static_cast<ID3D10Texture3D*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10Texture3D),&__uuidof(ID3D10Resource),&__uuidof(ID3D10DeviceChild)); }
    HRESULT STDMETHODCALLTYPE Map(UINT subresource,D3D10_MAP type,UINT flags,D3D10_MAPPED_TEXTURE3D* mapped) override {
        if(!this->inner)return E_FAIL;
        D3D11_MAPPED_SUBRESOURCE got{};
        HRESULT hr=context->Map(static_cast<ID3D11Resource*>(this->inner),subresource,D3D11_MAP(type),flags,&got);
        if(mapped){mapped->pData=got.pData;mapped->RowPitch=got.RowPitch;mapped->DepthPitch=got.DepthPitch;}
        return hr;
    }
    void STDMETHODCALLTYPE Unmap(UINT subresource) override {
        if(this->inner)context->Unmap(static_cast<ID3D11Resource*>(this->inner),subresource);
    }
    void STDMETHODCALLTYPE GetDesc(D3D10_TEXTURE3D_DESC* desc) override {
        if(!desc)return;
        D3D11_TEXTURE3D_DESC got{};
        if(this->inner)static_cast<ID3D11Texture3D*>(this->inner)->GetDesc(&got);
        *desc=D3D10_TEXTURE3D_DESC{};desc->Width=got.Width;desc->Height=got.Height;desc->Depth=got.Depth;
        desc->MipLevels=got.MipLevels;desc->Format=got.Format;desc->Usage=D3D10_USAGE(got.Usage);
        desc->BindFlags=got.BindFlags;desc->CPUAccessFlags=got.CPUAccessFlags;desc->MiscFlags=got.MiscFlags;
    }
};

// ----------------------------------------------------------------------- views
struct ShaderResourceView : View<ID3D10ShaderResourceView1> {
    ShaderResourceView() { self=static_cast<ID3D10ShaderResourceView1*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10ShaderResourceView1),&__uuidof(ID3D10ShaderResourceView),
                    &__uuidof(ID3D10View),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_SHADER_RESOURCE_VIEW_DESC* desc) override {
        if(desc)*desc=prefix<D3D10_SHADER_RESOURCE_VIEW_DESC>(value);
    }
    void STDMETHODCALLTYPE GetDesc1(D3D10_SHADER_RESOURCE_VIEW_DESC1* desc) override { if(desc)*desc=value; }
    D3D10_SHADER_RESOURCE_VIEW_DESC1 value{};
};
struct RenderTargetView : View<ID3D10RenderTargetView> {
    RenderTargetView() { self=static_cast<ID3D10RenderTargetView*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10RenderTargetView),&__uuidof(ID3D10View),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_RENDER_TARGET_VIEW_DESC* desc) override { if(desc)*desc=value; }
    D3D10_RENDER_TARGET_VIEW_DESC value{};
};
struct DepthStencilView : View<ID3D10DepthStencilView> {
    DepthStencilView() { self=static_cast<ID3D10DepthStencilView*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10DepthStencilView),&__uuidof(ID3D10View),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_DEPTH_STENCIL_VIEW_DESC* desc) override { if(desc)*desc=value; }
    D3D10_DEPTH_STENCIL_VIEW_DESC value{};
};

// ------------------------------------------------------- shaders and pipelines
struct VertexShader : Child<ID3D10VertexShader> {
    VertexShader() { self=static_cast<ID3D10VertexShader*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10VertexShader),&__uuidof(ID3D10DeviceChild)); }
};
struct GeometryShader : Child<ID3D10GeometryShader> {
    GeometryShader() { self=static_cast<ID3D10GeometryShader*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10GeometryShader),&__uuidof(ID3D10DeviceChild)); }
};
struct PixelShader : Child<ID3D10PixelShader> {
    PixelShader() { self=static_cast<ID3D10PixelShader*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10PixelShader),&__uuidof(ID3D10DeviceChild)); }
};
struct InputLayout : Child<ID3D10InputLayout> {
    InputLayout() { self=static_cast<ID3D10InputLayout*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10InputLayout),&__uuidof(ID3D10DeviceChild)); }
};

// ---------------------------------------------------------------------- states
struct BlendState : Child<ID3D10BlendState1> {
    BlendState() { self=static_cast<ID3D10BlendState1*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10BlendState1),&__uuidof(ID3D10BlendState),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_BLEND_DESC* desc) override {
        if(!desc)return;
        *desc=D3D10_BLEND_DESC{};
        desc->AlphaToCoverageEnable=value.AlphaToCoverageEnable;
        for(unsigned i=0;i<D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT;++i) {
            desc->BlendEnable[i]=value.RenderTarget[i].BlendEnable;
            desc->RenderTargetWriteMask[i]=value.RenderTarget[i].RenderTargetWriteMask;
        }
        desc->SrcBlend=value.RenderTarget[0].SrcBlend;desc->DestBlend=value.RenderTarget[0].DestBlend;
        desc->BlendOp=value.RenderTarget[0].BlendOp;desc->SrcBlendAlpha=value.RenderTarget[0].SrcBlendAlpha;
        desc->DestBlendAlpha=value.RenderTarget[0].DestBlendAlpha;desc->BlendOpAlpha=value.RenderTarget[0].BlendOpAlpha;
    }
    void STDMETHODCALLTYPE GetDesc1(D3D10_BLEND_DESC1* desc) override { if(desc)*desc=value; }
    D3D10_BLEND_DESC1 value{};
};
struct DepthStencilState : Child<ID3D10DepthStencilState> {
    DepthStencilState() { self=static_cast<ID3D10DepthStencilState*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10DepthStencilState),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_DEPTH_STENCIL_DESC* desc) override { if(desc)*desc=value; }
    D3D10_DEPTH_STENCIL_DESC value{};
};
struct RasterizerState : Child<ID3D10RasterizerState> {
    RasterizerState() { self=static_cast<ID3D10RasterizerState*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10RasterizerState),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_RASTERIZER_DESC* desc) override { if(desc)*desc=value; }
    D3D10_RASTERIZER_DESC value{};
};
struct SamplerState : Child<ID3D10SamplerState> {
    SamplerState() { self=static_cast<ID3D10SamplerState*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10SamplerState),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_SAMPLER_DESC* desc) override { if(desc)*desc=value; }
    D3D10_SAMPLER_DESC value{};
};

// --------------------------------------------------------------------- queries
struct Query : Asynchronous<ID3D10Query> {
    Query() { self=static_cast<ID3D10Query*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10Query),&__uuidof(ID3D10Asynchronous),&__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_QUERY_DESC* desc) override { if(desc)*desc=value; }
    D3D10_QUERY_DESC value{};
};
struct Predicate : Asynchronous<ID3D10Predicate> {
    Predicate() { self=static_cast<ID3D10Predicate*>(this);
        D3DSHIM_IDS(&__uuidof(ID3D10Predicate),&__uuidof(ID3D10Query),&__uuidof(ID3D10Asynchronous),
                    &__uuidof(ID3D10DeviceChild)); }
    void STDMETHODCALLTYPE GetDesc(D3D10_QUERY_DESC* desc) override { if(desc)*desc=value; }
    D3D10_QUERY_DESC value{};
};
}

namespace {
// Readback hands the guest the wrapper it already holds; an object the shim did
// not create (a swapchain buffer) is wrapped on demand. The reference D3D11
// handed out moves into the wrapper, or is dropped when one already exists.
template<class T,class Inner,class Guest> inline void adopt(Inner* object,Guest** out) {
    if(!out)return;
    *out=nullptr;
    if(!object)return;
    if(auto existing=d3dshim::wrap<Guest>(object)){object->Release();existing->AddRef();*out=existing;return;}
    auto wrapper=d3dshim::create<T>();
    if(!wrapper){object->Release();return;}
    wrapper->inner=object;
    wrapper->forward=object;
    d3dshim::attach(object,wrapper);
    *out=static_cast<Guest*>(wrapper->self);
}
inline void adopt(ID3D11Buffer* o,ID3D10Buffer** out) { adopt<Buffer>(o,out); }
inline void adopt(ID3D11ShaderResourceView* o,ID3D10ShaderResourceView** out) { adopt<ShaderResourceView>(o,out); }
inline void adopt(ID3D11SamplerState* o,ID3D10SamplerState** out) { adopt<SamplerState>(o,out); }
inline void adopt(ID3D11VertexShader* o,ID3D10VertexShader** out) { adopt<VertexShader>(o,out); }
inline void adopt(ID3D11PixelShader* o,ID3D10PixelShader** out) { adopt<PixelShader>(o,out); }
inline void adopt(ID3D11GeometryShader* o,ID3D10GeometryShader** out) { adopt<GeometryShader>(o,out); }
inline void adopt(ID3D11InputLayout* o,ID3D10InputLayout** out) { adopt<InputLayout>(o,out); }
inline void adopt(ID3D11RenderTargetView* o,ID3D10RenderTargetView** out) { adopt<RenderTargetView>(o,out); }
inline void adopt(ID3D11DepthStencilView* o,ID3D10DepthStencilView** out) { adopt<DepthStencilView>(o,out); }
inline void adopt(ID3D11BlendState* o,ID3D10BlendState** out) { adopt<BlendState>(o,out); }
inline void adopt(ID3D11DepthStencilState* o,ID3D10DepthStencilState** out) { adopt<DepthStencilState>(o,out); }
inline void adopt(ID3D11RasterizerState* o,ID3D10RasterizerState** out) { adopt<RasterizerState>(o,out); }
inline void adopt(ID3D11Predicate* o,ID3D10Predicate** out) { adopt<Predicate>(o,out); }
}

namespace {
struct Device : d3dshim::Base, ID3D10Device1 {
    d3dshim::Host host;
    UINT creationFlags=0;
    // Wine's d3d11 context does not reference what is bound to it (real D3D11
    // does), so the shim holds those references for as long as the slot is
    // bound. See d3dshim::Held.
    d3dshim::Held<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> vsConstantBuffers;
    d3dshim::Held<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> psConstantBuffers;
    d3dshim::Held<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> gsConstantBuffers;
    d3dshim::Held<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> vsShaderResources;
    d3dshim::Held<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> psShaderResources;
    d3dshim::Held<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> gsShaderResources;
    d3dshim::Held<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT> vsSamplers;
    d3dshim::Held<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT> psSamplers;
    d3dshim::Held<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT> gsSamplers;
    d3dshim::Held<1> vsShader, psShader, gsShader, inputLayout, indexBuffer, depthStencilState, blendState,
        rasterizerState, depthStencilView, predicate;
    d3dshim::Held<D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexBuffers;
    d3dshim::Held<D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets;
    d3dshim::Held<D3D10_SO_BUFFER_SLOT_COUNT> streamOutput;
    Multithread* multithreadObject=nullptr;
    Device() { self=static_cast<ID3D10Device1*>(this); }
    ~Device() {
        if(multithreadObject)multithreadObject->Release();
        host.destroy();
    }
    ID3D11Device* device() const { return host.device; }
    ID3D11DeviceContext* context() const { return host.context; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(IsEqualGUID(id,d3dshim::baseId)){*out=static_cast<d3dshim::Base*>(this);return S_OK;}
        if(IsEqualGUID(id,__uuidof(IUnknown))||IsEqualGUID(id,__uuidof(ID3D10Device))||
           IsEqualGUID(id,__uuidof(ID3D10Device1))) {
            *out=static_cast<ID3D10Device1*>(this);AddRef();return S_OK;
        }
        if(IsEqualGUID(id,__uuidof(ID3D10Multithread))) {
            if(!multithreadObject) {
                multithreadObject=d3dshim::create<Multithread>();
                if(!multithreadObject)return E_OUTOFMEMORY;
                multithreadObject->forward=static_cast<ID3D10Device1*>(this);
                multithreadObject->protectedDevice=!(creationFlags&D3D10_CREATE_DEVICE_SINGLETHREADED);
            }
            multithreadObject->AddRef();*out=multithreadObject;return S_OK;
        }
        // Wine's d3d10 device forwards everything else to the D3D11 device, so
        // ID3D11Device/IDXGIDevice and its private interfaces stay reachable.
        return device()?device()->QueryInterface(id,out):E_NOINTERFACE;
    }
    D3DSHIM_REF

    // ---- pipeline state: unwrap the guest's objects, forward everything else
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT start,UINT count,ID3D10Buffer* const* values) override {
        ID3D11Buffer* list[16]{};UINT n=unwrap(values,count,list);
        vsConstantBuffers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->VSSetConstantBuffers(start,n,list);
    }
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT start,UINT count,ID3D10Buffer* const* values) override {
        ID3D11Buffer* list[16]{};UINT n=unwrap(values,count,list);
        psConstantBuffers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->PSSetConstantBuffers(start,n,list);
    }
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT start,UINT count,ID3D10Buffer* const* values) override {
        ID3D11Buffer* list[16]{};UINT n=unwrap(values,count,list);
        gsConstantBuffers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->GSSetConstantBuffers(start,n,list);
    }
    void STDMETHODCALLTYPE VSSetShaderResources(UINT start,UINT count,ID3D10ShaderResourceView* const* values) override {
        ID3D11ShaderResourceView* list[128]{};UINT n=unwrap(values,count,list);
        vsShaderResources.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->VSSetShaderResources(start,n,list);
    }
    void STDMETHODCALLTYPE PSSetShaderResources(UINT start,UINT count,ID3D10ShaderResourceView* const* values) override {
        ID3D11ShaderResourceView* list[128]{};UINT n=unwrap(values,count,list);
        psShaderResources.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->PSSetShaderResources(start,n,list);
    }
    void STDMETHODCALLTYPE GSSetShaderResources(UINT start,UINT count,ID3D10ShaderResourceView* const* values) override {
        ID3D11ShaderResourceView* list[128]{};UINT n=unwrap(values,count,list);
        gsShaderResources.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->GSSetShaderResources(start,n,list);
    }
    void STDMETHODCALLTYPE VSSetSamplers(UINT start,UINT count,ID3D10SamplerState* const* values) override {
        ID3D11SamplerState* list[16]{};UINT n=unwrap(values,count,list);
        vsSamplers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->VSSetSamplers(start,n,list);
    }
    void STDMETHODCALLTYPE PSSetSamplers(UINT start,UINT count,ID3D10SamplerState* const* values) override {
        ID3D11SamplerState* list[16]{};UINT n=unwrap(values,count,list);
        psSamplers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->PSSetSamplers(start,n,list);
    }
    void STDMETHODCALLTYPE GSSetSamplers(UINT start,UINT count,ID3D10SamplerState* const* values) override {
        ID3D11SamplerState* list[16]{};UINT n=unwrap(values,count,list);
        gsSamplers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->GSSetSamplers(start,n,list);
    }
    void STDMETHODCALLTYPE VSSetShader(ID3D10VertexShader* value) override {
        auto object=behind<ID3D11VertexShader>(value);
        vsShader.set(0,object);context()->VSSetShader(object,nullptr,0);
    }
    void STDMETHODCALLTYPE PSSetShader(ID3D10PixelShader* value) override {
        auto object=behind<ID3D11PixelShader>(value);
        psShader.set(0,object);context()->PSSetShader(object,nullptr,0);
    }
    void STDMETHODCALLTYPE GSSetShader(ID3D10GeometryShader* value) override {
        auto object=behind<ID3D11GeometryShader>(value);
        gsShader.set(0,object);context()->GSSetShader(object,nullptr,0);
    }
    void STDMETHODCALLTYPE IASetInputLayout(ID3D10InputLayout* value) override {
        auto object=behind<ID3D11InputLayout>(value);
        inputLayout.set(0,object);context()->IASetInputLayout(object);
    }
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT start,UINT count,ID3D10Buffer* const* values,const UINT* strides,const UINT* offsets) override {
        ID3D11Buffer* list[16]{};UINT n=unwrap(values,count,list);
        vertexBuffers.setRange(start,n,reinterpret_cast<IUnknown* const*>(list));
        context()->IASetVertexBuffers(start,n,list,strides,offsets);
    }
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D10Buffer* value,DXGI_FORMAT format,UINT offset) override {
        auto object=behind<ID3D11Buffer>(value);
        indexBuffer.set(0,object);context()->IASetIndexBuffer(object,format,offset);
    }
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY topology) override {
        context()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY(topology));
    }
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT count,ID3D10RenderTargetView* const* values,ID3D10DepthStencilView* depth) override {
        ID3D11RenderTargetView* list[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        UINT n=unwrap(values,count,list);
        auto depthView=behind<ID3D11DepthStencilView>(depth);
        renderTargets.setRange(0,n,reinterpret_cast<IUnknown* const*>(list));
        depthStencilView.set(0,depthView);
        context()->OMSetRenderTargets(n,list,depthView);
    }
    void STDMETHODCALLTYPE OMSetBlendState(ID3D10BlendState* state,const FLOAT factor[4],UINT mask) override {
        auto object=behind<ID3D11BlendState>(state);
        blendState.set(0,object);context()->OMSetBlendState(object,factor,mask);
    }
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D10DepthStencilState* state,UINT reference) override {
        auto object=behind<ID3D11DepthStencilState>(state);
        depthStencilState.set(0,object);context()->OMSetDepthStencilState(object,reference);
    }
    void STDMETHODCALLTYPE SOSetTargets(UINT count,ID3D10Buffer* const* values,const UINT* offsets) override {
        ID3D11Buffer* list[D3D10_SO_BUFFER_SLOT_COUNT]{};
        UINT n=unwrap(values,count,list);
        streamOutput.setRange(0,n,reinterpret_cast<IUnknown* const*>(list));
        context()->SOSetTargets(n,list,offsets);
    }
    void STDMETHODCALLTYPE RSSetState(ID3D10RasterizerState* state) override {
        auto object=behind<ID3D11RasterizerState>(state);
        rasterizerState.set(0,object);context()->RSSetState(object);
    }
    void STDMETHODCALLTYPE RSSetViewports(UINT count,const D3D10_VIEWPORT* values) override {
        D3D11_VIEWPORT list[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT n=count>D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE?D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE:count;
        for(UINT i=0;i<n;++i)viewport(list[i],values[i]);
        context()->RSSetViewports(n,list);
    }
    void STDMETHODCALLTYPE RSSetScissorRects(UINT count,const D3D10_RECT* values) override { context()->RSSetScissorRects(count,values); }
    void STDMETHODCALLTYPE SetPredication(ID3D10Predicate* value,WINBOOL keep) override {
        auto object=behind<ID3D11Predicate>(value);
        predicate.set(0,object);context()->SetPredication(object,keep);
    }

    // ---- draws and copies
    void STDMETHODCALLTYPE Draw(UINT vertices,UINT start) override { context()->Draw(vertices,start); }
    void STDMETHODCALLTYPE DrawIndexed(UINT indices,UINT start,INT base) override { context()->DrawIndexed(indices,start,base); }
    void STDMETHODCALLTYPE DrawInstanced(UINT vertices,UINT instances,UINT start,UINT instanceStart) override {
        context()->DrawInstanced(vertices,instances,start,instanceStart);
    }
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT indices,UINT instances,UINT start,INT base,UINT instanceStart) override {
        context()->DrawIndexedInstanced(indices,instances,start,base,instanceStart);
    }
    void STDMETHODCALLTYPE DrawAuto() override { context()->DrawAuto(); }
    // D3D10_BOX and D3D11_BOX are the same six UINT fields.
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D10Resource* dst,UINT dstSubresource,UINT x,UINT y,UINT z,
            ID3D10Resource* src,UINT srcSubresource,const D3D10_BOX* box) override {
        context()->CopySubresourceRegion(behind<ID3D11Resource>(dst),dstSubresource,x,y,z,
            behind<ID3D11Resource>(src),srcSubresource,reinterpret_cast<const D3D11_BOX*>(box));
    }
    void STDMETHODCALLTYPE CopyResource(ID3D10Resource* dst,ID3D10Resource* src) override {
        context()->CopyResource(behind<ID3D11Resource>(dst),behind<ID3D11Resource>(src));
    }
    void STDMETHODCALLTYPE UpdateSubresource(ID3D10Resource* dst,UINT subresource,const D3D10_BOX* box,
            const void* data,UINT rowPitch,UINT depthPitch) override {
        context()->UpdateSubresource(behind<ID3D11Resource>(dst),subresource,
            reinterpret_cast<const D3D11_BOX*>(box),data,rowPitch,depthPitch);
    }
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D10RenderTargetView* view,const FLOAT color[4]) override {
        context()->ClearRenderTargetView(behind<ID3D11RenderTargetView>(view),color);
    }
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D10DepthStencilView* view,UINT flags,FLOAT depth,UINT8 stencil) override {
        context()->ClearDepthStencilView(behind<ID3D11DepthStencilView>(view),flags,depth,stencil);
    }
    void STDMETHODCALLTYPE GenerateMips(ID3D10ShaderResourceView* view) override {
        context()->GenerateMips(behind<ID3D11ShaderResourceView>(view));
    }
    void STDMETHODCALLTYPE ResolveSubresource(ID3D10Resource* dst,UINT dstSubresource,
            ID3D10Resource* src,UINT srcSubresource,DXGI_FORMAT format) override {
        context()->ResolveSubresource(behind<ID3D11Resource>(dst),dstSubresource,
            behind<ID3D11Resource>(src),srcSubresource,format);
    }

    // ---- state readback: the guest gets the same wrapper objects back, and an
    // object the shim did not create (a swapchain buffer) is wrapped on demand.
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT start,UINT count,ID3D10Buffer** out) override {
        ID3D11Buffer* got[16]{};
        UINT n=count>16?16:count;
        context()->VSGetConstantBuffers(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT start,UINT count,ID3D10Buffer** out) override {
        ID3D11Buffer* got[16]{};
        UINT n=count>16?16:count;
        context()->PSGetConstantBuffers(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT start,UINT count,ID3D10Buffer** out) override {
        ID3D11Buffer* got[16]{};
        UINT n=count>16?16:count;
        context()->GSGetConstantBuffers(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE VSGetShaderResources(UINT start,UINT count,ID3D10ShaderResourceView** out) override {
        ID3D11ShaderResourceView* got[128]{};
        UINT n=count>128?128:count;
        context()->VSGetShaderResources(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE PSGetShaderResources(UINT start,UINT count,ID3D10ShaderResourceView** out) override {
        ID3D11ShaderResourceView* got[128]{};
        UINT n=count>128?128:count;
        context()->PSGetShaderResources(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE GSGetShaderResources(UINT start,UINT count,ID3D10ShaderResourceView** out) override {
        ID3D11ShaderResourceView* got[128]{};
        UINT n=count>128?128:count;
        context()->GSGetShaderResources(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE VSGetSamplers(UINT start,UINT count,ID3D10SamplerState** out) override {
        ID3D11SamplerState* got[16]{};
        UINT n=count>16?16:count;
        context()->VSGetSamplers(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE PSGetSamplers(UINT start,UINT count,ID3D10SamplerState** out) override {
        ID3D11SamplerState* got[16]{};
        UINT n=count>16?16:count;
        context()->PSGetSamplers(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE GSGetSamplers(UINT start,UINT count,ID3D10SamplerState** out) override {
        ID3D11SamplerState* got[16]{};
        UINT n=count>16?16:count;
        context()->GSGetSamplers(start,n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE VSGetShader(ID3D10VertexShader** out) override {
        ID3D11VertexShader* got=nullptr;context()->VSGetShader(&got,nullptr,nullptr);adopt(got,out);
    }
    void STDMETHODCALLTYPE PSGetShader(ID3D10PixelShader** out) override {
        ID3D11PixelShader* got=nullptr;context()->PSGetShader(&got,nullptr,nullptr);adopt(got,out);
    }
    void STDMETHODCALLTYPE GSGetShader(ID3D10GeometryShader** out) override {
        ID3D11GeometryShader* got=nullptr;context()->GSGetShader(&got,nullptr,nullptr);adopt(got,out);
    }
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D10InputLayout** out) override {
        ID3D11InputLayout* got=nullptr;context()->IAGetInputLayout(&got);adopt(got,out);
    }
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT start,UINT count,ID3D10Buffer** out,UINT* strides,UINT* offsets) override {
        ID3D11Buffer* got[16]{};
        UINT n=count>16?16:count;
        context()->IAGetVertexBuffers(start,n,got,strides,offsets);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
    }
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D10Buffer** out,DXGI_FORMAT* format,UINT* offset) override {
        ID3D11Buffer* got=nullptr;context()->IAGetIndexBuffer(&got,format,offset);adopt(got,out);
    }
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY* topology) override {
        D3D11_PRIMITIVE_TOPOLOGY got=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        context()->IAGetPrimitiveTopology(&got);
        if(topology)*topology=D3D10_PRIMITIVE_TOPOLOGY(got);
    }
    void STDMETHODCALLTYPE GetPredication(ID3D10Predicate** out,WINBOOL* keep) override {
        ID3D11Predicate* got=nullptr;context()->GetPredication(&got,keep);adopt(got,out);
    }
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT count,ID3D10RenderTargetView** out,ID3D10DepthStencilView** depth) override {
        ID3D11RenderTargetView* got[8]{};
        ID3D11DepthStencilView* dsv=nullptr;
        UINT n=count>8?8:count;
        context()->OMGetRenderTargets(n,got,&dsv);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
        if(depth)adopt(dsv,depth);
    }
    void STDMETHODCALLTYPE OMGetBlendState(ID3D10BlendState** out,FLOAT factor[4],UINT* mask) override {
        ID3D11BlendState* got=nullptr;context()->OMGetBlendState(&got,factor,mask);adopt(got,out);
    }
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D10DepthStencilState** out,UINT* reference) override {
        ID3D11DepthStencilState* got=nullptr;context()->OMGetDepthStencilState(&got,reference);adopt(got,out);
    }
    void STDMETHODCALLTYPE SOGetTargets(UINT count,ID3D10Buffer** out,UINT* offsets) override {
        ID3D11Buffer* got[4]{};
        UINT n=count>4?4:count;
        context()->SOGetTargets(n,got);
        for(UINT i=0;i<n;++i)adopt(got[i],&out[i]);
        // D3D11 keeps the stream-output offsets on the buffer's own descriptor,
        // so they are read back from there rather than from the context.
        for(UINT i=0;i<n;++i)if(offsets){
            offsets[i]=0;
            if(got[i]) {
                D3D11_BUFFER_DESC desc{};
                got[i]->GetDesc(&desc);
                offsets[i]=desc.StructureByteStride;
            }
        }
    }
    void STDMETHODCALLTYPE RSGetState(ID3D10RasterizerState** out) override {
        ID3D11RasterizerState* got=nullptr;context()->RSGetState(&got);adopt(got,out);
    }
    void STDMETHODCALLTYPE RSGetViewports(UINT* count,D3D10_VIEWPORT* out) override {
        if(!count)return;
        D3D11_VIEWPORT got[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT n=*count>D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE?D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE:*count;
        context()->RSGetViewports(&n,got);
        if(out)for(UINT i=0;i<n;++i)viewport(out[i],got[i]);
        *count=n;
    }
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* count,D3D10_RECT* out) override {
        if(!count)return;
        UINT n=*count>D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE?D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE:*count;
        context()->RSGetScissorRects(&n,out);
        *count=n;
    }

    // ---- device bookkeeping
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return device()->GetDeviceRemovedReason(); }
    // D3D10 exception mode is a debug-layer feature; Wine refuses it too.
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT) override { return E_NOTIMPL; }
    UINT STDMETHODCALLTYPE GetExceptionMode() override { return 0; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT* size,void* data) override { return device()->GetPrivateData(id,size,data); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT size,const void* data) override { return device()->SetPrivateData(id,size,data); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown* data) override { return device()->SetPrivateDataInterface(id,data); }
    void STDMETHODCALLTYPE ClearState() override {
        context()->ClearState();
        // The context dropped everything, so the shim's references go too.
        vsConstantBuffers.clear();psConstantBuffers.clear();gsConstantBuffers.clear();
        vsShaderResources.clear();psShaderResources.clear();gsShaderResources.clear();
        vsSamplers.clear();psSamplers.clear();gsSamplers.clear();
        vsShader.clear();psShader.clear();gsShader.clear();
        inputLayout.clear();indexBuffer.clear();depthStencilState.clear();blendState.clear();
        rasterizerState.clear();depthStencilView.clear();predicate.clear();
        vertexBuffers.clear();renderTargets.clear();streamOutput.clear();
    }
    void STDMETHODCALLTYPE Flush() override { context()->Flush(); }

    // ---- creation
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D10_BUFFER_DESC* desc,const D3D10_SUBRESOURCE_DATA* data,ID3D10Buffer** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        D3D11_BUFFER_DESC converted=bufferDesc(*desc);
        ID3D11Buffer* object=nullptr;
        HRESULT hr=device()->CreateBuffer(&converted,reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(data),&object);
        return finish<Buffer>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D10_TEXTURE1D_DESC* desc,const D3D10_SUBRESOURCE_DATA* data,ID3D10Texture1D** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        D3D11_TEXTURE1D_DESC converted=texture1DDesc(*desc);
        ID3D11Texture1D* object=nullptr;
        HRESULT hr=device()->CreateTexture1D(&converted,reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(data),&object);
        return finish<Texture1D>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D10_TEXTURE2D_DESC* desc,const D3D10_SUBRESOURCE_DATA* data,ID3D10Texture2D** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC converted=texture2DDesc(*desc);
        ID3D11Texture2D* object=nullptr;
        HRESULT hr=device()->CreateTexture2D(&converted,reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(data),&object);
        return finish<Texture2D>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D10_TEXTURE3D_DESC* desc,const D3D10_SUBRESOURCE_DATA* data,ID3D10Texture3D** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        D3D11_TEXTURE3D_DESC converted=texture3DDesc(*desc);
        ID3D11Texture3D* object=nullptr;
        HRESULT hr=device()->CreateTexture3D(&converted,reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(data),&object);
        return finish<Texture3D>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D10Resource* resource,const D3D10_SHADER_RESOURCE_VIEW_DESC* desc,ID3D10ShaderResourceView** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!resource)return E_INVALIDARG;
        D3D10_SHADER_RESOURCE_VIEW_DESC1 widened{};
        if(desc)widened=prefix<D3D10_SHADER_RESOURCE_VIEW_DESC1>(*desc);
        ID3D10ShaderResourceView1* object=nullptr;
        HRESULT hr=CreateShaderResourceView1(resource,desc?&widened:nullptr,&object);
        if(FAILED(hr))return hr;
        *out=static_cast<ID3D10ShaderResourceView*>(object);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(ID3D10Resource* resource,const D3D10_SHADER_RESOURCE_VIEW_DESC1* desc,ID3D10ShaderResourceView1** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!resource)return E_INVALIDARG;
        // D3D11's SRV descriptor is the D3D10_1 one plus BufferEx and the
        // TextureCubeArray member, both at the end (Wine casts the same way).
        D3D11_SHADER_RESOURCE_VIEW_DESC converted{};
        const D3D11_SHADER_RESOURCE_VIEW_DESC* use=nullptr;
        if(desc){converted=extended<D3D11_SHADER_RESOURCE_VIEW_DESC>(*desc);use=&converted;}
        ID3D11ShaderResourceView* object=nullptr;
        HRESULT hr=device()->CreateShaderResourceView(behind<ID3D11Resource>(resource),use,&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<ShaderResourceView>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        if(desc)wrapper->value=*desc;
        return finish<ShaderResourceView>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D10Resource* resource,const D3D10_RENDER_TARGET_VIEW_DESC* desc,ID3D10RenderTargetView** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!resource)return E_INVALIDARG;
        D3D11_RENDER_TARGET_VIEW_DESC converted{};
        const D3D11_RENDER_TARGET_VIEW_DESC* use=nullptr;
        if(desc){converted=extended<D3D11_RENDER_TARGET_VIEW_DESC>(*desc);use=&converted;}
        ID3D11RenderTargetView* object=nullptr;
        HRESULT hr=device()->CreateRenderTargetView(behind<ID3D11Resource>(resource),use,&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<RenderTargetView>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        if(desc)wrapper->value=*desc;
        return finish<RenderTargetView>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D10Resource* resource,const D3D10_DEPTH_STENCIL_VIEW_DESC* desc,ID3D10DepthStencilView** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!resource)return E_INVALIDARG;
        D3D11_DEPTH_STENCIL_VIEW_DESC converted{};
        const D3D11_DEPTH_STENCIL_VIEW_DESC* use=nullptr;
        if(desc){converted=depthStencilViewDesc(*desc);use=&converted;}
        ID3D11DepthStencilView* object=nullptr;
        HRESULT hr=device()->CreateDepthStencilView(behind<ID3D11Resource>(resource),use,&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<DepthStencilView>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        if(desc)wrapper->value=*desc;
        return finish<DepthStencilView>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D10_INPUT_ELEMENT_DESC* elements,UINT count,
            const void* bytecode,SIZE_T length,ID3D10InputLayout** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!elements||!bytecode)return E_INVALIDARG;
        if(count>32)return E_INVALIDARG;
        D3D11_INPUT_ELEMENT_DESC converted[32]{};
        for(UINT i=0;i<count;++i) {
            converted[i].SemanticName=elements[i].SemanticName;
            converted[i].SemanticIndex=elements[i].SemanticIndex;
            converted[i].Format=elements[i].Format;
            converted[i].InputSlot=elements[i].InputSlot;
            converted[i].AlignedByteOffset=elements[i].AlignedByteOffset;
            converted[i].InputSlotClass=D3D11_INPUT_CLASSIFICATION(elements[i].InputSlotClass);
            converted[i].InstanceDataStepRate=elements[i].InstanceDataStepRate;
        }
        ID3D11InputLayout* object=nullptr;
        HRESULT hr=device()->CreateInputLayout(converted,count,bytecode,length,&object);
        return finish<InputLayout>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void* bytecode,SIZE_T length,ID3D10VertexShader** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        ID3D11VertexShader* object=nullptr;
        HRESULT hr=device()->CreateVertexShader(bytecode,length,nullptr,&object);
        return finish<VertexShader>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void* bytecode,SIZE_T length,ID3D10PixelShader** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        ID3D11PixelShader* object=nullptr;
        HRESULT hr=device()->CreatePixelShader(bytecode,length,nullptr,&object);
        return finish<PixelShader>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void* bytecode,SIZE_T length,ID3D10GeometryShader** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        ID3D11GeometryShader* object=nullptr;
        HRESULT hr=device()->CreateGeometryShader(bytecode,length,nullptr,&object);
        return finish<GeometryShader>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void* bytecode,SIZE_T length,
            const D3D10_SO_DECLARATION_ENTRY* declarations,UINT count,UINT stride,ID3D10GeometryShader** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        // D3D10 has one output stream, so every entry names stream 0 and the
        // stride array D3D11 wants has a single element. Wine's rule: a stride
        // with no declarations, or any entry on another output slot, is invalid.
        if(!count&&stride)return E_INVALIDARG;
        if(count>32)return E_INVALIDARG;
        D3D11_SO_DECLARATION_ENTRY converted[32]{};
        UINT strideCount=1;
        for(UINT i=0;i<count;++i) {
            converted[i].Stream=0;
            converted[i].SemanticName=declarations[i].SemanticName;
            converted[i].SemanticIndex=declarations[i].SemanticIndex;
            converted[i].StartComponent=declarations[i].StartComponent;
            converted[i].ComponentCount=declarations[i].ComponentCount;
            converted[i].OutputSlot=declarations[i].OutputSlot;
            if(declarations[i].OutputSlot) {
                strideCount=0;
                if(stride)return E_INVALIDARG;
            }
        }
        ID3D11GeometryShader* object=nullptr;
        HRESULT hr=device()->CreateGeometryShaderWithStreamOutput(bytecode,length,converted,count,
            &stride,strideCount,0,nullptr,&object);
        return finish<GeometryShader>(object,out,hr);
    }
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D10_BLEND_DESC* desc,ID3D10BlendState** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        D3D10_BLEND_DESC1 widened{};
        widened.AlphaToCoverageEnable=desc->AlphaToCoverageEnable;
        // D3D10's single set of factors is target 0's; the per-target enable and
        // write mask carry over. IndependentBlendEnable follows the same rule as
        // the D3D11 conversion above.
        widened.IndependentBlendEnable=FALSE;
        for(unsigned i=0;i<D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT-1;++i)
            if(desc->BlendEnable[i]!=desc->BlendEnable[i+1]||desc->RenderTargetWriteMask[i]!=desc->RenderTargetWriteMask[i+1])
                widened.IndependentBlendEnable=TRUE;
        for(unsigned i=0;i<D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT;++i) {
            auto& rt=widened.RenderTarget[i];
            rt.BlendEnable=desc->BlendEnable[i];
            rt.SrcBlend=desc->SrcBlend;rt.DestBlend=desc->DestBlend;rt.BlendOp=desc->BlendOp;
            rt.SrcBlendAlpha=desc->SrcBlendAlpha;rt.DestBlendAlpha=desc->DestBlendAlpha;
            rt.BlendOpAlpha=desc->BlendOpAlpha;rt.RenderTargetWriteMask=desc->RenderTargetWriteMask[i];
        }
        ID3D10BlendState1* object=nullptr;
        HRESULT hr=CreateBlendState1(&widened,&object);
        if(FAILED(hr))return hr;
        *out=static_cast<ID3D10BlendState*>(object);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlendState1(const D3D10_BLEND_DESC1* desc,ID3D10BlendState1** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        D3D11_BLEND_DESC1 converted=blendDesc1(*desc);
        ID3D11BlendState* object=nullptr;
        HRESULT hr=device()->CreateBlendState(reinterpret_cast<const D3D11_BLEND_DESC*>(&converted),&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<BlendState>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->value=*desc;
        return finish<BlendState>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D10_DEPTH_STENCIL_DESC* desc,ID3D10DepthStencilState** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        static_assert(sizeof(D3D10_DEPTH_STENCIL_DESC)==sizeof(D3D11_DEPTH_STENCIL_DESC),"depth-stencil layout");
        ID3D11DepthStencilState* object=nullptr;
        HRESULT hr=device()->CreateDepthStencilState(reinterpret_cast<const D3D11_DEPTH_STENCIL_DESC*>(desc),&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<DepthStencilState>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->value=*desc;
        return finish<DepthStencilState>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D10_RASTERIZER_DESC* desc,ID3D10RasterizerState** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        static_assert(sizeof(D3D10_RASTERIZER_DESC)==sizeof(D3D11_RASTERIZER_DESC),"rasterizer layout");
        ID3D11RasterizerState* object=nullptr;
        HRESULT hr=device()->CreateRasterizerState(reinterpret_cast<const D3D11_RASTERIZER_DESC*>(desc),&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<RasterizerState>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->value=*desc;
        return finish<RasterizerState>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D10_SAMPLER_DESC* desc,ID3D10SamplerState** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        static_assert(sizeof(D3D10_SAMPLER_DESC)==sizeof(D3D11_SAMPLER_DESC),"sampler layout");
        ID3D11SamplerState* object=nullptr;
        HRESULT hr=device()->CreateSamplerState(reinterpret_cast<const D3D11_SAMPLER_DESC*>(desc),&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<SamplerState>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->value=*desc;
        return finish<SamplerState>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D10_QUERY_DESC* desc,ID3D10Query** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        static_assert(sizeof(D3D10_QUERY_DESC)==sizeof(D3D11_QUERY_DESC),"query layout");
        ID3D11Query* object=nullptr;
        HRESULT hr=device()->CreateQuery(reinterpret_cast<const D3D11_QUERY_DESC*>(desc),&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<Query>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->value=*desc;
        return finish<Query>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D10_QUERY_DESC* desc,ID3D10Predicate** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(!desc)return E_INVALIDARG;
        ID3D11Predicate* object=nullptr;
        HRESULT hr=device()->CreatePredicate(reinterpret_cast<const D3D11_QUERY_DESC*>(desc),&object);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<Predicate>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->value=*desc;
        return finish<Predicate>(object,out,hr,wrapper);
    }
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D10_COUNTER_DESC*,ID3D10Counter** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D10 shim: performance counters are not supported on this platform");
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT format,UINT* support) override {
        return device()->CheckFormatSupport(format,support);
    }
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT format,UINT count,UINT* levels) override {
        return device()->CheckMultisampleQualityLevels(format,count,levels);
    }
    void STDMETHODCALLTYPE CheckCounterInfo(D3D10_COUNTER_INFO* info) override {
        if(info){info->LastDeviceDependentCounter=D3D10_COUNTER(0);info->NumSimultaneousCounters=0;info->NumDetectableParallelUnits=0;}
    }
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D10_COUNTER_DESC*,D3D10_COUNTER_TYPE*,UINT*,char*,UINT*,char*,UINT*,char*,UINT*) override {
        return E_NOTIMPL;
    }
    UINT STDMETHODCALLTYPE GetCreationFlags() override { return creationFlags; }
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE,REFIID,void** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D10 shim: shared resources are not supported on this platform");
        return E_NOTIMPL;
    }
    void STDMETHODCALLTYPE SetTextFilterSize(UINT,UINT) override {}
    void STDMETHODCALLTYPE GetTextFilterSize(UINT* width,UINT* height) override {
        if(width)*width=0;
        if(height)*height=0;
    }
    D3D10_FEATURE_LEVEL1 STDMETHODCALLTYPE GetFeatureLevel() override { return featureLevel1(host.level); }

private:
    // Unwrap a guest array into a D3D11 pointer array, bounded by the caller's
    // storage; returns the number of entries written.
    template<class Inner,class Guest,size_t N> static UINT unwrap(const Guest* const* values,UINT count,Inner* (&out)[N]) {
        UINT n=count>UINT(N)?UINT(N):count;
        for(UINT i=0;i<n;++i)out[i]=behind<Inner>(const_cast<Guest*>(values?values[i]:nullptr));
        return n;
    }
    // Take ownership of a freshly created D3D11 object into a wrapper. The
    // wrapper holds the one reference D3D11 handed out, and the D3D11 object
    // carries the wrapper so a later Get* returns the same D3D10 object.
    template<class T,class Inner,class Guest> HRESULT finish(Inner* object,Guest** out,HRESULT hr,T* wrapper=nullptr) {
        if(FAILED(hr)) {
            if(object)object->Release();
            if(wrapper)d3dshim::destroy(wrapper);
            return hr;
        }
        if(!wrapper)wrapper=d3dshim::create<T>();
        if(!wrapper){object->Release();return E_OUTOFMEMORY;}
        wrapper->inner=object;
        wrapper->owner=static_cast<IUnknown*>(static_cast<ID3D10Device1*>(this));
        wrapper->owner->AddRef();
        wrapper->context=context();
        wrapper->forward=object;
        d3dshim::attach(object,wrapper);
        *out=static_cast<Guest*>(wrapper->self);
        return S_OK;
    }
};
}

namespace {
// -------------------------------------------------------------------- exports
// The device is created through Play's D3D11CreateDevice route, which unwraps
// the guest's adapter and adopts the device for guest-window swapchains. The
// D3D10 feature level the guest asked for is a floor: D3D11CreateDevice picks
// the first level in the list it supports, so asking for 10_0 first is what
// makes a 10_0 game get a 10_0 device.
HRESULT createDevice(IDXGIAdapter* adapter,D3D10_DRIVER_TYPE type,UINT flags,
                     D3D10_FEATURE_LEVEL1 level,UINT sdk,UINT expected,ID3D10Device1** out,
                     Device** concrete=nullptr) {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(sdk!=expected)return E_INVALIDARG;
    auto device=d3dshim::create<Device>();
    if(!device)return E_OUTOFMEMORY;
    const D3D_FEATURE_LEVEL levels[]={featureLevel(level)};
    HRESULT hr=device->host.create(adapter,driverType(type),levels,1,createFlags(flags));
    if(FAILED(hr)){d3dshim::destroy(device);return hr;}
    device->creationFlags=flags;
    *out=device;
    if(concrete)*concrete=device;
    return S_OK;
}
// The swapchain is created on the device's own adapter factory, as Wine's
// d3d10_create_device_and_swapchain does. Play redirects that factory's
// CreateSwapChain for a guest window to the validated CoreWindow adapter, so
// the legacy descriptor reaches the same conversion a D3D11 game's would.
HRESULT createSwapChain(ID3D11Device* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** swapchain) {
    if(!swapchain)return E_POINTER;
    *swapchain=nullptr;
    if(!desc)return E_INVALIDARG;
    IDXGIDevice* dxgi=nullptr;
    IDXGIAdapter* adapter=nullptr;
    IDXGIFactory* factory=nullptr;
    HRESULT hr=device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi));
    if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
    if(dxgi)dxgi->Release();
    if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(&factory));
    if(adapter)adapter->Release();
    if(FAILED(hr))return hr;
    hr=factory->CreateSwapChain(device,desc,swapchain);
    factory->Release();
    return hr;
}
}

namespace {
// ------------------------------------------------------------------ ID3D10Blob
// The D3D10 blob is the D3D11 one: ID3DBlob is ID3D10Blob and shares its IID, so
// the compiler's object is handed straight to the guest. No wrapper is needed,
// and the reference the compiler gave the shim is the one the guest receives.
HRESULT blob(ID3DBlob* object,ID3D10Blob** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(!object)return E_FAIL;
    *out=object;
    return S_OK;
}
}

namespace {
// ------------------------------------------------- D3D10 shader reflection
// D3D10ReflectShader exists only in d3dcompiler_43, which registers the
// ID3D10ShaderReflection IID and answers the D3D10 method set. The console has
// d3dcompiler_47, which registers only ID3D11ShaderReflection and whose objects
// have longer vtables, so a D3D10 reflector cannot be forwarded to it. These
// thin wrappers hold the D3D11 reflector and copy the D3D10 prefix of each
// descriptor; the D3D11 interfaces are not IUnknown-derived, so no reference
// counting is involved and the reflector is held by the top-level object only.
struct Reflection;
struct ReflectionType;
struct ReflectionVariable;
struct ReflectionConstantBuffer;

struct ReflectionType : ID3D10ShaderReflectionType {
    // D3D10's reflection type is not IUnknown-derived: its vtable starts at
    // GetDesc, so no QueryInterface/AddRef/Release slots exist. The owner
    // allocates the nested wrappers and frees them with itself.
    ID3D11ShaderReflectionType* inner=nullptr;
    Reflection* owner=nullptr;
    HRESULT STDMETHODCALLTYPE GetDesc(D3D10_SHADER_TYPE_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SHADER_TYPE_DESC got{};
        HRESULT hr=inner?inner->GetDesc(&got):E_FAIL;
        *desc=prefix<D3D10_SHADER_TYPE_DESC>(got);
        return hr;
    }
    ID3D10ShaderReflectionType* STDMETHODCALLTYPE GetMemberTypeByIndex(UINT index) override;
    ID3D10ShaderReflectionType* STDMETHODCALLTYPE GetMemberTypeByName(const char* name) override;
    const char* STDMETHODCALLTYPE GetMemberTypeName(UINT index) override {
        return inner?inner->GetMemberTypeName(index):nullptr;
    }
};
struct ReflectionVariable : ID3D10ShaderReflectionVariable {
    ID3D11ShaderReflectionVariable* inner=nullptr;
    Reflection* owner=nullptr;
    HRESULT STDMETHODCALLTYPE GetDesc(D3D10_SHADER_VARIABLE_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SHADER_VARIABLE_DESC got{};
        HRESULT hr=inner?inner->GetDesc(&got):E_FAIL;
        *desc=prefix<D3D10_SHADER_VARIABLE_DESC>(got);
        return hr;
    }
    ID3D10ShaderReflectionType* STDMETHODCALLTYPE GetType() override;
};
struct ReflectionConstantBuffer : ID3D10ShaderReflectionConstantBuffer {
    ID3D11ShaderReflectionConstantBuffer* inner=nullptr;
    Reflection* owner=nullptr;
    HRESULT STDMETHODCALLTYPE GetDesc(D3D10_SHADER_BUFFER_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SHADER_BUFFER_DESC got{};
        HRESULT hr=inner?inner->GetDesc(&got):E_FAIL;
        *desc=prefix<D3D10_SHADER_BUFFER_DESC>(got);
        return hr;
    }
    ID3D10ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByIndex(UINT index) override;
    ID3D10ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByName(const char* name) override;
};
struct Reflection : d3dshim::Base, ID3D10ShaderReflection {
    ID3D11ShaderReflection* reflector=nullptr;
    Reflection() { self=static_cast<ID3D10ShaderReflection*>(this);D3DSHIM_IDS(&__uuidof(ID3D10ShaderReflection)); }
    ~Reflection() {
        if(reflector){reflector->Release();reflector=nullptr;}
        for(unsigned i=0;i<typeCount;++i)if(types[i])d3dshim::destroy(types[i]);
        for(unsigned i=0;i<variableCount;++i)if(variables[i])d3dshim::destroy(variables[i]);
        for(unsigned i=0;i<constantBufferCount;++i)if(constantBuffers[i])d3dshim::destroy(constantBuffers[i]);
    }
    D3DSHIM_REF
    D3DSHIM_QI
    // The reflection objects are owned by this reflector and live as long as it
    // does, so the same wrapper answers every lookup (as the D3D11 objects do).
    static constexpr unsigned capacity=32;
    ReflectionType* types[capacity]{};
    ReflectionVariable* variables[capacity]{};
    ReflectionConstantBuffer* constantBuffers[capacity]{};
    unsigned typeCount=0,variableCount=0,constantBufferCount=0;
    ReflectionType* type(ID3D11ShaderReflectionType* object) {
        if(!object)return nullptr;
        for(unsigned i=0;i<typeCount;++i)if(types[i]->inner==object)return types[i];
        if(typeCount==capacity)return nullptr;
        auto wrapper=d3dshim::create<ReflectionType>();
        if(!wrapper)return nullptr;
        wrapper->inner=object;wrapper->owner=this;types[typeCount++]=wrapper;return wrapper;
    }
    ReflectionVariable* variable(ID3D11ShaderReflectionVariable* object) {
        if(!object)return nullptr;
        for(unsigned i=0;i<variableCount;++i)if(variables[i]->inner==object)return variables[i];
        if(variableCount==capacity)return nullptr;
        auto wrapper=d3dshim::create<ReflectionVariable>();
        if(!wrapper)return nullptr;
        wrapper->inner=object;wrapper->owner=this;variables[variableCount++]=wrapper;return wrapper;
    }
    ReflectionConstantBuffer* constantBuffer(ID3D11ShaderReflectionConstantBuffer* object) {
        if(!object)return nullptr;
        for(unsigned i=0;i<constantBufferCount;++i)if(constantBuffers[i]->inner==object)return constantBuffers[i];
        if(constantBufferCount==capacity)return nullptr;
        auto wrapper=d3dshim::create<ReflectionConstantBuffer>();
        if(!wrapper)return nullptr;
        wrapper->inner=object;wrapper->owner=this;constantBuffers[constantBufferCount++]=wrapper;return wrapper;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3D10_SHADER_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SHADER_DESC got{};
        HRESULT hr=reflector?reflector->GetDesc(&got):E_FAIL;
        *desc=prefix<D3D10_SHADER_DESC>(got);
        return hr;
    }
    ID3D10ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByIndex(UINT index) override {
        return reflector?constantBuffer(reflector->GetConstantBufferByIndex(index)):nullptr;
    }
    ID3D10ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByName(const char* name) override {
        return reflector?constantBuffer(reflector->GetConstantBufferByName(name)):nullptr;
    }
    HRESULT STDMETHODCALLTYPE GetResourceBindingDesc(UINT index,D3D10_SHADER_INPUT_BIND_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SHADER_INPUT_BIND_DESC got{};
        HRESULT hr=reflector?reflector->GetResourceBindingDesc(index,&got):E_FAIL;
        *desc=prefix<D3D10_SHADER_INPUT_BIND_DESC>(got);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetInputParameterDesc(UINT index,D3D10_SIGNATURE_PARAMETER_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SIGNATURE_PARAMETER_DESC got{};
        HRESULT hr=reflector?reflector->GetInputParameterDesc(index,&got):E_FAIL;
        *desc=prefix<D3D10_SIGNATURE_PARAMETER_DESC>(got);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetOutputParameterDesc(UINT index,D3D10_SIGNATURE_PARAMETER_DESC* desc) override {
        if(!desc)return E_INVALIDARG;
        D3D11_SIGNATURE_PARAMETER_DESC got{};
        HRESULT hr=reflector?reflector->GetOutputParameterDesc(index,&got):E_FAIL;
        *desc=prefix<D3D10_SIGNATURE_PARAMETER_DESC>(got);
        return hr;
    }
};
// A nested reflection object needs its owner to allocate the wrapper, so the
// two lookups that return one are defined once Reflection is complete.
ID3D10ShaderReflectionType* ReflectionType::GetMemberTypeByIndex(UINT index) {
    return inner?owner->type(inner->GetMemberTypeByIndex(index)):nullptr;
}
ID3D10ShaderReflectionType* ReflectionType::GetMemberTypeByName(const char* name) {
    return inner?owner->type(inner->GetMemberTypeByName(name)):nullptr;
}
ID3D10ShaderReflectionType* ReflectionVariable::GetType() {
    return inner?owner->type(inner->GetType()):nullptr;
}
ID3D10ShaderReflectionVariable* ReflectionConstantBuffer::GetVariableByIndex(UINT index) {
    return inner?owner->variable(inner->GetVariableByIndex(index)):nullptr;
}
ID3D10ShaderReflectionVariable* ReflectionConstantBuffer::GetVariableByName(const char* name) {
    return inner?owner->variable(inner->GetVariableByName(name)):nullptr;
}
}

namespace {
// ------------------------------------------------------------- state blocks
// A faithful port of Wine's dlls/d3d10/stateblock.c: the mask bit arithmetic,
// the per-field capture/apply, and the eight mask functions. The stateblock
// holds D3D10 references, so the objects it captured survive until it is
// applied or released.
struct StateBlock : d3dshim::Base, ID3D10StateBlock {
    StateBlock() { self=static_cast<ID3D10StateBlock*>(this);D3DSHIM_IDS(&__uuidof(ID3D10StateBlock)); }
    ~StateBlock() { cleanup(); }
    D3DSHIM_REF
    D3DSHIM_QI
    ID3D10Device* device=nullptr;
    D3D10_STATE_BLOCK_MASK mask{};
    ID3D10VertexShader* vs=nullptr;
    ID3D10SamplerState* vsSamplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT]{};
    ID3D10ShaderResourceView* vsResources[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    ID3D10Buffer* vsConstantBuffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]{};
    ID3D10GeometryShader* gs=nullptr;
    ID3D10SamplerState* gsSamplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT]{};
    ID3D10ShaderResourceView* gsResources[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    ID3D10Buffer* gsConstantBuffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]{};
    ID3D10PixelShader* ps=nullptr;
    ID3D10SamplerState* psSamplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT]{};
    ID3D10ShaderResourceView* psResources[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    ID3D10Buffer* psConstantBuffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]{};
    ID3D10Buffer* vertexBuffers[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
    UINT vertexStrides[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
    UINT vertexOffsets[D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
    ID3D10Buffer* indexBuffer=nullptr;
    DXGI_FORMAT indexFormat=DXGI_FORMAT_UNKNOWN;
    UINT indexOffset=0;
    ID3D10InputLayout* inputLayout=nullptr;
    D3D10_PRIMITIVE_TOPOLOGY topology=D3D10_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D10RenderTargetView* renderTargets[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D10DepthStencilView* depthStencilView=nullptr;
    ID3D10DepthStencilState* depthStencilState=nullptr;
    UINT stencilReference=0;
    ID3D10BlendState* blendState=nullptr;
    FLOAT blendFactor[4]{};
    UINT sampleMask=0;
    D3D10_VIEWPORT viewports[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    D3D10_RECT scissorRects[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    ID3D10RasterizerState* rasterizerState=nullptr;
    ID3D10Buffer* streamOutput[D3D10_SO_BUFFER_SLOT_COUNT]{};
    UINT streamOutputOffsets[D3D10_SO_BUFFER_SLOT_COUNT]{};
    ID3D10Predicate* predicate=nullptr;
    WINBOOL predicateValue=FALSE;

    template<class T,size_t N> static void drop(T* (&list)[N]) {
        for(size_t i=0;i<N;++i)if(list[i]){list[i]->Release();list[i]=nullptr;}
    }
    template<class T> static void drop(T*& value) { if(value){value->Release();value=nullptr;} }
    void cleanup() {
        drop(vs);drop(vsSamplers);drop(vsResources);drop(vsConstantBuffers);
        drop(gs);drop(gsSamplers);drop(gsResources);drop(gsConstantBuffers);
        drop(ps);drop(psSamplers);drop(psResources);drop(psConstantBuffers);
        drop(vertexBuffers);drop(indexBuffer);drop(inputLayout);
        drop(renderTargets);drop(depthStencilView);drop(depthStencilState);drop(blendState);
        drop(rasterizerState);drop(streamOutput);drop(predicate);
    }
    static bool bit(const BYTE* field,unsigned fieldSize,unsigned index) {
        return index<fieldSize&&(field[index>>3]&(1<<(index&7)));
    }
    HRESULT STDMETHODCALLTYPE Capture() override {
        unsigned count=D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        cleanup();
        if(mask.VS)device->VSGetShader(&vs);
        for(unsigned i=0;i<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)
            if(bit(mask.VSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,i))device->VSGetSamplers(i,1,&vsSamplers[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.VSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,i))device->VSGetShaderResources(i,1,&vsResources[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)
            if(bit(mask.VSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,i))device->VSGetConstantBuffers(i,1,&vsConstantBuffers[i]);
        if(mask.GS)device->GSGetShader(&gs);
        for(unsigned i=0;i<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)
            if(bit(mask.GSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,i))device->GSGetSamplers(i,1,&gsSamplers[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.GSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,i))device->GSGetShaderResources(i,1,&gsResources[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)
            if(bit(mask.GSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,i))device->GSGetConstantBuffers(i,1,&gsConstantBuffers[i]);
        if(mask.PS)device->PSGetShader(&ps);
        for(unsigned i=0;i<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)
            if(bit(mask.PSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,i))device->PSGetSamplers(i,1,&psSamplers[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.PSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,i))device->PSGetShaderResources(i,1,&psResources[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)
            if(bit(mask.PSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,i))device->PSGetConstantBuffers(i,1,&psConstantBuffers[i]);
        for(unsigned i=0;i<D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.IAVertexBuffers,D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,i))
                device->IAGetVertexBuffers(i,1,&vertexBuffers[i],&vertexStrides[i],&vertexOffsets[i]);
        if(mask.IAIndexBuffer)device->IAGetIndexBuffer(&indexBuffer,&indexFormat,&indexOffset);
        if(mask.IAInputLayout)device->IAGetInputLayout(&inputLayout);
        if(mask.IAPrimitiveTopology)device->IAGetPrimitiveTopology(&topology);
        if(mask.OMRenderTargets)device->OMGetRenderTargets(D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT,renderTargets,&depthStencilView);
        if(mask.OMDepthStencilState)device->OMGetDepthStencilState(&depthStencilState,&stencilReference);
        if(mask.OMBlendState)device->OMGetBlendState(&blendState,blendFactor,&sampleMask);
        if(mask.RSViewports)device->RSGetViewports(&count,viewports);
        if(mask.RSScissorRects)device->RSGetScissorRects(&count,scissorRects);
        if(mask.RSRasterizerState)device->RSGetState(&rasterizerState);
        if(mask.SOBuffers)device->SOGetTargets(D3D10_SO_BUFFER_SLOT_COUNT,streamOutput,streamOutputOffsets);
        if(mask.Predication)device->GetPredication(&predicate,&predicateValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Apply() override {
        if(mask.VS)device->VSSetShader(vs);
        for(unsigned i=0;i<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)
            if(bit(mask.VSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,i))device->VSSetSamplers(i,1,&vsSamplers[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.VSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,i))device->VSSetShaderResources(i,1,&vsResources[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)
            if(bit(mask.VSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,i))device->VSSetConstantBuffers(i,1,&vsConstantBuffers[i]);
        if(mask.GS)device->GSSetShader(gs);
        for(unsigned i=0;i<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)
            if(bit(mask.GSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,i))device->GSSetSamplers(i,1,&gsSamplers[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.GSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,i))device->GSSetShaderResources(i,1,&gsResources[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)
            if(bit(mask.GSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,i))device->GSSetConstantBuffers(i,1,&gsConstantBuffers[i]);
        if(mask.PS)device->PSSetShader(ps);
        for(unsigned i=0;i<D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)
            if(bit(mask.PSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT,i))device->PSSetSamplers(i,1,&psSamplers[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.PSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,i))device->PSSetShaderResources(i,1,&psResources[i]);
        for(unsigned i=0;i<D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)
            if(bit(mask.PSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,i))device->PSSetConstantBuffers(i,1,&psConstantBuffers[i]);
        for(unsigned i=0;i<D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++i)
            if(bit(mask.IAVertexBuffers,D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,i))
                device->IASetVertexBuffers(i,1,&vertexBuffers[i],&vertexStrides[i],&vertexOffsets[i]);
        if(mask.IAIndexBuffer)device->IASetIndexBuffer(indexBuffer,indexFormat,indexOffset);
        if(mask.IAInputLayout)device->IASetInputLayout(inputLayout);
        if(mask.IAPrimitiveTopology)device->IASetPrimitiveTopology(topology);
        if(mask.OMRenderTargets)device->OMSetRenderTargets(D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT,renderTargets,depthStencilView);
        if(mask.OMDepthStencilState)device->OMSetDepthStencilState(depthStencilState,stencilReference);
        if(mask.OMBlendState)device->OMSetBlendState(blendState,blendFactor,sampleMask);
        if(mask.RSViewports)device->RSSetViewports(D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE,viewports);
        if(mask.RSScissorRects)device->RSSetScissorRects(D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE,scissorRects);
        if(mask.RSRasterizerState)device->RSSetState(rasterizerState);
        if(mask.SOBuffers)device->SOSetTargets(D3D10_SO_BUFFER_SLOT_COUNT,streamOutput,streamOutputOffsets);
        if(mask.Predication)device->SetPredication(predicate,predicateValue);
        return S_OK;
    }
    // Wine leaves both of these unimplemented; so does this.
    HRESULT STDMETHODCALLTYPE ReleaseAllDeviceObjects() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDevice(ID3D10Device** out) override { return E_NOTIMPL; }
};
}

namespace {
// ------------------------------------------------------- state block masks
// Wine's bit helpers, ported exactly: a field is a byte array of `fieldSize`
// bits, and a range may start and end inside a byte.
BOOL maskBit(const BYTE* field,UINT fieldSize,UINT index) {
    if(index>=fieldSize)return FALSE;
    return field[index>>3]&(1<<(index&7));
}
HRESULT maskSet(BYTE* field,UINT fieldSize,UINT startBit,UINT count) {
    UINT endBit=startBit+count;
    BYTE startMask=BYTE(0xff<<(startBit&7));
    BYTE endMask=BYTE(0x7f>>(~endBit&7));
    UINT startIndex=startBit>>3;
    UINT endIndex=endBit>>3;
    if(startBit>=fieldSize||fieldSize-startBit<count)return E_INVALIDARG;
    if(startIndex==endIndex) {
        field[startIndex]|=BYTE(startMask&endMask);
        return S_OK;
    }
    if(startBit&7) {
        field[startIndex]|=startMask;
        ++startIndex;
    }
    memset(&field[startIndex],0xff,endIndex-startIndex);
    if(endBit&7)field[endIndex]|=endMask;
    return S_OK;
}
HRESULT maskClear(BYTE* field,UINT fieldSize,UINT startBit,UINT count) {
    UINT endBit=startBit+count;
    BYTE startMask=BYTE(0x7f>>(~startBit&7));
    BYTE endMask=BYTE(0xff<<(endBit&7));
    UINT startIndex=startBit>>3;
    UINT endIndex=endBit>>3;
    if(startBit>=fieldSize||fieldSize-startBit<count)return E_INVALIDARG;
    if(startIndex==endIndex) {
        field[startIndex]&=BYTE(startMask|endMask);
        return S_OK;
    }
    if(startBit&7) {
        field[startIndex]&=startMask;
        ++startIndex;
    }
    memset(&field[startIndex],0,endIndex-startIndex);
    if(endBit&7)field[endIndex]&=endMask;
    return S_OK;
}
// The field and its width for a state type, or null when the type is unknown.
struct Field { BYTE* bits; UINT size; };
Field maskField(D3D10_STATE_BLOCK_MASK* mask,D3D10_DEVICE_STATE_TYPES type) {
    switch(type) {
    case D3D10_DST_SO_BUFFERS:return {&mask->SOBuffers,1};
    case D3D10_DST_OM_RENDER_TARGETS:return {&mask->OMRenderTargets,1};
    case D3D10_DST_OM_DEPTH_STENCIL_STATE:return {&mask->OMDepthStencilState,1};
    case D3D10_DST_OM_BLEND_STATE:return {&mask->OMBlendState,1};
    case D3D10_DST_VS:return {&mask->VS,1};
    case D3D10_DST_VS_SAMPLERS:return {mask->VSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT};
    case D3D10_DST_VS_SHADER_RESOURCES:return {mask->VSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT};
    case D3D10_DST_VS_CONSTANT_BUFFERS:return {mask->VSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT};
    case D3D10_DST_GS:return {&mask->GS,1};
    case D3D10_DST_GS_SAMPLERS:return {mask->GSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT};
    case D3D10_DST_GS_SHADER_RESOURCES:return {mask->GSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT};
    case D3D10_DST_GS_CONSTANT_BUFFERS:return {mask->GSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT};
    case D3D10_DST_PS:return {&mask->PS,1};
    case D3D10_DST_PS_SAMPLERS:return {mask->PSSamplers,D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT};
    case D3D10_DST_PS_SHADER_RESOURCES:return {mask->PSShaderResources,D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT};
    case D3D10_DST_PS_CONSTANT_BUFFERS:return {mask->PSConstantBuffers,D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT};
    case D3D10_DST_IA_VERTEX_BUFFERS:return {mask->IAVertexBuffers,D3D10_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT};
    case D3D10_DST_IA_INDEX_BUFFER:return {&mask->IAIndexBuffer,1};
    case D3D10_DST_IA_INPUT_LAYOUT:return {&mask->IAInputLayout,1};
    case D3D10_DST_IA_PRIMITIVE_TOPOLOGY:return {&mask->IAPrimitiveTopology,1};
    case D3D10_DST_RS_VIEWPORTS:return {&mask->RSViewports,1};
    case D3D10_DST_RS_SCISSOR_RECTS:return {&mask->RSScissorRects,1};
    case D3D10_DST_RS_RASTERIZER_STATE:return {&mask->RSRasterizerState,1};
    case D3D10_DST_PREDICATION:return {&mask->Predication,1};
    default:return {nullptr,0};
    }
}
}

extern "C" {
// ------------------------------------------------------------------- d3d10.dll
__declspec(dllexport) HRESULT WINAPI D3D10CreateDevice(IDXGIAdapter* adapter,D3D10_DRIVER_TYPE type,
        HMODULE swrast,UINT flags,UINT sdk,ID3D10Device** out) {
    (void)swrast;
    ID3D10Device1* device=nullptr;
    HRESULT hr=createDevice(adapter,type,flags,D3D10_FEATURE_LEVEL_10_0,sdk,D3D10_SDK_VERSION,&device);
    if(FAILED(hr))return hr;
    *out=static_cast<ID3D10Device*>(device);
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10CreateDeviceAndSwapChain(IDXGIAdapter* adapter,
        D3D10_DRIVER_TYPE type,HMODULE swrast,UINT flags,UINT sdk,DXGI_SWAP_CHAIN_DESC* desc,
        IDXGISwapChain** swapchain,ID3D10Device** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    ID3D10Device1* device=nullptr;
    Device* concrete=nullptr;
    HRESULT hr=createDevice(adapter,type,flags,D3D10_FEATURE_LEVEL_10_0,sdk,D3D10_SDK_VERSION,&device,&concrete);
    if(FAILED(hr))return hr;
    hr=createSwapChain(concrete->host.device,desc,swapchain);
    if(FAILED(hr)){device->Release();return hr;}
    *out=static_cast<ID3D10Device*>(device);
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10CreateBlob(SIZE_T size,ID3D10Blob** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    using Fn=HRESULT(WINAPI*)(SIZE_T,ID3DBlob**);
    auto fn=(Fn)compiler.proc("D3DCreateBlob");
    if(!fn)return E_FAIL;
    ID3DBlob* object=nullptr;
    HRESULT hr=fn(size,&object);
    if(FAILED(hr))return hr;
    return blob(object,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10CompileShader(const char* data,SIZE_T size,const char* filename,
        const D3D10_SHADER_MACRO* defines,ID3D10Include* include,const char* entrypoint,const char* profile,
        UINT flags,ID3D10Blob** shader,ID3D10Blob** errors) {
    using Fn=HRESULT(WINAPI*)(const void*,SIZE_T,const char*,const void*,const void*,const char*,const char*,
        UINT,UINT,ID3DBlob**,ID3DBlob**);
    auto fn=(Fn)compiler.proc("D3DCompile");
    if(!fn)return E_FAIL;
    ID3DBlob* code=nullptr;ID3DBlob* messages=nullptr;
    HRESULT hr=fn(data,size,filename,defines,include,entrypoint,profile,flags,0,&code,&messages);
    if(shader)hr=FAILED(hr)?(messages?messages->Release():S_OK,hr):blob(code,shader);
    else if(code)code->Release();
    if(errors) {
        if(messages)blob(messages,errors);
        else *errors=nullptr;
    } else if(messages)messages->Release();
    return hr;
}
__declspec(dllexport) HRESULT WINAPI D3D10DisassembleShader(const void* data,SIZE_T size,WINBOOL colors,
        const char* comments,ID3D10Blob** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    using Fn=HRESULT(WINAPI*)(const void*,SIZE_T,UINT,const char*,ID3DBlob**);
    auto fn=(Fn)compiler.proc("D3DDisassemble");
    if(!fn)return E_FAIL;
    ID3DBlob* object=nullptr;
    HRESULT hr=fn(data,size,colors?0x1:0,comments,&object);
    if(FAILED(hr))return hr;
    return blob(object,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10ReflectShader(const void* data,SIZE_T size,ID3D10ShaderReflection** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    // d3dcompiler_47 answers ID3D11ShaderReflection only (Wine's reflection.c
    // accepts that IID alone), so the reflector is always the D3D11 one and the
    // wrapper below narrows it to the D3D10 method set.
    using Fn=HRESULT(WINAPI*)(const void*,SIZE_T,REFIID,void**);
    auto fn=(Fn)compiler.proc("D3DReflect");
    if(!fn)return E_FAIL;
    ID3D11ShaderReflection* reflector=nullptr;
    // Windows' d3dcompiler_47 answers the v47 IID; Wine's reflection.c maps
    // IID_ID3D11ShaderReflection onto IID_ID3D10ShaderReflection and answers
    // only that, so both are tried. mingw's d3d11shader.h declares the IID
    // without a __CRT_UUID_DECL, so __uuidof cannot be used for it.
    static const GUID v47={0x8d536ca1,0x0cca,0x4956,{0xa8,0x37,0x78,0x69,0x63,0x75,0x55,0x84}};
    HRESULT hr=fn(data,size,v47,reinterpret_cast<void**>(&reflector));
    if(FAILED(hr))hr=fn(data,size,__uuidof(ID3D10ShaderReflection),reinterpret_cast<void**>(&reflector));
    if(FAILED(hr))return hr;
    auto wrapper=d3dshim::create<Reflection>();
    if(!wrapper){reflector->Release();return E_OUTOFMEMORY;}
    wrapper->reflector=reflector;
    *out=static_cast<ID3D10ShaderReflection*>(wrapper->self);
    return S_OK;
}
// The signature and debug blobs are the compiler's own; the D3D10 entry points
// are the D3D11 ones with a different name.
static HRESULT signatureBlob(const char* name,const void* data,SIZE_T size,ID3D10Blob** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    using Fn=HRESULT(WINAPI*)(const void*,SIZE_T,ID3DBlob**);
    auto fn=(Fn)compiler.proc(name);
    if(!fn)return E_FAIL;
    ID3DBlob* object=nullptr;
    HRESULT hr=fn(data,size,&object);
    if(FAILED(hr))return hr;
    return blob(object,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10GetInputSignatureBlob(const void* data,SIZE_T size,ID3D10Blob** out) {
    return signatureBlob("D3DGetInputSignatureBlob",data,size,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10GetOutputSignatureBlob(const void* data,SIZE_T size,ID3D10Blob** out) {
    return signatureBlob("D3DGetOutputSignatureBlob",data,size,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10GetInputAndOutputSignatureBlob(const void* data,SIZE_T size,ID3D10Blob** out) {
    return signatureBlob("D3DGetInputAndOutputSignatureBlob",data,size,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10GetShaderDebugInfo(const void* data,SIZE_T size,ID3D10Blob** out) {
    return signatureBlob("D3DGetDebugInfo",data,size,out);
}
// Wine answers the D3D10 profiles with the 4_0 ones: a D3D10 device runs 4_0
// shaders, and the D3D10 SDK has no separate profile string.
__declspec(dllexport) const char* WINAPI D3D10GetVertexShaderProfile(ID3D10Device*) { return "vs_4_0"; }
__declspec(dllexport) const char* WINAPI D3D10GetGeometryShaderProfile(ID3D10Device*) { return "gs_4_0"; }
__declspec(dllexport) const char* WINAPI D3D10GetPixelShaderProfile(ID3D10Device*) { return "ps_4_0"; }
// The FX10 runtime is not implemented; see the file header.
__declspec(dllexport) HRESULT WINAPI D3D10CompileEffectFromMemory(void*,SIZE_T,const char*,
        const D3D10_SHADER_MACRO*,ID3D10Include*,UINT,UINT,ID3D10Blob** effect,ID3D10Blob** errors) {
    if(effect)*effect=nullptr;
    if(errors)*errors=nullptr;
    d3dshim::noteOnce("D3D10 shim: the FX10 runtime is not implemented; effect compilation refused");
    return E_NOTIMPL;
}
__declspec(dllexport) HRESULT WINAPI D3D10CreateEffectFromMemory(void*,SIZE_T,UINT,ID3D10Device*,
        ID3D10EffectPool*,ID3D10Effect** effect) {
    if(effect)*effect=nullptr;
    d3dshim::noteOnce("D3D10 shim: the FX10 runtime is not implemented; effects refused");
    return E_NOTIMPL;
}
__declspec(dllexport) HRESULT WINAPI D3D10CreateEffectPoolFromMemory(void*,SIZE_T,UINT,ID3D10Device*,
        ID3D10EffectPool** pool) {
    if(pool)*pool=nullptr;
    d3dshim::noteOnce("D3D10 shim: the FX10 runtime is not implemented; effect pools refused");
    return E_NOTIMPL;
}
// Export stubs, as in Wine's d3d10.spec.
__declspec(dllexport) HRESULT WINAPI D3D10DisassembleEffect(void*,SIZE_T,ID3D10Blob**) { return E_NOTIMPL; }
__declspec(dllexport) HRESULT WINAPI D3D10GetVersion(ID3D10Blob** out) { if(out)*out=nullptr;return E_NOTIMPL; }
__declspec(dllexport) HRESULT WINAPI D3D10PreprocessShader(const char*,SIZE_T,const char*,const D3D10_SHADER_MACRO*,
        ID3D10Include*,ID3D10Blob**,ID3D10Blob**) { return E_NOTIMPL; }
__declspec(dllexport) HRESULT WINAPI D3D10RegisterLayers() { return E_NOTIMPL; }
__declspec(dllexport) void WINAPI RevertToOldImplementation() {}

// ------------------------------------------------------------- state blocks
__declspec(dllexport) HRESULT WINAPI D3D10CreateStateBlock(ID3D10Device* device,D3D10_STATE_BLOCK_MASK* mask,
        ID3D10StateBlock** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(!device||!mask)return E_INVALIDARG;
    auto block=d3dshim::create<StateBlock>();
    if(!block)return E_OUTOFMEMORY;
    block->device=device;device->AddRef();
    block->mask=*mask;
    *out=static_cast<ID3D10StateBlock*>(block->self);
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskDifference(D3D10_STATE_BLOCK_MASK* x,
        D3D10_STATE_BLOCK_MASK* y,D3D10_STATE_BLOCK_MASK* result) {
    if(!x||!y||!result)return E_INVALIDARG;
    // Wine XORs whole DWORDs first and then any trailing bytes.
    UINT words=sizeof(*result)/sizeof(DWORD);
    for(UINT i=0;i<words;++i)((DWORD*)result)[i]=((DWORD*)x)[i]^((DWORD*)y)[i];
    for(UINT i=words*sizeof(DWORD);i<sizeof(*result);++i)((BYTE*)result)[i]=((BYTE*)x)[i]^((BYTE*)y)[i];
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskDisableAll(D3D10_STATE_BLOCK_MASK* mask) {
    if(!mask)return E_INVALIDARG;
    memset(mask,0,sizeof(*mask));
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskEnableAll(D3D10_STATE_BLOCK_MASK* mask) {
    if(!mask)return E_INVALIDARG;
    memset(mask,0xff,sizeof(*mask));
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskDisableCapture(D3D10_STATE_BLOCK_MASK* mask,
        D3D10_DEVICE_STATE_TYPES type,UINT start,UINT count) {
    if(!mask)return E_INVALIDARG;
    auto field=maskField(mask,type);
    return field.bits?maskClear(field.bits,field.size,start,count):E_INVALIDARG;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskEnableCapture(D3D10_STATE_BLOCK_MASK* mask,
        D3D10_DEVICE_STATE_TYPES type,UINT start,UINT count) {
    if(!mask)return E_INVALIDARG;
    auto field=maskField(mask,type);
    return field.bits?maskSet(field.bits,field.size,start,count):E_INVALIDARG;
}
__declspec(dllexport) WINBOOL WINAPI D3D10StateBlockMaskGetSetting(D3D10_STATE_BLOCK_MASK* mask,
        D3D10_DEVICE_STATE_TYPES type,UINT index) {
    if(!mask)return FALSE;
    auto field=maskField(mask,type);
    return field.bits?maskBit(field.bits,field.size,index):FALSE;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskIntersect(D3D10_STATE_BLOCK_MASK* x,
        D3D10_STATE_BLOCK_MASK* y,D3D10_STATE_BLOCK_MASK* result) {
    if(!x||!y||!result)return E_INVALIDARG;
    UINT words=sizeof(*result)/sizeof(DWORD);
    for(UINT i=0;i<words;++i)((DWORD*)result)[i]=((DWORD*)x)[i]&((DWORD*)y)[i];
    for(UINT i=words*sizeof(DWORD);i<sizeof(*result);++i)((BYTE*)result)[i]=((BYTE*)x)[i]&((BYTE*)y)[i];
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10StateBlockMaskUnion(D3D10_STATE_BLOCK_MASK* x,
        D3D10_STATE_BLOCK_MASK* y,D3D10_STATE_BLOCK_MASK* result) {
    if(!x||!y||!result)return E_INVALIDARG;
    UINT words=sizeof(*result)/sizeof(DWORD);
    for(UINT i=0;i<words;++i)((DWORD*)result)[i]=((DWORD*)x)[i]|((DWORD*)y)[i];
    for(UINT i=words*sizeof(DWORD);i<sizeof(*result);++i)((BYTE*)result)[i]=((BYTE*)x)[i]|((BYTE*)y)[i];
    return S_OK;
}

// ---------------------------------------------------------------- d3d10_1.dll
// The D3D10.1 device is the same object; only the creation call and the
// feature level differ.
__declspec(dllexport) HRESULT WINAPI D3D10CreateDevice1(IDXGIAdapter* adapter,D3D10_DRIVER_TYPE type,
        HMODULE swrast,UINT flags,D3D10_FEATURE_LEVEL1 level,UINT sdk,ID3D10Device1** out) {
    (void)swrast;
    return createDevice(adapter,type,flags,level,sdk,D3D10_1_SDK_VERSION,out);
}
__declspec(dllexport) HRESULT WINAPI D3D10CreateDeviceAndSwapChain1(IDXGIAdapter* adapter,
        D3D10_DRIVER_TYPE type,HMODULE swrast,UINT flags,D3D10_FEATURE_LEVEL1 level,UINT sdk,
        DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** swapchain,ID3D10Device1** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    Device* concrete=nullptr;
    HRESULT hr=createDevice(adapter,type,flags,level,sdk,D3D10_1_SDK_VERSION,out,&concrete);
    if(FAILED(hr))return hr;
    hr=createSwapChain(concrete->host.device,desc,swapchain);
    if(FAILED(hr)){(*out)->Release();*out=nullptr;return hr;}
    return S_OK;
}

// -------------------------------------------------------------- d3d10core.dll
// Wine's d3d10core is the internal layer d3d10.dll sits on; nothing on Windows
// ships it, and a guest reaching it wants the same device.
__declspec(dllexport) HRESULT WINAPI D3D10CoreCreateDevice(IDXGIFactory*,IDXGIAdapter* adapter,UINT flags,
        D3D_FEATURE_LEVEL level,ID3D10Device** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    D3D10_FEATURE_LEVEL1 wanted=featureLevel1(level);
    ID3D10Device1* device=nullptr;
    HRESULT hr=createDevice(adapter,D3D10_DRIVER_TYPE_HARDWARE,flags,wanted,D3D10_SDK_VERSION,
        D3D10_SDK_VERSION,&device);
    if(FAILED(hr))return hr;
    *out=static_cast<ID3D10Device*>(device);
    return S_OK;
}
__declspec(dllexport) HRESULT WINAPI D3D10CoreRegisterLayers() { return S_OK; }
}
