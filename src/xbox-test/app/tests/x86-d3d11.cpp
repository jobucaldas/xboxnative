// SPDX-License-Identifier: GPL-3.0-or-later
// x86 D3D11 bridge audit: each registered interface has the SDK's slot count,
// every slot after IUnknown is registered (bridged or a named gap) with the
// argument count the table declares, the named slots read as their SDK methods,
// and the PE32 guest structs the marshallers copy have the sizes they assume.
// The SDK slot order itself is proven at build time by x86-d3d11-slots.cpp.
#include <cstdio>
#include <cstring>
#include "../host/x86/x86_play.h"
// The bridged argument counts equal the SDK's, proven at build time by
// x86-d3d11-slots.cpp (ARGC on each vtbl member, 32-bit slots, plus this).
static_assert(x86::kD3D11SwapChainArgc[8]==3,"kD3D11SwapChainArgc[8] argc");
static_assert(x86::kD3D11SwapChainArgc[9]==4,"kD3D11SwapChainArgc[9] argc");
static_assert(x86::kD3D11SwapChainArgc[12]==2,"kD3D11SwapChainArgc[12] argc");
static_assert(x86::kD3D11SwapChainArgc[13]==6,"kD3D11SwapChainArgc[13] argc");
static_assert(x86::kD3D11DeviceArgc[3]==4,"kD3D11DeviceArgc[3] argc");
static_assert(x86::kD3D11DeviceArgc[5]==4,"kD3D11DeviceArgc[5] argc");
static_assert(x86::kD3D11DeviceArgc[7]==4,"kD3D11DeviceArgc[7] argc");
static_assert(x86::kD3D11DeviceArgc[9]==4,"kD3D11DeviceArgc[9] argc");
static_assert(x86::kD3D11DeviceArgc[11]==6,"kD3D11DeviceArgc[11] argc");
static_assert(x86::kD3D11DeviceArgc[12]==5,"kD3D11DeviceArgc[12] argc");
static_assert(x86::kD3D11DeviceArgc[15]==5,"kD3D11DeviceArgc[15] argc");
static_assert(x86::kD3D11DeviceArgc[20]==3,"kD3D11DeviceArgc[20] argc");
static_assert(x86::kD3D11DeviceArgc[21]==3,"kD3D11DeviceArgc[21] argc");
static_assert(x86::kD3D11DeviceArgc[22]==3,"kD3D11DeviceArgc[22] argc");
static_assert(x86::kD3D11DeviceArgc[23]==3,"kD3D11DeviceArgc[23] argc");
static_assert(x86::kD3D11DeviceArgc[29]==3,"kD3D11DeviceArgc[29] argc");
static_assert(x86::kD3D11DeviceArgc[37]==1,"kD3D11DeviceArgc[37] argc");
static_assert(x86::kD3D11DeviceArgc[40]==2,"kD3D11DeviceArgc[40] argc");
static_assert(x86::kD3D11ContextArgc[7]==4,"kD3D11ContextArgc[7] argc");
static_assert(x86::kD3D11ContextArgc[8]==4,"kD3D11ContextArgc[8] argc");
static_assert(x86::kD3D11ContextArgc[9]==4,"kD3D11ContextArgc[9] argc");
static_assert(x86::kD3D11ContextArgc[10]==4,"kD3D11ContextArgc[10] argc");
static_assert(x86::kD3D11ContextArgc[11]==4,"kD3D11ContextArgc[11] argc");
static_assert(x86::kD3D11ContextArgc[12]==4,"kD3D11ContextArgc[12] argc");
static_assert(x86::kD3D11ContextArgc[13]==3,"kD3D11ContextArgc[13] argc");
static_assert(x86::kD3D11ContextArgc[14]==6,"kD3D11ContextArgc[14] argc");
static_assert(x86::kD3D11ContextArgc[15]==3,"kD3D11ContextArgc[15] argc");
static_assert(x86::kD3D11ContextArgc[16]==4,"kD3D11ContextArgc[16] argc");
static_assert(x86::kD3D11ContextArgc[17]==2,"kD3D11ContextArgc[17] argc");
static_assert(x86::kD3D11ContextArgc[18]==6,"kD3D11ContextArgc[18] argc");
static_assert(x86::kD3D11ContextArgc[19]==4,"kD3D11ContextArgc[19] argc");
static_assert(x86::kD3D11ContextArgc[24]==2,"kD3D11ContextArgc[24] argc");
static_assert(x86::kD3D11ContextArgc[33]==4,"kD3D11ContextArgc[33] argc");
static_assert(x86::kD3D11ContextArgc[35]==4,"kD3D11ContextArgc[35] argc");
static_assert(x86::kD3D11ContextArgc[36]==3,"kD3D11ContextArgc[36] argc");
static_assert(x86::kD3D11ContextArgc[43]==2,"kD3D11ContextArgc[43] argc");
static_assert(x86::kD3D11ContextArgc[44]==3,"kD3D11ContextArgc[44] argc");
static_assert(x86::kD3D11ContextArgc[45]==3,"kD3D11ContextArgc[45] argc");
static_assert(x86::kD3D11ContextArgc[48]==7,"kD3D11ContextArgc[48] argc");
static_assert(x86::kD3D11ContextArgc[50]==3,"kD3D11ContextArgc[50] argc");
static_assert(x86::kD3D11ContextArgc[110]==1,"kD3D11ContextArgc[110] argc");
static_assert(x86::kD3D11ContextArgc[114]==3,"kD3D11ContextArgc[114] argc");

