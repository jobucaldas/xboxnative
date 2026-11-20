// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 D3D11 imports and COM proxy marshalling onto the host's 64-bit D3D11.
// Phase 1 is device and swapchain creation plus the draw path a 2D game uses.
// Every other vtable slot is a named gap: it logs "x86 D3D11 gap: Interface::Method"
// and returns E_NOTIMPL, the same rule the D3D9 bridge applies.
//
// Slot order comes from the SDK vtables (see tests/x86-d3d11-slots.cpp, which
// proves each bridged slot with offsetof under CINTERFACE). Argument counts are
// x86 stdcall DWORD slots, this included.
#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "x86com.h"
#include "x86_d3d9.h"
#if defined(_WIN32)
#include <windows.h>
#pragma push_macro("WINAPI_FAMILY")
#undef WINAPI_FAMILY
#define WINAPI_FAMILY WINAPI_FAMILY_DESKTOP_APP
#include <d3d11.h>
#include <dxgi.h>
#pragma pop_macro("WINAPI_FAMILY")
namespace x86 {
inline constexpr uint32_t D3D11_GAP_HRESULT = 0x80004001u;

// Vtable member order (IUnknown first) and stack-slot counts, from the SDK.
inline constexpr const char* const kD3D11SwapChainNames[] = {"QueryInterface","AddRef","Release","SetPrivateData","SetPrivateDataInterface","GetPrivateData","GetParent","GetDevice","Present","GetBuffer","SetFullscreenState","GetFullscreenState","GetDesc","ResizeBuffers","ResizeTarget","GetContainingOutput","GetFrameStatistics","GetLastPresentCount"};
inline constexpr unsigned kD3D11SwapChainArgc[] = {3,1,1,4,3,4,3,3,3,4,3,3,2,6,2,2,2,2};
inline constexpr const char* const kD3D11DeviceNames[] = {"QueryInterface","AddRef","Release","CreateBuffer","CreateTexture1D","CreateTexture2D","CreateTexture3D","CreateShaderResourceView","CreateUnorderedAccessView","CreateRenderTargetView","CreateDepthStencilView","CreateInputLayout","CreateVertexShader","CreateGeometryShader","CreateGeometryShaderWithStreamOutput","CreatePixelShader","CreateHullShader","CreateDomainShader","CreateComputeShader","CreateClassLinkage","CreateBlendState","CreateDepthStencilState","CreateRasterizerState","CreateSamplerState","CreateQuery","CreatePredicate","CreateCounter","CreateDeferredContext","OpenSharedResource","CheckFormatSupport","CheckMultisampleQualityLevels","CheckCounterInfo","CheckCounter","CheckFeatureSupport","GetPrivateData","SetPrivateData","SetPrivateDataInterface","GetFeatureLevel","GetCreationFlags","GetDeviceRemovedReason","GetImmediateContext","SetExceptionMode","GetExceptionMode"};
inline constexpr unsigned kD3D11DeviceArgc[] = {3,1,1,4,4,4,4,4,4,4,4,6,5,5,10,5,5,5,5,2,3,3,3,3,3,3,3,3,4,3,4,2,10,4,4,4,3,1,1,1,2,2,1};
inline constexpr const char* const kD3D11ContextNames[] = {"QueryInterface","AddRef","Release","GetDevice","GetPrivateData","SetPrivateData","SetPrivateDataInterface","VSSetConstantBuffers","PSSetShaderResources","PSSetShader","PSSetSamplers","VSSetShader","DrawIndexed","Draw","Map","Unmap","PSSetConstantBuffers","IASetInputLayout","IASetVertexBuffers","IASetIndexBuffer","DrawIndexedInstanced","DrawInstanced","GSSetConstantBuffers","GSSetShader","IASetPrimitiveTopology","VSSetShaderResources","VSSetSamplers","Begin","End","GetData","SetPredication","GSSetShaderResources","GSSetSamplers","OMSetRenderTargets","OMSetRenderTargetsAndUnorderedAccessViews","OMSetBlendState","OMSetDepthStencilState","SOSetTargets","DrawAuto","DrawIndexedInstancedIndirect","DrawInstancedIndirect","Dispatch","DispatchIndirect","RSSetState","RSSetViewports","RSSetScissorRects","CopySubresourceRegion","CopyResource","UpdateSubresource","CopyStructureCount","ClearRenderTargetView","ClearUnorderedAccessViewUint","ClearUnorderedAccessViewFloat","ClearDepthStencilView","GenerateMips","SetResourceMinLOD","GetResourceMinLOD","ResolveSubresource","ExecuteCommandList","HSSetShaderResources","HSSetShader","HSSetSamplers","HSSetConstantBuffers","DSSetShaderResources","DSSetShader","DSSetSamplers","DSSetConstantBuffers","CSSetShaderResources","CSSetUnorderedAccessViews","CSSetShader","CSSetSamplers","CSSetConstantBuffers","VSGetConstantBuffers","PSGetShaderResources","PSGetShader","PSGetSamplers","VSGetShader","PSGetConstantBuffers","IAGetInputLayout","IAGetVertexBuffers","IAGetIndexBuffer","GSGetConstantBuffers","GSGetShader","IAGetPrimitiveTopology","VSGetShaderResources","VSGetSamplers","GetPredication","GSGetShaderResources","GSGetSamplers","OMGetRenderTargets","OMGetRenderTargetsAndUnorderedAccessViews","OMGetBlendState","OMGetDepthStencilState","SOGetTargets","RSGetState","RSGetViewports","RSGetScissorRects","HSGetShaderResources","HSGetShader","HSGetSamplers","HSGetConstantBuffers","DSGetShaderResources","DSGetShader","DSGetSamplers","DSGetConstantBuffers","CSGetShaderResources","CSGetUnorderedAccessViews","CSGetShader","CSGetSamplers","CSGetConstantBuffers","ClearState","Flush","GetType","GetContextFlags","FinishCommandList"};
inline constexpr unsigned kD3D11ContextArgc[] = {3,1,1,2,4,4,3,4,4,4,4,4,4,3,6,3,4,2,6,4,6,5,4,4,2,4,4,2,2,5,3,4,4,4,8,4,3,4,1,3,3,4,3,2,3,3,9,3,7,4,3,3,3,5,2,3,2,6,3,4,4,4,4,4,4,4,4,4,5,4,4,4,4,4,4,4,4,4,2,6,4,4,4,2,4,4,3,4,4,4,7,4,3,3,2,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,1,1,1,1,3};
inline constexpr const char* const kD3D11ResourceNames[] = {"QueryInterface","AddRef","Release","GetDevice","GetPrivateData","SetPrivateData","SetPrivateDataInterface","GetType","SetEvictionPriority","GetEvictionPriority","GetDesc"};
inline constexpr unsigned kD3D11ResourceArgc[] = {3,1,1,2,4,4,3,2,2,1,2};
inline constexpr const char* const kD3D11ViewNames[] = {"QueryInterface","AddRef","Release","GetDevice","GetPrivateData","SetPrivateData","SetPrivateDataInterface","GetResource","GetDesc"};
inline constexpr unsigned kD3D11ViewArgc[] = {3,1,1,2,4,4,3,2,2};
inline constexpr const char* const kD3D11ChildNames[] = {"QueryInterface","AddRef","Release","GetDevice","GetPrivateData","SetPrivateData","SetPrivateDataInterface"};
inline constexpr unsigned kD3D11ChildArgc[] = {3,1,1,2,4,4,3};
inline constexpr const char* const kD3D11StateNames[] = {"QueryInterface","AddRef","Release","GetDevice","GetPrivateData","SetPrivateData","SetPrivateDataInterface","GetDesc"};
inline constexpr unsigned kD3D11StateArgc[] = {3,1,1,2,4,4,3,2};

// The gap for an unbridged slot, and for a bridged slot's unsupported input.
inline uint32_t d3d11_unbridged(Host& h, Proxy& p, const char* method) {
    std::string where = std::string(p.iface) + "::" + method;
    char line[256]; snprintf(line, sizeof line, "x86 D3D11 gap: %s", where.c_str());
    if (h.tee) h.tee(line);
    return D3D11_GAP_HRESULT;
}
inline void d3d11_gap_text(Host& h, const char* where) {
    char line[256]; snprintf(line, sizeof line, "x86 D3D11 gap: %s", where);
    if (h.tee) h.tee(line);
}
template<class T> inline T* d3d11_as(void* host) { return static_cast<T*>(static_cast<IUnknown*>(host)); }
inline ID3D11Device* d3d11_device(Proxy& p) { return d3d11_as<ID3D11Device>(p.host); }
inline ID3D11DeviceContext* d3d11_context(Proxy& p) { return d3d11_as<ID3D11DeviceContext>(p.host); }
template<class T> inline T* d3d11_obj(ComSpace& s, uint32_t guest) { return guest ? d3d11_as<T>(s.by_guest(guest).host) : nullptr; }
// Reads a guest array of interface pointers (a count bounded by the API's slot limit).
template<class T> inline bool d3d11_objects(ComSpace& s, Cpu& c, uint32_t array, uint32_t count, std::vector<T*>& out, uint32_t limit) {
    if (count > limit || (count && !array)) return false;
    out.assign(count, nullptr);
    for (uint32_t i = 0; i < count; ++i) { uint32_t g = c.mem.r32(array + 4 * i); out[i] = g ? d3d11_as<T>(s.by_guest(g).host) : nullptr; }
    return true;
}
// Writes a created object's guest proxy to `out`; a null out drops the host reference.
template<class T> inline uint32_t d3d11_out(ComSpace& s, Cpu& c, uint32_t out, const char* iface, HRESULT hr, T* object) {
    if (!out) { if (object) object->Release(); return uint32_t(hr); }
    c.mem.w32(out, SUCCEEDED(hr) && object ? d3d9_wrap(s, c, iface, object) : 0);
    return uint32_t(hr);
}
inline std::string d3d11_cstring(Cpu& c, uint32_t address) {
    std::string s;
    for (uint32_t i = 0; address && i < 256; ++i) { uint8_t ch = c.mem.r8(address + i); if (!ch) break; s.push_back(char(ch)); }
    return s;
}
// Bytes of one subresource: buffers are ByteWidth, 2D textures are pitch * rows of that mip.
inline uint32_t d3d11_subresource_bytes(ID3D11Resource* r, uint32_t sub, uint32_t row_pitch) {
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) { D3D11_BUFFER_DESC d{}; static_cast<ID3D11Buffer*>(r)->GetDesc(&d); return d.ByteWidth; }
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d{}; static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
        uint32_t mips = d.MipLevels ? d.MipLevels : 1;
        return row_pitch * std::max<uint32_t>(1, d.Height >> (sub % mips));
    }
    return 0;
}
// DXGI_SWAP_CHAIN_DESC as the PE32 guest lays it out: 15 DWORDs, 60 bytes, no pointers
// except OutputWindow (an HWND, mapped to the host window like the D3D9 hDeviceWindow).
inline DXGI_SWAP_CHAIN_DESC d3d11_read_swapchain_desc(Host& h, Cpu& c, uint32_t guest) {
    uint32_t v[15]{};
    for (unsigned i = 0; i < 15; ++i) v[i] = c.mem.r32(guest + i * 4);
    DXGI_SWAP_CHAIN_DESC d{};
    d.BufferDesc.Width = v[0]; d.BufferDesc.Height = v[1];
    d.BufferDesc.RefreshRate.Numerator = v[2]; d.BufferDesc.RefreshRate.Denominator = v[3];
    d.BufferDesc.Format = DXGI_FORMAT(v[4]); d.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER(v[5]); d.BufferDesc.Scaling = DXGI_MODE_SCALING(v[6]);
    d.SampleDesc.Count = v[7]; d.SampleDesc.Quality = v[8];
    d.BufferUsage = v[9]; d.BufferCount = v[10];
    d.OutputWindow = reinterpret_cast<HWND>(h.d3d9_window ? h.d3d9_window(v[11]) : 0);
    d.Windowed = v[12]; d.SwapEffect = DXGI_SWAP_EFFECT(v[13]); d.Flags = v[14];
    return d;
}
inline void d3d11_write_swapchain_desc(Cpu& c, uint32_t guest, const DXGI_SWAP_CHAIN_DESC& d, uint32_t guest_window) {
    const uint32_t v[15] = {d.BufferDesc.Width, d.BufferDesc.Height, d.BufferDesc.RefreshRate.Numerator, d.BufferDesc.RefreshRate.Denominator,
        uint32_t(d.BufferDesc.Format), uint32_t(d.BufferDesc.ScanlineOrdering), uint32_t(d.BufferDesc.Scaling),
        d.SampleDesc.Count, d.SampleDesc.Quality, d.BufferUsage, d.BufferCount, guest_window,
        uint32_t(d.Windowed), uint32_t(d.SwapEffect), d.Flags};
    for (unsigned i = 0; i < 15; ++i) c.mem.w32(guest + i * 4, v[i]);
}

