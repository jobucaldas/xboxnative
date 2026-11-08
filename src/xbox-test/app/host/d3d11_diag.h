// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostics for the D3D11 device Play hands a guest. Games fall back or skip
// whole layers silently when a Create* call fails; nothing logged those. The
// entries of the device class's vtable that create resources, shaders and
// states (and CheckFormatSupport/CheckFeatureSupport, plus the immediate
// context's Map) are redirected to wrappers that call the original entry and
// then, on failure, log one line per distinct (method, hr, key parameters)
// through the probe sink, up to a cap. Results and arguments pass through
// unchanged. Like play_graphics.h the patch is per vtable (every device of a
// class shares it), so it is safe for several devices and for the d3d11.1+
// interfaces, whose vtables extend ID3D11Device's: slots beyond ID3D11Device
// are only written to vtables reached through a successful QueryInterface for
// that interface.
#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace d3d11_diag {
using Sink=void(*)(const char*);
inline Sink sink=nullptr;
constexpr size_t kMaxFailures=200;   // distinct failed calls logged
constexpr size_t kMaxProbes=160;     // distinct capability answers logged
constexpr int kSlots=50;
constexpr size_t kMaxClasses=16;

// ---- vtable slots (ID3D11Device, ID3D11Device1, ID3D11DeviceContext)
enum DeviceSlot {
    CreateBuffer=3,CreateTexture1D,CreateTexture2D,CreateTexture3D,CreateShaderResourceView,CreateUnorderedAccessView,
    CreateRenderTargetView,CreateDepthStencilView,CreateInputLayout,CreateVertexShader,CreateGeometryShader,
    CreateGeometryShaderWithStreamOutput,CreatePixelShader,CreateHullShader,CreateDomainShader,CreateComputeShader,
    CreateClassLinkage,CreateBlendState,CreateDepthStencilState,CreateRasterizerState,CreateSamplerState,CreateQuery,
    CreatePredicate,CreateCounter,CreateDeferredContext,OpenSharedResource,CheckFormatSupport,CheckMultisampleQualityLevels,
    CheckCounterInfo,CheckCounter,CheckFeatureSupport,
    CreateDeferredContext1=44,CreateBlendState1=45,CreateRasterizerState1=46,
    ContextMap=14
};
static_assert(CheckFeatureSupport==33&&CreateDeferredContext==27&&CreateSamplerState==23,"ID3D11Device slot layout");

// ---- reporting: once per key, bounded
inline std::mutex reportM;
inline std::set<std::string> seen;
inline size_t failures=0,probes=0;
inline bool failuresCapped=false,probesCapped=false;
inline void emit(const std::string& text) {if(auto s=sink)s(text.c_str());}
inline void report(bool probe,const std::string& key,const std::string& text) {
    std::string line=text,note;
    {
        std::lock_guard<std::mutex> lock(reportM);
        size_t& used=probe?probes:failures;bool& capped=probe?probesCapped:failuresCapped;
        if(seen.count(key))return;
        if(used>=(probe?kMaxProbes:kMaxFailures)) {
            if(!capped){capped=true;note=probe?"further capability answers suppressed":"further failed calls suppressed";}
            else return;
            line.clear();
        } else {seen.insert(key);++used;}
    }
    if(!line.empty())emit(line);
    if(!note.empty())emit(note+" (cap reached)");
}
inline std::string hex(HRESULT hr){char b[16];snprintf(b,sizeof b,"0x%08lx",(unsigned long)hr);return b;}
inline std::string fmt(const char* f,...) {
    char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);return b;
}