template<size_t N> static bool audit_iface(const x86::ComSpace& s, const char* iface, const char* const (&names)[N], const unsigned (&argc)[N]) {
    const auto& record = s.by_name(iface);
    if (record.slots != N || record.methods.size() != N - 3) {
        std::fprintf(stderr, "X86-D3D11 FAIL: %s slots=%u methods=%zu expected=%zu\n", iface, record.slots, record.methods.size(), N);
        return false;
    }
    for (unsigned slot = 3; slot < N; ++slot) {
        auto method = record.methods.find(slot);
        if (method == record.methods.end() || method->second.argc != argc[slot]) {
            std::fprintf(stderr, "X86-D3D11 FAIL: %s::%s argc table=%u registered=%u\n", iface, names[slot], argc[slot], method == record.methods.end() ? 0u : method->second.argc);
            return false;
        }
    }
    return true;
}
template<size_t N> static bool named(const char* iface, const char* const (&names)[N], unsigned slot, const char* expected) {
    if (slot >= N || std::strcmp(names[slot], expected) != 0) {
        std::fprintf(stderr, "X86-D3D11 FAIL: %s slot %u is %s, expected %s\n", iface, slot, slot < N ? names[slot] : "(out of range)", expected);
        return false;
    }
    return true;
}

int main() {
    // PE32 guest layouts: every field the marshallers copy is a DWORD, so these
    // host sizes equal the 32-bit ones. DXGI_SWAP_CHAIN_DESC is read field by
    // field (15 DWORDs = 60 bytes), built from these components.
    static_assert(sizeof(DXGI_MODE_DESC) == 28, "DXGI_MODE_DESC is seven DWORDs");
    static_assert(sizeof(DXGI_SAMPLE_DESC) == 8, "DXGI_SAMPLE_DESC is two DWORDs");
    static_assert(sizeof(D3D11_BUFFER_DESC) == 24, "D3D11_BUFFER_DESC is six DWORDs");
    static_assert(sizeof(D3D11_TEXTURE2D_DESC) == 44, "D3D11_TEXTURE2D_DESC is eleven DWORDs");
    static_assert(sizeof(D3D11_VIEWPORT) == 24, "D3D11_VIEWPORT is six floats");
    static_assert(sizeof(D3D11_SAMPLER_DESC) == 52, "D3D11_SAMPLER_DESC is pointer-free");
    static_assert(sizeof(RECT) == 16, "RECT is four LONGs");

    x86::Host host;
    x86::register_d3d11_com(host.com, host);
    x86::Registry registry;
    x86::register_d3d11(registry, host);

    bool ok = audit_iface(host.com, "IDXGISwapChain", x86::kD3D11SwapChainNames, x86::kD3D11SwapChainArgc)
        && audit_iface(host.com, "ID3D11Device", x86::kD3D11DeviceNames, x86::kD3D11DeviceArgc)
        && audit_iface(host.com, "ID3D11DeviceContext", x86::kD3D11ContextNames, x86::kD3D11ContextArgc)
        && audit_iface(host.com, "ID3D11Texture2D", x86::kD3D11ResourceNames, x86::kD3D11ResourceArgc)
        && audit_iface(host.com, "ID3D11Buffer", x86::kD3D11ResourceNames, x86::kD3D11ResourceArgc)
        && audit_iface(host.com, "ID3D11RenderTargetView", x86::kD3D11ViewNames, x86::kD3D11ViewArgc)
        && audit_iface(host.com, "ID3D11ShaderResourceView", x86::kD3D11ViewNames, x86::kD3D11ViewArgc)
        && audit_iface(host.com, "ID3D11VertexShader", x86::kD3D11ChildNames, x86::kD3D11ChildArgc)
        && audit_iface(host.com, "ID3D11PixelShader", x86::kD3D11ChildNames, x86::kD3D11ChildArgc)
        && audit_iface(host.com, "ID3D11InputLayout", x86::kD3D11ChildNames, x86::kD3D11ChildArgc)
        && audit_iface(host.com, "ID3D11BlendState", x86::kD3D11StateNames, x86::kD3D11StateArgc)
        && audit_iface(host.com, "ID3D11RasterizerState", x86::kD3D11StateNames, x86::kD3D11StateArgc)
        && audit_iface(host.com, "ID3D11DepthStencilState", x86::kD3D11StateNames, x86::kD3D11StateArgc)
        && audit_iface(host.com, "ID3D11SamplerState", x86::kD3D11StateNames, x86::kD3D11StateArgc);
    ok = ok
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 8, "Present")
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 9, "GetBuffer")
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 12, "GetDesc")
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 13, "ResizeBuffers")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 3, "CreateBuffer")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 12, "CreateVertexShader")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 29, "CheckFormatSupport")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 37, "GetFeatureLevel")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 14, "Map")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 15, "Unmap")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 33, "OMSetRenderTargets")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 48, "UpdateSubresource")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 50, "ClearRenderTargetView")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 114, "FinishCommandList");
    if (!ok) return 1;
    std::puts("X86-D3D11 PASS: 14 interfaces match the SDK slot tables, bridged arities match, PE32 struct sizes hold");
    return 0;
}
