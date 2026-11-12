// SPDX-License-Identifier: GPL-3.0-or-later
// Build-time proof that the slot numbers in host/d3d11_diag_slots.h match the
// Windows SDK's own vtable order. offsetof on the SDK's C-style vtbl structs is
// the derivation: if a method moves (SDK update, wrong inheritance count), the
// index the diagnostics hook by is wrong and a hook would forward to the wrong
// method at runtime - device removals with "invalid command" are what that
// looks like on the console. A wrong slot fails this build, not a game.
// The C-style vtbl structs only exist under CINTERFACE (in C++ mode the SDK
// declares pure-virtual interfaces instead), so this TU compiles with
// CINTERFACE defined and never touches the C++ path of the SDK headers.
#define CINTERFACE
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <stddef.h>

#include "../host/d3d11_diag_slots.h"

// offsetof yields bytes; a vtable slot is one pointer wide.
#define SLOT(vtbl,method) (offsetof(vtbl,method)/sizeof(void*))

// ---- ID3D11Device slots vs the SDK's DeviceVtbl. The 11.1 additions live in
// Device1Vtbl, which continues the same backing vtable.
static_assert(SLOT(ID3D11DeviceVtbl,CreateBuffer)==d3d11_diag::CreateBuffer,"CreateBuffer slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateTexture1D)==d3d11_diag::CreateTexture1D,"CreateTexture1D slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateTexture2D)==d3d11_diag::CreateTexture2D,"CreateTexture2D slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateTexture3D)==d3d11_diag::CreateTexture3D,"CreateTexture3D slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateShaderResourceView)==d3d11_diag::CreateShaderResourceView,"CreateShaderResourceView slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateUnorderedAccessView)==d3d11_diag::CreateUnorderedAccessView,"CreateUnorderedAccessView slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateRenderTargetView)==d3d11_diag::CreateRenderTargetView,"CreateRenderTargetView slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateDepthStencilView)==d3d11_diag::CreateDepthStencilView,"CreateDepthStencilView slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateInputLayout)==d3d11_diag::CreateInputLayout,"CreateInputLayout slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateVertexShader)==d3d11_diag::CreateVertexShader,"CreateVertexShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateGeometryShader)==d3d11_diag::CreateGeometryShader,"CreateGeometryShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateGeometryShaderWithStreamOutput)==d3d11_diag::CreateGeometryShaderWithStreamOutput,"CreateGeometryShaderWithStreamOutput slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreatePixelShader)==d3d11_diag::CreatePixelShader,"CreatePixelShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateHullShader)==d3d11_diag::CreateHullShader,"CreateHullShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateDomainShader)==d3d11_diag::CreateDomainShader,"CreateDomainShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateComputeShader)==d3d11_diag::CreateComputeShader,"CreateComputeShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateClassLinkage)==d3d11_diag::CreateClassLinkage,"CreateClassLinkage slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateBlendState)==d3d11_diag::CreateBlendState,"CreateBlendState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateDepthStencilState)==d3d11_diag::CreateDepthStencilState,"CreateDepthStencilState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateRasterizerState)==d3d11_diag::CreateRasterizerState,"CreateRasterizerState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateSamplerState)==d3d11_diag::CreateSamplerState,"CreateSamplerState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateQuery)==d3d11_diag::CreateQuery,"CreateQuery slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreatePredicate)==d3d11_diag::CreatePredicate,"CreatePredicate slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateDeferredContext)==d3d11_diag::CreateDeferredContext,"CreateDeferredContext slot");
static_assert(SLOT(ID3D11DeviceVtbl,CheckFormatSupport)==d3d11_diag::CheckFormatSupport,"CheckFormatSupport slot");
static_assert(SLOT(ID3D11DeviceVtbl,CheckMultisampleQualityLevels)==d3d11_diag::CheckMultisampleQualityLevels,"CheckMultisampleQualityLevels slot");
static_assert(SLOT(ID3D11DeviceVtbl,CheckFeatureSupport)==d3d11_diag::CheckFeatureSupport,"CheckFeatureSupport slot");
static_assert(SLOT(ID3D11Device1Vtbl,CreateDeferredContext1)==d3d11_diag::CreateDeferredContext1,"CreateDeferredContext1 slot");
static_assert(SLOT(ID3D11Device1Vtbl,CreateBlendState1)==d3d11_diag::CreateBlendState1,"CreateBlendState1 slot");
static_assert(SLOT(ID3D11Device1Vtbl,CreateRasterizerState1)==d3d11_diag::CreateRasterizerState1,"CreateRasterizerState1 slot");