// ---- formatting helpers
inline const char* formatName(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_UNKNOWN:return "UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:return "R32G32B32A32_FLOAT";case DXGI_FORMAT_R32G32B32_FLOAT:return "R32G32B32_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:return "R16G16B16A16_FLOAT";case DXGI_FORMAT_R16G16B16A16_UNORM:return "R16G16B16A16_UNORM";
    case DXGI_FORMAT_R32G32_FLOAT:return "R32G32_FLOAT";case DXGI_FORMAT_R10G10B10A2_UNORM:return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R11G11B10_FLOAT:return "R11G11B10_FLOAT";case DXGI_FORMAT_R8G8B8A8_TYPELESS:return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:return "R8G8B8A8_UNORM";case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R16G16_FLOAT:return "R16G16_FLOAT";case DXGI_FORMAT_R16G16_UNORM:return "R16G16_UNORM";
    case DXGI_FORMAT_R32_TYPELESS:return "R32_TYPELESS";case DXGI_FORMAT_D32_FLOAT:return "D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:return "R32_FLOAT";case DXGI_FORMAT_R32_UINT:return "R32_UINT";
    case DXGI_FORMAT_R24G8_TYPELESS:return "R24G8_TYPELESS";case DXGI_FORMAT_D24_UNORM_S8_UINT:return "D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return "R24_UNORM_X8_TYPELESS";case DXGI_FORMAT_R8G8_UNORM:return "R8G8_UNORM";
    case DXGI_FORMAT_R16_TYPELESS:return "R16_TYPELESS";case DXGI_FORMAT_R16_FLOAT:return "R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM:return "D16_UNORM";case DXGI_FORMAT_R16_UNORM:return "R16_UNORM";
    case DXGI_FORMAT_R16_UINT:return "R16_UINT";case DXGI_FORMAT_R8_UNORM:return "R8_UNORM";
    case DXGI_FORMAT_A8_UNORM:return "A8_UNORM";case DXGI_FORMAT_BC1_UNORM:return "BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:return "BC1_UNORM_SRGB";case DXGI_FORMAT_BC2_UNORM:return "BC2_UNORM";
    case DXGI_FORMAT_BC3_UNORM:return "BC3_UNORM";case DXGI_FORMAT_BC3_UNORM_SRGB:return "BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_UNORM:return "BC4_UNORM";case DXGI_FORMAT_BC5_UNORM:return "BC5_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:return "B8G8R8A8_UNORM";case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_UF16:return "BC6H_UF16";case DXGI_FORMAT_BC7_UNORM:return "BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:return "BC7_UNORM_SRGB";
    default:return "";
    }
}
inline std::string formatText(DXGI_FORMAT f) {
    const char* n=formatName(f);return *n?fmt("%d(%s)",int(f),n):fmt("%d",int(f));
}
inline std::string supportText(UINT s) {
    static const struct {UINT bit;const char* name;} bits[]={
        {0x1,"BUFFER"},{0x2,"IA_VB"},{0x4,"IA_IB"},{0x10,"TEX1D"},{0x20,"TEX2D"},{0x40,"TEX3D"},{0x80,"CUBE"},{0x100,"LOAD"},
        {0x200,"SAMPLE"},{0x400,"SAMPLE_CMP"},{0x1000,"MIP"},{0x2000,"MIP_AUTOGEN"},{0x4000,"RT"},{0x8000,"BLEND"},
        {0x10000,"DS"},{0x20000,"CPU_LOCK"},{0x40000,"MS_RESOLVE"},{0x200000,"MS_RT"},{0x400000,"MS_LOAD"},{0x800000,"GATHER"},
        {0x1000000,"TYPED_UAV"}};
    std::string out;
    for(auto& b:bits) if(s&b.bit){if(!out.empty())out+=",";out+=b.name;}
    return out.empty()?"none":out;
}
inline uint64_t fnv(const void* data,size_t n,uint64_t h=1469598103934665603ull) {
    auto p=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
}

// ---- vtable patching
struct Patch {int slot;void* hook;};
struct Class {void** vtable=nullptr;bool patched[kSlots]={};void* original[kSlots]={};};
inline Class classes[kMaxClasses];
inline std::atomic<size_t> classCount{0};
inline std::mutex patchM;
inline void* original(void* self,int slot) {
    void** vtable=*static_cast<void***>(self);
    size_t n=classCount.load(std::memory_order_acquire);
    for(size_t i=0;i<n;++i) if(classes[i].vtable==vtable) return classes[i].original[slot];
    return nullptr;
}
// Writes the hooks missing from this vtable; returns whether the vtable now
// carries them. Callers pass only slots the vtable's interface defines.
inline bool patchClass(void** vtable,const Patch* patches,size_t count) {
    std::lock_guard<std::mutex> lock(patchM);
    size_t n=classCount.load();Class* c=nullptr;
    for(size_t i=0;i<n;++i) if(classes[i].vtable==vtable)c=&classes[i];
    if(!c) {
        if(n>=kMaxClasses)return false;
        c=&classes[n];c->vtable=vtable;
    }
    int low=kSlots,high=-1;
    for(size_t i=0;i<count;++i) if(!c->patched[patches[i].slot]) {low=std::min(low,patches[i].slot);high=std::max(high,patches[i].slot);}
    if(high<0)return true;
    DWORD old=0;
    if(!VirtualProtect(vtable+low,sizeof(void*)*size_t(high-low+1),PAGE_READWRITE,&old)) {
        emit(fmt("diagnostics cannot patch a device vtable (error %lu)",GetLastError()));return false;
    }
    for(size_t i=0;i<count;++i) {
        int s=patches[i].slot;if(c->patched[s])continue;
        c->original[s]=vtable[s];vtable[s]=patches[i].hook;c->patched[s]=true;
    }
    DWORD ignored=0;VirtualProtect(vtable+low,sizeof(void*)*size_t(high-low+1),old,&ignored);
    if(c==&classes[n])classCount.store(n+1,std::memory_order_release);
    return true;
}

