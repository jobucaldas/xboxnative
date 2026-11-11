// SPDX-License-Identifier: GPL-3.0-or-later
// Order contract for the GPU drain the ResizeBuffers hook runs
// (host/resize_drain.h). The Bugbot HIGH finding this pins down: draining
// must submit the query (End, then Flush) BEFORE polling GetData — a wait
// before that Flush spins on a query the driver has not received while real
// work stays queued, which is exactly the in-flight back-buffer use the drain
// exists to prevent. Fake COM objects log every call; the test asserts the
// exact sequence. No D3D device, runs under Wine headless.
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../host/resize_drain.h"

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

// GENERATED from this NixOS mingw d3d11.h pure virtuals (gcc-16.2.0 sys-include); fakes log every call the drain makes.
static std::vector<std::string> g_log;
static void log_call(const char* s){ g_log.push_back(s); }

struct FakeContext;
struct FakeQuery : ID3D11Query {
    ULONG refs = 1;
    D3D11_QUERY_DESC desc{};
    unsigned polls = 0, sFalseLeft = 2;   // GetData answers S_FALSE twice, then S_OK
    HRESULT answer(void* out){
        log_call("ctx.GetData");
        if(polls++ < sFalseLeft) return S_FALSE;
        *static_cast<BOOL*>(out)=TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if(__uuidof(ID3D11Query)==riid||__uuidof(ID3D11DeviceChild)==riid||__uuidof(IUnknown)==riid){*out=this;AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { log_call("query.Release"); return --refs; }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_FAIL; }
    UINT STDMETHODCALLTYPE GetDataSize() override { return sizeof(BOOL); }
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC *pDesc) override {}
};
struct FakeContext : ID3D11DeviceContext {
    ULONG refs = 1;
    FakeQuery* query = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if(__uuidof(ID3D11DeviceContext)==riid||__uuidof(ID3D11DeviceChild)==riid||__uuidof(IUnknown)==riid){*out=this;AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { log_call("ctx.Release"); return --refs; }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_FAIL; }
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT, UINT, ID3D11Buffer *const *ppConstantBuffers) override {}
    void STDMETHODCALLTYPE PSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT) override {}
    void STDMETHODCALLTYPE PSSetSamplers(UINT, UINT, ID3D11SamplerState *const *ppSamplers) override {}
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT) override {}
    void STDMETHODCALLTYPE DrawIndexed(UINT, UINT, INT) override {}
    void STDMETHODCALLTYPE Draw(UINT, UINT) override {}
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource *pResource, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE *pMappedResource) override {return E_FAIL;}
    void STDMETHODCALLTYPE Unmap(ID3D11Resource *pResource, UINT) override {}
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT, UINT, ID3D11Buffer *const *ppConstantBuffers) override {}
    void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout *pInputLayout) override {}
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT, UINT, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets) override {}
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT, UINT) override {}
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT, UINT, UINT, INT, UINT) override {}
    void STDMETHODCALLTYPE DrawInstanced(UINT, UINT, UINT, UINT) override {}
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT, UINT, ID3D11Buffer *const *ppConstantBuffers) override {}
    void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT) override {}
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY) override {}
    void STDMETHODCALLTYPE VSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE VSSetSamplers(UINT, UINT, ID3D11SamplerState *const *ppSamplers) override {}
    void STDMETHODCALLTYPE Begin(ID3D11Asynchronous *pAsync) override {}
    void STDMETHODCALLTYPE End(ID3D11Asynchronous *pAsync) override {log_call(pAsync==reinterpret_cast<ID3D11Asynchronous*>(query)?"ctx.End":"ctx.End(?!)");}
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous *pAsync, void *pData, UINT, UINT) override {(void)pAsync;return query->answer(pData);}
    void STDMETHODCALLTYPE SetPredication(ID3D11Predicate *pPredicate, WINBOOL) override {}
    void STDMETHODCALLTYPE GSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE GSSetSamplers(UINT, UINT, ID3D11SamplerState *const *ppSamplers) override {}
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView) override {log_call("ctx.OMSetRenderTargets(0)");}
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT, UINT, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts) override {}
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[4], UINT) override {}
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT) override {}
    void STDMETHODCALLTYPE SOSetTargets(UINT, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets) override {}
    void STDMETHODCALLTYPE DrawAuto( ) override {}
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT) override {}
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT) override {}
    void STDMETHODCALLTYPE Dispatch(UINT, UINT, UINT) override {}
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT) override {}
    void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState *pRasterizerState) override {}
    void STDMETHODCALLTYPE RSSetViewports(UINT, const D3D11_VIEWPORT *pViewports) override {}
    void STDMETHODCALLTYPE RSSetScissorRects(UINT, const D3D11_RECT *pRects) override {}
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource *pDstResource, UINT, UINT, UINT, UINT, ID3D11Resource *pSrcResource, UINT, const D3D11_BOX *pSrcBox) override {}
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource) override {}
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource *pDstResource, UINT, const D3D11_BOX *pDstBox, const void *pSrcData, UINT, UINT) override {}
    void STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT, ID3D11UnorderedAccessView *pSrcView) override {}
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[4]) override {}
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[4]) override {}
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[4]) override {}
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT, FLOAT, UINT8) override {}
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView *pShaderResourceView) override {}
    void STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource *pResource, FLOAT) override {}
    FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource *pResource) override {return 0.f;}
    void STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource *pDstResource, UINT, ID3D11Resource *pSrcResource, UINT, DXGI_FORMAT) override {}
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList *pCommandList, WINBOOL) override {}
    void STDMETHODCALLTYPE HSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT) override {}
    void STDMETHODCALLTYPE HSSetSamplers(UINT, UINT, ID3D11SamplerState *const *ppSamplers) override {}
    void STDMETHODCALLTYPE HSSetConstantBuffers(UINT, UINT, ID3D11Buffer *const *ppConstantBuffers) override {}
    void STDMETHODCALLTYPE DSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT) override {}
    void STDMETHODCALLTYPE DSSetSamplers(UINT, UINT, ID3D11SamplerState *const *ppSamplers) override {}
    void STDMETHODCALLTYPE DSSetConstantBuffers(UINT, UINT, ID3D11Buffer *const *ppConstantBuffers) override {}
    void STDMETHODCALLTYPE CSSetShaderResources(UINT, UINT, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts) override {}
    void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT) override {}
    void STDMETHODCALLTYPE CSSetSamplers(UINT, UINT, ID3D11SamplerState *const *ppSamplers) override {}
    void STDMETHODCALLTYPE CSSetConstantBuffers(UINT, UINT, ID3D11Buffer *const *ppConstantBuffers) override {}
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT, UINT, ID3D11Buffer **ppConstantBuffers) override {}
    void STDMETHODCALLTYPE PSGetShaderResources(UINT, UINT, ID3D11ShaderResourceView **ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {}
    void STDMETHODCALLTYPE PSGetSamplers(UINT, UINT, ID3D11SamplerState **ppSamplers) override {}
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {}
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT, UINT, ID3D11Buffer **ppConstantBuffers) override {}
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout **ppInputLayout) override {}
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT, UINT, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets) override {}
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset) override {}
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT, UINT, ID3D11Buffer **ppConstantBuffers) override {}
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {}
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology) override {}
    void STDMETHODCALLTYPE VSGetShaderResources(UINT, UINT, ID3D11ShaderResourceView **ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE VSGetSamplers(UINT, UINT, ID3D11SamplerState **ppSamplers) override {}
    void STDMETHODCALLTYPE GetPredication(ID3D11Predicate **ppPredicate, WINBOOL *pPredicateValue) override {}
    void STDMETHODCALLTYPE GSGetShaderResources(UINT, UINT, ID3D11ShaderResourceView **ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE GSGetSamplers(UINT, UINT, ID3D11SamplerState **ppSamplers) override {}
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView) override {}
    void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT, UINT, ID3D11UnorderedAccessView **ppUnorderedAccessViews) override {}
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[4], UINT *pSampleMask) override {}
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef) override {}
    void STDMETHODCALLTYPE SOGetTargets(UINT, ID3D11Buffer **ppSOTargets) override {}
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState **ppRasterizerState) override {}
    void STDMETHODCALLTYPE RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports) override {}
    void STDMETHODCALLTYPE RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects) override {}
    void STDMETHODCALLTYPE HSGetShaderResources(UINT, UINT, ID3D11ShaderResourceView **ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {}
    void STDMETHODCALLTYPE HSGetSamplers(UINT, UINT, ID3D11SamplerState **ppSamplers) override {}
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT, UINT, ID3D11Buffer **ppConstantBuffers) override {}
    void STDMETHODCALLTYPE DSGetShaderResources(UINT, UINT, ID3D11ShaderResourceView **ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {}
    void STDMETHODCALLTYPE DSGetSamplers(UINT, UINT, ID3D11SamplerState **ppSamplers) override {}
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT, UINT, ID3D11Buffer **ppConstantBuffers) override {}
    void STDMETHODCALLTYPE CSGetShaderResources(UINT, UINT, ID3D11ShaderResourceView **ppShaderResourceViews) override {}
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT, UINT, ID3D11UnorderedAccessView **ppUnorderedAccessViews) override {}
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {}
    void STDMETHODCALLTYPE CSGetSamplers(UINT, UINT, ID3D11SamplerState **ppSamplers) override {}
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT, UINT, ID3D11Buffer **ppConstantBuffers) override {}
    void STDMETHODCALLTYPE ClearState( ) override {log_call("ctx.ClearState");}
    void STDMETHODCALLTYPE Flush( ) override {log_call("ctx.Flush");}
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType( ) override {return D3D11_DEVICE_CONTEXT_IMMEDIATE;}
    UINT STDMETHODCALLTYPE GetContextFlags( ) override {return 0;}
    HRESULT STDMETHODCALLTYPE FinishCommandList(WINBOOL, ID3D11CommandList **ppCommandList) override {return E_FAIL;}
};
struct FakeDevice : ID3D11Device {
    ULONG refs = 1;
    FakeQuery* query = nullptr;
    FakeContext* ctx = nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if(__uuidof(ID3D11Device)==riid||__uuidof(IUnknown)==riid){*out=this;AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT, const void *pShaderBytecodeWithInputSignature, SIZE_T, ID3D11InputLayout **ppInputLayout) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void *pShaderBytecode, SIZE_T, ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void *pShaderBytecode, SIZE_T, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void *pShaderBytecode, SIZE_T, const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT, const UINT *pBufferStrides, UINT, UINT, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void *pShaderBytecode, SIZE_T, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void *pShaderBytecode, SIZE_T, ID3D11ClassLinkage *pClassLinkage, ID3D11HullShader **ppHullShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void *pShaderBytecode, SIZE_T, ID3D11ClassLinkage *pClassLinkage, ID3D11DomainShader **ppDomainShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void *pShaderBytecode, SIZE_T, ID3D11ClassLinkage *pClassLinkage, ID3D11ComputeShader **ppComputeShader) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateClassLinkage(ID3D11ClassLinkage **ppLinkage) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery) override {log_call("dev.CreateQuery(EVENT)");query->desc=*pQueryDesc;*ppQuery=query;query->AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D11_QUERY_DESC *pPredicateDesc, ID3D11Predicate **ppPredicate) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc, ID3D11Counter **ppCounter) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT, ID3D11DeviceContext **ppDeferredContext) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE, REFIID, void **ppResource) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT, UINT *pFormatSupport) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT, UINT, UINT *pNumQualityLevels) override {return E_FAIL;}
    void STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo) override {}
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D11_COUNTER_DESC *pDesc, D3D11_COUNTER_TYPE *pType, UINT *pActiveCounters, LPSTR, UINT *pNameLength, LPSTR, UINT *pUnitsLength, LPSTR, UINT *pDescriptionLength) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE, void *pFeatureSupportData, UINT) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT *pDataSize, void *pData) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void *pData) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown *pData) override {return E_FAIL;}
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel( ) override {return D3D_FEATURE_LEVEL_11_0;}
    UINT STDMETHODCALLTYPE GetCreationFlags( ) override {return 0;}
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason( ) override {return E_FAIL;}
    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext **ppImmediateContext) override {log_call("dev.GetImmediateContext");*ppImmediateContext=ctx;ctx->AddRef();}
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT) override {return E_FAIL;}
    UINT STDMETHODCALLTYPE GetExceptionMode( ) override {return 0;}
};

