// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contracts of the D3D11 failure diagnostics (host/d3d11_diag.h) on a
// native Wine d3d11 device: failed calls log once per distinct call, results
// pass through unchanged, several devices and d3d11.1 interfaces are safe, the
// log is bounded. No rendering claim.
#include <d3d11_1.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../host/d3d11_diag.h"
static unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)
static std::vector<std::string> lines;
static void sinkLine(const char* text){lines.push_back(text);}
static size_t count(const char* needle){size_t n=0;for(auto& l:lines)if(l.find(needle)!=std::string::npos)++n;return n;}
static HRESULT STDMETHODCALLTYPE alwaysInvalid(void*,void*,void*,void*){return E_INVALIDARG;}
static ID3D11Device* make(ID3D11DeviceContext** context=nullptr) {
    static const D3D_DRIVER_TYPE types[]={D3D_DRIVER_TYPE_HARDWARE,D3D_DRIVER_TYPE_WARP,D3D_DRIVER_TYPE_REFERENCE};
    for(auto type:types) {
        ID3D11Device* device=nullptr;
        if(SUCCEEDED(D3D11CreateDevice(nullptr,type,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,context)))return device;
    }
    return nullptr;
}
int main() {
    ID3D11DeviceContext* context=nullptr;
    ID3D11Device* device=make(&context);
    if(!device){printf("OFF-DEVICE no d3d11 device available\n");return 2;}
    // A bare DXBC container with a pixel shader chunk (ps_5_0): enough for the
    // model/hash parser; the device rejects it as a shader.
    unsigned char dxbc[64]{};memcpy(dxbc,"DXBC",4);
    uint32_t chunkCount=1,chunkOffset=36;memcpy(dxbc+28,&chunkCount,4);memcpy(dxbc+32,&chunkOffset,4);
    memcpy(dxbc+36,"SHEX",4);uint32_t chunkSize=8,token=0x50;memcpy(dxbc+40,&chunkSize,4);memcpy(dxbc+44,&token,4);
    std::string key,text=d3d11_diag::shaderText(dxbc,sizeof dxbc,&key);
    CHECK(text.find("model=ps_5_0")!=std::string::npos&&key.size()==16);
    CHECK(d3d11_diag::shaderText("junk",4).find("model=not-dxbc")!=std::string::npos);
    CHECK(d3d11_diag::shaderText(nullptr,0)=="bytecode=none");

    // Nothing is logged, and nothing is patched, before the sink is set.
    d3d11_diag::adopt(device);
    CHECK(lines.empty()&&d3d11_diag::classCount.load()==0);
    d3d11_diag::configure(sinkLine);
    d3d11_diag::adopt(device);
    size_t classesAfterFirst=d3d11_diag::classCount.load();
    CHECK(classesAfterFirst>=1&&classesAfterFirst<=3);
    CHECK(count("diagnostics on")==1&&count("device feature level=")==1);

    // Failed creations: same results as the device gives, one line each.
    D3D11_TEXTURE2D_DESC bad{};bad.Width=64;bad.Height=64;bad.MipLevels=1;bad.ArraySize=1;bad.Format=DXGI_FORMAT_UNKNOWN;
    bad.SampleDesc.Count=1;bad.Usage=D3D11_USAGE_DEFAULT;bad.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* texture=nullptr;
    HRESULT hr=device->CreateTexture2D(&bad,nullptr,&texture);
    CHECK(FAILED(hr)&&!texture);
    CHECK(count("CreateTexture2D hr=")==1&&count("fmt=0")==1&&count("64x64")==1);
    bad.Width=128;bad.Height=32;   // same kind of failure at another size: not logged again
    CHECK(FAILED(device->CreateTexture2D(&bad,nullptr,&texture)));
    CHECK(count("CreateTexture2D hr=")==1);
    bad.BindFlags=D3D11_BIND_RENDER_TARGET;   // a different kind of failure
    CHECK(FAILED(device->CreateTexture2D(&bad,nullptr,&texture)));
    CHECK(count("CreateTexture2D hr=")==2);
    // A success is untouched and silent.
    D3D11_TEXTURE2D_DESC good=bad;good.Format=DXGI_FORMAT_R8G8B8A8_UNORM;good.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    size_t before=lines.size();
    CHECK(SUCCEEDED(device->CreateTexture2D(&good,nullptr,&texture))&&texture&&lines.size()==before);

    D3D11_BUFFER_DESC emptyBuffer{};emptyBuffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer* buffer=nullptr;
    CHECK(FAILED(device->CreateBuffer(&emptyBuffer,nullptr,&buffer))&&!buffer&&count("CreateBuffer hr=")==1);
    // Views report the resource they were made on.
    ID3D11ShaderResourceView* srv=nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC badView{};badView.Format=DXGI_FORMAT(0x7fff);badView.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;badView.Texture2D.MipLevels=1;
    CHECK(FAILED(device->CreateShaderResourceView(texture,&badView,&srv))&&!srv);
    CHECK(count("CreateShaderResourceView hr=")==1&&count("res=tex2d fmt=28(R8G8B8A8_UNORM)")==1);
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    CHECK(FAILED(device->CreateVertexShader("junk",4,nullptr,&vs))&&!vs&&count("CreateVertexShader hr=")==1);
    CHECK(FAILED(device->CreatePixelShader(dxbc,sizeof dxbc,nullptr,&ps))&&!ps);
    CHECK(count("CreatePixelShader hr=")==1&&count("model=ps_5_0")==1);
    CHECK(FAILED(device->CreatePixelShader(dxbc,sizeof dxbc,nullptr,&ps))&&count("CreatePixelShader hr=")==1);
    ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;ID3D11ComputeShader* cs=nullptr;ID3D11GeometryShader* gs=nullptr;
    CHECK(FAILED(device->CreateHullShader("junk",4,nullptr,&hs))&&count("CreateHullShader hr=")==1);
    CHECK(FAILED(device->CreateDomainShader("junk",4,nullptr,&ds))&&count("CreateDomainShader hr=")==1);
    CHECK(FAILED(device->CreateComputeShader("junk",4,nullptr,&cs))&&count("CreateComputeShader hr=")==1);
    CHECK(FAILED(device->CreateGeometryShader("junk",4,nullptr,&gs))&&count("CreateGeometryShader hr=")==1);
    D3D11_SAMPLER_DESC sampler{};   // zero address modes are invalid
    ID3D11SamplerState* samplerState=nullptr;
    CHECK(FAILED(device->CreateSamplerState(&sampler,&samplerState))&&count("CreateSamplerState hr=")==1);
    D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    ID3D11InputLayout* layout=nullptr;
    CHECK(FAILED(device->CreateInputLayout(&element,1,"junk",4,&layout))&&count("CreateInputLayout hr=")==1&&count("POSITION0:6@0")==1);
    // Capability answers.
    UINT support=1;
    CHECK(FAILED(device->CheckFormatSupport(DXGI_FORMAT(0x7fff),&support))||support==0);
    CHECK(count("CheckFormatSupport hr=")==1);
    CHECK(SUCCEEDED(device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM,&support))&&support!=0);
    CHECK(count("CheckFormatSupport fmt=28(R8G8B8A8_UNORM)")==1);
    CHECK(FAILED(device->CheckFeatureSupport(D3D11_FEATURE(9999),&support,sizeof support))&&count("CheckFeatureSupport hr=")==1);
    UINT levels=0;
    HRESULT msaa=device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,64,&levels);
    CHECK((FAILED(msaa)||levels==0)==(count("CheckMultisampleQualityLevels hr=")==1));

    // Map failure (default-usage buffer is not CPU mappable) on the immediate context.
    D3D11_BUFFER_DESC gpuOnly{};gpuOnly.ByteWidth=64;gpuOnly.BindFlags=D3D11_BIND_VERTEX_BUFFER;gpuOnly.Usage=D3D11_USAGE_DEFAULT;
    CHECK(SUCCEEDED(device->CreateBuffer(&gpuOnly,nullptr,&buffer)));
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(FAILED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))&&count("Map hr=")==1&&count("res=buffer bytes=64")==1);
    CHECK(FAILED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))&&count("Map hr=")==1);


    // Every other hook, against a stand-in whose entries all fail (Wine accepts
    // many invalid state descriptions): installed through the same patch tables.
    {
        static void* vtable[d3d11_diag::kSlots];
        for(auto& entry:vtable)entry=reinterpret_cast<void*>(&alwaysInvalid);
        struct Fake {void** vtable;} fake{vtable};
        size_t baseCount=0,extendedCount=0;
        auto base=d3d11_diag::basePatches(baseCount);auto extended=d3d11_diag::extendedPatches(extendedCount);
        CHECK(d3d11_diag::patchClass(vtable,base,baseCount)&&d3d11_diag::patchClass(vtable,extended,extendedCount));
        CHECK(d3d11_diag::patchClass(vtable,base,baseCount));   // idempotent
        for(size_t i=0;i<baseCount;++i)CHECK(vtable[base[i].slot]==base[i].hook);
        void* self=&fake;
        auto call=[&](int slot,auto... args)->HRESULT{return reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,decltype(args)...)>(vtable[slot])(self,args...);};
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].BlendEnable=TRUE;
        D3D11_BLEND_DESC1 blend1{};D3D11_DEPTH_STENCIL_DESC depth{};D3D11_RASTERIZER_DESC raster{};D3D11_RASTERIZER_DESC1 raster1{};
        D3D11_QUERY_DESC query{};query.Query=D3D11_QUERY_OCCLUSION;D3D11_TEXTURE1D_DESC t1{};t1.Width=16;D3D11_TEXTURE3D_DESC t3{};t3.Depth=4;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        D3D11_SO_DECLARATION_ENTRY so{};
        void* out=nullptr;
        CHECK(call(d3d11_diag::CreateBlendState,&blend,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateBlendState1,&blend1,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDepthStencilState,&depth,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateRasterizerState,&raster,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateRasterizerState1,&raster1,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateQuery,&query,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreatePredicate,&query,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateTexture1D,&t1,(void*)nullptr,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateTexture3D,&t3,(void*)nullptr,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateUnorderedAccessView,(void*)nullptr,&uav,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDepthStencilView,(void*)nullptr,&dsv,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateClassLinkage,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDeferredContext,0u,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateDeferredContext1,0u,&out)==E_INVALIDARG);
        CHECK(call(d3d11_diag::CreateGeometryShaderWithStreamOutput,"junk",size_t(4),&so,1u,(const UINT*)nullptr,0u,0u,(void*)nullptr,&out)==E_INVALIDARG);
        for(const char* name:{"CreateBlendState hr=","CreateBlendState1 hr=","CreateDepthStencilState hr=","CreateRasterizerState hr=","CreateRasterizerState1 hr=",
                "CreateQuery hr=","CreatePredicate hr=","CreateTexture1D hr=","CreateTexture3D hr=","CreateUnorderedAccessView hr=","CreateDepthStencilView hr=",
                "CreateClassLinkage hr=","CreateDeferredContext hr=","CreateDeferredContext1 hr=","CreateGeometryShaderWithStreamOutput hr="})
            CHECK(count(name)==1);
        CHECK(count("rt0:enable=1")==1&&count("query=")>=2);
    }

    // A second device and the d3d11.1 interface share the class: no new patch, same dedupe.
    ID3D11Device* second=make();
    CHECK(second!=nullptr);
    if(second) {
        d3d11_diag::adopt(second);
        CHECK(d3d11_diag::classCount.load()<=classesAfterFirst+2);
        ID3D11Texture2D* again=nullptr;bad.BindFlags=D3D11_BIND_RENDER_TARGET;
        CHECK(FAILED(second->CreateTexture2D(&bad,nullptr,&again)));
        CHECK(count("CreateTexture2D hr=")==2);   // already logged through the first device
        second->Release();
    }

    // The log is bounded; one line says so.
    for(UINT i=0;i<400;++i){D3D11_TEXTURE2D_DESC d=bad;d.MiscFlags=0x100000u+i;ID3D11Texture2D* t=nullptr;device->CreateTexture2D(&d,nullptr,&t);}
    CHECK(lines.size()<=d3d11_diag::kMaxFailures+d3d11_diag::kMaxProbes+4);
    CHECK(count("further failed calls suppressed")==1);
    if(failures)for(auto& l:lines)printf("  %s\n",l.c_str());
    if(texture)texture->Release();
    printf("OFF-DEVICE D3D11 failure diagnostics checks=%u failures=%u lines=%zu; no rendering claim\n",checks,failures,lines.size());
    return failures?1:0;
}