template<size_t N> inline void d3d11_iface(ComSpace& s, Host& h, const char* iface, const char* const (&names)[N], const unsigned (&argc)[N]) {
    s.interface_(iface, unsigned(N));
    for (unsigned slot = 3; slot < N; ++slot) {
        std::string where = std::string(iface) + "::" + names[slot];
        s.method(iface, slot, argc[slot], [&h, where](Proxy&, Cpu&) -> uint32_t { d3d11_gap_text(h, where.c_str()); return D3D11_GAP_HRESULT; });
    }
}

// Shared by D3D11CreateDevice (10 stack slots) and ...AndSwapChain (12). The
// chain variant inserts pSwapChainDesc at slot 7 and ppSwapChain at slot 8.
inline uint32_t d3d11_create_device(Host& h, ComSpace& s, Cpu& c, bool chain) {
    const unsigned desc_slot = 7, swap_slot = 8, dev_slot = chain ? 9 : 7, level_slot = chain ? 10 : 8, ctx_slot = chain ? 11 : 9;
    auto fail = [&](uint32_t hr) {
        if (chain && c.arg(swap_slot)) c.mem.w32(c.arg(swap_slot), 0);
        if (c.arg(dev_slot)) c.mem.w32(c.arg(dev_slot), 0);
        if (c.arg(ctx_slot)) c.mem.w32(c.arg(ctx_slot), 0);
        return hr;
    };
    const char* api = chain ? "D3D11CreateDeviceAndSwapChain" : "D3D11CreateDevice";
    if (c.arg(0)) { d3d11_gap_text(h, (std::string(api) + " non-default adapter").c_str()); return fail(D3D11_GAP_HRESULT); }
    if (c.arg(5) > 16 || (c.arg(5) && !c.arg(4))) return fail(uint32_t(E_INVALIDARG));
    if (chain && (!c.arg(desc_slot) || !c.arg(swap_slot))) return fail(uint32_t(E_INVALIDARG));
    x86::D3D11CreateArgs a;
    a.driver = c.arg(1); a.flags = c.arg(3); a.sdk = c.arg(6);
    for (uint32_t i = 0; i < c.arg(5); ++i) a.levels.push_back(c.mem.r32(c.arg(4) + 4 * i));
    DXGI_SWAP_CHAIN_DESC desc{};
    if (chain) { desc = d3d11_read_swapchain_desc(h, c, c.arg(desc_slot)); a.swapchain_desc = &desc; }
    if (!h.d3d11_create) { d3d11_gap_text(h, api); return fail(D3D11_GAP_HRESULT); }
    uint32_t hr = h.d3d11_create(a);
    if (int32_t(hr) < 0) return fail(hr);
    if (chain) {
        uint32_t swap = a.swapchain ? d3d9_wrap(s, c, "IDXGISwapChain", static_cast<IUnknown*>(a.swapchain)) : 0;
        if (swap) s.by_guest(swap).d3d9_focus_window = c.mem.r32(c.arg(desc_slot) + 44);
        c.mem.w32(c.arg(swap_slot), swap);
    }
    if (c.arg(dev_slot)) c.mem.w32(c.arg(dev_slot), a.device ? d3d9_wrap(s, c, "ID3D11Device", static_cast<IUnknown*>(a.device)) : 0);
    else if (a.device) static_cast<IUnknown*>(a.device)->Release();
    if (c.arg(ctx_slot)) c.mem.w32(c.arg(ctx_slot), a.context ? d3d9_wrap(s, c, "ID3D11DeviceContext", static_cast<IUnknown*>(a.context)) : 0);
    else if (a.context) static_cast<IUnknown*>(a.context)->Release();
    if (c.arg(level_slot)) c.mem.w32(c.arg(level_slot), a.feature_level);
    return hr;
}

