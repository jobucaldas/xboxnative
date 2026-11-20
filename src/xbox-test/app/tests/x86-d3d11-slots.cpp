// SPDX-License-Identifier: GPL-3.0-or-later
// Build-time proof that the vtable slots the x86 D3D11 bridge names (host/x86/
// x86_d3d11.h) match the Windows SDK's own vtable order. offsetof on the SDK's
// C-style vtbl structs is the derivation: a slot that drifts fails this build,
// not a guest that calls the wrong method. The C-style vtbl structs exist only
// under CINTERFACE, so this TU never mixes with the C++ interface path.
#define CINTERFACE
#include <d3d11.h>
#include <dxgi.h>
#include <stddef.h>

#define SLOT(vtbl,method) (offsetof(vtbl,method)/sizeof(void*))

// ---- IDXGISwapChain
static_assert(SLOT(IDXGISwapChainVtbl,Present)==8,"IDXGISwapChain::Present slot");
static_assert(SLOT(IDXGISwapChainVtbl,GetBuffer)==9,"IDXGISwapChain::GetBuffer slot");
static_assert(SLOT(IDXGISwapChainVtbl,ResizeBuffers)==13,"IDXGISwapChain::ResizeBuffers slot");
static_assert(SLOT(IDXGISwapChainVtbl,GetDesc)==12,"IDXGISwapChain::GetDesc slot");

// ---- ID3D11Device (the creation set and the feature queries)
static_assert(SLOT(ID3D11DeviceVtbl,CreateBuffer)==3,"CreateBuffer slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateTexture2D)==5,"CreateTexture2D slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateShaderResourceView)==7,"CreateShaderResourceView slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateRenderTargetView)==9,"CreateRenderTargetView slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateInputLayout)==11,"CreateInputLayout slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateVertexShader)==12,"CreateVertexShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreatePixelShader)==15,"CreatePixelShader slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateBlendState)==20,"CreateBlendState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateDepthStencilState)==21,"CreateDepthStencilState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateRasterizerState)==22,"CreateRasterizerState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CreateSamplerState)==23,"CreateSamplerState slot");
static_assert(SLOT(ID3D11DeviceVtbl,CheckFormatSupport)==29,"CheckFormatSupport slot");
static_assert(SLOT(ID3D11DeviceVtbl,GetFeatureLevel)==37,"GetFeatureLevel slot");
static_assert(SLOT(ID3D11DeviceVtbl,GetImmediateContext)==40,"GetImmediateContext slot");

// ---- ID3D11DeviceContext (the draw path)
static_assert(SLOT(ID3D11DeviceContextVtbl,VSSetConstantBuffers)==7,"VSSetConstantBuffers slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetShaderResources)==8,"PSSetShaderResources slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetShader)==9,"PSSetShader slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetSamplers)==10,"PSSetSamplers slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,VSSetShader)==11,"VSSetShader slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawIndexed)==12,"DrawIndexed slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Draw)==13,"Draw slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Map)==14,"Map slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,Unmap)==15,"Unmap slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetConstantBuffers)==16,"PSSetConstantBuffers slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetInputLayout)==17,"IASetInputLayout slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetVertexBuffers)==18,"IASetVertexBuffers slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetIndexBuffer)==19,"IASetIndexBuffer slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetPrimitiveTopology)==24,"IASetPrimitiveTopology slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetRenderTargets)==33,"OMSetRenderTargets slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetBlendState)==35,"OMSetBlendState slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetDepthStencilState)==36,"OMSetDepthStencilState slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetState)==43,"RSSetState slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetViewports)==44,"RSSetViewports slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetScissorRects)==45,"RSSetScissorRects slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,UpdateSubresource)==48,"UpdateSubresource slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,ClearRenderTargetView)==50,"ClearRenderTargetView slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,ClearState)==110,"ClearState slot");
static_assert(SLOT(ID3D11DeviceContextVtbl,FinishCommandList)==114,"FinishCommandList slot");

int main() { return 0; }
