// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 D3D11 imports and COM proxy marshalling onto the host's 64-bit D3D11.
// Phase 1 is device and swapchain creation plus the draw path a 2D game uses.
// Phase 2 adds depth-stencil views, mip/array/BC textures and boxed updates,
// direction-aware Map, queries, copies, instancing and swapchain fullscreen state.
// Every other vtable slot is a named gap: it logs "x86 D3D11 gap: Interface::Method"
// and returns E_NOTIMPL, the same rule the D3D9 bridge applies.
//
// Slot order comes from the SDK vtables (see tests/x86-d3d11-slots.cpp, which
// proves each bridged slot with offsetof under CINTERFACE). Argument counts are
// x86 stdcall DWORD slots, this included.
#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>
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
// ID3D11Query: ID3D11DeviceChild + ID3D11Asynchronous::GetDataSize + ID3D11Query::GetDesc.
inline constexpr const char* const kD3D11QueryNames[] = {"QueryInterface","AddRef","Release","GetDevice","GetPrivateData","SetPrivateData","SetPrivateDataInterface","GetDataSize","GetDesc"};
inline constexpr unsigned kD3D11QueryArgc[] = {3,1,1,2,4,4,3,1,2};
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
    // A bad guest pointer faults here, before a proxy takes the host reference, so nothing leaks.
    try { c.mem.w32(out, 0); } catch (...) { if (object) object->Release(); throw; }
    c.mem.w32(out, SUCCEEDED(hr) && object ? d3d9_wrap(s, c, iface, object) : 0);
    return uint32_t(hr);
}
inline std::string d3d11_cstring(Cpu& c, uint32_t address) {
    std::string s;
    for (uint32_t i = 0; address && i < 256; ++i) { uint8_t ch = c.mem.r8(address + i); if (!ch) break; s.push_back(char(ch)); }
    return s;
}
inline bool d3d11_is_bc(DXGI_FORMAT f) {
    return (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) || (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB);
}
// Rows of pixel data in one mip: BC formats store 4x4 blocks, so ceil(height / 4) block rows.
inline uint32_t d3d11_mip_rows(const D3D11_TEXTURE2D_DESC& d, uint32_t level) {
    uint32_t h = std::max<uint32_t>(1, d.Height >> level);
    return d3d11_is_bc(d.Format) ? (h + 3) / 4 : h;
}
// Bits per element of an uncompressed format; 0 for BC (sized per block), planar/video and unknown formats.
inline uint32_t d3d11_format_bits(DXGI_FORMAT f) {
    const unsigned v = unsigned(f);
    if (v >= 1 && v <= 4) return 128;
    if (v >= 5 && v <= 8) return 96;
    if (v >= 9 && v <= 22) return 64;
    if (v >= 23 && v <= 47) return 32;
    if (v >= 48 && v <= 59) return 16;
    if (v >= 60 && v <= 65) return 8;
    if (v == 66) return 1;
    if (v == 67 || (v >= 87 && v <= 93)) return 32;
    if (v == 85 || v == 86 || v == 115) return 16;
    return 0;
}
// Bytes of one row of `width` texels. BC formats are 4x4 blocks of 8 bytes (BC1, BC4) or 16 (the rest),
// so a row is ceil(width / 4) blocks; the packed 4:2:2 formats hold two texels per 4 bytes. 0 is unsupported.
inline uint64_t d3d11_row_bytes(DXGI_FORMAT f, uint32_t width) {
    const unsigned v = unsigned(f);
    if (!width) return 0;
    if (d3d11_is_bc(f)) return uint64_t((width + 3) / 4) * (((v >= 70 && v <= 72) || (v >= 79 && v <= 81)) ? 8 : 16);
    if (v == 68 || v == 69) return uint64_t((width + 1) / 2) * 4;
    const uint32_t bits = d3d11_format_bits(f);
    return bits ? (uint64_t(bits) * width + 7) / 8 : 0;
}
// The bytes an image copy touches: every row but the last spans a pitch, the last only its texels.
// 0 is invalid: no rows, a pitch smaller than a row, no pitch for several rows, or past the guest window.
inline uint64_t d3d11_image_span(uint64_t row_bytes, uint32_t rows, uint32_t pitch) {
    if (!row_bytes || !rows || (pitch && pitch < row_bytes) || (rows > 1 && !pitch)) return 0;
    uint64_t span = uint64_t(rows - 1) * pitch + row_bytes;
    return span > D3D9_GUEST_SIZE ? 0 : span;
}
// Bytes of one subresource: buffers are ByteWidth, 2D textures are the image span of that mip at the
// given pitch. 0 means unsupported, out of range, a bad pitch, or larger than the guest scratch window.
inline uint32_t d3d11_subresource_bytes(ID3D11Resource* r, uint32_t sub, uint32_t row_pitch) {
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) { D3D11_BUFFER_DESC d{}; static_cast<ID3D11Buffer*>(r)->GetDesc(&d); return d.ByteWidth; }
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d{}; static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
        uint32_t mips = d.MipLevels ? d.MipLevels : 1;
        if (mips > 15 || sub >= uint64_t(mips) * d.ArraySize) return 0;
        uint32_t level = sub % mips;
        return uint32_t(d3d11_image_span(d3d11_row_bytes(d.Format, std::max<uint32_t>(1, d.Width >> level)), d3d11_mip_rows(d, level), row_pitch));
    }
    return 0;
}
// True when the CPU may read the resource back: a staging resource with read access.
inline bool d3d11_cpu_readable(ID3D11Resource* r) {
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
    D3D11_USAGE usage = D3D11_USAGE_DEFAULT; UINT access = 0;
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) { D3D11_BUFFER_DESC d{}; static_cast<ID3D11Buffer*>(r)->GetDesc(&d); usage = d.Usage; access = d.CPUAccessFlags; }
    else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d{}; static_cast<ID3D11Texture2D*>(r)->GetDesc(&d); usage = d.Usage; access = d.CPUAccessFlags; }
    return usage == D3D11_USAGE_STAGING && (access & D3D11_CPU_ACCESS_READ);
}
// Guest bytes an UpdateSubresource reads: the whole subresource, or the box of it.
// -1 is an invalid box, pitch or subresource; -2 an unsupported format; 0 is an empty box (a no-op in D3D11).
inline int64_t d3d11_update_bytes(ID3D11Resource* r, uint32_t sub, const D3D11_BOX* box, uint32_t row_pitch) {
    if (!box) {
        uint32_t n = d3d11_subresource_bytes(r, sub, row_pitch);
        if (n) return int64_t(n);
        D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
        if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) { D3D11_TEXTURE2D_DESC d{}; static_cast<ID3D11Texture2D*>(r)->GetDesc(&d); if (!d3d11_row_bytes(d.Format, std::max<uint32_t>(1, d.Width))) return -2; }
        return -1;
    }
    if (box->left > box->right || box->top > box->bottom || box->front > box->back) return -1;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
    uint32_t width = 0, height = 1, level = 0; const D3D11_TEXTURE2D_DESC* tex = nullptr; D3D11_TEXTURE2D_DESC d{};
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC b{}; static_cast<ID3D11Buffer*>(r)->GetDesc(&b); width = b.ByteWidth;
        if (sub != 0) return -1;
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        static_cast<ID3D11Texture2D*>(r)->GetDesc(&d); tex = &d;
        uint32_t mips = d.MipLevels ? d.MipLevels : 1;
        if (mips > 15 || sub >= uint64_t(mips) * d.ArraySize) return -1;
        level = sub % mips; width = std::max<uint32_t>(1, d.Width >> level); height = std::max<uint32_t>(1, d.Height >> level);
    } else return -1;
    if (box->right > width || box->bottom > height || box->back > 1) return -1;
    if (box->left == box->right || box->top == box->bottom || box->front == box->back) return 0;
    if (!tex) return int64_t(box->right - box->left);
    uint32_t rows = d3d11_is_bc(d.Format) ? (box->bottom - box->top + 3) / 4 : box->bottom - box->top;
    uint64_t row_bytes = d3d11_row_bytes(d.Format, box->right - box->left);
    if (!row_bytes) return -2;
    uint64_t span = d3d11_image_span(row_bytes, rows, row_pitch);
    return span ? int64_t(span) : -1;
}
// CreateTexture2D initial data: pInitialData is MipLevels * ArraySize guest D3D11_SUBRESOURCE_DATA
// entries of 12 bytes ({pSysMem, SysMemPitch, SysMemSlicePitch}), mip-major within each array slice.
// MipLevels 0 is the full chain down to 1x1. Each entry reads its image span (the last row only its
// texels) and the total stays inside the guest scratch window. A format with no row size (planar and
// video formats) answers the gap HRESULT.
inline HRESULT d3d11_read_texture_init(Cpu& c, uint32_t guest, const D3D11_TEXTURE2D_DESC& desc,
                                       std::vector<std::vector<uint8_t>>& bytes, std::vector<D3D11_SUBRESOURCE_DATA>& subs) {
    if (!desc.Width || !desc.Height || !desc.ArraySize || desc.ArraySize > 2048 || desc.MipLevels > 15) return E_INVALIDARG;
    uint32_t mips = desc.MipLevels;
    if (!mips) for (uint32_t m = std::max(desc.Width, desc.Height); m; m >>= 1) ++mips;
    uint32_t count = mips * desc.ArraySize;
    bytes.assign(count, {}); subs.assign(count, D3D11_SUBRESOURCE_DATA{});
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t entry = guest + 12 * i, at = c.mem.r32(entry), pitch = c.mem.r32(entry + 4);
        uint32_t level = i % mips;
        uint64_t row_bytes = d3d11_row_bytes(desc.Format, std::max<uint32_t>(1, desc.Width >> level));
        if (!row_bytes) return HRESULT(D3D11_GAP_HRESULT);
        uint64_t size = d3d11_image_span(row_bytes, d3d11_mip_rows(desc, level), pitch);
        total += size;
        if (!at || !size || total > D3D9_GUEST_SIZE) return E_INVALIDARG;
        bytes[i] = c.mem.read_bytes(at, uint32_t(size));
        subs[i].pSysMem = bytes[i].data(); subs[i].SysMemPitch = pitch; subs[i].SysMemSlicePitch = c.mem.r32(entry + 8);
    }
    return S_OK;
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