int main() {
    // 1. The happy path: exact call order, with Flush strictly between End
    //    and the first GetData.
    {
        FakeQuery q; FakeContext c; c.query=&q; FakeDevice d; d.ctx=&c; d.query=&q;
        resize_drain::drain(&d);
        std::string seq;for(auto& s:g_log)seq+=s+";";
        const char* want="dev.GetImmediateContext;"
                         "ctx.OMSetRenderTargets(0);"
                         "dev.CreateQuery(EVENT);"
                         "ctx.End;"
                         "ctx.Flush;"
                         "ctx.GetData;ctx.GetData;ctx.GetData;"
                         "query.Release;"
                         "ctx.Release;";
        CHECK(seq==want);
        if(seq!=want)printf("  got:      %s\n  expected: %s\n",seq.c_str(),want);
        // The spin kept polling through the S_FALSEs and stopped at S_OK.
        CHECK(q.polls==3);
        // No ClearState anywhere in the drain (Bugbot medium finding).
        CHECK(seq.find("ClearState")==std::string::npos);
    }
    g_log.clear();

    // 2. A device whose CreateQuery fails: the drain still unbinds and
    //    returns — no wait, no hang.
    {
        FakeContext c; FakeDevice d; d.ctx=&c;
        struct NoQueryDevice : FakeDevice {
            HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC*, ID3D11Query**) override {
                log_call("dev.CreateQuery(fail)");return E_OUTOFMEMORY;
            }
        } dq; dq.ctx=&c;
        resize_drain::drain(&dq);
        std::string seq;for(auto& s:g_log)seq+=s+";";
        CHECK(seq=="dev.GetImmediateContext;ctx.OMSetRenderTargets(0);dev.CreateQuery(fail);ctx.Flush;ctx.Release;");
        CHECK(seq.find("GetData")==std::string::npos);
    }
    g_log.clear();

    // 3. A null immediate context: the drain returns without dereferencing.
    {
        FakeDevice d; d.ctx=nullptr;
        // GetImmediateContext hands out null.
        struct NullCtxDevice : FakeDevice {
            void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext **ppImmediateContext) override {
                log_call("dev.GetImmediateContext(null)");*ppImmediateContext=nullptr;
            }
        } dn;
        resize_drain::drain(&dn);
        std::string seq;for(auto& s:g_log)seq+=s+";";
        CHECK(seq=="dev.GetImmediateContext(null);");
    }

    if(failures){printf("drain-order: %u checks, %u FAIL\n",checks,failures);return 1;}
    printf("drain-order: %u checks — drain order End->Flush->GetData, no ClearState, failure paths PASS\n",checks);
    return 0;
}