// ---- description of the arguments of a failed call
inline std::string resourceText(ID3D11Resource* r) {
    if(!r)return "res=null";
    D3D11_RESOURCE_DIMENSION d=D3D11_RESOURCE_DIMENSION_UNKNOWN;r->GetType(&d);
    switch(d) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:{D3D11_BUFFER_DESC b{};static_cast<ID3D11Buffer*>(r)->GetDesc(&b);
        return fmt("res=buffer bytes=%u bind=0x%x usage=%d misc=0x%x stride=%u",b.ByteWidth,b.BindFlags,int(b.Usage),b.MiscFlags,b.StructureByteStride);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:{D3D11_TEXTURE1D_DESC t{};static_cast<ID3D11Texture1D*>(r)->GetDesc(&t);
        return fmt("res=tex1d fmt=%s %u mips=%u arr=%u bind=0x%x usage=%d",formatText(t.Format).c_str(),t.Width,t.MipLevels,t.ArraySize,t.BindFlags,int(t.Usage));}
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:{D3D11_TEXTURE2D_DESC t{};static_cast<ID3D11Texture2D*>(r)->GetDesc(&t);
        return fmt("res=tex2d fmt=%s %ux%u mips=%u arr=%u ms=%u bind=0x%x usage=%d misc=0x%x",formatText(t.Format).c_str(),t.Width,t.Height,t.MipLevels,t.ArraySize,t.SampleDesc.Count,t.BindFlags,int(t.Usage),t.MiscFlags);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:{D3D11_TEXTURE3D_DESC t{};static_cast<ID3D11Texture3D*>(r)->GetDesc(&t);
        return fmt("res=tex3d fmt=%s %ux%ux%u mips=%u bind=0x%x usage=%d",formatText(t.Format).c_str(),t.Width,t.Height,t.Depth,t.MipLevels,t.BindFlags,int(t.Usage));}
    default:return "res=unknown";
    }
}
// Shape of a resource without its sizes: what decides whether a call is a
// new kind of failure.
inline std::string resourceKey(ID3D11Resource* r) {
    if(!r)return "null";
    D3D11_RESOURCE_DIMENSION d=D3D11_RESOURCE_DIMENSION_UNKNOWN;r->GetType(&d);
    switch(d) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:{D3D11_BUFFER_DESC b{};static_cast<ID3D11Buffer*>(r)->GetDesc(&b);return fmt("b%x/%d/%x/%u",b.BindFlags,int(b.Usage),b.MiscFlags,b.StructureByteStride);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:{D3D11_TEXTURE1D_DESC t{};static_cast<ID3D11Texture1D*>(r)->GetDesc(&t);return fmt("1d%d/%x/%d",int(t.Format),t.BindFlags,int(t.Usage));}
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:{D3D11_TEXTURE2D_DESC t{};static_cast<ID3D11Texture2D*>(r)->GetDesc(&t);return fmt("2d%d/%x/%d/%x/%u/%u",int(t.Format),t.BindFlags,int(t.Usage),t.MiscFlags,t.SampleDesc.Count,t.ArraySize);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:{D3D11_TEXTURE3D_DESC t{};static_cast<ID3D11Texture3D*>(r)->GetDesc(&t);return fmt("3d%d/%x/%d",int(t.Format),t.BindFlags,int(t.Usage));}
    default:return "?";
    }
}
// Shader model, program type and a content hash of a DXBC container, so a
// failed shader can be told apart and matched to the game's own files.
inline std::string shaderText(const void* code,SIZE_T length,std::string* key=nullptr) {
    if(!code||!length){if(key)*key="none";return "bytecode=none";}
    auto bytes=static_cast<const unsigned char*>(code);
    uint64_t hash=fnv(code,size_t(std::min<SIZE_T>(length,16u<<20)));
    std::string model="?";
    auto word=[&](size_t at)->uint32_t{uint32_t v=0;if(at+4<=length)memcpy(&v,bytes+at,4);return v;};
    if(length>=32&&!memcmp(bytes,"DXBC",4)) {
        uint32_t chunks=word(28);
        for(uint32_t i=0;i<chunks&&i<64;++i) {
            size_t at=word(32+4*i);
            if(at+16>length)break;
            if(!memcmp(bytes+at,"SHDR",4)||!memcmp(bytes+at,"SHEX",4)) {
                uint32_t token=word(at+8);
                static const char* types[]={"ps","vs","gs","hs","ds","cs"};
                unsigned type=token>>16;
                model=fmt("%s_%u_%u",type<6?types[type]:"?",(token>>4)&0xf,token&0xf);
                break;
            }
        }
    } else model="not-dxbc";
    if(key)*key=fmt("%016llx",(unsigned long long)hash);
    return fmt("model=%s bytes=%zu hash=%016llx",model.c_str(),size_t(length),(unsigned long long)hash);
}

#define D3D11_DIAG_HOOK(Type,slot) auto call=reinterpret_cast<Type>(original(self,slot));if(!call)return E_FAIL;
#define D3D11_DIAG_FAILED(name,key,text) do{if(FAILED(hr))report(false,std::string(name)+" "+hex(hr)+" "+(key),std::string(name)+" hr="+hex(hr)+" "+(text));}while(0)

using DevicePtr=void*;
static HRESULT STDMETHODCALLTYPE createBuffer(DevicePtr self,const D3D11_BUFFER_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Buffer** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer**);
    D3D11_DIAG_HOOK(Fn,CreateBuffer)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateBuffer",fmt("%x/%d/%x/%x/%u/%d",d->BindFlags,int(d->Usage),d->MiscFlags,d->CPUAccessFlags,d->StructureByteStride,init!=nullptr),
        fmt("bytes=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x stride=%u init=%d",d->ByteWidth,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,d->StructureByteStride,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateBuffer","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createTexture1D(DevicePtr self,const D3D11_TEXTURE1D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture1D** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_TEXTURE1D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture1D**);
    D3D11_DIAG_HOOK(Fn,CreateTexture1D)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateTexture1D",fmt("%d/%x/%d/%x/%x/%d",int(d->Format),d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr),
        fmt("fmt=%s width=%u mips=%u arr=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x init=%d",formatText(d->Format).c_str(),d->Width,d->MipLevels,d->ArraySize,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateTexture1D","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createTexture2D(DevicePtr self,const D3D11_TEXTURE2D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture2D** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
    D3D11_DIAG_HOOK(Fn,CreateTexture2D)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateTexture2D",fmt("%d/%x/%d/%x/%x/%u/%u/%d",int(d->Format),d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,d->SampleDesc.Count,d->ArraySize,init!=nullptr),
        fmt("fmt=%s %ux%u mips=%u arr=%u ms=%u/%u bind=0x%x usage=%d cpu=0x%x misc=0x%x init=%d",formatText(d->Format).c_str(),d->Width,d->Height,d->MipLevels,d->ArraySize,d->SampleDesc.Count,d->SampleDesc.Quality,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateTexture2D","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createTexture3D(DevicePtr self,const D3D11_TEXTURE3D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture3D** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_TEXTURE3D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture3D**);
    D3D11_DIAG_HOOK(Fn,CreateTexture3D)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateTexture3D",fmt("%d/%x/%d/%x/%x/%d",int(d->Format),d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr),
        fmt("fmt=%s %ux%ux%u mips=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x init=%d",formatText(d->Format).c_str(),d->Width,d->Height,d->Depth,d->MipLevels,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateTexture3D","null","desc=null");
    return hr;
}
// Views share one shape: (resource, optional desc, out). Only the view
// description differs.
template<int Slot,class Desc,class Out,class Describe>
static HRESULT viewCall(DevicePtr self,const char* name,ID3D11Resource* resource,const Desc* d,Out** out,Describe describe) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,const Desc*,Out**);
    D3D11_DIAG_HOOK(Fn,Slot)HRESULT hr=call(self,resource,d,out);
    if(FAILED(hr)) {
        std::string view,viewKey;describe(d,view,viewKey);
        D3D11_DIAG_FAILED(name,viewKey+"|"+resourceKey(resource),view+" "+resourceText(resource));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createSrv(DevicePtr self,ID3D11Resource* r,const D3D11_SHADER_RESOURCE_VIEW_DESC* d,ID3D11ShaderResourceView** out) {
    return viewCall<CreateShaderResourceView>(self,"CreateShaderResourceView",r,d,out,[](const D3D11_SHADER_RESOURCE_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createUav(DevicePtr self,ID3D11Resource* r,const D3D11_UNORDERED_ACCESS_VIEW_DESC* d,ID3D11UnorderedAccessView** out) {
    return viewCall<CreateUnorderedAccessView>(self,"CreateUnorderedAccessView",r,d,out,[](const D3D11_UNORDERED_ACCESS_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createRtv(DevicePtr self,ID3D11Resource* r,const D3D11_RENDER_TARGET_VIEW_DESC* d,ID3D11RenderTargetView** out) {
    return viewCall<CreateRenderTargetView>(self,"CreateRenderTargetView",r,d,out,[](const D3D11_RENDER_TARGET_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createDsv(DevicePtr self,ID3D11Resource* r,const D3D11_DEPTH_STENCIL_VIEW_DESC* d,ID3D11DepthStencilView** out) {
    return viewCall<CreateDepthStencilView>(self,"CreateDepthStencilView",r,d,out,[](const D3D11_DEPTH_STENCIL_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d flags=0x%x",formatText(v->Format).c_str(),int(v->ViewDimension),v->Flags);key=fmt("%d/%d/%x",int(v->Format),int(v->ViewDimension),v->Flags);});
}
static HRESULT STDMETHODCALLTYPE createInputLayout(DevicePtr self,const D3D11_INPUT_ELEMENT_DESC* elements,UINT count,const void* code,SIZE_T length,ID3D11InputLayout** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
    D3D11_DIAG_HOOK(Fn,CreateInputLayout)HRESULT hr=call(self,elements,count,code,length,out);
    if(FAILED(hr)) {
        std::string list,key;
        for(UINT i=0;i<count&&elements;++i) {
            if(i<12)list+=fmt("%s%u:%d@%u ",elements[i].SemanticName?elements[i].SemanticName:"?",elements[i].SemanticIndex,int(elements[i].Format),elements[i].InputSlot);
            key+=fmt("%s%u:%d:%u:%u,",elements[i].SemanticName?elements[i].SemanticName:"?",elements[i].SemanticIndex,int(elements[i].Format),elements[i].InputSlot,elements[i].AlignedByteOffset);
        }
        std::string shaderKey,shader=shaderText(code,length,&shaderKey);
        D3D11_DIAG_FAILED("CreateInputLayout",fmt("%016llx",(unsigned long long)fnv(key.data(),key.size()))+"/"+shaderKey,fmt("elements=%u [%s] %s",count,list.c_str(),shader.c_str()));
    }
    return hr;
}
template<int Slot>
static HRESULT STDMETHODCALLTYPE createShader(DevicePtr self,const void* code,SIZE_T length,ID3D11ClassLinkage* linkage,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const void*,SIZE_T,ID3D11ClassLinkage*,void**);
    D3D11_DIAG_HOOK(Fn,Slot)HRESULT hr=call(self,code,length,linkage,out);
    if(FAILED(hr)) {
        static const struct {int slot;const char* name;} names[]={{CreateVertexShader,"CreateVertexShader"},{CreateGeometryShader,"CreateGeometryShader"},
            {CreatePixelShader,"CreatePixelShader"},{CreateHullShader,"CreateHullShader"},{CreateDomainShader,"CreateDomainShader"},{CreateComputeShader,"CreateComputeShader"}};
        const char* name="CreateShader";for(auto& n:names) if(n.slot==Slot)name=n.name;
        std::string key,text=shaderText(code,length,&key);
        D3D11_DIAG_FAILED(name,key,text+fmt(" linkage=%d",linkage!=nullptr));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createGeometryShaderWithStreamOutput(DevicePtr self,const void* code,SIZE_T length,const D3D11_SO_DECLARATION_ENTRY* entries,UINT entryCount,
        const UINT* strides,UINT strideCount,UINT rasterized,ID3D11ClassLinkage* linkage,ID3D11GeometryShader** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**);
    D3D11_DIAG_HOOK(Fn,CreateGeometryShaderWithStreamOutput)HRESULT hr=call(self,code,length,entries,entryCount,strides,strideCount,rasterized,linkage,out);
    if(FAILED(hr)){std::string key,text=shaderText(code,length,&key);D3D11_DIAG_FAILED("CreateGeometryShaderWithStreamOutput",key,text+fmt(" so-entries=%u strides=%u rasterized=%u",entryCount,strideCount,rasterized));}
    return hr;
}
static HRESULT STDMETHODCALLTYPE createClassLinkage(DevicePtr self,ID3D11ClassLinkage** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D11ClassLinkage**);
    D3D11_DIAG_HOOK(Fn,CreateClassLinkage)HRESULT hr=call(self,out);
    D3D11_DIAG_FAILED("CreateClassLinkage","","");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createBlendState(DevicePtr self,const D3D11_BLEND_DESC* d,ID3D11BlendState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_BLEND_DESC*,ID3D11BlendState**);
    D3D11_DIAG_HOOK(Fn,CreateBlendState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d) {
        auto& t=d->RenderTarget[0];
        D3D11_DIAG_FAILED("CreateBlendState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
            fmt("a2c=%d independent=%d rt0:enable=%d src=%d dst=%d op=%d srca=%d dsta=%d opa=%d mask=0x%x",d->AlphaToCoverageEnable,d->IndependentBlendEnable,t.BlendEnable,
                int(t.SrcBlend),int(t.DestBlend),int(t.BlendOp),int(t.SrcBlendAlpha),int(t.DestBlendAlpha),int(t.BlendOpAlpha),t.RenderTargetWriteMask));
    } else if(FAILED(hr))D3D11_DIAG_FAILED("CreateBlendState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDepthStencilState(DevicePtr self,const D3D11_DEPTH_STENCIL_DESC* d,ID3D11DepthStencilState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_DEPTH_STENCIL_DESC*,ID3D11DepthStencilState**);
    D3D11_DIAG_HOOK(Fn,CreateDepthStencilState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateDepthStencilState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("depth=%d write=%d func=%d stencil=%d",d->DepthEnable,int(d->DepthWriteMask),int(d->DepthFunc),d->StencilEnable));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateDepthStencilState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createRasterizerState(DevicePtr self,const D3D11_RASTERIZER_DESC* d,ID3D11RasterizerState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_RASTERIZER_DESC*,ID3D11RasterizerState**);
    D3D11_DIAG_HOOK(Fn,CreateRasterizerState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateRasterizerState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("fill=%d cull=%d depthclip=%d scissor=%d multisample=%d aa-line=%d bias=%d",int(d->FillMode),int(d->CullMode),d->DepthClipEnable,d->ScissorEnable,d->MultisampleEnable,d->AntialiasedLineEnable,d->DepthBias));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateRasterizerState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createSamplerState(DevicePtr self,const D3D11_SAMPLER_DESC* d,ID3D11SamplerState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_SAMPLER_DESC*,ID3D11SamplerState**);
    D3D11_DIAG_HOOK(Fn,CreateSamplerState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateSamplerState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("filter=0x%x address=%d/%d/%d aniso=%u cmp=%d lod=%g..%g",unsigned(d->Filter),int(d->AddressU),int(d->AddressV),int(d->AddressW),d->MaxAnisotropy,int(d->ComparisonFunc),d->MinLOD,d->MaxLOD));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateSamplerState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createBlendState1(DevicePtr self,const D3D11_BLEND_DESC1* d,ID3D11BlendState1** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_BLEND_DESC1*,ID3D11BlendState1**);
    D3D11_DIAG_HOOK(Fn,CreateBlendState1)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d) {
        auto& t=d->RenderTarget[0];
        D3D11_DIAG_FAILED("CreateBlendState1",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
            fmt("a2c=%d independent=%d rt0:enable=%d logicop=%d/%d src=%d dst=%d mask=0x%x",d->AlphaToCoverageEnable,d->IndependentBlendEnable,t.BlendEnable,t.LogicOpEnable,int(t.LogicOp),int(t.SrcBlend),int(t.DestBlend),t.RenderTargetWriteMask));
    } else if(FAILED(hr))D3D11_DIAG_FAILED("CreateBlendState1","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createRasterizerState1(DevicePtr self,const D3D11_RASTERIZER_DESC1* d,ID3D11RasterizerState1** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_RASTERIZER_DESC1*,ID3D11RasterizerState1**);
    D3D11_DIAG_HOOK(Fn,CreateRasterizerState1)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateRasterizerState1",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("fill=%d cull=%d depthclip=%d scissor=%d forcedsamples=%u",int(d->FillMode),int(d->CullMode),d->DepthClipEnable,d->ScissorEnable,d->ForcedSampleCount));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateRasterizerState1","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createQuery(DevicePtr self,const D3D11_QUERY_DESC* d,ID3D11Query** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_QUERY_DESC*,ID3D11Query**);
    D3D11_DIAG_HOOK(Fn,CreateQuery)HRESULT hr=call(self,d,out);
    if(FAILED(hr))D3D11_DIAG_FAILED("CreateQuery",d?fmt("%d/%x",int(d->Query),d->MiscFlags):"null",d?fmt("query=%d misc=0x%x",int(d->Query),d->MiscFlags):"desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createPredicate(DevicePtr self,const D3D11_QUERY_DESC* d,ID3D11Predicate** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_QUERY_DESC*,ID3D11Predicate**);
    D3D11_DIAG_HOOK(Fn,CreatePredicate)HRESULT hr=call(self,d,out);
    if(FAILED(hr))D3D11_DIAG_FAILED("CreatePredicate",d?fmt("%d/%x",int(d->Query),d->MiscFlags):"null",d?fmt("query=%d misc=0x%x",int(d->Query),d->MiscFlags):"desc=null");
    return hr;
}

// ---- immediate/deferred context: only Map, the call with the most
// undocumented driver restrictions (NO_OVERWRITE on non-buffers, staging use).
inline bool patchContext(ID3D11DeviceContext* context);
static HRESULT STDMETHODCALLTYPE contextMap(DevicePtr self,ID3D11Resource* r,UINT subresource,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* mapped) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    auto call=reinterpret_cast<Fn>(original(self,ContextMap));if(!call)return E_FAIL;
    HRESULT hr=call(self,r,subresource,type,flags,mapped);
    if(FAILED(hr)&&hr!=DXGI_ERROR_WAS_STILL_DRAWING)   // expected with D3D11_MAP_FLAG_DO_NOT_WAIT
        D3D11_DIAG_FAILED("Map",fmt("%d/%x/%s",int(type),flags,resourceKey(r).c_str()),fmt("type=%d flags=0x%x subresource=%u %s",int(type),flags,subresource,resourceText(r).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDeferredContext(DevicePtr self,UINT flags,ID3D11DeviceContext** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D11DeviceContext**);
    D3D11_DIAG_HOOK(Fn,CreateDeferredContext)HRESULT hr=call(self,flags,out);
    if(SUCCEEDED(hr)&&out&&*out)patchContext(*out);
    D3D11_DIAG_FAILED("CreateDeferredContext",fmt("%x",flags),fmt("flags=0x%x",flags));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDeferredContext1(DevicePtr self,UINT flags,ID3D11DeviceContext1** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D11DeviceContext1**);
    D3D11_DIAG_HOOK(Fn,CreateDeferredContext1)HRESULT hr=call(self,flags,out);
    if(SUCCEEDED(hr)&&out&&*out)patchContext(*out);
    D3D11_DIAG_FAILED("CreateDeferredContext1",fmt("%x",flags),fmt("flags=0x%x",flags));
    return hr;
}
inline bool patchContext(ID3D11DeviceContext* context) {
    static const Patch patches[]={{ContextMap,(void*)&contextMap}};
    return context&&patchClass(*reinterpret_cast<void***>(context),patches,1);
}

// ---- capability queries
inline std::string bytesText(const void* data,UINT size) {
    std::string out;auto p=static_cast<const unsigned char*>(data);
    for(UINT i=0;i<size&&i<32;++i)out+=fmt("%02x",p[i]);
    return out.empty()?"-":out;
}
static HRESULT STDMETHODCALLTYPE checkFormatSupport(DevicePtr self,DXGI_FORMAT format,UINT* support) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,DXGI_FORMAT,UINT*);
    D3D11_DIAG_HOOK(Fn,CheckFormatSupport)HRESULT hr=call(self,format,support);
    UINT bits=SUCCEEDED(hr)&&support?*support:0;
    if(FAILED(hr)||!bits)D3D11_DIAG_FAILED("CheckFormatSupport",fmt("%d/%x",int(format),bits),fmt("fmt=%s support=0x%x UNSUPPORTED",formatText(format).c_str(),bits));
    else report(true,fmt("CheckFormatSupport %d/%x",int(format),bits),fmt("CheckFormatSupport fmt=%s support=0x%x [%s]",formatText(format).c_str(),bits,supportText(bits).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE checkMultisampleQualityLevels(DevicePtr self,DXGI_FORMAT format,UINT samples,UINT* levels) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,DXGI_FORMAT,UINT,UINT*);
    D3D11_DIAG_HOOK(Fn,CheckMultisampleQualityLevels)HRESULT hr=call(self,format,samples,levels);
    UINT n=SUCCEEDED(hr)&&levels?*levels:0;
    if(FAILED(hr)||!n)D3D11_DIAG_FAILED("CheckMultisampleQualityLevels",fmt("%d/%u",int(format),samples),fmt("fmt=%s samples=%u levels=%u UNSUPPORTED",formatText(format).c_str(),samples,n));
    return hr;
}
static HRESULT STDMETHODCALLTYPE checkFeatureSupport(DevicePtr self,D3D11_FEATURE feature,void* data,UINT size) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D11_FEATURE,void*,UINT);
    D3D11_DIAG_HOOK(Fn,CheckFeatureSupport)HRESULT hr=call(self,feature,data,size);
    if(FAILED(hr))D3D11_DIAG_FAILED("CheckFeatureSupport",fmt("%d/%u",int(feature),size),fmt("feature=%d size=%u",int(feature),size));
    else report(true,fmt("CheckFeatureSupport %d/%u/%s",int(feature),size,bytesText(data,size).c_str()),
        fmt("CheckFeatureSupport feature=%d size=%u data=%s",int(feature),size,bytesText(data,size).c_str()));
    return hr;
}

// The hooks every ID3D11Device vtable gets, and the ones only an
// ID3D11Device1-or-later vtable (a superset) may get.
inline const Patch* basePatches(size_t& count){
    static const Patch table[]={
        {CreateBuffer,(void*)&createBuffer},{CreateTexture1D,(void*)&createTexture1D},{CreateTexture2D,(void*)&createTexture2D},
        {CreateTexture3D,(void*)&createTexture3D},{CreateShaderResourceView,(void*)&createSrv},{CreateUnorderedAccessView,(void*)&createUav},
        {CreateRenderTargetView,(void*)&createRtv},{CreateDepthStencilView,(void*)&createDsv},{CreateInputLayout,(void*)&createInputLayout},
        {CreateVertexShader,(void*)&createShader<CreateVertexShader>},{CreateGeometryShader,(void*)&createShader<CreateGeometryShader>},
        {CreateGeometryShaderWithStreamOutput,(void*)&createGeometryShaderWithStreamOutput},{CreatePixelShader,(void*)&createShader<CreatePixelShader>},
        {CreateHullShader,(void*)&createShader<CreateHullShader>},{CreateDomainShader,(void*)&createShader<CreateDomainShader>},
        {CreateComputeShader,(void*)&createShader<CreateComputeShader>},{CreateClassLinkage,(void*)&createClassLinkage},
        {CreateBlendState,(void*)&createBlendState},{CreateDepthStencilState,(void*)&createDepthStencilState},
        {CreateRasterizerState,(void*)&createRasterizerState},{CreateSamplerState,(void*)&createSamplerState},{CreateQuery,(void*)&createQuery},
        {CreatePredicate,(void*)&createPredicate},{CreateDeferredContext,(void*)&createDeferredContext},{CheckFormatSupport,(void*)&checkFormatSupport},
        {CheckMultisampleQualityLevels,(void*)&checkMultisampleQualityLevels},{CheckFeatureSupport,(void*)&checkFeatureSupport}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* extendedPatches(size_t& count){
    static const Patch table[]={
        {CreateDeferredContext1,(void*)&createDeferredContext1},{CreateBlendState1,(void*)&createBlendState1},{CreateRasterizerState1,(void*)&createRasterizerState1}};
    count=sizeof table/sizeof *table;return table;
}
// ---- entry points
inline void configure(Sink s){sink=s;}
// Called with every device Play gives a guest. Patches the device class and
// the immediate context; logs the adapter the guest can see and the
// capability answers that pick engine code paths.
inline void adopt(ID3D11Device* device) {
    if(!device||!sink)return;
    size_t baseCount=0,extendedCount=0;
    const Patch* base=basePatches(baseCount);const Patch* extended=extendedPatches(extendedCount);
    bool ok=patchClass(*reinterpret_cast<void***>(device),base,baseCount);
    // ID3D11Device1's vtable continues ID3D11Device's: the base hooks plus the
    // 11.1 creators go on every vtable QueryInterface returns for it.
    ID3D11Device1* device1=nullptr;
    if(SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(&device1)))&&device1) {
        void** vtable=*reinterpret_cast<void***>(device1);
        patchClass(vtable,base,baseCount);patchClass(vtable,extended,extendedCount);
        device1->Release();
    }
    ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);
    if(context){patchContext(context);context->Release();}
    if(!ok)return;
    emit(fmt("diagnostics on: failed Create*/Check*/Map calls are logged once per distinct call (caps %zu/%zu)",kMaxFailures,kMaxProbes));
    IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;
    if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi)))&&dxgi&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&adapter) {
        DXGI_ADAPTER_DESC d{};
        if(SUCCEEDED(adapter->GetDesc(&d))) {
            char name[128]{};WideCharToMultiByte(CP_UTF8,0,d.Description,-1,name,sizeof name-1,nullptr,nullptr);
            report(true,fmt("adapter %04x/%04x",d.VendorId,d.DeviceId),fmt("adapter vendor=0x%04x device=0x%04x rev=%u name=\"%s\" vram=%zuMB sysmem=%zuMB shared=%zuMB",
                d.VendorId,d.DeviceId,d.Revision,name,size_t(d.DedicatedVideoMemory>>20),size_t(d.DedicatedSystemMemory>>20),size_t(d.SharedSystemMemory>>20)));
        }
    }
    if(adapter)adapter->Release();
    if(dxgi)dxgi->Release();
    report(true,"device feature level",fmt("device feature level=0x%x creation flags=0x%x",unsigned(device->GetFeatureLevel()),device->GetCreationFlags()));
    // Capability answers games branch on. Unsupported ones log as failures.
    D3D11_FEATURE_DATA_THREADING threading{};device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof threading);
    D3D11_FEATURE_DATA_DOUBLES doubles{};device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES,&doubles,sizeof doubles);
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS tenx{};device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS,&tenx,sizeof tenx);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof options);
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO architecture{};device->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO,&architecture,sizeof architecture);
    D3D11_FEATURE_DATA_D3D9_OPTIONS nine{};device->CheckFeatureSupport(D3D11_FEATURE_D3D9_OPTIONS,&nine,sizeof nine);
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 options1{};device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1,&options1,sizeof options1);
    // Formats terrain/height-map and compressed-texture paths probe for.
    static const DXGI_FORMAT formats[]={DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC3_UNORM,DXGI_FORMAT_BC5_UNORM,
        DXGI_FORMAT_BC6H_UF16,DXGI_FORMAT_BC7_UNORM,DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_R8_UNORM};
    for(auto format:formats){UINT s=0;device->CheckFormatSupport(format,&s);}
}
#undef D3D11_DIAG_HOOK
#undef D3D11_DIAG_FAILED
}