inline void register_d3d11_com(ComSpace& s, Host& h) {
    d3d11_iface(s, h, "IDXGISwapChain", kD3D11SwapChainNames, kD3D11SwapChainArgc);
    d3d11_iface(s, h, "ID3D11Device", kD3D11DeviceNames, kD3D11DeviceArgc);
    d3d11_iface(s, h, "ID3D11DeviceContext", kD3D11ContextNames, kD3D11ContextArgc);
    d3d11_iface(s, h, "ID3D11Buffer", kD3D11ResourceNames, kD3D11ResourceArgc);
    d3d11_iface(s, h, "ID3D11Texture2D", kD3D11ResourceNames, kD3D11ResourceArgc);
    d3d11_iface(s, h, "ID3D11RenderTargetView", kD3D11ViewNames, kD3D11ViewArgc);
    d3d11_iface(s, h, "ID3D11ShaderResourceView", kD3D11ViewNames, kD3D11ViewArgc);
    d3d11_iface(s, h, "ID3D11VertexShader", kD3D11ChildNames, kD3D11ChildArgc);
    d3d11_iface(s, h, "ID3D11PixelShader", kD3D11ChildNames, kD3D11ChildArgc);
    d3d11_iface(s, h, "ID3D11InputLayout", kD3D11ChildNames, kD3D11ChildArgc);
    d3d11_iface(s, h, "ID3D11BlendState", kD3D11StateNames, kD3D11StateArgc);
    d3d11_iface(s, h, "ID3D11RasterizerState", kD3D11StateNames, kD3D11StateArgc);
    d3d11_iface(s, h, "ID3D11DepthStencilState", kD3D11StateNames, kD3D11StateArgc);
    d3d11_iface(s, h, "ID3D11SamplerState", kD3D11StateNames, kD3D11StateArgc);

    // ---- IDXGISwapChain
    s.method("IDXGISwapChain", 8, 3, [&h](Proxy& p, Cpu& c) -> uint32_t {
        if (h.route_trace) h.route_trace("D3D11 SwapChain Present");
        return uint32_t(d3d11_as<IDXGISwapChain>(p.host)->Present(c.arg(1), c.arg(2)));
    });
    s.method("IDXGISwapChain", 9, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(2) || !c.arg(3)) return uint32_t(E_POINTER);
        GUID iid = d3d9_read_struct<GUID>(c, c.arg(2));
        void* raw = nullptr;
        HRESULT hr = d3d11_as<IDXGISwapChain>(p.host)->GetBuffer(c.arg(1), iid, &raw);
        IUnknown* object = static_cast<IUnknown*>(raw);
        if (SUCCEEDED(hr) && !IsEqualGUID(iid, __uuidof(ID3D11Texture2D))) { object->Release(); object = nullptr; hr = E_NOINTERFACE; }
        return d3d11_out(s, c, c.arg(3), "ID3D11Texture2D", hr, object);
    });
    s.method("IDXGISwapChain", 12, 2, [&h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        DXGI_SWAP_CHAIN_DESC d{};
        HRESULT hr = d3d11_as<IDXGISwapChain>(p.host)->GetDesc(&d);
        if (SUCCEEDED(hr)) d3d11_write_swapchain_desc(c, c.arg(1), d, p.d3d9_focus_window);
        return uint32_t(hr);
    });
    s.method("IDXGISwapChain", 13, 6, [](Proxy& p, Cpu& c) -> uint32_t {
        return uint32_t(d3d11_as<IDXGISwapChain>(p.host)->ResizeBuffers(c.arg(1), c.arg(2), c.arg(3), DXGI_FORMAT(c.arg(4)), c.arg(5)));
    });

    // ---- ID3D11Device creation (the 2D game set) and feature queries
    s.method("ID3D11Device", 3, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(3)) return uint32_t(E_POINTER);
        D3D11_BUFFER_DESC desc = d3d9_read_struct<D3D11_BUFFER_DESC>(c, c.arg(1));
        if (desc.ByteWidth > D3D9_GUEST_SIZE) return uint32_t(E_INVALIDARG);
        std::vector<uint8_t> init; D3D11_SUBRESOURCE_DATA sub{};
        if (c.arg(2)) { init = c.mem.read_bytes(c.mem.r32(c.arg(2)), desc.ByteWidth); sub.pSysMem = init.data(); }
        ID3D11Buffer* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateBuffer(&desc, c.arg(2) ? &sub : nullptr, &x);
        return d3d11_out(s, c, c.arg(3), "ID3D11Buffer", hr, x);
    });
    s.method("ID3D11Device", 5, 4, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(3)) return uint32_t(E_POINTER);
        D3D11_TEXTURE2D_DESC desc = d3d9_read_struct<D3D11_TEXTURE2D_DESC>(c, c.arg(1));
        std::vector<uint8_t> init; D3D11_SUBRESOURCE_DATA sub{};
        if (c.arg(2)) {
            // Phase 1 copies one subresource only: a mip chain or array needs every subresource.
            if (desc.MipLevels != 1 || desc.ArraySize != 1) return d3d11_unbridged(h, p, "CreateTexture2D multi-subresource data");
            uint32_t pitch = c.mem.r32(c.arg(2) + 4);
            if (uint64_t(pitch) * desc.Height > D3D9_GUEST_SIZE) return uint32_t(E_INVALIDARG);
            init = c.mem.read_bytes(c.mem.r32(c.arg(2)), pitch * desc.Height);
            sub.pSysMem = init.data(); sub.SysMemPitch = pitch;
        }
        ID3D11Texture2D* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateTexture2D(&desc, c.arg(2) ? &sub : nullptr, &x);
        return d3d11_out(s, c, c.arg(3), "ID3D11Texture2D", hr, x);
    });
    s.method("ID3D11Device", 7, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(3)) return uint32_t(E_POINTER);
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; D3D11_SHADER_RESOURCE_VIEW_DESC* dp = nullptr;
        if (c.arg(2)) { desc = d3d9_read_struct<D3D11_SHADER_RESOURCE_VIEW_DESC>(c, c.arg(2)); dp = &desc; }
        ID3D11ShaderResourceView* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateShaderResourceView(d3d11_obj<ID3D11Resource>(s, c.arg(1)), dp, &x);
        return d3d11_out(s, c, c.arg(3), "ID3D11ShaderResourceView", hr, x);
    });
    s.method("ID3D11Device", 9, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(3)) return uint32_t(E_POINTER);
        D3D11_RENDER_TARGET_VIEW_DESC desc{}; D3D11_RENDER_TARGET_VIEW_DESC* dp = nullptr;
        if (c.arg(2)) { desc = d3d9_read_struct<D3D11_RENDER_TARGET_VIEW_DESC>(c, c.arg(2)); dp = &desc; }
        ID3D11RenderTargetView* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateRenderTargetView(d3d11_obj<ID3D11Resource>(s, c.arg(1)), dp, &x);
        return d3d11_out(s, c, c.arg(3), "ID3D11RenderTargetView", hr, x);
    });
    s.method("ID3D11Device", 11, 6, [&s](Proxy& p, Cpu& c) -> uint32_t {
        // D3D11_INPUT_ELEMENT_DESC is 28 bytes in PE32 (SemanticName is a guest pointer).
        if (!c.arg(5) || !c.arg(1) || !c.arg(3) || !c.arg(2) || c.arg(2) > 32 || !c.arg(4)) return uint32_t(E_INVALIDARG);
        uint32_t n = c.arg(2);
        std::vector<std::string> names(n);
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements(n);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t a = c.arg(1) + 28 * i;
            names[i] = d3d11_cstring(c, c.mem.r32(a));
            D3D11_INPUT_ELEMENT_DESC& e = elements[i];
            e.SemanticName = names[i].c_str(); e.SemanticIndex = c.mem.r32(a + 4);
            e.Format = DXGI_FORMAT(c.mem.r32(a + 8)); e.InputSlot = c.mem.r32(a + 12);
            e.AlignedByteOffset = c.mem.r32(a + 16); e.InputSlotClass = D3D11_INPUT_CLASSIFICATION(c.mem.r32(a + 20));
            e.InstanceDataStepRate = c.mem.r32(a + 24);
        }
        std::vector<uint8_t> code = c.mem.read_bytes(c.arg(3), c.arg(4));
        ID3D11InputLayout* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateInputLayout(elements.data(), n, code.data(), c.arg(4), &x);
        return d3d11_out(s, c, c.arg(5), "ID3D11InputLayout", hr, x);
    });
    s.method("ID3D11Device", 12, 5, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(4) || !c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        if (c.arg(3)) return d3d11_unbridged(h, p, "CreateVertexShader class linkage");
        std::vector<uint8_t> code = c.mem.read_bytes(c.arg(1), c.arg(2));
        ID3D11VertexShader* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateVertexShader(code.data(), c.arg(2), nullptr, &x);
        return d3d11_out(s, c, c.arg(4), "ID3D11VertexShader", hr, x);
    });
    s.method("ID3D11Device", 15, 5, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(4) || !c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        if (c.arg(3)) return d3d11_unbridged(h, p, "CreatePixelShader class linkage");
        std::vector<uint8_t> code = c.mem.read_bytes(c.arg(1), c.arg(2));
        ID3D11PixelShader* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreatePixelShader(code.data(), c.arg(2), nullptr, &x);
        return d3d11_out(s, c, c.arg(4), "ID3D11PixelShader", hr, x);
    });
    s.method("ID3D11Device", 20, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        D3D11_BLEND_DESC d = d3d9_read_struct<D3D11_BLEND_DESC>(c, c.arg(1));
        ID3D11BlendState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateBlendState(&d, &x);
        return d3d11_out(s, c, c.arg(2), "ID3D11BlendState", hr, x);
    });
    s.method("ID3D11Device", 21, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        D3D11_DEPTH_STENCIL_DESC d = d3d9_read_struct<D3D11_DEPTH_STENCIL_DESC>(c, c.arg(1));
        ID3D11DepthStencilState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateDepthStencilState(&d, &x);
        return d3d11_out(s, c, c.arg(2), "ID3D11DepthStencilState", hr, x);
    });
    s.method("ID3D11Device", 22, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        D3D11_RASTERIZER_DESC d = d3d9_read_struct<D3D11_RASTERIZER_DESC>(c, c.arg(1));
        ID3D11RasterizerState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateRasterizerState(&d, &x);
        return d3d11_out(s, c, c.arg(2), "ID3D11RasterizerState", hr, x);
    });
    s.method("ID3D11Device", 23, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        D3D11_SAMPLER_DESC d = d3d9_read_struct<D3D11_SAMPLER_DESC>(c, c.arg(1));
        ID3D11SamplerState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateSamplerState(&d, &x);
        return d3d11_out(s, c, c.arg(2), "ID3D11SamplerState", hr, x);
    });
    s.method("ID3D11Device", 29, 3, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(2)) return uint32_t(E_POINTER);
        UINT support = 0;
        HRESULT hr = d3d11_device(p)->CheckFormatSupport(DXGI_FORMAT(c.arg(1)), &support);
        if (SUCCEEDED(hr)) c.mem.w32(c.arg(2), support);
        return uint32_t(hr);
    });
    s.method("ID3D11Device", 37, 1, [](Proxy& p, Cpu&) -> uint32_t { return uint32_t(d3d11_device(p)->GetFeatureLevel()); });
    s.method("ID3D11Device", 40, 2, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        ID3D11DeviceContext* x = nullptr; d3d11_device(p)->GetImmediateContext(&x);
        return d3d11_out(s, c, c.arg(1), "ID3D11DeviceContext", S_OK, x);
    });

    // ---- ID3D11DeviceContext: the draw path (state binds, uploads, clears, draws)
    s.method("ID3D11DeviceContext", 7, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11Buffer*> buffers;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), buffers, 14)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->VSSetConstantBuffers(c.arg(1), c.arg(2), buffers.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 16, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11Buffer*> buffers;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), buffers, 14)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->PSSetConstantBuffers(c.arg(1), c.arg(2), buffers.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 8, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11ShaderResourceView*> views;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), views, 128)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->PSSetShaderResources(c.arg(1), c.arg(2), views.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 10, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11SamplerState*> samplers;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), samplers, 16)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->PSSetSamplers(c.arg(1), c.arg(2), samplers.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 9, 4, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (c.arg(2) || c.arg(3)) return d3d11_unbridged(h, p, "PSSetShader class instances");
        d3d11_context(p)->PSSetShader(d3d11_obj<ID3D11PixelShader>(s, c.arg(1)), nullptr, 0); return 0;
    });
    s.method("ID3D11DeviceContext", 11, 4, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (c.arg(2) || c.arg(3)) return d3d11_unbridged(h, p, "VSSetShader class instances");
        d3d11_context(p)->VSSetShader(d3d11_obj<ID3D11VertexShader>(s, c.arg(1)), nullptr, 0); return 0;
    });
    s.method("ID3D11DeviceContext", 12, 4, [](Proxy& p, Cpu& c) -> uint32_t { d3d11_context(p)->DrawIndexed(c.arg(1), c.arg(2), int32_t(c.arg(3))); return 0; });
    s.method("ID3D11DeviceContext", 13, 3, [](Proxy& p, Cpu& c) -> uint32_t { d3d11_context(p)->Draw(c.arg(1), c.arg(2)); return 0; });
    s.method("ID3D11DeviceContext", 14, 6, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(5)) return uint32_t(E_POINTER);
        ID3D11Resource* resource = d3d11_obj<ID3D11Resource>(s, c.arg(1));
        D3D11_MAPPED_SUBRESOURCE m{};
        HRESULT hr = d3d11_context(p)->Map(resource, c.arg(2), D3D11_MAP(c.arg(3)), c.arg(4), &m);
        if (FAILED(hr)) return uint32_t(hr);
        uint32_t size = d3d11_subresource_bytes(resource, c.arg(2), m.RowPitch);
        if (!size || !m.pData) { d3d11_context(p)->Unmap(resource, c.arg(2)); return d3d11_unbridged(h, p, "Map resource type"); }
        uint32_t at = d3d9_guest_buffer(h, c, size);
        c.mem.write_bytes(at, m.pData, size);
        h.d3d9_locks[{c.arg(1), c.arg(2)}] = {m.pData, size, at};
        c.mem.w32(c.arg(5), at); c.mem.w32(c.arg(5) + 4, m.RowPitch); c.mem.w32(c.arg(5) + 8, m.DepthPitch);
        return uint32_t(hr);
    });
    s.method("ID3D11DeviceContext", 15, 3, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        // Copy the guest scratch back to the mapped host memory before the host Unmap.
        auto it = h.d3d9_locks.find({c.arg(1), c.arg(2)});
        if (it != h.d3d9_locks.end()) {
            if (std::get<0>(it->second) && std::get<1>(it->second)) {
                auto bytes = c.mem.read_bytes(std::get<2>(it->second), std::get<1>(it->second));
                memcpy(std::get<0>(it->second), bytes.data(), bytes.size());
            }
            h.d3d9_locks.erase(it);
        }
        if (c.arg(1)) d3d11_context(p)->Unmap(d3d11_obj<ID3D11Resource>(s, c.arg(1)), c.arg(2));
        return 0;
    });
    s.method("ID3D11DeviceContext", 17, 2, [&s](Proxy& p, Cpu& c) -> uint32_t { d3d11_context(p)->IASetInputLayout(d3d11_obj<ID3D11InputLayout>(s, c.arg(1))); return 0; });
    s.method("ID3D11DeviceContext", 18, 6, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11Buffer*> buffers;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), buffers, 32) || (c.arg(2) && (!c.arg(4) || !c.arg(5)))) return uint32_t(E_INVALIDARG);
        std::vector<UINT> strides(c.arg(2)), offsets(c.arg(2));
        for (uint32_t i = 0; i < c.arg(2); ++i) { strides[i] = c.mem.r32(c.arg(4) + 4 * i); offsets[i] = c.mem.r32(c.arg(5) + 4 * i); }
        d3d11_context(p)->IASetVertexBuffers(c.arg(1), c.arg(2), buffers.data(), strides.data(), offsets.data());
        return 0;
    });
    s.method("ID3D11DeviceContext", 19, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        d3d11_context(p)->IASetIndexBuffer(d3d11_obj<ID3D11Buffer>(s, c.arg(1)), DXGI_FORMAT(c.arg(2)), c.arg(3)); return 0;
    });
    s.method("ID3D11DeviceContext", 24, 2, [](Proxy& p, Cpu& c) -> uint32_t { d3d11_context(p)->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY(c.arg(1))); return 0; });
    s.method("ID3D11DeviceContext", 33, 4, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11RenderTargetView*> targets;
        if (!d3d11_objects(s, c, c.arg(2), c.arg(1), targets, 8)) return uint32_t(E_INVALIDARG);
        if (c.arg(3)) return d3d11_unbridged(h, p, "OMSetRenderTargets depth-stencil view");
        d3d11_context(p)->OMSetRenderTargets(c.arg(1), targets.data(), nullptr); return 0;
    });
    s.method("ID3D11DeviceContext", 35, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        float factor[4]{1, 1, 1, 1};
        if (c.arg(2)) { auto bytes = c.mem.read_bytes(c.arg(2), sizeof(factor)); memcpy(factor, bytes.data(), sizeof(factor)); }
        d3d11_context(p)->OMSetBlendState(d3d11_obj<ID3D11BlendState>(s, c.arg(1)), c.arg(2) ? factor : nullptr, c.arg(3)); return 0;
    });
    s.method("ID3D11DeviceContext", 36, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        d3d11_context(p)->OMSetDepthStencilState(d3d11_obj<ID3D11DepthStencilState>(s, c.arg(1)), c.arg(2)); return 0;
    });
    s.method("ID3D11DeviceContext", 43, 2, [&s](Proxy& p, Cpu& c) -> uint32_t { d3d11_context(p)->RSSetState(d3d11_obj<ID3D11RasterizerState>(s, c.arg(1))); return 0; });
    s.method("ID3D11DeviceContext", 44, 3, [](Proxy& p, Cpu& c) -> uint32_t {
        if (c.arg(1) > 16 || (c.arg(1) && !c.arg(2))) return uint32_t(E_INVALIDARG);
        std::vector<D3D11_VIEWPORT> viewports(c.arg(1));
        for (uint32_t i = 0; i < c.arg(1); ++i) viewports[i] = d3d9_read_struct<D3D11_VIEWPORT>(c, c.arg(2) + i * sizeof(D3D11_VIEWPORT));
        d3d11_context(p)->RSSetViewports(c.arg(1), viewports.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 45, 3, [](Proxy& p, Cpu& c) -> uint32_t {
        if (c.arg(1) > 16 || (c.arg(1) && !c.arg(2))) return uint32_t(E_INVALIDARG);
        std::vector<RECT> rects(c.arg(1));
        for (uint32_t i = 0; i < c.arg(1); ++i) rects[i] = d3d9_read_struct<RECT>(c, c.arg(2) + i * sizeof(D3D11_RECT));
        d3d11_context(p)->RSSetScissorRects(c.arg(1), rects.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 48, 7, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (c.arg(3)) return d3d11_unbridged(h, p, "UpdateSubresource destination box");
        ID3D11Resource* dst = d3d11_obj<ID3D11Resource>(s, c.arg(1));
        uint32_t size = dst ? d3d11_subresource_bytes(dst, c.arg(2), c.arg(5)) : 0;
        if (!c.arg(4) || !size) return uint32_t(E_INVALIDARG);
        std::vector<uint8_t> data = c.mem.read_bytes(c.arg(4), size);
        d3d11_context(p)->UpdateSubresource(dst, c.arg(2), nullptr, data.data(), c.arg(5), c.arg(6)); return 0;
    });
    s.method("ID3D11DeviceContext", 50, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(2)) return uint32_t(E_POINTER);
        float color[4];
        auto bytes = c.mem.read_bytes(c.arg(2), sizeof(color)); memcpy(color, bytes.data(), sizeof(color));
        d3d11_context(p)->ClearRenderTargetView(d3d11_obj<ID3D11RenderTargetView>(s, c.arg(1)), color);
        return 0;
    });
}
inline void register_d3d11(Registry& r, Host& h) {
    register_d3d11_com(h.com, h);
    r.add("d3d11.dll", "D3D11CreateDevice", Conv::Stdcall, 10, [&h](Cpu& c) -> uint32_t { return d3d11_create_device(h, h.com, c, false); });
    r.add("d3d11.dll", "D3D11CreateDeviceAndSwapChain", Conv::Stdcall, 12, [&h](Cpu& c) -> uint32_t { return d3d11_create_device(h, h.com, c, true); });
}
} // namespace x86
#else
namespace x86 { inline void register_d3d11(Registry&, Host&) {} }
#endif
