// SPDX-License-Identifier: GPL-3.0-or-later
// Build-time proof that the vtable slots and argument counts the x86 D3D11
// bridge names (host/x86/x86_d3d11.h) match the Windows SDK's own vtables.
// Slot: offsetof on the SDK's C-style vtbl structs. Argument count: the
// function-pointer type of each vtbl member, with every parameter counted in
// 32-bit stack slots (pointers and 4-byte scalars are one slot, an 8-byte value
// is two), plus `this`. The C-style vtbl structs exist only under CINTERFACE,
// so this TU never mixes with the C++ interface path.
#define CINTERFACE
#include <d3d11.h>
#include <dxgi.h>
#include <stddef.h>
#include <type_traits>
#include <utility>

#define SLOT(vtbl,method) (offsetof(vtbl,method)/sizeof(void*))

// 32-bit stack slots for one parameter. Pointers and references (REFIID, a
// const GUID&) are one slot; a scalar wider than 4 bytes (UINT64) is two. The
// bridged methods pass no by-value struct. This counts with the PE32 guest's
// widths, so it is evaluated on i686 only: on x64 SIZE_T is 8 bytes.
template<class T> struct slots32 { static constexpr unsigned value = (std::is_pointer<T>::value || std::is_reference<T>::value || sizeof(T) <= 4) ? 1u : 2u; };
template<class F> struct fn_slots;
template<class R, class... A> struct fn_slots<R(*)(A...)> { static constexpr unsigned value = (0u + ... + slots32<A>::value); };
#if defined(__i386__)
// On i686 STDMETHODCALLTYPE is __stdcall, which is part of the function type.
template<class R, class... A> struct fn_slots<R(__stdcall*)(A...)> { static constexpr unsigned value = (0u + ... + slots32<A>::value); };
#endif
#define ARGC(vtbl,method) (fn_slots<decltype(std::declval<vtbl&>().method)>::value)
#if defined(__i386__)
#define ARGC_CHECK(vtbl,method,n,msg) static_assert(ARGC(vtbl,method)==n,msg)
#else
#define ARGC_CHECK(vtbl,method,n,msg)
#endif

// ---- IDXGISwapChain
static_assert(SLOT(IDXGISwapChainVtbl,Present)==8,"IDXGISwapChain::Present");
ARGC_CHECK(IDXGISwapChainVtbl,Present,3,"IDXGISwapChain::Present argc");
static_assert(SLOT(IDXGISwapChainVtbl,GetBuffer)==9,"IDXGISwapChain::GetBuffer");
ARGC_CHECK(IDXGISwapChainVtbl,GetBuffer,4,"IDXGISwapChain::GetBuffer argc");
static_assert(SLOT(IDXGISwapChainVtbl,GetDesc)==12,"IDXGISwapChain::GetDesc");
ARGC_CHECK(IDXGISwapChainVtbl,GetDesc,2,"IDXGISwapChain::GetDesc argc");
static_assert(SLOT(IDXGISwapChainVtbl,ResizeBuffers)==13,"IDXGISwapChain::ResizeBuffers");
ARGC_CHECK(IDXGISwapChainVtbl,ResizeBuffers,6,"IDXGISwapChain::ResizeBuffers argc");

