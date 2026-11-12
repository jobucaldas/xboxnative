// SPDX-License-Identifier: GPL-3.0-or-later
// Vtable slot numbers for the classes d3d11_diag.h hooks. Kept free of every
// include so both the diagnostics and the slot audit test (tests/
// d3d11-diag-slots.cpp) can share one definition: the test derives each slot
// from the SDK's own vtbl structs with offsetof, so a slot that drifts from
// the SDK order fails the build here rather than forwarding to the wrong
// method at runtime.
#pragma once

namespace d3d11_diag {

// ---- ID3D11Device (IUnknown's 3 slots come first)
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

// ---- ID3D11DeviceContext (ID3D11DeviceChild's 3 come first). Only the ones
// the draw summary reads are named.
enum ContextSlot {
    PSSetShaderResources=8,PSSetShader=9,DrawIndexed=12,Draw=13,Map=14,Unmap=15,DrawIndexedInstanced=20,DrawInstanced=21,
    GSSetShader=23,IASetPrimitiveTopology=24,VSSetShaderResources=25,SetPredication=30,GSSetShaderResources=31,
    OMSetRenderTargets=33,OMSetRenderTargetsAndUnorderedAccessViews=34,SOSetTargets=37,DrawAuto=38,
    DrawIndexedInstancedIndirect=39,DrawInstancedIndirect=40,Dispatch=41,DispatchIndirect=42,
    RSSetViewports=44,RSSetScissorRects=45,CopySubresourceRegion=46,CopyResource=47,UpdateSubresource=48,
    ClearRenderTargetView=50,ClearDepthStencilView=53,GenerateMips=54,ResolveSubresource=57,ExecuteCommandList=58,
    HSSetShaderResources=59,HSSetShader=60,DSSetShaderResources=63,DSSetShader=64,FinishCommandList=114,
    // ID3D11DeviceContext1 continues the same vtable: ClearView is the 11.1
    // way to clear a render target or depth view, and a game that clears only
    // through it shows clears=0 in the summary. It is the 18th Context1
    // method, so it sits 18 slots past FinishCommandList (the last base one):
    // 114 + 18 = 132.
    Context1ClearView=132
};

}  // namespace d3d11_diag