// Map state per (host resource, subresource). Guest memory is a bump window that never frees, so each
// subresource keeps one scratch range (grown only when a bigger map needs it) and a dropped resource
// hands its range back to a coalescing free list for reuse. The shadow is host RAM: what the guest last wrote, so a WRITE or
// NO_OVERWRITE map seeds its scratch from it (never reading the host's possibly write-combined memory)
// and Unmap copies back only the bytes that differ from it, leaving earlier data and GPU-owned bytes alone.
struct D3d11MapState {
    struct Entry {
        uint32_t scratch = 0, capacity = 0, size = 0, mode = 0; void* host = nullptr; bool active = false;
        std::vector<uint8_t> shadow; bool shadow_valid = false;
    };
    std::map<std::pair<void*, uint32_t>, Entry> entries;
    std::set<void*> hooked;                                    // resources whose proxy release drops their entries
    // {guest address, capacity} ranges no resource owns: sorted by address and never adjacent, because
    // give_back merges neighbours. Ranges are multiples of 16 bytes, like the bump window's alignment.
    std::vector<std::pair<uint32_t, uint32_t>> free_scratch;
    void give_back(uint32_t at, uint32_t capacity) {
        auto it = free_scratch.insert(std::lower_bound(free_scratch.begin(), free_scratch.end(), std::make_pair(at, 0u)), {at, capacity});
        if (it + 1 != free_scratch.end() && it->first + it->second == (it + 1)->first) { it->second += (it + 1)->second; free_scratch.erase(it + 1); }
        if (it != free_scratch.begin() && (it - 1)->first + (it - 1)->second == it->first) { (it - 1)->second += it->second; free_scratch.erase(it); }
    }
    // One range per subresource, reused while it is big enough. A bigger map takes the smallest free range
    // that fits (splitting off the rest); failing that, a free range that ends at the bump cursor grows
    // into the untouched window, so released scratch never strands the 16 MiB window. A throw (the window
    // cannot hold it) leaves the entry with no scratch and the list consistent.
    uint32_t acquire(Host& h, Cpu& c, Entry& e, uint32_t size) {
        if (size > D3D9_GUEST_SIZE) return d3d9_guest_buffer(h, c, size);   // the window fault
        const uint32_t need = (size + 15) & ~15u;
        if (e.scratch && e.capacity >= need) return e.scratch;
        if (e.scratch) { give_back(e.scratch, e.capacity); e.scratch = 0; e.capacity = 0; }
        size_t best = free_scratch.size();
        for (size_t i = 0; i < free_scratch.size(); ++i)
            if (free_scratch[i].second >= need && (best == free_scratch.size() || free_scratch[i].second < free_scratch[best].second)) best = i;
        uint32_t at = 0;
        if (best != free_scratch.size()) {
            at = free_scratch[best].first;
            if (free_scratch[best].second > need) free_scratch[best] = {at + need, free_scratch[best].second - need};
            else free_scratch.erase(free_scratch.begin() + best);
        } else if (!free_scratch.empty() && (h.d3d9_guest_top & 15) == 0 && free_scratch.back().first + free_scratch.back().second == h.d3d9_guest_top) {
            at = free_scratch.back().first;
            d3d9_guest_buffer(h, c, need - free_scratch.back().second);   // lands at the cursor, right after the free range
            free_scratch.pop_back();
        } else at = d3d9_guest_buffer(h, c, need);
        e.scratch = at; e.capacity = need;
        return at;
    }
    // The guest no longer holds the resource: its scratch ranges go back to the free list.
    void drop(void* resource) {
        hooked.erase(resource);
        for (auto it = entries.lower_bound({resource, 0}); it != entries.end() && it->first.first == resource; it = entries.erase(it))
            if (it->second.scratch) give_back(it->second.scratch, it->second.capacity);
    }
    // The host copy changed behind the shadow (an update or a copy), so the shadow no longer says what is there.
    void invalidate(void* resource) {
        for (auto it = entries.lower_bound({resource, 0}); it != entries.end() && it->first.first == resource; ++it) it->second.shadow_valid = false;
    }
};
// Like d3d11_out for a buffer or texture: also drops the resource's map state when its proxy is released.
template<class T> inline uint32_t d3d11_out_resource(const std::shared_ptr<D3d11MapState>& maps, ComSpace& s, Cpu& c, uint32_t out, const char* iface, HRESULT hr, T* object) {
    uint32_t result = d3d11_out(s, c, out, iface, hr, object);
    if (!out || FAILED(hr) || !object) return result;
    uint32_t guest = c.mem.r32(out);
    if (!guest) return result;
    Proxy& p = s.by_guest(guest);
    if (maps->hooked.insert(p.host).second) {
        auto previous = p.host_release;
        p.host_release = [maps, host = p.host, previous]() { maps->drop(host); if (previous) previous(); };
    }
    return result;
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
    d3d11_iface(s, h, "ID3D11DepthStencilView", kD3D11ViewNames, kD3D11ViewArgc);
    d3d11_iface(s, h, "ID3D11Query", kD3D11QueryNames, kD3D11QueryArgc);
    d3d11_iface(s, h, "ID3D11VertexShader", kD3D11ChildNames, kD3D11ChildArgc);
    d3d11_iface(s, h, "ID3D11PixelShader", kD3D11ChildNames, kD3D11ChildArgc);
    d3d11_iface(s, h, "ID3D11InputLayout", kD3D11ChildNames, kD3D11ChildArgc);
    d3d11_iface(s, h, "ID3D11BlendState", kD3D11StateNames, kD3D11StateArgc);
    d3d11_iface(s, h, "ID3D11RasterizerState", kD3D11StateNames, kD3D11StateArgc);
    d3d11_iface(s, h, "ID3D11DepthStencilState", kD3D11StateNames, kD3D11StateArgc);
    d3d11_iface(s, h, "ID3D11SamplerState", kD3D11StateNames, kD3D11StateArgc);
    const auto maps = std::make_shared<D3d11MapState>();

    // ---- IDXGISwapChain
    s.method("IDXGISwapChain", 8, 3, [&h](Proxy& p, Cpu& c) -> uint32_t {
        if (h.route_trace) h.route_trace("D3D11 SwapChain Present");
        return uint32_t(d3d11_as<IDXGISwapChain>(p.host)->Present(c.arg(1), c.arg(2)));
    });
    s.method("IDXGISwapChain", 9, 4, [&s, maps](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(2) || !c.arg(3)) return uint32_t(E_POINTER);
        GUID iid = d3d9_read_struct<GUID>(c, c.arg(2));
        void* raw = nullptr;
        HRESULT hr = d3d11_as<IDXGISwapChain>(p.host)->GetBuffer(c.arg(1), iid, &raw);
        IUnknown* object = static_cast<IUnknown*>(raw);
        if (SUCCEEDED(hr) && !IsEqualGUID(iid, __uuidof(ID3D11Texture2D))) { object->Release(); object = nullptr; hr = E_NOINTERFACE; }
        return d3d11_out_resource(maps, s, c, c.arg(3), "ID3D11Texture2D", hr, object);
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
    // IDXGIOutput has no guest proxy, so a target output cannot cross: only the default (null) one.
    s.method("IDXGISwapChain", 10, 3, [&h](Proxy& p, Cpu& c) -> uint32_t {
        if (c.arg(2)) return d3d11_unbridged(h, p, "SetFullscreenState target output");
        return uint32_t(d3d11_as<IDXGISwapChain>(p.host)->SetFullscreenState(c.arg(1), nullptr));
    });
    // Windowed answers S_OK with a null target. Fullscreen has a real output that cannot cross, so a
    // caller that asked for it gets the gap (with the BOOL still written); a BOOL-only query is fine.
    s.method("IDXGISwapChain", 11, 3, [&h](Proxy& p, Cpu& c) -> uint32_t {
        BOOL fullscreen = FALSE; IDXGIOutput* target = nullptr;
        HRESULT hr = d3d11_as<IDXGISwapChain>(p.host)->GetFullscreenState(&fullscreen, c.arg(2) ? &target : nullptr);
        if (target) target->Release();
        if (FAILED(hr)) return uint32_t(hr);
        if (c.arg(1)) c.mem.w32(c.arg(1), uint32_t(fullscreen));
        if (c.arg(2)) c.mem.w32(c.arg(2), 0);
        if (c.arg(2) && fullscreen) return d3d11_unbridged(h, p, "GetFullscreenState target output");
        return uint32_t(hr);
    });
    s.method("IDXGISwapChain", 14, 2, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        DXGI_MODE_DESC mode = d3d9_read_struct<DXGI_MODE_DESC>(c, c.arg(1));
        return uint32_t(d3d11_as<IDXGISwapChain>(p.host)->ResizeTarget(&mode));
    });

    // ---- ID3D11Device creation (the 2D game set) and feature queries
    // A null output pointer is D3D11's validation-only call: the host checks the arguments and answers
    // S_FALSE when they are valid, so each creation passes its null output straight through.
    s.method("ID3D11Device", 3, 4, [&s, maps](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_BUFFER_DESC desc = d3d9_read_struct<D3D11_BUFFER_DESC>(c, c.arg(1));
        if (desc.ByteWidth > D3D9_GUEST_SIZE) return uint32_t(E_INVALIDARG);
        std::vector<uint8_t> init; D3D11_SUBRESOURCE_DATA sub{};
        if (c.arg(2)) { init = c.mem.read_bytes(c.mem.r32(c.arg(2)), desc.ByteWidth); sub.pSysMem = init.data(); }
        ID3D11Buffer* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateBuffer(&desc, c.arg(2) ? &sub : nullptr, c.arg(3) ? &x : nullptr);
        return d3d11_out_resource(maps, s, c, c.arg(3), "ID3D11Buffer", hr, x);
    });
    s.method("ID3D11Device", 5, 4, [&s, &h, maps](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_TEXTURE2D_DESC desc = d3d9_read_struct<D3D11_TEXTURE2D_DESC>(c, c.arg(1));
        std::vector<std::vector<uint8_t>> init; std::vector<D3D11_SUBRESOURCE_DATA> subs;
        if (c.arg(2)) {
            HRESULT bad = d3d11_read_texture_init(c, c.arg(2), desc, init, subs);
            if (bad == HRESULT(D3D11_GAP_HRESULT)) return d3d11_unbridged(h, p, "CreateTexture2D initial data format");
            if (FAILED(bad)) return uint32_t(bad);
        }
        ID3D11Texture2D* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateTexture2D(&desc, c.arg(2) ? subs.data() : nullptr, c.arg(3) ? &x : nullptr);
        return d3d11_out_resource(maps, s, c, c.arg(3), "ID3D11Texture2D", hr, x);
    });
    s.method("ID3D11Device", 7, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; D3D11_SHADER_RESOURCE_VIEW_DESC* dp = nullptr;
        if (c.arg(2)) { desc = d3d9_read_struct<D3D11_SHADER_RESOURCE_VIEW_DESC>(c, c.arg(2)); dp = &desc; }
        ID3D11ShaderResourceView* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateShaderResourceView(d3d11_obj<ID3D11Resource>(s, c.arg(1)), dp, c.arg(3) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(3), "ID3D11ShaderResourceView", hr, x);
    });
    s.method("ID3D11Device", 9, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        D3D11_RENDER_TARGET_VIEW_DESC desc{}; D3D11_RENDER_TARGET_VIEW_DESC* dp = nullptr;
        if (c.arg(2)) { desc = d3d9_read_struct<D3D11_RENDER_TARGET_VIEW_DESC>(c, c.arg(2)); dp = &desc; }
        ID3D11RenderTargetView* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateRenderTargetView(d3d11_obj<ID3D11Resource>(s, c.arg(1)), dp, c.arg(3) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(3), "ID3D11RenderTargetView", hr, x);
    });
    s.method("ID3D11Device", 10, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        D3D11_DEPTH_STENCIL_VIEW_DESC desc{}; D3D11_DEPTH_STENCIL_VIEW_DESC* dp = nullptr;
        if (c.arg(2)) { desc = d3d9_read_struct<D3D11_DEPTH_STENCIL_VIEW_DESC>(c, c.arg(2)); dp = &desc; }
        ID3D11DepthStencilView* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateDepthStencilView(d3d11_obj<ID3D11Resource>(s, c.arg(1)), dp, c.arg(3) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(3), "ID3D11DepthStencilView", hr, x);
    });
    s.method("ID3D11Device", 11, 6, [&s](Proxy& p, Cpu& c) -> uint32_t {
        // D3D11_INPUT_ELEMENT_DESC is 28 bytes in PE32 (SemanticName is a guest pointer).
        if (!c.arg(1) || !c.arg(3) || !c.arg(2) || c.arg(2) > 32 || !c.arg(4)) return uint32_t(E_INVALIDARG);
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
        HRESULT hr = d3d11_device(p)->CreateInputLayout(elements.data(), n, code.data(), c.arg(4), c.arg(5) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(5), "ID3D11InputLayout", hr, x);
    });
    s.method("ID3D11Device", 12, 5, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        if (c.arg(3)) return d3d11_unbridged(h, p, "CreateVertexShader class linkage");
        std::vector<uint8_t> code = c.mem.read_bytes(c.arg(1), c.arg(2));
        ID3D11VertexShader* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreateVertexShader(code.data(), c.arg(2), nullptr, c.arg(4) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(4), "ID3D11VertexShader", hr, x);
    });
    s.method("ID3D11Device", 15, 5, [&s, &h](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(2)) return uint32_t(E_POINTER);
        if (c.arg(3)) return d3d11_unbridged(h, p, "CreatePixelShader class linkage");
        std::vector<uint8_t> code = c.mem.read_bytes(c.arg(1), c.arg(2));
        ID3D11PixelShader* x = nullptr;
        HRESULT hr = d3d11_device(p)->CreatePixelShader(code.data(), c.arg(2), nullptr, c.arg(4) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(4), "ID3D11PixelShader", hr, x);
    });
    s.method("ID3D11Device", 20, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_BLEND_DESC d = d3d9_read_struct<D3D11_BLEND_DESC>(c, c.arg(1));
        ID3D11BlendState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateBlendState(&d, c.arg(2) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(2), "ID3D11BlendState", hr, x);
    });
    s.method("ID3D11Device", 21, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_DEPTH_STENCIL_DESC d = d3d9_read_struct<D3D11_DEPTH_STENCIL_DESC>(c, c.arg(1));
        ID3D11DepthStencilState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateDepthStencilState(&d, c.arg(2) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(2), "ID3D11DepthStencilState", hr, x);
    });
    s.method("ID3D11Device", 22, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_RASTERIZER_DESC d = d3d9_read_struct<D3D11_RASTERIZER_DESC>(c, c.arg(1));
        ID3D11RasterizerState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateRasterizerState(&d, c.arg(2) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(2), "ID3D11RasterizerState", hr, x);
    });
    s.method("ID3D11Device", 23, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_SAMPLER_DESC d = d3d9_read_struct<D3D11_SAMPLER_DESC>(c, c.arg(1));
        ID3D11SamplerState* x = nullptr; HRESULT hr = d3d11_device(p)->CreateSamplerState(&d, c.arg(2) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(2), "ID3D11SamplerState", hr, x);
    });
    s.method("ID3D11Device", 24, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_QUERY_DESC desc = d3d9_read_struct<D3D11_QUERY_DESC>(c, c.arg(1));
        ID3D11Query* x = nullptr; HRESULT hr = d3d11_device(p)->CreateQuery(&desc, c.arg(2) ? &x : nullptr);
        return d3d11_out(s, c, c.arg(2), "ID3D11Query", hr, x);
    });
    s.method("ID3D11Device", 29, 3, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(2)) return uint32_t(E_POINTER);
        UINT support = 0;
        HRESULT hr = d3d11_device(p)->CheckFormatSupport(DXGI_FORMAT(c.arg(1)), &support);
        if (SUCCEEDED(hr)) c.mem.w32(c.arg(2), support);
        return uint32_t(hr);
    });
    s.method("ID3D11Device", 30, 4, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(3)) return uint32_t(E_POINTER);
        UINT levels = 0;
        HRESULT hr = d3d11_device(p)->CheckMultisampleQualityLevels(DXGI_FORMAT(c.arg(1)), c.arg(2), &levels);
        if (SUCCEEDED(hr)) c.mem.w32(c.arg(3), levels);
        return uint32_t(hr);
    });
    s.method("ID3D11Device", 37, 1, [](Proxy& p, Cpu&) -> uint32_t { return uint32_t(d3d11_device(p)->GetFeatureLevel()); });
    s.method("ID3D11Device", 38, 1, [](Proxy& p, Cpu&) -> uint32_t { return d3d11_device(p)->GetCreationFlags(); });
    s.method("ID3D11Device", 39, 1, [](Proxy& p, Cpu&) -> uint32_t { return uint32_t(d3d11_device(p)->GetDeviceRemovedReason()); });
    s.method("ID3D11Device", 40, 2, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        ID3D11DeviceContext* x = nullptr; d3d11_device(p)->GetImmediateContext(&x);
        return d3d11_out(s, c, c.arg(1), "ID3D11DeviceContext", S_OK, x);
    });

    // ---- ID3D11Buffer / ID3D11Texture2D: both descriptors are pointer-free DWORDs (24 and 44 bytes)
    for (const char* iface : {"ID3D11Buffer", "ID3D11Texture2D"})
        s.method(iface, 7, 2, [](Proxy& p, Cpu& c) -> uint32_t {
            if (!c.arg(1)) return uint32_t(E_POINTER);
            D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; d3d11_as<ID3D11Resource>(p.host)->GetType(&dim);
            c.mem.w32(c.arg(1), uint32_t(dim)); return 0;
        });
    s.method("ID3D11Buffer", 10, 2, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_BUFFER_DESC d{}; d3d11_as<ID3D11Buffer>(p.host)->GetDesc(&d);
        c.mem.write_bytes(c.arg(1), &d, sizeof d); return 0;
    });
    s.method("ID3D11Texture2D", 10, 2, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_TEXTURE2D_DESC d{}; d3d11_as<ID3D11Texture2D>(p.host)->GetDesc(&d);
        c.mem.write_bytes(c.arg(1), &d, sizeof d); return 0;
    });

    // ---- ID3D11Query: GetDataSize (a byte count, returned as is) and GetDesc (two DWORDs)
    s.method("ID3D11Query", 7, 1, [](Proxy& p, Cpu&) -> uint32_t { return d3d11_as<ID3D11Query>(p.host)->GetDataSize(); });
    s.method("ID3D11Query", 8, 2, [](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return uint32_t(E_POINTER);
        D3D11_QUERY_DESC d{}; d3d11_as<ID3D11Query>(p.host)->GetDesc(&d);
        c.mem.write_bytes(c.arg(1), &d, sizeof d); return 0;
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
    // Map direction follows the mode, and the host's memory is read only when the guest may read it:
    //   READ, READ_WRITE   host -> scratch now; READ_WRITE copies the whole scratch back on Unmap.
    //   WRITE_DISCARD      scratch zeroed (contents are undefined); the whole scratch is copied back.
    //   WRITE_NO_OVERWRITE scratch seeded from the shadow of the last write, never from the host; Unmap
    //                      copies back only the bytes the guest changed, so earlier data and bytes the GPU
    //                      may still read are untouched. With no shadow to seed from it is a named gap.
    //   WRITE              the same, and a staging resource the CPU can read is seeded from the host instead.
    // The scratch is one guest range per subresource, reused by every map. The guest's output struct is
    // probed before the host Map, and a failure after it unmaps again, so the host is never left mapped.
    s.method("ID3D11DeviceContext", 14, 6, [&s, &h, maps](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1) || !c.arg(5)) return uint32_t(E_POINTER);
        Proxy& rp = s.by_guest(c.arg(1));
        ID3D11Resource* resource = d3d11_as<ID3D11Resource>(rp.host);
        const uint32_t sub = c.arg(2), mode = c.arg(3);
        if (mode < D3D11_MAP_READ || mode > D3D11_MAP_WRITE_NO_OVERWRITE) return uint32_t(E_INVALIDARG);
        { const uint8_t zero[12] = {}; c.mem.write_bytes(c.arg(5), zero, sizeof zero); }
        D3d11MapState::Entry& e = maps->entries[{rp.host, sub}];
        if (e.active) return 0x887A0001u;   // DXGI_ERROR_INVALID_CALL: already mapped
        const bool diff = mode == D3D11_MAP_WRITE || mode == D3D11_MAP_WRITE_NO_OVERWRITE;
        bool seed_from_host = false;
        if (mode == D3D11_MAP_WRITE_NO_OVERWRITE && !e.shadow_valid) return d3d11_unbridged(h, p, "Map WRITE_NO_OVERWRITE without a prior write to seed from");
        if (mode == D3D11_MAP_WRITE && !e.shadow_valid) {
            if (!d3d11_cpu_readable(resource)) return d3d11_unbridged(h, p, "Map WRITE without a prior write to seed from");
            seed_from_host = true;
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        HRESULT hr = d3d11_context(p)->Map(resource, sub, D3D11_MAP(mode), c.arg(4), &m);
        if (FAILED(hr)) return uint32_t(hr);
        try {
            uint32_t size = d3d11_subresource_bytes(resource, sub, m.RowPitch);
            if (!size || !m.pData) { d3d11_context(p)->Unmap(resource, sub); return d3d11_unbridged(h, p, "Map resource type"); }
            if (diff && e.shadow_valid && e.shadow.size() != size) { d3d11_context(p)->Unmap(resource, sub); return d3d11_unbridged(h, p, "Map size changed since the last write"); }
            uint32_t at = maps->acquire(h, c, e, size);
            if (mode == D3D11_MAP_READ || mode == D3D11_MAP_READ_WRITE) c.mem.write_bytes(at, m.pData, size);
            else if (mode == D3D11_MAP_WRITE_DISCARD) { std::vector<uint8_t> zeros(size); c.mem.write_bytes(at, zeros.data(), size); }
            else {
                if (seed_from_host) { e.shadow.assign(static_cast<uint8_t*>(m.pData), static_cast<uint8_t*>(m.pData) + size); e.shadow_valid = true; }
                c.mem.write_bytes(at, e.shadow.data(), size);
            }
            c.mem.w32(c.arg(5), at); c.mem.w32(c.arg(5) + 4, m.RowPitch); c.mem.w32(c.arg(5) + 8, m.DepthPitch);
            e.active = true; e.host = m.pData; e.size = size; e.mode = mode;
        } catch (...) { e.active = false; d3d11_context(p)->Unmap(resource, sub); throw; }
        return uint32_t(hr);
    });
    // Unmap writes back through the mode's rule before the host Unmap; one with no active map is a no-op.
    s.method("ID3D11DeviceContext", 15, 3, [&s, maps](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(1)) return 0;
        Proxy& rp = s.by_guest(c.arg(1));
        auto it = maps->entries.find({rp.host, c.arg(2)});
        if (it == maps->entries.end() || !it->second.active) return 0;
        D3d11MapState::Entry& e = it->second;
        if (e.mode != D3D11_MAP_READ) {
            std::vector<uint8_t> bytes = c.mem.read_bytes(e.scratch, e.size);
            uint8_t* host = static_cast<uint8_t*>(e.host);
            if (e.mode == D3D11_MAP_WRITE || e.mode == D3D11_MAP_WRITE_NO_OVERWRITE) {
                for (size_t i = 0; i < bytes.size();) {
                    if (bytes[i] == e.shadow[i]) { ++i; continue; }
                    size_t j = i; while (j < bytes.size() && bytes[j] != e.shadow[j]) ++j;
                    memcpy(host + i, bytes.data() + i, j - i); i = j;
                }
            } else memcpy(host, bytes.data(), bytes.size());
            e.shadow = std::move(bytes); e.shadow_valid = true;
        }
        e.active = false;
        d3d11_as<ID3D11DeviceContext>(p.host)->Unmap(d3d11_as<ID3D11Resource>(rp.host), c.arg(2));
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
    s.method("ID3D11DeviceContext", 33, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11RenderTargetView*> targets;
        if (!d3d11_objects(s, c, c.arg(2), c.arg(1), targets, 8)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->OMSetRenderTargets(c.arg(1), targets.data(), d3d11_obj<ID3D11DepthStencilView>(s, c.arg(3))); return 0;
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
    s.method("ID3D11DeviceContext", 48, 7, [&s, &h, maps](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Resource* dst = d3d11_obj<ID3D11Resource>(s, c.arg(1));
        D3D11_BOX box{}; const D3D11_BOX* region = nullptr;
        if (c.arg(3)) { box = d3d9_read_struct<D3D11_BOX>(c, c.arg(3)); region = &box; }
        int64_t size = dst && c.arg(4) ? d3d11_update_bytes(dst, c.arg(2), region, c.arg(5)) : -1;
        if (size == -2) return d3d11_unbridged(h, p, "UpdateSubresource format");
        if (size < 0) return uint32_t(E_INVALIDARG);
        if (size == 0) return 0;
        std::vector<uint8_t> data = c.mem.read_bytes(c.arg(4), uint32_t(size));
        d3d11_context(p)->UpdateSubresource(dst, c.arg(2), region, data.data(), c.arg(5), c.arg(6));
        maps->invalidate(s.by_guest(c.arg(1)).host); return 0;
    });
    s.method("ID3D11DeviceContext", 50, 3, [&s](Proxy& p, Cpu& c) -> uint32_t {
        if (!c.arg(2)) return uint32_t(E_POINTER);
        float color[4];
        auto bytes = c.mem.read_bytes(c.arg(2), sizeof(color)); memcpy(color, bytes.data(), sizeof(color));
        d3d11_context(p)->ClearRenderTargetView(d3d11_obj<ID3D11RenderTargetView>(s, c.arg(1)), color);
        return 0;
    });
    // Depth is a float and Stencil a UINT8, each passed in a 4-byte stack slot.
    s.method("ID3D11DeviceContext", 53, 5, [&s](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11DepthStencilView* view = d3d11_obj<ID3D11DepthStencilView>(s, c.arg(1));
        if (!view) return uint32_t(E_POINTER);
        float depth; uint32_t bits = c.arg(3); memcpy(&depth, &bits, sizeof depth);
        d3d11_context(p)->ClearDepthStencilView(view, c.arg(2), depth, uint8_t(c.arg(4) & 0xFF));
        return 0;
    });

    // ---- ID3D11DeviceContext: instancing, vertex-stage binds, queries, copies, state
    s.method("ID3D11DeviceContext", 20, 6, [](Proxy& p, Cpu& c) -> uint32_t {
        d3d11_context(p)->DrawIndexedInstanced(c.arg(1), c.arg(2), c.arg(3), int32_t(c.arg(4)), c.arg(5)); return 0;
    });
    s.method("ID3D11DeviceContext", 21, 5, [](Proxy& p, Cpu& c) -> uint32_t {
        d3d11_context(p)->DrawInstanced(c.arg(1), c.arg(2), c.arg(3), c.arg(4)); return 0;
    });
    s.method("ID3D11DeviceContext", 25, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11ShaderResourceView*> views;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), views, 128)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->VSSetShaderResources(c.arg(1), c.arg(2), views.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 26, 4, [&s](Proxy& p, Cpu& c) -> uint32_t {
        std::vector<ID3D11SamplerState*> samplers;
        if (!d3d11_objects(s, c, c.arg(3), c.arg(2), samplers, 16)) return uint32_t(E_INVALIDARG);
        d3d11_context(p)->VSSetSamplers(c.arg(1), c.arg(2), samplers.data()); return 0;
    });
    s.method("ID3D11DeviceContext", 27, 2, [&s](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Asynchronous* async = d3d11_obj<ID3D11Asynchronous>(s, c.arg(1));
        if (!async) return uint32_t(E_POINTER);
        d3d11_context(p)->Begin(async); return 0;
    });
    s.method("ID3D11DeviceContext", 28, 2, [&s](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Asynchronous* async = d3d11_obj<ID3D11Asynchronous>(s, c.arg(1));
        if (!async) return uint32_t(E_POINTER);
        d3d11_context(p)->End(async); return 0;
    });
    // GetData: S_FALSE (not ready) crosses unchanged and writes nothing; query results are
    // small (the largest, pipeline statistics, is 88 bytes), so a big DataSize is invalid.
    s.method("ID3D11DeviceContext", 29, 5, [&s](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Asynchronous* async = d3d11_obj<ID3D11Asynchronous>(s, c.arg(1));
        if (!async) return uint32_t(E_POINTER);
        uint32_t size = c.arg(3);
        if (size > 4096) return uint32_t(E_INVALIDARG);
        std::vector<uint8_t> data(c.arg(2) ? size : 0);
        HRESULT hr = d3d11_context(p)->GetData(async, c.arg(2) ? data.data() : nullptr, size, c.arg(4));
        if (hr == S_OK && !data.empty()) c.mem.write_bytes(c.arg(2), data.data(), uint32_t(data.size()));
        return uint32_t(hr);
    });
    s.method("ID3D11DeviceContext", 46, 9, [&s, maps](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Resource* dst = d3d11_obj<ID3D11Resource>(s, c.arg(1));
        ID3D11Resource* src = d3d11_obj<ID3D11Resource>(s, c.arg(6));
        if (!dst || !src) return uint32_t(E_POINTER);
        D3D11_BOX box{}; const D3D11_BOX* region = nullptr;
        if (c.arg(8)) { box = d3d9_read_struct<D3D11_BOX>(c, c.arg(8)); region = &box; }
        d3d11_context(p)->CopySubresourceRegion(dst, c.arg(2), c.arg(3), c.arg(4), c.arg(5), src, c.arg(7), region);
        maps->invalidate(s.by_guest(c.arg(1)).host); return 0;
    });
    s.method("ID3D11DeviceContext", 47, 3, [&s, maps](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Resource* dst = d3d11_obj<ID3D11Resource>(s, c.arg(1));
        ID3D11Resource* src = d3d11_obj<ID3D11Resource>(s, c.arg(2));
        if (!dst || !src) return uint32_t(E_POINTER);
        d3d11_context(p)->CopyResource(dst, src);
        maps->invalidate(s.by_guest(c.arg(1)).host); return 0;
    });
    s.method("ID3D11DeviceContext", 54, 2, [&s](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11ShaderResourceView* view = d3d11_obj<ID3D11ShaderResourceView>(s, c.arg(1));
        if (!view) return uint32_t(E_POINTER);
        d3d11_context(p)->GenerateMips(view); return 0;
    });
    s.method("ID3D11DeviceContext", 57, 6, [&s, maps](Proxy& p, Cpu& c) -> uint32_t {
        ID3D11Resource* dst = d3d11_obj<ID3D11Resource>(s, c.arg(1));
        ID3D11Resource* src = d3d11_obj<ID3D11Resource>(s, c.arg(3));
        if (!dst || !src) return uint32_t(E_POINTER);
        d3d11_context(p)->ResolveSubresource(dst, c.arg(2), src, c.arg(4), DXGI_FORMAT(c.arg(5)));
        maps->invalidate(s.by_guest(c.arg(1)).host); return 0;
    });
    s.method("ID3D11DeviceContext", 110, 1, [](Proxy& p, Cpu&) -> uint32_t { d3d11_context(p)->ClearState(); return 0; });
    s.method("ID3D11DeviceContext", 111, 1, [](Proxy& p, Cpu&) -> uint32_t { d3d11_context(p)->Flush(); return 0; });
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