// ---- ID3D11Device (the creation set and the feature queries)
static_assert(SLOT(ID3D11DeviceVtbl,CreateBuffer)==3,"CreateBuffer");
ARGC_CHECK(ID3D11DeviceVtbl,CreateBuffer,4,"CreateBuffer argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateTexture2D)==5,"CreateTexture2D");
ARGC_CHECK(ID3D11DeviceVtbl,CreateTexture2D,4,"CreateTexture2D argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateShaderResourceView)==7,"CreateShaderResourceView");
ARGC_CHECK(ID3D11DeviceVtbl,CreateShaderResourceView,4,"CreateShaderResourceView argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateRenderTargetView)==9,"CreateRenderTargetView");
ARGC_CHECK(ID3D11DeviceVtbl,CreateRenderTargetView,4,"CreateRenderTargetView argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateInputLayout)==11,"CreateInputLayout");
ARGC_CHECK(ID3D11DeviceVtbl,CreateInputLayout,6,"CreateInputLayout argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateVertexShader)==12,"CreateVertexShader");
ARGC_CHECK(ID3D11DeviceVtbl,CreateVertexShader,5,"CreateVertexShader argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreatePixelShader)==15,"CreatePixelShader");
ARGC_CHECK(ID3D11DeviceVtbl,CreatePixelShader,5,"CreatePixelShader argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateBlendState)==20,"CreateBlendState");
ARGC_CHECK(ID3D11DeviceVtbl,CreateBlendState,3,"CreateBlendState argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateDepthStencilState)==21,"CreateDepthStencilState");
ARGC_CHECK(ID3D11DeviceVtbl,CreateDepthStencilState,3,"CreateDepthStencilState argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateRasterizerState)==22,"CreateRasterizerState");
ARGC_CHECK(ID3D11DeviceVtbl,CreateRasterizerState,3,"CreateRasterizerState argc");
static_assert(SLOT(ID3D11DeviceVtbl,CreateSamplerState)==23,"CreateSamplerState");
ARGC_CHECK(ID3D11DeviceVtbl,CreateSamplerState,3,"CreateSamplerState argc");
static_assert(SLOT(ID3D11DeviceVtbl,CheckFormatSupport)==29,"CheckFormatSupport");
ARGC_CHECK(ID3D11DeviceVtbl,CheckFormatSupport,3,"CheckFormatSupport argc");
static_assert(SLOT(ID3D11DeviceVtbl,GetFeatureLevel)==37,"GetFeatureLevel");
ARGC_CHECK(ID3D11DeviceVtbl,GetFeatureLevel,1,"GetFeatureLevel argc");
static_assert(SLOT(ID3D11DeviceVtbl,GetImmediateContext)==40,"GetImmediateContext");
ARGC_CHECK(ID3D11DeviceVtbl,GetImmediateContext,2,"GetImmediateContext argc");

// ---- ID3D11DeviceContext (the draw path)
static_assert(SLOT(ID3D11DeviceContextVtbl,VSSetConstantBuffers)==7,"VSSetConstantBuffers");
ARGC_CHECK(ID3D11DeviceContextVtbl,VSSetConstantBuffers,4,"VSSetConstantBuffers argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetShaderResources)==8,"PSSetShaderResources");
ARGC_CHECK(ID3D11DeviceContextVtbl,PSSetShaderResources,4,"PSSetShaderResources argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetShader)==9,"PSSetShader");
ARGC_CHECK(ID3D11DeviceContextVtbl,PSSetShader,4,"PSSetShader argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetSamplers)==10,"PSSetSamplers");
ARGC_CHECK(ID3D11DeviceContextVtbl,PSSetSamplers,4,"PSSetSamplers argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,VSSetShader)==11,"VSSetShader");
ARGC_CHECK(ID3D11DeviceContextVtbl,VSSetShader,4,"VSSetShader argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,DrawIndexed)==12,"DrawIndexed");
ARGC_CHECK(ID3D11DeviceContextVtbl,DrawIndexed,4,"DrawIndexed argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,Draw)==13,"Draw");
ARGC_CHECK(ID3D11DeviceContextVtbl,Draw,3,"Draw argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,Map)==14,"Map");
ARGC_CHECK(ID3D11DeviceContextVtbl,Map,6,"Map argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,Unmap)==15,"Unmap");
ARGC_CHECK(ID3D11DeviceContextVtbl,Unmap,3,"Unmap argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,PSSetConstantBuffers)==16,"PSSetConstantBuffers");
ARGC_CHECK(ID3D11DeviceContextVtbl,PSSetConstantBuffers,4,"PSSetConstantBuffers argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetInputLayout)==17,"IASetInputLayout");
ARGC_CHECK(ID3D11DeviceContextVtbl,IASetInputLayout,2,"IASetInputLayout argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetVertexBuffers)==18,"IASetVertexBuffers");
ARGC_CHECK(ID3D11DeviceContextVtbl,IASetVertexBuffers,6,"IASetVertexBuffers argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetIndexBuffer)==19,"IASetIndexBuffer");
ARGC_CHECK(ID3D11DeviceContextVtbl,IASetIndexBuffer,4,"IASetIndexBuffer argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,IASetPrimitiveTopology)==24,"IASetPrimitiveTopology");
ARGC_CHECK(ID3D11DeviceContextVtbl,IASetPrimitiveTopology,2,"IASetPrimitiveTopology argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetRenderTargets)==33,"OMSetRenderTargets");
ARGC_CHECK(ID3D11DeviceContextVtbl,OMSetRenderTargets,4,"OMSetRenderTargets argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetBlendState)==35,"OMSetBlendState");
ARGC_CHECK(ID3D11DeviceContextVtbl,OMSetBlendState,4,"OMSetBlendState argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,OMSetDepthStencilState)==36,"OMSetDepthStencilState");
ARGC_CHECK(ID3D11DeviceContextVtbl,OMSetDepthStencilState,3,"OMSetDepthStencilState argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetState)==43,"RSSetState");
ARGC_CHECK(ID3D11DeviceContextVtbl,RSSetState,2,"RSSetState argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetViewports)==44,"RSSetViewports");
ARGC_CHECK(ID3D11DeviceContextVtbl,RSSetViewports,3,"RSSetViewports argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,RSSetScissorRects)==45,"RSSetScissorRects");
ARGC_CHECK(ID3D11DeviceContextVtbl,RSSetScissorRects,3,"RSSetScissorRects argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,UpdateSubresource)==48,"UpdateSubresource");
ARGC_CHECK(ID3D11DeviceContextVtbl,UpdateSubresource,7,"UpdateSubresource argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,ClearRenderTargetView)==50,"ClearRenderTargetView");
ARGC_CHECK(ID3D11DeviceContextVtbl,ClearRenderTargetView,3,"ClearRenderTargetView argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,ClearState)==110,"ClearState");
ARGC_CHECK(ID3D11DeviceContextVtbl,ClearState,1,"ClearState argc");
static_assert(SLOT(ID3D11DeviceContextVtbl,FinishCommandList)==114,"FinishCommandList");
ARGC_CHECK(ID3D11DeviceContextVtbl,FinishCommandList,3,"FinishCommandList argc");

// ---- Phase 2: one line proves a slot (offsetof) and its argument count (i686 only).
#define PROVE(vtbl,method,slot,argc) static_assert(SLOT(vtbl,method)==slot,#vtbl "::" #method); ARGC_CHECK(vtbl,method,argc,#vtbl "::" #method " argc")
PROVE(IDXGISwapChainVtbl,SetFullscreenState,10,3);
PROVE(IDXGISwapChainVtbl,GetFullscreenState,11,3);
PROVE(IDXGISwapChainVtbl,ResizeTarget,14,2);
PROVE(ID3D11DeviceVtbl,CreateDepthStencilView,10,4);
PROVE(ID3D11DeviceVtbl,CreateQuery,24,3);
PROVE(ID3D11DeviceVtbl,CheckMultisampleQualityLevels,30,4);
PROVE(ID3D11DeviceVtbl,GetCreationFlags,38,1);
PROVE(ID3D11DeviceVtbl,GetDeviceRemovedReason,39,1);
PROVE(ID3D11DeviceContextVtbl,DrawIndexedInstanced,20,6);
PROVE(ID3D11DeviceContextVtbl,DrawInstanced,21,5);
PROVE(ID3D11DeviceContextVtbl,VSSetShaderResources,25,4);
PROVE(ID3D11DeviceContextVtbl,VSSetSamplers,26,4);
PROVE(ID3D11DeviceContextVtbl,Begin,27,2);
PROVE(ID3D11DeviceContextVtbl,End,28,2);
PROVE(ID3D11DeviceContextVtbl,GetData,29,5);
PROVE(ID3D11DeviceContextVtbl,CopySubresourceRegion,46,9);
PROVE(ID3D11DeviceContextVtbl,CopyResource,47,3);
PROVE(ID3D11DeviceContextVtbl,ClearDepthStencilView,53,5);
PROVE(ID3D11DeviceContextVtbl,GenerateMips,54,2);
PROVE(ID3D11DeviceContextVtbl,ResolveSubresource,57,6);
PROVE(ID3D11DeviceContextVtbl,Flush,111,1);
// ID3D11Buffer and ID3D11Texture2D share the ID3D11Resource layout.
PROVE(ID3D11BufferVtbl,GetType,7,2);
PROVE(ID3D11BufferVtbl,GetDesc,10,2);
PROVE(ID3D11Texture2DVtbl,GetType,7,2);
PROVE(ID3D11Texture2DVtbl,GetDesc,10,2);
// ID3D11Query is a DeviceChild (0-6) + Asynchronous::GetDataSize + Query::GetDesc: nine slots.
PROVE(ID3D11QueryVtbl,GetDataSize,7,1);
PROVE(ID3D11QueryVtbl,GetDesc,8,2);
static_assert(sizeof(ID3D11QueryVtbl)==9*sizeof(void*),"ID3D11Query has nine slots");
// ID3D11DepthStencilView is a View: GetResource 7, GetDesc 8.
PROVE(ID3D11DepthStencilViewVtbl,GetResource,7,2);
PROVE(ID3D11DepthStencilViewVtbl,GetDesc,8,2);
static_assert(sizeof(ID3D11DepthStencilViewVtbl)==9*sizeof(void*),"ID3D11DepthStencilView has nine slots");

int main() { return 0; }