// ---- ID3D11DeviceContext slots vs the SDK's ContextVtbl. The 11.1 additions
// live in Context1Vtbl, which continues the same backing vtable.
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetShaderResources)==d3d11_diag::PSSetShaderResources,"PSSetShaderResources slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetShader)==d3d11_diag::PSSetShader,"PSSetShader slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawIndexed)==d3d11_diag::DrawIndexed,"DrawIndexed slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Draw)==d3d11_diag::Draw,"Draw slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Map)==d3d11_diag::Map,"Map slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Unmap)==d3d11_diag::Unmap,"Unmap slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawIndexedInstanced)==d3d11_diag::DrawIndexedInstanced,"DrawIndexedInstanced slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawInstanced)==d3d11_diag::DrawInstanced,"DrawInstanced slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,GSSetShader)==d3d11_diag::GSSetShader,"GSSetShader slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetPrimitiveTopology)==d3d11_diag::IASetPrimitiveTopology,"IASetPrimitiveTopology slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,VSSetShaderResources)==d3d11_diag::VSSetShaderResources,"VSSetShaderResources slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,SetPredication)==d3d11_diag::SetPredication,"SetPredication slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,GSSetShaderResources)==d3d11_diag::GSSetShaderResources,"GSSetShaderResources slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetRenderTargets)==d3d11_diag::OMSetRenderTargets,"OMSetRenderTargets slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetRenderTargetsAndUnorderedAccessViews)==d3d11_diag::OMSetRenderTargetsAndUnorderedAccessViews,"OMSetRenderTargetsAndUnorderedAccessViews slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,SOSetTargets)==d3d11_diag::SOSetTargets,"SOSetTargets slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawAuto)==d3d11_diag::DrawAuto,"DrawAuto slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawIndexedInstancedIndirect)==d3d11_diag::DrawIndexedInstancedIndirect,"DrawIndexedInstancedIndirect slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawInstancedIndirect)==d3d11_diag::DrawInstancedIndirect,"DrawInstancedIndirect slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Dispatch)==d3d11_diag::Dispatch,"Dispatch slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DispatchIndirect)==d3d11_diag::DispatchIndirect,"DispatchIndirect slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetViewports)==d3d11_diag::RSSetViewports,"RSSetViewports slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetScissorRects)==d3d11_diag::RSSetScissorRects,"RSSetScissorRects slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,CopySubresourceRegion)==d3d11_diag::CopySubresourceRegion,"CopySubresourceRegion slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,CopyResource)==d3d11_diag::CopyResource,"CopyResource slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,UpdateSubresource)==d3d11_diag::UpdateSubresource,"UpdateSubresource slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,ClearRenderTargetView)==d3d11_diag::ClearRenderTargetView,"ClearRenderTargetView slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,ClearDepthStencilView)==d3d11_diag::ClearDepthStencilView,"ClearDepthStencilView slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,GenerateMips)==d3d11_diag::GenerateMips,"GenerateMips slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,ResolveSubresource)==d3d11_diag::ResolveSubresource,"ResolveSubresource slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,ExecuteCommandList)==d3d11_diag::ExecuteCommandList,"ExecuteCommandList slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,HSSetShaderResources)==d3d11_diag::HSSetShaderResources,"HSSetShaderResources slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,HSSetShader)==d3d11_diag::HSSetShader,"HSSetShader slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DSSetShaderResources)==d3d11_diag::DSSetShaderResources,"DSSetShaderResources slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DSSetShader)==d3d11_diag::DSSetShader,"DSSetShader slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,FinishCommandList)==d3d11_diag::FinishCommandList,"FinishCommandList slot");
static_assert(SLOT(ID3D11DeviceContext1Vtbl,ClearView)==d3d11_diag::Context1ClearView,"ClearView slot");

// ---- IDXGISwapChain slots vs the SDK's SwapChainVtbl. guest_overlay.h hooks
// Present (8), ResizeBuffers (13) and Present1 (22, which continues the same
// backing vtable in SwapChain1Vtbl). The peer RCA showed the device reset
// lands in the resize path, so the resize slot is the one a drift here would
// corrupt first.
static_assert(SLOT(IDXGISwapChainVtbl,Present)==8,"IDXGISwapChain::Present slot");
static_assert(SLOT(IDXGISwapChainVtbl,GetBuffer)==9,"IDXGISwapChain::GetBuffer slot");
static_assert(SLOT(IDXGISwapChainVtbl,ResizeBuffers)==13,"IDXGISwapChain::ResizeBuffers slot");
static_assert(SLOT(IDXGISwapChainVtbl,ResizeTarget)==14,"IDXGISwapChain::ResizeTarget slot");
static_assert(SLOT(IDXGISwapChain1Vtbl,Present1)==22,"IDXGISwapChain1::Present1 slot");

int main() {
    // Nothing runs: every check above is compile-time. A build that passes is
    // the pass.
    return 0;
}
