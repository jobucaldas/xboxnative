// SPDX-License-Identifier: GPL-3.0-or-later
// x86 D3D11 bridge audit: each registered interface has the SDK's slot count,
// every slot after IUnknown is registered (bridged or a named gap) with the
// argument count the table declares, the named slots read as their SDK methods,
// and the PE32 guest structs the marshallers copy have the sizes they assume.
// The SDK slot order itself is proven at build time by x86-d3d11-slots.cpp.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
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
// Phase 2
static_assert(x86::kD3D11SwapChainArgc[10]==3,"kD3D11SwapChainArgc[10] argc");
static_assert(x86::kD3D11SwapChainArgc[11]==3,"kD3D11SwapChainArgc[11] argc");
static_assert(x86::kD3D11SwapChainArgc[14]==2,"kD3D11SwapChainArgc[14] argc");
static_assert(x86::kD3D11DeviceArgc[10]==4,"kD3D11DeviceArgc[10] argc");
static_assert(x86::kD3D11DeviceArgc[24]==3,"kD3D11DeviceArgc[24] argc");
static_assert(x86::kD3D11DeviceArgc[30]==4,"kD3D11DeviceArgc[30] argc");
static_assert(x86::kD3D11DeviceArgc[38]==1,"kD3D11DeviceArgc[38] argc");
static_assert(x86::kD3D11DeviceArgc[39]==1,"kD3D11DeviceArgc[39] argc");
static_assert(x86::kD3D11ContextArgc[20]==6,"kD3D11ContextArgc[20] argc");
static_assert(x86::kD3D11ContextArgc[21]==5,"kD3D11ContextArgc[21] argc");
static_assert(x86::kD3D11ContextArgc[25]==4,"kD3D11ContextArgc[25] argc");
static_assert(x86::kD3D11ContextArgc[26]==4,"kD3D11ContextArgc[26] argc");
static_assert(x86::kD3D11ContextArgc[27]==2,"kD3D11ContextArgc[27] argc");
static_assert(x86::kD3D11ContextArgc[28]==2,"kD3D11ContextArgc[28] argc");
static_assert(x86::kD3D11ContextArgc[29]==5,"kD3D11ContextArgc[29] argc");
static_assert(x86::kD3D11ContextArgc[46]==9,"kD3D11ContextArgc[46] argc");
static_assert(x86::kD3D11ContextArgc[47]==3,"kD3D11ContextArgc[47] argc");
static_assert(x86::kD3D11ContextArgc[53]==5,"kD3D11ContextArgc[53] argc");
static_assert(x86::kD3D11ContextArgc[54]==2,"kD3D11ContextArgc[54] argc");
static_assert(x86::kD3D11ContextArgc[57]==6,"kD3D11ContextArgc[57] argc");
static_assert(x86::kD3D11ContextArgc[111]==1,"kD3D11ContextArgc[111] argc");
static_assert(x86::kD3D11ResourceArgc[7]==2,"kD3D11ResourceArgc[7] argc");
static_assert(x86::kD3D11ResourceArgc[10]==2,"kD3D11ResourceArgc[10] argc");
static_assert(x86::kD3D11QueryArgc[7]==1 && x86::kD3D11QueryArgc[8]==2,"kD3D11QueryArgc argc");
static_assert(sizeof(x86::kD3D11QueryNames)/sizeof(x86::kD3D11QueryNames[0])==9,"ID3D11Query has nine slots");

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

// ---- Route tests: the guest-facing marshallers run against fake host COM objects.
// A fake is a vtable of plain functions in SDK slot order, filled by name from the bridge's
// own tables (which x86-d3d11-slots.cpp proves against the SDK). The bridge calls the real C++
// interfaces, so a call lands in the function for the slot the compiler derives from the SDK
// header; a wrong slot lands in f_trap and fails the log check.
static int failures = 0;
#define EXPECT(c, ...) do { if (!(c)) { ++failures; std::printf("X86-D3D11 FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
#define HR(got, want) EXPECT(uint32_t(got) == uint32_t(want), "hr 0x%08x, want 0x%08x", uint32_t(got), uint32_t(want))
static std::vector<std::string> g_log, g_gaps;
static HRESULT g_hr = S_OK;        // forced result for the next fake create/map/query call
static HRESULT take_hr() { HRESULT h = g_hr; g_hr = S_OK; return h; }
static std::string F(const char* fmt, ...) { char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); return b; }
#define LOGF(...) g_log.push_back(F(__VA_ARGS__))
static void expect_log(int line, std::initializer_list<std::string> want) {
    bool ok = g_log.size() == want.size(); size_t i = 0;
    if (ok) for (const auto& w : want) ok = ok && g_log[i++] == w;
    if (!ok) {
        ++failures; std::printf("X86-D3D11 FAIL line %d: host call log differs\n", line);
        for (const auto& l : g_log) std::printf("   got  %s\n", l.c_str());
        for (const auto& w : want) std::printf("   want %s\n", w.c_str());
    }
    g_log.clear();
}
#define LOG(...) expect_log(__LINE__, {__VA_ARGS__})
static bool g_expect_fault = false;   // inside faults(), a guest fault is the expected result, not a failure
template<class Fn> static bool faults(Fn fn) {
    g_expect_fault = true;
    try { fn(); } catch (const x86::Fault&) { g_expect_fault = false; return true; }
    g_expect_fault = false; return false;
}
static uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

struct Fake {
    void** vtbl = nullptr; int id = 0; int refs = 1;
    D3D11_BUFFER_DESC buf{}; D3D11_TEXTURE2D_DESC tex{};
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; D3D11_QUERY_DESC qdesc{};
    std::vector<uint8_t> mem; uint32_t row_pitch = 0;   // what Map hands out
};
static std::deque<Fake> g_pool;
static void* vt_dev[std::size(x86::kD3D11DeviceNames)];
static void* vt_ctx[std::size(x86::kD3D11ContextNames)];
static void* vt_swap[std::size(x86::kD3D11SwapChainNames)];
static void* vt_buf[std::size(x86::kD3D11ResourceNames)];
static void* vt_tex[std::size(x86::kD3D11ResourceNames)];
static void* vt_view[std::size(x86::kD3D11ViewNames)];   // views: AddRef/Release only
static void* vt_query[std::size(x86::kD3D11QueryNames)];
static Fake* make(void** vt) { g_pool.emplace_back(); Fake* f = &g_pool.back(); f->vtbl = vt; f->id = int(g_pool.size()); return f; }
static int idof(const void* p) { return p ? static_cast<const Fake*>(p)->id : 0; }
static std::string boxs(const D3D11_BOX* b) { return b ? F("%u,%u,%u,%u,%u,%u", b->left, b->top, b->front, b->right, b->bottom, b->back) : "null"; }

static ULONG f_AddRef(Fake* f) { return ULONG(++f->refs); }
static ULONG f_Release(Fake* f) { return ULONG(--f->refs); }
static void f_trap() { g_log.push_back("TRAP"); }
// Hands a created object to the bridge the way COM does: one reference, owned by the caller. A null
// output is D3D11's validation-only call: nothing is handed out and a valid call answers S_FALSE.
template<class T> static HRESULT give(HRESULT hr, Fake* f, T** out) {
    if (!out) return FAILED(hr) ? hr : S_FALSE;
    *out = SUCCEEDED(hr) ? reinterpret_cast<T*>(f) : nullptr; return hr;
}

// ID3D11Device
static HRESULT d_CreateBuffer(Fake*, const D3D11_BUFFER_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Buffer** out) {
    LOGF("CreateBuffer bytes=%u usage=%u init=%s", d->ByteWidth, unsigned(d->Usage), init ? "ptr" : "null");
    Fake* b = make(vt_buf); b->dim = D3D11_RESOURCE_DIMENSION_BUFFER; b->buf = *d; b->mem.assign(d->ByteWidth, 0); b->row_pitch = d->ByteWidth;
    return give(take_hr(), b, out);
}
static HRESULT d_CreateTexture2D(Fake*, const D3D11_TEXTURE2D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) {
    std::string s = init ? "" : " init=null";
    if (init) {
        uint32_t mips = d->MipLevels; if (!mips) for (uint32_t m = std::max(d->Width, d->Height); m; m >>= 1) ++mips;
        s = F(" init=%u", mips * d->ArraySize);
        for (uint32_t i = 0; i < mips * d->ArraySize; ++i) s += F(" [%u:%u:%u]", init[i].SysMemPitch, init[i].SysMemSlicePitch, unsigned(static_cast<const uint8_t*>(init[i].pSysMem)[0]));
    }
    LOGF("CreateTexture2D %ux%u mips=%u arr=%u fmt=%u%s", d->Width, d->Height, d->MipLevels, d->ArraySize, unsigned(d->Format), s.c_str());
    HRESULT hr = take_hr(); Fake* t = make(vt_tex); t->dim = D3D11_RESOURCE_DIMENSION_TEXTURE2D; t->tex = *d;
    return give(hr, t, out);
}
static HRESULT d_CreateDepthStencilView(Fake*, ID3D11Resource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* d, ID3D11DepthStencilView** out) {
    LOGF("CreateDepthStencilView res=%d %s", idof(r), d ? F("fmt=%u dim=%u", unsigned(d->Format), unsigned(d->ViewDimension)).c_str() : "desc=null");
    return give(take_hr(), make(vt_view), out);
}
static HRESULT d_CreateQuery(Fake*, const D3D11_QUERY_DESC* d, ID3D11Query** out) {
    LOGF("CreateQuery q=%u misc=%u", unsigned(d->Query), d->MiscFlags);
    Fake* q = make(vt_query); q->qdesc = *d; return give(take_hr(), q, out);
}
static HRESULT d_CheckMultisampleQualityLevels(Fake*, DXGI_FORMAT f, UINT count, UINT* levels) {
    LOGF("CheckMultisampleQualityLevels fmt=%u count=%u", unsigned(f), count);
    HRESULT hr = take_hr(); if (SUCCEEDED(hr)) *levels = 3; return hr;
}
static UINT d_GetCreationFlags(Fake*) { return 0x22; }
static HRESULT d_GetDeviceRemovedReason(Fake*) { return HRESULT(0x887A0005u); }   // DXGI_ERROR_DEVICE_REMOVED

// ID3D11Query
static UINT q_GetDataSize(Fake* f) { return f->qdesc.Query == D3D11_QUERY_TIMESTAMP_DISJOINT ? 16 : 8; }
static void q_GetDesc(Fake* f, D3D11_QUERY_DESC* d) { *d = f->qdesc; }

// ID3D11Buffer / ID3D11Texture2D
static void r_GetType(Fake* f, D3D11_RESOURCE_DIMENSION* d) { *d = f->dim; }
static void r_GetDescBuffer(Fake* f, D3D11_BUFFER_DESC* d) { *d = f->buf; }
static void r_GetDescTexture(Fake* f, D3D11_TEXTURE2D_DESC* d) { *d = f->tex; }

// IDXGISwapChain
static HRESULT s_SetFullscreenState(Fake*, BOOL fs, IDXGIOutput* target) { LOGF("SetFullscreenState fs=%d target=%d", int(fs), idof(target)); return take_hr(); }
static BOOL g_fullscreen = FALSE;   // what the fake swap chain reports; fullscreen hands out an output
static Fake* g_output = nullptr;    // the last output handed out
static HRESULT s_GetFullscreenState(Fake*, BOOL* fs, IDXGIOutput** target) {
    LOGF("GetFullscreenState fs=%s target=%s", fs ? "ptr" : "null", target ? "ptr" : "null");
    if (fs) *fs = g_fullscreen;
    if (target && g_fullscreen) { g_output = make(vt_view); *target = reinterpret_cast<IDXGIOutput*>(g_output); }
    return S_OK;
}
static HRESULT s_ResizeTarget(Fake*, const DXGI_MODE_DESC* m) {
    LOGF("ResizeTarget %ux%u fmt=%u refresh=%u/%u scanline=%u scaling=%u", m->Width, m->Height, unsigned(m->Format), m->RefreshRate.Numerator, m->RefreshRate.Denominator, unsigned(m->ScanlineOrdering), unsigned(m->Scaling));
    return take_hr();
}

// ID3D11DeviceContext
static HRESULT c_Map(Fake*, ID3D11Resource* r, UINT sub, D3D11_MAP mode, UINT flags, D3D11_MAPPED_SUBRESOURCE* m) {
    LOGF("Map res=%d sub=%u mode=%u flags=%u", idof(r), sub, unsigned(mode), flags);
    HRESULT hr = take_hr(); if (FAILED(hr)) return hr;
    Fake* f = reinterpret_cast<Fake*>(r); m->pData = f->mem.data(); m->RowPitch = f->row_pitch; m->DepthPitch = 0; return hr;
}
static void c_Unmap(Fake*, ID3D11Resource* r, UINT sub) { LOGF("Unmap res=%d sub=%u", idof(r), sub); }
static void c_OMSetRenderTargets(Fake*, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv) {
    LOGF("OMSetRenderTargets n=%u rtv0=%d dsv=%d", n, n && rtv ? idof(rtv[0]) : 0, idof(dsv));
}
static void c_ClearDepthStencilView(Fake*, ID3D11DepthStencilView* v, UINT flags, FLOAT depth, UINT8 stencil) {
    LOGF("ClearDepthStencilView dsv=%d flags=%u depth=%.3f stencil=%u", idof(v), flags, double(depth), unsigned(stencil));
}
static void c_UpdateSubresource(Fake*, ID3D11Resource* dst, UINT sub, const D3D11_BOX* box, const void* data, UINT rp, UINT dp) {
    LOGF("UpdateSubresource dst=%d sub=%u box=%s rp=%u dp=%u first=%u", idof(dst), sub, boxs(box).c_str(), rp, dp, unsigned(static_cast<const uint8_t*>(data)[0]));
}
static void c_DrawIndexedInstanced(Fake*, UINT a, UINT b, UINT c, INT d, UINT e) { LOGF("DrawIndexedInstanced %u %u %u %d %u", a, b, c, int(d), e); }
static void c_DrawInstanced(Fake*, UINT a, UINT b, UINT c, UINT d) { LOGF("DrawInstanced %u %u %u %u", a, b, c, d); }
static void c_VSSetShaderResources(Fake*, UINT start, UINT n, ID3D11ShaderResourceView* const* v) {
    LOGF("VSSetShaderResources start=%u n=%u v0=%d v1=%d", start, n, n > 0 ? idof(v[0]) : 0, n > 1 ? idof(v[1]) : 0);
}
static void c_VSSetSamplers(Fake*, UINT start, UINT n, ID3D11SamplerState* const* v) {
    LOGF("VSSetSamplers start=%u n=%u v0=%d v1=%d", start, n, n > 0 ? idof(v[0]) : 0, n > 1 ? idof(v[1]) : 0);
}
static void c_Begin(Fake*, ID3D11Asynchronous* a) { LOGF("Begin async=%d", idof(a)); }
static void c_End(Fake*, ID3D11Asynchronous* a) { LOGF("End async=%d", idof(a)); }
static HRESULT c_GetData(Fake*, ID3D11Asynchronous* a, void* data, UINT size, UINT flags) {
    LOGF("GetData async=%d data=%s size=%u flags=%u", idof(a), data ? "ptr" : "null", size, flags);
    HRESULT hr = take_hr(); if (hr == S_OK && data) for (UINT i = 0; i < size; ++i) static_cast<uint8_t*>(data)[i] = uint8_t(0x11 + i);
    return hr;
}
static void c_CopySubresourceRegion(Fake*, ID3D11Resource* dst, UINT ds, UINT x, UINT y, UINT z, ID3D11Resource* src, UINT ss, const D3D11_BOX* box) {
    LOGF("CopySubresourceRegion dst=%d/%u at=%u,%u,%u src=%d/%u box=%s", idof(dst), ds, x, y, z, idof(src), ss, boxs(box).c_str());
}
static void c_CopyResource(Fake*, ID3D11Resource* dst, ID3D11Resource* src) { LOGF("CopyResource dst=%d src=%d", idof(dst), idof(src)); }
static void c_GenerateMips(Fake*, ID3D11ShaderResourceView* v) { LOGF("GenerateMips srv=%d", idof(v)); }
static void c_ResolveSubresource(Fake*, ID3D11Resource* dst, UINT ds, ID3D11Resource* src, UINT ss, DXGI_FORMAT f) {
    LOGF("ResolveSubresource dst=%d/%u src=%d/%u fmt=%u", idof(dst), ds, idof(src), ss, unsigned(f));
}
static void c_ClearState(Fake*) { LOGF("ClearState"); }
static void c_Flush(Fake*) { LOGF("Flush"); }

template<size_t N> static void fill(void* (&vt)[N]) {
    for (auto& s : vt) s = reinterpret_cast<void*>(&f_trap);
    vt[1] = reinterpret_cast<void*>(&f_AddRef); vt[2] = reinterpret_cast<void*>(&f_Release);
}
template<size_t N> static void put(void* (&vt)[N], const char* const (&names)[N], const char* name, void* fn) {
    for (size_t i = 0; i < N; ++i) if (!std::strcmp(names[i], name)) { vt[i] = fn; return; }
    ++failures; std::printf("X86-D3D11 FAIL: the fakes name a slot the tables lack: %s\n", name);
}
#define FN(f) reinterpret_cast<void*>(&f)
static void init_fakes() {
    fill(vt_dev); fill(vt_ctx); fill(vt_swap); fill(vt_buf); fill(vt_tex); fill(vt_view); fill(vt_query);
    put(vt_query, x86::kD3D11QueryNames, "GetDataSize", FN(q_GetDataSize)); put(vt_query, x86::kD3D11QueryNames, "GetDesc", FN(q_GetDesc));
    put(vt_dev, x86::kD3D11DeviceNames, "CreateBuffer", FN(d_CreateBuffer));
    const auto& dn = x86::kD3D11DeviceNames; const auto& cn = x86::kD3D11ContextNames;
    const auto& sn = x86::kD3D11SwapChainNames; const auto& rn = x86::kD3D11ResourceNames;
    put(vt_dev, dn, "CreateTexture2D", FN(d_CreateTexture2D)); put(vt_dev, dn, "CreateDepthStencilView", FN(d_CreateDepthStencilView));
    put(vt_dev, dn, "CreateQuery", FN(d_CreateQuery)); put(vt_dev, dn, "CheckMultisampleQualityLevels", FN(d_CheckMultisampleQualityLevels));
    put(vt_dev, dn, "GetCreationFlags", FN(d_GetCreationFlags)); put(vt_dev, dn, "GetDeviceRemovedReason", FN(d_GetDeviceRemovedReason));
    put(vt_buf, rn, "GetType", FN(r_GetType)); put(vt_buf, rn, "GetDesc", FN(r_GetDescBuffer));
    put(vt_tex, rn, "GetType", FN(r_GetType)); put(vt_tex, rn, "GetDesc", FN(r_GetDescTexture));
    put(vt_swap, sn, "SetFullscreenState", FN(s_SetFullscreenState)); put(vt_swap, sn, "GetFullscreenState", FN(s_GetFullscreenState));
    put(vt_swap, sn, "ResizeTarget", FN(s_ResizeTarget));
    put(vt_ctx, cn, "Map", FN(c_Map)); put(vt_ctx, cn, "Unmap", FN(c_Unmap)); put(vt_ctx, cn, "OMSetRenderTargets", FN(c_OMSetRenderTargets));
    put(vt_ctx, cn, "ClearDepthStencilView", FN(c_ClearDepthStencilView)); put(vt_ctx, cn, "UpdateSubresource", FN(c_UpdateSubresource));
    put(vt_ctx, cn, "DrawIndexedInstanced", FN(c_DrawIndexedInstanced)); put(vt_ctx, cn, "DrawInstanced", FN(c_DrawInstanced));
    put(vt_ctx, cn, "VSSetShaderResources", FN(c_VSSetShaderResources)); put(vt_ctx, cn, "VSSetSamplers", FN(c_VSSetSamplers));
    put(vt_ctx, cn, "Begin", FN(c_Begin)); put(vt_ctx, cn, "End", FN(c_End)); put(vt_ctx, cn, "GetData", FN(c_GetData));
    put(vt_ctx, cn, "CopySubresourceRegion", FN(c_CopySubresourceRegion)); put(vt_ctx, cn, "CopyResource", FN(c_CopyResource));
    put(vt_ctx, cn, "GenerateMips", FN(c_GenerateMips)); put(vt_ctx, cn, "ResolveSubresource", FN(c_ResolveSubresource));
    put(vt_ctx, cn, "ClearState", FN(c_ClearState)); put(vt_ctx, cn, "Flush", FN(c_Flush));
}

// A flat guest: stack, a data window, and a probe window that ends at an unmapped page, so a
// marshaller that reads one byte too many faults and one that reads one too few does not reach
// a byte that was placed on the unmapped page.
struct Env {
    x86::Memory mem; x86::ThunkTable thunks; x86::Host host; x86::Cpu cpu{mem, thunks};
    static constexpr uint32_t STACK = 0x30000, DATA = 0x40000, DATA_END = 0x140000, PROBE = 0x200000, PROBE_END = 0x220000;
    uint32_t top = DATA;
    Env() {
        thunks.base = 0x7FFC0000; x86::map_thunk_page(mem, thunks);
        mem.map(STACK, 0x1000, x86::PROT_R | x86::PROT_W); mem.map(DATA, DATA_END - DATA, x86::PROT_R | x86::PROT_W);
        mem.map(PROBE, PROBE_END - PROBE, x86::PROT_R | x86::PROT_W);
        host.tee = [](const std::string& s) { g_gaps.push_back(s); };
        x86::register_d3d11_com(host.com, host);
    }
    uint32_t put(const void* p, uint32_t n) { uint32_t a = (top + 15) & ~15u; mem.write_bytes(a, p, n); top = a + n; return a; }
    uint32_t alloc(uint32_t n) { std::vector<uint8_t> z(n); return put(z.data(), n); }
    uint32_t words(const std::vector<uint32_t>& w) { return put(w.data(), uint32_t(w.size() * 4)); }
    // `shift` bytes of `data` land on the unmapped page, so reading all of it must fault.
    uint32_t at_end(const std::vector<uint8_t>& data, uint32_t shift = 0) {
        uint32_t n = uint32_t(data.size()), at = PROBE_END - n + shift;
        mem.write_bytes(at, data.data(), n - shift); return at;
    }
    uint32_t proxy(const char* iface, Fake* f) { return host.com.create(cpu, iface, f, [f] { ++f->refs; }, [f] { --f->refs; }); }
    uint32_t call(const char* iface, unsigned slot, std::initializer_list<uint32_t> args) {
        const auto& m = host.com.by_name(iface).methods.at(slot);
        EXPECT(m.argc == args.size(), "%s slot %u registered argc %u, call passes %zu", iface, slot, m.argc, args.size());
        cpu.reg[x86::ESP] = STACK + 0x800; unsigned i = 0;
        for (uint32_t a : args) mem.w32(cpu.reg[x86::ESP] + 4 + 4 * i++, a);
        try { return m.fn(host.com.by_guest(*args.begin()), cpu); }
        catch (const std::exception& ex) {
            if (g_expect_fault) throw;
            ++failures; std::printf("X86-D3D11 FAIL: %s slot %u threw: %s\n", iface, slot, ex.what()); return 0xDEADC0DEu;
        }
    }
    const char* iface_of(uint32_t slot_addr) { uint32_t g = mem.r32(slot_addr); return g ? host.com.by_guest(g).iface : ""; }
};

static std::string subs_log(const std::vector<std::pair<uint32_t, uint32_t>>& subs) {
    std::string s; for (size_t i = 0; i < subs.size(); ++i) s += F(" [%u:%u:%u]", subs[i].first, unsigned(0x1000 + i), unsigned(i + 1));
    return F(" init=%zu", subs.size()) + s;
}

static void test_routes() {
    init_fakes();
    Env e;
    Fake* dev = make(vt_dev); Fake* ctx = make(vt_ctx); Fake* swap = make(vt_swap);
    const uint32_t dev_g = e.proxy("ID3D11Device", dev), ctx_g = e.proxy("ID3D11DeviceContext", ctx), sw_g = e.proxy("IDXGISwapChain", swap);
    Fake* buf = make(vt_buf); buf->dim = D3D11_RESOURCE_DIMENSION_BUFFER;
    buf->buf.ByteWidth = 64; buf->buf.Usage = D3D11_USAGE_DYNAMIC; buf->buf.BindFlags = D3D11_BIND_VERTEX_BUFFER; buf->buf.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    buf->mem.assign(64, 0xAA); buf->row_pitch = 64;
    Fake* tex = make(vt_tex); tex->dim = D3D11_RESOURCE_DIMENSION_TEXTURE2D;   // 16x16 RGBA8, 3 mips, 2 slices: 6 subresources
    tex->tex.Width = 16; tex->tex.Height = 16; tex->tex.MipLevels = 3; tex->tex.ArraySize = 2; tex->tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM; tex->tex.SampleDesc.Count = 1;
    tex->mem.assign(1024, 0xAA); tex->row_pitch = 64;
    Fake* bc = make(vt_tex); bc->dim = D3D11_RESOURCE_DIMENSION_TEXTURE2D;     // 16x16 BC3: 4 block rows of 64 bytes
    bc->tex.Width = 16; bc->tex.Height = 16; bc->tex.MipLevels = 1; bc->tex.ArraySize = 1; bc->tex.Format = DXGI_FORMAT_BC3_UNORM; bc->tex.SampleDesc.Count = 1;
    bc->mem.assign(1024, 0xAA); bc->row_pitch = 64;
    const uint32_t buf_g = e.proxy("ID3D11Buffer", buf), tex_g = e.proxy("ID3D11Texture2D", tex), bc_g = e.proxy("ID3D11Texture2D", bc);
    Fake* nv = make(vt_tex); nv->dim = D3D11_RESOURCE_DIMENSION_TEXTURE2D;    // a planar video format has no row size to bridge
    nv->tex.Width = 16; nv->tex.Height = 16; nv->tex.MipLevels = 1; nv->tex.ArraySize = 1; nv->tex.Format = DXGI_FORMAT(103); nv->tex.SampleDesc.Count = 1;
    const uint32_t nv_g = e.proxy("ID3D11Texture2D", nv);
    Fake* dsv = make(vt_view); Fake* rtv = make(vt_view); Fake* srv = make(vt_view); Fake* srv2 = make(vt_view); Fake* smp = make(vt_view);
    Fake* qry = make(vt_query); qry->qdesc.Query = D3D11_QUERY_OCCLUSION;
    const uint32_t dsv_g = e.proxy("ID3D11DepthStencilView", dsv), rtv_g = e.proxy("ID3D11RenderTargetView", rtv);
    const uint32_t srv_g = e.proxy("ID3D11ShaderResourceView", srv), srv2_g = e.proxy("ID3D11ShaderResourceView", srv2);
    const uint32_t smp_g = e.proxy("ID3D11SamplerState", smp), qry_g = e.proxy("ID3D11Query", qry);
    uint32_t r, out = e.alloc(4);

    // ---- CreateDepthStencilView (device slot 10)
    r = e.call("ID3D11Device", 10, {dev_g, tex_g, e.words({45, 3, 0, 0, 0, 0}), out});
    HR(r, S_OK); LOG(F("CreateDepthStencilView res=%d fmt=45 dim=3", tex->id));
    EXPECT(std::string(e.iface_of(out)) == "ID3D11DepthStencilView", "DSV proxy is %s", e.iface_of(out));
    r = e.call("ID3D11Device", 10, {dev_g, tex_g, 0, out});
    HR(r, S_OK); LOG(F("CreateDepthStencilView res=%d desc=null", tex->id));
    r = e.call("ID3D11Device", 10, {dev_g, tex_g, 0, 0}); HR(r, S_FALSE);   // validation-only: a null output reaches the host
    LOG(F("CreateDepthStencilView res=%d desc=null", tex->id));
    {   // a bad guest output pointer faults before a proxy takes the host object, which is released, not leaked
        const unsigned live = e.host.com.live();
        EXPECT(faults([&] { e.call("ID3D11Device", 10, {dev_g, tex_g, 0, 0xDEAD0000u}); }), "a bad output pointer faults");
        LOG(F("CreateDepthStencilView res=%d desc=null", tex->id));
        EXPECT(g_pool.back().refs == 0 && e.host.com.live() == live, "bad output pointer leaked: refs=%d live %u -> %u", g_pool.back().refs, live, e.host.com.live());
    }
    e.mem.w32(out, 0x1234); g_hr = E_INVALIDARG;
    r = e.call("ID3D11Device", 10, {dev_g, tex_g, 0, out});
    HR(r, E_INVALIDARG); LOG(F("CreateDepthStencilView res=%d desc=null", tex->id)); EXPECT(e.mem.r32(out) == 0, "failed create leaves a null out");

    // ---- OMSetRenderTargets with a depth-stencil view (slot 33), ClearDepthStencilView (slot 53)
    uint32_t rtvs = e.words({rtv_g});
    r = e.call("ID3D11DeviceContext", 33, {ctx_g, 1, rtvs, dsv_g}); LOG(F("OMSetRenderTargets n=1 rtv0=%d dsv=%d", rtv->id, dsv->id));
    r = e.call("ID3D11DeviceContext", 33, {ctx_g, 1, rtvs, 0}); LOG(F("OMSetRenderTargets n=1 rtv0=%d dsv=0", rtv->id));
    r = e.call("ID3D11DeviceContext", 33, {ctx_g, 0, 0, dsv_g}); LOG(F("OMSetRenderTargets n=0 rtv0=0 dsv=%d", dsv->id));
    r = e.call("ID3D11DeviceContext", 53, {ctx_g, dsv_g, 3, fbits(0.5f), 0xFFFFFF7Fu});
    LOG(F("ClearDepthStencilView dsv=%d flags=3 depth=0.500 stencil=127", dsv->id));   // the high bytes of the stencil slot are ignored
    r = e.call("ID3D11DeviceContext", 53, {ctx_g, dsv_g, 1, fbits(1.0f), 0}); LOG(F("ClearDepthStencilView dsv=%d flags=1 depth=1.000 stencil=0", dsv->id));
    r = e.call("ID3D11DeviceContext", 53, {ctx_g, 0, 1, fbits(1.0f), 0}); HR(r, E_POINTER); LOG();

    // ---- CreateTexture2D initial data (device slot 5): every mip of every slice
    auto make_init = [&](const std::vector<std::pair<uint32_t, uint32_t>>& subs, uint32_t shift) {   // {pitch, bytes}
        std::vector<uint32_t> w;
        for (size_t i = 0; i < subs.size(); ++i) {
            std::vector<uint8_t> d(subs[i].second, uint8_t(i + 1));
            uint32_t at = i + 1 == subs.size() ? e.at_end(d, shift) : e.put(d.data(), uint32_t(d.size()));
            w.insert(w.end(), {at, subs[i].first, uint32_t(0x1000 + i)});
        }
        return e.put(w.data(), uint32_t(w.size() * 4));
    };
    auto tex_desc = [&](uint32_t w, uint32_t h, uint32_t mips, uint32_t arr, uint32_t fmt) { return e.words({w, h, mips, arr, fmt, 1, 0, 0, 8, 0, 0}); };
    auto create_tex = [&](uint32_t w, uint32_t h, uint32_t mips, uint32_t arr, uint32_t fmt, const std::vector<std::pair<uint32_t, uint32_t>>& subs, uint32_t shift) {
        return e.call("ID3D11Device", 5, {dev_g, tex_desc(w, h, mips, arr, fmt), make_init(subs, shift), out});
    };
    {   // 8x8 RGBA8, 4 mips, 2 slices: 8 entries, mip-major within each slice
        std::vector<std::pair<uint32_t, uint32_t>> s = {{32, 256}, {16, 64}, {8, 16}, {4, 4}, {32, 256}, {16, 64}, {8, 16}, {4, 4}};
        e.mem.w32(out, 0); r = create_tex(8, 8, 4, 2, 28, s, 0);
        HR(r, S_OK); LOG("CreateTexture2D 8x8 mips=4 arr=2 fmt=28" + subs_log(s));
        EXPECT(std::string(e.iface_of(out)) == "ID3D11Texture2D", "texture proxy is %s", e.iface_of(out));
        EXPECT(faults([&] { create_tex(8, 8, 4, 2, 28, s, 1); }), "the last subresource is read in full"); g_log.clear();
    }
    {   // MipLevels 0 is the full chain from max(Width, Height): 16x4 is 16,8,4,2,1 wide
        std::vector<std::pair<uint32_t, uint32_t>> s = {{64, 256}, {32, 64}, {16, 16}, {8, 8}, {4, 4}};
        r = create_tex(16, 4, 0, 1, 28, s, 0); HR(r, S_OK); LOG("CreateTexture2D 16x4 mips=0 arr=1 fmt=28" + subs_log(s));
    }
    {   // BC3: 8x8 is 2 block rows of 32 bytes; the 4x4 mip is one row of 16
        std::vector<std::pair<uint32_t, uint32_t>> s = {{32, 64}, {16, 16}};
        r = create_tex(8, 8, 2, 1, 77, s, 0); HR(r, S_OK); LOG("CreateTexture2D 8x8 mips=2 arr=1 fmt=77" + subs_log(s));
        EXPECT(faults([&] { create_tex(8, 8, 2, 1, 77, s, 1); }), "BC3 data is read to its last block row"); g_log.clear();
    }
    {   // BC mips below 4x4 still take one whole block: an 8x8 base is 8, 4, 2, 1 wide
        std::vector<std::pair<uint32_t, uint32_t>> s = {{32, 64}, {16, 16}, {16, 16}, {16, 16}};
        r = create_tex(8, 8, 4, 1, 77, s, 0); HR(r, S_OK); LOG("CreateTexture2D 8x8 mips=4 arr=1 fmt=77" + subs_log(s));
        EXPECT(faults([&] { create_tex(8, 8, 4, 1, 77, s, 1); }), "the 1x1 BC3 mip is a whole block"); g_log.clear();
        std::vector<std::pair<uint32_t, uint32_t>> b = {{16, 32}, {8, 8}, {8, 8}, {8, 8}};   // BC1 blocks are 8 bytes
        r = create_tex(8, 8, 4, 1, 71, b, 0); HR(r, S_OK); LOG("CreateTexture2D 8x8 mips=4 arr=1 fmt=71" + subs_log(b));
        EXPECT(faults([&] { create_tex(8, 8, 4, 1, 71, b, 1); }), "the 1x1 BC1 mip is a whole block"); g_log.clear();
    }
    {   // the last row spans only its texels: 8x8 BC3 at a 64-byte pitch is (2 - 1) * 64 + 32 = 96 bytes, not 128
        std::vector<std::pair<uint32_t, uint32_t>> s = {{64, 96}};
        r = create_tex(8, 8, 1, 1, 77, s, 0); HR(r, S_OK); LOG("CreateTexture2D 8x8 mips=1 arr=1 fmt=77" + subs_log(s));
        EXPECT(faults([&] { create_tex(8, 8, 1, 1, 77, s, 1); }), "the last block row's bytes are read"); g_log.clear();
        std::vector<std::pair<uint32_t, uint32_t>> rgba = {{64, 96}};   // 8x2 RGBA8 at a 64-byte pitch: 64 + 32
        r = create_tex(8, 2, 1, 1, 28, rgba, 0); HR(r, S_OK); LOG("CreateTexture2D 8x2 mips=1 arr=1 fmt=28" + subs_log(rgba));
    }
    {   // one subresource still works, and no data still reaches the host as null
        std::vector<std::pair<uint32_t, uint32_t>> s = {{16, 64}};
        r = create_tex(4, 4, 1, 1, 28, s, 0); HR(r, S_OK); LOG("CreateTexture2D 4x4 mips=1 arr=1 fmt=28" + subs_log(s));
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 1, 28), 0, out}); HR(r, S_OK); LOG("CreateTexture2D 4x4 mips=1 arr=1 fmt=28 init=null");
    }
    {   // invalid input never reaches the host
        uint32_t good = e.put(std::vector<uint8_t>(64, 1).data(), 64);
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 1, 28), e.words({0, 16, 0}), out}); HR(r, E_INVALIDARG);   // null pSysMem
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 1, 28), e.words({good, 0, 0}), out}); HR(r, E_INVALIDARG);   // zero pitch
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 16, 1, 28), e.words({good, 16, 0}), out}); HR(r, E_INVALIDARG);   // 16 mips
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 2049, 28), e.words({good, 16, 0}), out}); HR(r, E_INVALIDARG);   // 2049 slices
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(0, 4, 1, 1, 28), e.words({good, 16, 0}), out}); HR(r, E_INVALIDARG);   // empty
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4096, 4096, 1, 1, 28), e.words({good, 16384, 0}), out}); HR(r, E_INVALIDARG);   // 64 MiB > guest window
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 1, 28), e.words({good, 8, 0}), out}); HR(r, E_INVALIDARG);   // pitch 8 < the 16-byte row
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(8, 8, 1, 1, 77), e.words({good, 16, 0}), out}); HR(r, E_INVALIDARG);   // BC3 pitch 16 < the 32-byte block row
        g_gaps.clear(); r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 1, 103), e.words({good, 16, 0}), out});   // NV12 is planar
        HR(r, x86::D3D11_GAP_HRESULT);
        EXPECT(g_gaps.size() == 1 && g_gaps[0] == "x86 D3D11 gap: ID3D11Device::CreateTexture2D initial data format", "gap line %s", g_gaps.empty() ? "(none)" : g_gaps[0].c_str());
        LOG();
        r = e.call("ID3D11Device", 5, {dev_g, tex_desc(4, 4, 1, 1, 28), 0, 0}); HR(r, S_FALSE);   // validation-only
        LOG("CreateTexture2D 4x4 mips=1 arr=1 fmt=28 init=null");
        std::vector<std::pair<uint32_t, uint32_t>> s = {{16, 64}};
        e.mem.w32(out, 0x1234); g_hr = E_OUTOFMEMORY; r = create_tex(4, 4, 1, 1, 28, s, 0);
        HR(r, E_OUTOFMEMORY); LOG("CreateTexture2D 4x4 mips=1 arr=1 fmt=28" + subs_log(s)); EXPECT(e.mem.r32(out) == 0, "failed create leaves a null out");
    }

    // ---- UpdateSubresource with a destination box (slot 48): {this, dst, sub, box, src, rowpitch, depthpitch}
    auto update = [&](uint32_t dst, uint32_t sub, const std::vector<uint32_t>& box, uint32_t bytes, uint32_t pitch, uint32_t shift) {
        std::vector<uint8_t> d(bytes, 0x77);
        return e.call("ID3D11DeviceContext", 48, {ctx_g, dst, sub, box.empty() ? 0 : e.words(box), e.at_end(d, shift), pitch, 0});
    };
    r = update(buf_g, 0, {8, 0, 0, 24, 1, 1}, 16, 0, 0);   // a buffer box is byte offsets: 16 bytes
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=8,0,0,24,1,1 rp=0 dp=0 first=119", buf->id));
    EXPECT(faults([&] { update(buf_g, 0, {8, 0, 0, 24, 1, 1}, 16, 0, 1); }), "buffer box reads right - left bytes"); g_log.clear();
    r = update(buf_g, 0, {}, 64, 0, 0); HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=null rp=0 dp=0 first=119", buf->id));
    r = update(tex_g, 0, {4, 4, 0, 12, 8, 1}, 128, 32, 0);   // 8x4 texels of RGBA8 in a 32-byte pitch
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=4,4,0,12,8,1 rp=32 dp=0 first=119", tex->id));
    EXPECT(faults([&] { update(tex_g, 0, {4, 4, 0, 12, 8, 1}, 128, 32, 1); }), "texture box reads pitch * box rows"); g_log.clear();
    r = update(tex_g, 4, {0, 0, 0, 8, 8, 1}, 256, 32, 0);   // slice 1, mip 1 is 8x8
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=4 box=0,0,0,8,8,1 rp=32 dp=0 first=119", tex->id));
    r = update(tex_g, 0, {}, 1024, 64, 0); HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=null rp=64 dp=0 first=119", tex->id));
    r = update(bc_g, 0, {0, 0, 0, 8, 8, 1}, 64, 32, 0);   // BC3: 8 texel rows are 2 block rows
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=0,0,0,8,8,1 rp=32 dp=0 first=119", bc->id));
    EXPECT(faults([&] { update(bc_g, 0, {0, 0, 0, 8, 8, 1}, 64, 32, 1); }), "BC box reads ceil(rows / 4) block rows"); g_log.clear();
    r = update(bc_g, 0, {}, 256, 64, 0); HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=null rp=64 dp=0 first=119", bc->id));
    r = update(bc_g, 0, {0, 0, 0, 8, 8, 1}, 96, 64, 0);   // the last block row is 32 of the 64 pitch bytes: (2 - 1) * 64 + 32
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=0,0,0,8,8,1 rp=64 dp=0 first=119", bc->id));
    EXPECT(faults([&] { update(bc_g, 0, {0, 0, 0, 8, 8, 1}, 96, 64, 1); }), "the last block row's bytes are read"); g_log.clear();
    r = update(tex_g, 0, {0, 0, 0, 8, 2, 1}, 96, 64, 0);   // 8x2 RGBA8 at a 64-byte pitch: 64 + 32
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=0,0,0,8,2,1 rp=64 dp=0 first=119", tex->id));
    EXPECT(faults([&] { update(tex_g, 0, {0, 0, 0, 8, 2, 1}, 96, 64, 1); }), "the last row's texels are read"); g_log.clear();
    r = update(tex_g, 0, {0, 0, 0, 8, 1, 1}, 32, 0, 0);   // one row ignores the pitch
    HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=0,0,0,8,1,1 rp=0 dp=0 first=119", tex->id));
    r = update(buf_g, 0, {8, 0, 0, 8, 1, 1}, 16, 0, 0); HR(r, S_OK); LOG();   // empty box: nothing to copy
    const std::vector<std::vector<uint32_t>> bad_buffer_boxes = {
        {24, 0, 0, 8, 1, 1}, {0, 1, 0, 8, 0, 1}, {0, 0, 1, 8, 1, 0}, {0, 0, 0, 65, 1, 1}, {0, 0, 0, 8, 2, 1}, {0, 0, 0, 8, 1, 2}};
    for (const auto& box : bad_buffer_boxes) { r = update(buf_g, 0, box, 16, 0, 0); HR(r, E_INVALIDARG); }
    r = update(buf_g, 1, {0, 0, 0, 8, 1, 1}, 16, 0, 0); HR(r, E_INVALIDARG);                 // a buffer has one subresource
    r = update(tex_g, 4, {0, 0, 0, 9, 8, 1}, 256, 32, 0); HR(r, E_INVALIDARG);               // 9 > the 8-wide mip 1
    r = update(tex_g, 0, {0, 0, 0, 17, 16, 1}, 256, 32, 0); HR(r, E_INVALIDARG);
    r = update(tex_g, 6, {0, 0, 0, 4, 4, 1}, 64, 16, 0); HR(r, E_INVALIDARG);                // 3 mips * 2 slices = 6 subresources
    r = update(tex_g, 6, {}, 64, 16, 0); HR(r, E_INVALIDARG);
    r = update(tex_g, 0, {0, 0, 0, 4, 4, 1}, 64, 0, 0); HR(r, E_INVALIDARG);                 // no pitch
    r = update(tex_g, 0, {0, 0, 0, 16, 16, 1}, 64, 0xFFFFFFFFu, 0); HR(r, E_INVALIDARG);     // pitch * rows beyond the window
    r = update(tex_g, 0, {}, 64, 0x10000000u, 0); HR(r, E_INVALIDARG);                       // 16 rows * 256 MiB overflows 32 bits
    r = update(bc_g, 0, {0, 0, 0, 8, 8, 1}, 64, 16, 0); HR(r, E_INVALIDARG);                 // BC3 block row is 32 bytes, pitch 16
    r = update(tex_g, 0, {0, 0, 0, 8, 8, 1}, 64, 16, 0); HR(r, E_INVALIDARG);                // 8 RGBA8 texels are 32 bytes, pitch 16
    r = update(tex_g, 0, {}, 64, 32, 0); HR(r, E_INVALIDARG);                                // a 16-texel row is 64 bytes
    g_gaps.clear(); r = update(nv_g, 0, {}, 64, 16, 0); HR(r, x86::D3D11_GAP_HRESULT);       // planar format: a named gap
    r = update(nv_g, 0, {0, 0, 0, 8, 8, 1}, 64, 16, 0); HR(r, x86::D3D11_GAP_HRESULT);
    EXPECT(g_gaps.size() == 2 && g_gaps[0] == "x86 D3D11 gap: ID3D11DeviceContext::UpdateSubresource format", "gap line %s", g_gaps.empty() ? "(none)" : g_gaps[0].c_str());
    r = e.call("ID3D11DeviceContext", 48, {ctx_g, buf_g, 0, 0, 0, 0, 0}); HR(r, E_INVALIDARG);   // no source
    r = e.call("ID3D11DeviceContext", 48, {ctx_g, 0, 0, 0, e.alloc(16), 0, 0}); HR(r, E_INVALIDARG);   // no destination
    LOG();

    // ---- Map / Unmap (slots 14 and 15): direction, shadow, scratch reuse, rollback
    auto map = [&](uint32_t res_g, uint32_t sub, uint32_t mode, uint32_t out_ptr) { return e.call("ID3D11DeviceContext", 14, {ctx_g, res_g, sub, mode, 0, out_ptr}); };
    auto unmap = [&](uint32_t res_g, uint32_t sub) { return e.call("ID3D11DeviceContext", 15, {ctx_g, res_g, sub}); };
    auto mlog = [&](Fake* f, uint32_t sub, uint32_t mode) { return F("Map res=%d sub=%u mode=%u flags=0", f->id, sub, mode); };
    auto ulog = [&](Fake* f, uint32_t sub) { return F("Unmap res=%d sub=%u", f->id, sub); };
    auto put_guest = [&](uint32_t at, uint32_t from, uint32_t n, uint8_t v) { std::vector<uint8_t> w(n, v); e.mem.write_bytes(at + from, w.data(), n); };
    auto guest_is = [&](uint32_t at, uint32_t from, uint32_t n, uint8_t v) { for (uint32_t i = from; i < from + n; ++i) if (e.mem.r8(at + i) != v) return false; return true; };
    auto host_is = [&](Fake* f, uint32_t from, uint32_t n, uint8_t v) { for (uint32_t i = from; i < from + n; ++i) if (f->mem[i] != v) return false; return true; };
    auto create_buffer = [&](uint32_t bytes, uint32_t usage, uint32_t access) {
        uint32_t o = e.alloc(4);
        uint32_t hr = e.call("ID3D11Device", 3, {dev_g, e.words({bytes, usage, D3D11_BIND_VERTEX_BUFFER, access, 0, 0}), 0, o});
        HR(hr, S_OK); LOG(F("CreateBuffer bytes=%u usage=%u init=null", bytes, usage));
        return std::make_pair(e.mem.r32(o), &g_pool.back());
    };
    const std::string no_nooverwrite = "x86 D3D11 gap: ID3D11DeviceContext::Map WRITE_NO_OVERWRITE without a prior write to seed from";
    const std::string no_write = "x86 D3D11 gap: ID3D11DeviceContext::Map WRITE without a prior write to seed from";
    uint32_t mapped = e.alloc(12), at = 0;
    {   // READ: host -> guest, and Unmap writes nothing back
        std::fill(buf->mem.begin(), buf->mem.end(), uint8_t(0xAA));
        r = map(buf_g, 0, D3D11_MAP_READ, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 1)); at = e.mem.r32(mapped);
        EXPECT(e.mem.r32(mapped + 4) == 64 && e.mem.r32(mapped + 8) == 0, "pitches %u/%u", e.mem.r32(mapped + 4), e.mem.r32(mapped + 8));
        EXPECT(guest_is(at, 0, 64, 0xAA), "READ: the guest sees the host bytes");
        put_guest(at, 0, 64, 0x55); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        EXPECT(host_is(buf, 0, 64, 0xAA), "READ: Unmap writes nothing back");
        // READ_WRITE: host -> guest, then the whole scratch goes back
        r = map(buf_g, 0, D3D11_MAP_READ_WRITE, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 3)); at = e.mem.r32(mapped);
        EXPECT(guest_is(at, 0, 64, 0xAA), "READ_WRITE: the guest sees the host bytes");
        put_guest(at, 0, 64, 0x55); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        EXPECT(host_is(buf, 0, 64, 0x55), "READ_WRITE: Unmap writes the scratch back");
    }
    {   // WRITE_DISCARD: the reused scratch is zeroed, not stale and not host bytes; the whole scratch goes back
        std::fill(buf->mem.begin(), buf->mem.end(), uint8_t(0xAA));
        r = map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 4)); at = e.mem.r32(mapped);
        EXPECT(guest_is(at, 0, 64, 0x00), "WRITE_DISCARD: the scratch is zeroed");
        put_guest(at, 0, 32, 0x11); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        EXPECT(host_is(buf, 0, 32, 0x11) && host_is(buf, 32, 32, 0x00), "WRITE_DISCARD: Unmap writes the scratch back");
        // WRITE_NO_OVERWRITE: seeded from what the guest last wrote, never from the host. Bytes 0..31 now
        // hold 0x99, as if another owner changed them; only the span the guest writes may reach the host.
        std::fill(buf->mem.begin(), buf->mem.begin() + 32, uint8_t(0x99));
        r = map(buf_g, 0, D3D11_MAP_WRITE_NO_OVERWRITE, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 5)); at = e.mem.r32(mapped);
        EXPECT(guest_is(at, 0, 32, 0x11) && guest_is(at, 32, 32, 0x00), "WRITE_NO_OVERWRITE: the scratch is the shadow, not the host");
        put_guest(at, 32, 32, 0x22); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        EXPECT(host_is(buf, 0, 32, 0x99) && host_is(buf, 32, 32, 0x22), "WRITE_NO_OVERWRITE: bytes the guest did not touch are untouched (host %02x %02x)", buf->mem[0], buf->mem[32]);
        // a second append: only the one byte the guest changes is copied, not the whole shadow
        buf->mem[40] = 0x77;
        r = map(buf_g, 0, D3D11_MAP_WRITE_NO_OVERWRITE, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 5)); at = e.mem.r32(mapped);
        EXPECT(guest_is(at, 0, 32, 0x11) && guest_is(at, 32, 32, 0x22), "the shadow keeps the guest's earlier writes");
        put_guest(at, 5, 1, 0x33); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        EXPECT(buf->mem[5] == 0x33 && buf->mem[40] == 0x77 && buf->mem[0] == 0x99 && buf->mem[32] == 0x22, "only the dirty byte is copied (host 5=%02x 40=%02x 0=%02x)", buf->mem[5], buf->mem[40], buf->mem[0]);
        // WRITE with a shadow takes the same path
        r = map(buf_g, 0, D3D11_MAP_WRITE, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 2)); at = e.mem.r32(mapped);
        EXPECT(guest_is(at, 5, 1, 0x33), "WRITE: the scratch is the shadow");
        put_guest(at, 10, 2, 0x44); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        EXPECT(host_is(buf, 10, 2, 0x44) && buf->mem[40] == 0x77 && buf->mem[0] == 0x99, "WRITE: only the dirty span is copied");
        // a host-side update makes the shadow stale: WRITE_NO_OVERWRITE and WRITE fail closed, DISCARD starts over
        r = update(buf_g, 0, {}, 64, 0, 0); HR(r, S_OK); LOG(F("UpdateSubresource dst=%d sub=0 box=null rp=0 dp=0 first=119", buf->id));
        g_gaps.clear();
        r = map(buf_g, 0, D3D11_MAP_WRITE_NO_OVERWRITE, mapped); HR(r, x86::D3D11_GAP_HRESULT);
        r = map(buf_g, 0, D3D11_MAP_WRITE, mapped); HR(r, x86::D3D11_GAP_HRESULT);
        LOG(); EXPECT(g_gaps.size() == 2 && g_gaps[0] == no_nooverwrite && g_gaps[1] == no_write, "gap lines (%zu)", g_gaps.size());
        r = map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 4)); r = unmap(buf_g, 0); LOG(ulog(buf, 0));
    }
    {   // no shadow and no way to read the host: a fresh dynamic buffer fails closed instead of zeroing unknown bytes
        auto fresh = create_buffer(64, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        g_gaps.clear();
        r = map(fresh.first, 0, D3D11_MAP_WRITE_NO_OVERWRITE, mapped); HR(r, x86::D3D11_GAP_HRESULT);
        r = map(fresh.first, 0, D3D11_MAP_WRITE, mapped); HR(r, x86::D3D11_GAP_HRESULT);
        LOG(); EXPECT(g_gaps.size() == 2 && g_gaps[0] == no_nooverwrite && g_gaps[1] == no_write, "gap lines (%zu)", g_gaps.size());
        // a staging buffer the CPU can read seeds WRITE from the host, then copies back only what changed
        auto stg = create_buffer(32, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);
        std::fill(stg.second->mem.begin(), stg.second->mem.end(), uint8_t(0xBB));
        r = map(stg.first, 0, D3D11_MAP_WRITE, mapped); HR(r, S_OK); LOG(mlog(stg.second, 0, 2)); at = e.mem.r32(mapped);
        EXPECT(guest_is(at, 0, 32, 0xBB), "staging WRITE: seeded from the host");
        put_guest(at, 3, 1, 0xCC); stg.second->mem[10] = 0xEE; r = unmap(stg.first, 0); LOG(ulog(stg.second, 0));
        EXPECT(stg.second->mem[3] == 0xCC && stg.second->mem[10] == 0xEE && stg.second->mem[0] == 0xBB, "staging WRITE: only the dirty byte is copied");
    }
    {   // one active map per subresource, an Unmap with none is a no-op, and failures never leave the host mapped
        r = map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, S_OK); LOG(mlog(buf, 0, 4));
        r = map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, 0x887A0001u); LOG();   // DXGI_ERROR_INVALID_CALL, host not asked
        r = unmap(buf_g, 0); LOG(ulog(buf, 0));
        r = unmap(buf_g, 0); HR(r, S_OK); LOG();
        g_hr = HRESULT(0x887A000Au); r = map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, mapped);   // DXGI_ERROR_WAS_STILL_DRAWING
        HR(r, 0x887A000Au); LOG(mlog(buf, 0, 4)); r = unmap(buf_g, 0); LOG();
        EXPECT(faults([&] { map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, 0xDEAD0000u); }), "a bad output pointer faults");   // probed before the host Map
        LOG();
        r = map(buf_g, 0, D3D11_MAP_WRITE_DISCARD, 0); HR(r, E_POINTER);
        r = map(buf_g, 0, 0, mapped); HR(r, E_INVALIDARG); r = map(buf_g, 0, 6, mapped); HR(r, E_INVALIDARG); LOG();   // not a D3D11_MAP
        Fake* huge = make(vt_buf); huge->dim = D3D11_RESOURCE_DIMENSION_BUFFER; huge->buf.ByteWidth = 0x1000001; huge->buf.Usage = D3D11_USAGE_DYNAMIC; huge->mem.assign(1, 0);
        const uint32_t huge_g = e.proxy("ID3D11Buffer", huge);   // one byte past the guest scratch window: marshalling fails after the host Map
        EXPECT(faults([&] { map(huge_g, 0, D3D11_MAP_WRITE_DISCARD, mapped); }), "a map the scratch window cannot hold fails");
        LOG(mlog(huge, 0, 4), ulog(huge, 0));   // the host Map is undone
        r = unmap(huge_g, 0); LOG();
    }
    {   // a texture hands out its row pitch, and a BC mip is block rows (4 * 64 = 256 bytes), not texel rows
        r = map(tex_g, 0, D3D11_MAP_READ_WRITE, mapped); HR(r, S_OK); LOG(mlog(tex, 0, 3));
        EXPECT(e.mem.r32(mapped + 4) == 64 && e.mem.r8(e.mem.r32(mapped)) == 0xAA, "texture map pitch %u", e.mem.r32(mapped + 4));
        r = unmap(tex_g, 0); LOG(ulog(tex, 0));
        std::fill(bc->mem.begin(), bc->mem.end(), uint8_t(0xAA));
        r = map(bc_g, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, S_OK); LOG(mlog(bc, 0, 4));
        put_guest(e.mem.r32(mapped), 0, 256, 0x55); r = unmap(bc_g, 0); LOG(ulog(bc, 0));
        EXPECT(host_is(bc, 0, 256, 0x55) && host_is(bc, 256, 768, 0xAA), "BC map copies exactly 256 bytes");
    }
    {   // the scratch is reused: 300 maps of a 64 KiB buffer take one range, and a released buffer hands it on
        auto big = create_buffer(65536, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        r = map(big.first, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, S_OK); r = unmap(big.first, 0);
        const uint32_t top = e.host.d3d9_guest_top; g_log.clear();
        bool all_ok = true;
        for (unsigned i = 0; i < 300; ++i) {
            if (map(big.first, 0, D3D11_MAP_WRITE_DISCARD, mapped) != 0) { all_ok = false; break; }
            put_guest(e.mem.r32(mapped), 0, 1, uint8_t(i + 1)); unmap(big.first, 0);
            all_ok = all_ok && big.second->mem[0] == uint8_t(i + 1);
        }
        EXPECT(all_ok, "300 maps of one buffer all copy back");
        EXPECT(e.host.d3d9_guest_top == top, "the scratch window grew by %u bytes over 300 maps", e.host.d3d9_guest_top - top);
        EXPECT(g_log.size() == 600, "host calls %zu", g_log.size()); g_log.clear();
        e.host.com.guest_release(e.host.com.by_guest(big.first));   // the proxy dies: its scratch goes back to the free list
        auto next = create_buffer(65536, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        r = map(next.first, 0, D3D11_MAP_WRITE_DISCARD, mapped); HR(r, S_OK); r = unmap(next.first, 0); g_log.clear();
        EXPECT(e.host.d3d9_guest_top == top, "a new buffer did not reuse the released scratch (window grew %u)", e.host.d3d9_guest_top - top);
    }

    // ---- Queries: CreateQuery (device 24), Begin 27, End 28, GetData 29
    r = e.call("ID3D11Device", 24, {dev_g, e.words({1, 0}), out}); HR(r, S_OK); LOG("CreateQuery q=1 misc=0");
    EXPECT(std::string(e.iface_of(out)) == "ID3D11Query", "query proxy is %s", e.iface_of(out));
    {   // GetDataSize (slot 7) is a byte count, not a status; GetDesc (slot 8) writes the two-DWORD desc
        const uint32_t q_g = e.mem.r32(out), d = e.alloc(8);
        r = e.call("ID3D11Query", 7, {q_g}); HR(r, 8);
        r = e.call("ID3D11Query", 8, {q_g, d}); HR(r, 0);
        EXPECT(e.mem.r32(d) == D3D11_QUERY_OCCLUSION && e.mem.r32(d + 4) == 0, "query desc %u/%u", e.mem.r32(d), e.mem.r32(d + 4));
        r = e.call("ID3D11Query", 8, {q_g, 0}); HR(r, E_POINTER);
        r = e.call("ID3D11Device", 24, {dev_g, e.words({D3D11_QUERY_TIMESTAMP_DISJOINT, 1}), out}); HR(r, S_OK); LOG(F("CreateQuery q=%u misc=1", unsigned(D3D11_QUERY_TIMESTAMP_DISJOINT)));
        const uint32_t t_g = e.mem.r32(out);
        r = e.call("ID3D11Query", 7, {t_g}); HR(r, 16);
        r = e.call("ID3D11Query", 8, {t_g, d}); EXPECT(e.mem.r32(d) == D3D11_QUERY_TIMESTAMP_DISJOINT && e.mem.r32(d + 4) == 1, "disjoint desc %u/%u", e.mem.r32(d), e.mem.r32(d + 4));
    }
    r = e.call("ID3D11Device", 24, {dev_g, 0, out}); HR(r, E_POINTER); LOG();
    r = e.call("ID3D11Device", 24, {dev_g, e.words({1, 0}), 0}); HR(r, S_FALSE); LOG("CreateQuery q=1 misc=0");   // validation-only
    e.mem.w32(out, 0x1234); g_hr = E_OUTOFMEMORY; r = e.call("ID3D11Device", 24, {dev_g, e.words({0, 0}), out});
    HR(r, E_OUTOFMEMORY); LOG("CreateQuery q=0 misc=0"); EXPECT(e.mem.r32(out) == 0, "failed CreateQuery leaves a null out");
    r = e.call("ID3D11DeviceContext", 27, {ctx_g, qry_g}); LOG(F("Begin async=%d", qry->id));
    r = e.call("ID3D11DeviceContext", 28, {ctx_g, qry_g}); LOG(F("End async=%d", qry->id));
    r = e.call("ID3D11DeviceContext", 27, {ctx_g, 0}); HR(r, E_POINTER);
    r = e.call("ID3D11DeviceContext", 28, {ctx_g, 0}); HR(r, E_POINTER); LOG();
    {
        uint32_t data = e.put(std::vector<uint8_t>(8, 0xCC).data(), 8);
        g_hr = S_FALSE; r = e.call("ID3D11DeviceContext", 29, {ctx_g, qry_g, data, 8, 1});   // not ready: S_FALSE crosses, nothing is written
        HR(r, S_FALSE); LOG(F("GetData async=%d data=ptr size=8 flags=1", qry->id)); EXPECT(e.mem.r8(data) == 0xCC && e.mem.r8(data + 7) == 0xCC, "an unready query writes nothing");
        r = e.call("ID3D11DeviceContext", 29, {ctx_g, qry_g, data, 8, 0});
        HR(r, S_OK); LOG(F("GetData async=%d data=ptr size=8 flags=0", qry->id)); EXPECT(e.mem.r8(data) == 0x11 && e.mem.r8(data + 7) == 0x18, "a ready query copies host -> guest");
        r = e.call("ID3D11DeviceContext", 29, {ctx_g, qry_g, 0, 0, 0}); HR(r, S_OK); LOG(F("GetData async=%d data=null size=0 flags=0", qry->id));
        r = e.call("ID3D11DeviceContext", 29, {ctx_g, qry_g, data, 4097, 0}); HR(r, E_INVALIDARG);
        r = e.call("ID3D11DeviceContext", 29, {ctx_g, 0, data, 8, 0}); HR(r, E_POINTER); LOG();
    }

    // ---- Device queries: CheckMultisampleQualityLevels 30, GetCreationFlags 38, GetDeviceRemovedReason 39
    {
        uint32_t levels = e.words({0xFFFF});
        r = e.call("ID3D11Device", 30, {dev_g, 28, 4, levels}); HR(r, S_OK); LOG("CheckMultisampleQualityLevels fmt=28 count=4"); EXPECT(e.mem.r32(levels) == 3, "quality levels %u", e.mem.r32(levels));
        e.mem.w32(levels, 0xFFFF); g_hr = E_INVALIDARG; r = e.call("ID3D11Device", 30, {dev_g, 28, 3, levels});
        HR(r, E_INVALIDARG); LOG("CheckMultisampleQualityLevels fmt=28 count=3"); EXPECT(e.mem.r32(levels) == 0xFFFF, "a failed check writes nothing");
        r = e.call("ID3D11Device", 30, {dev_g, 28, 4, 0}); HR(r, E_POINTER); LOG();
        r = e.call("ID3D11Device", 38, {dev_g}); HR(r, 0x22);
        r = e.call("ID3D11Device", 39, {dev_g}); HR(r, 0x887A0005u);
    }

    // ---- Context: Flush 111, ClearState 110, copies, instancing, vertex-stage binds
    r = e.call("ID3D11DeviceContext", 111, {ctx_g}); LOG("Flush");
    r = e.call("ID3D11DeviceContext", 110, {ctx_g}); LOG("ClearState");
    r = e.call("ID3D11DeviceContext", 47, {ctx_g, buf_g, bc_g}); LOG(F("CopyResource dst=%d src=%d", buf->id, bc->id));
    r = e.call("ID3D11DeviceContext", 47, {ctx_g, 0, bc_g}); HR(r, E_POINTER);
    r = e.call("ID3D11DeviceContext", 47, {ctx_g, buf_g, 0}); HR(r, E_POINTER); LOG();
    r = e.call("ID3D11DeviceContext", 46, {ctx_g, tex_g, 1, 2, 3, 0, bc_g, 4, e.words({1, 2, 0, 9, 10, 1})});
    LOG(F("CopySubresourceRegion dst=%d/1 at=2,3,0 src=%d/4 box=1,2,0,9,10,1", tex->id, bc->id));
    r = e.call("ID3D11DeviceContext", 46, {ctx_g, tex_g, 1, 2, 3, 0, bc_g, 4, 0});
    LOG(F("CopySubresourceRegion dst=%d/1 at=2,3,0 src=%d/4 box=null", tex->id, bc->id));
    r = e.call("ID3D11DeviceContext", 46, {ctx_g, tex_g, 0, 0, 0, 0, 0, 0, 0}); HR(r, E_POINTER); LOG();
    r = e.call("ID3D11DeviceContext", 57, {ctx_g, tex_g, 2, bc_g, 3, 28}); LOG(F("ResolveSubresource dst=%d/2 src=%d/3 fmt=28", tex->id, bc->id));
    r = e.call("ID3D11DeviceContext", 57, {ctx_g, 0, 2, bc_g, 3, 28}); HR(r, E_POINTER); LOG();
    r = e.call("ID3D11DeviceContext", 54, {ctx_g, srv_g}); LOG(F("GenerateMips srv=%d", srv->id));
    r = e.call("ID3D11DeviceContext", 54, {ctx_g, 0}); HR(r, E_POINTER); LOG();
    r = e.call("ID3D11DeviceContext", 20, {ctx_g, 6, 3, 1, 0xFFFFFFFEu, 2}); LOG("DrawIndexedInstanced 6 3 1 -2 2");   // BaseVertexLocation is signed
    r = e.call("ID3D11DeviceContext", 21, {ctx_g, 4, 5, 6, 7}); LOG("DrawInstanced 4 5 6 7");
    r = e.call("ID3D11DeviceContext", 25, {ctx_g, 1, 2, e.words({srv_g, srv2_g})}); LOG(F("VSSetShaderResources start=1 n=2 v0=%d v1=%d", srv->id, srv2->id));
    r = e.call("ID3D11DeviceContext", 25, {ctx_g, 0, 129, e.words({srv_g})}); HR(r, E_INVALIDARG);
    r = e.call("ID3D11DeviceContext", 26, {ctx_g, 2, 2, e.words({smp_g, 0})}); LOG(F("VSSetSamplers start=2 n=2 v0=%d v1=0", smp->id));
    r = e.call("ID3D11DeviceContext", 26, {ctx_g, 0, 17, e.words({smp_g})}); HR(r, E_INVALIDARG); LOG();

    // ---- ID3D11Buffer / ID3D11Texture2D GetType (7) and GetDesc (10)
    {
        uint32_t o = e.alloc(64);
        r = e.call("ID3D11Buffer", 7, {buf_g, o}); EXPECT(e.mem.r32(o) == D3D11_RESOURCE_DIMENSION_BUFFER, "buffer type %u", e.mem.r32(o));
        r = e.call("ID3D11Texture2D", 7, {tex_g, o}); EXPECT(e.mem.r32(o) == D3D11_RESOURCE_DIMENSION_TEXTURE2D, "texture type %u", e.mem.r32(o));
        r = e.call("ID3D11Buffer", 7, {buf_g, 0}); HR(r, E_POINTER);
        r = e.call("ID3D11Buffer", 10, {buf_g, o}); auto b = e.mem.read_bytes(o, sizeof(D3D11_BUFFER_DESC));
        EXPECT(std::memcmp(b.data(), &buf->buf, sizeof buf->buf) == 0 && e.mem.r32(o) == 64, "buffer desc");
        r = e.call("ID3D11Texture2D", 10, {tex_g, o}); auto t = e.mem.read_bytes(o, sizeof(D3D11_TEXTURE2D_DESC));
        EXPECT(std::memcmp(t.data(), &tex->tex, sizeof tex->tex) == 0 && e.mem.r32(o) == 16 && e.mem.r32(o + 8) == 3, "texture desc");
        r = e.call("ID3D11Buffer", 10, {buf_g, 0}); HR(r, E_POINTER);
        r = e.call("ID3D11Texture2D", 10, {tex_g, 0}); HR(r, E_POINTER); LOG();
    }

    // ---- IDXGISwapChain: SetFullscreenState 10, GetFullscreenState 11, ResizeTarget 14
    r = e.call("IDXGISwapChain", 10, {sw_g, 1, 0}); HR(r, S_OK); LOG("SetFullscreenState fs=1 target=0");
    g_hr = HRESULT(0x887A0022u); r = e.call("IDXGISwapChain", 10, {sw_g, 0, 0}); HR(r, 0x887A0022u); LOG("SetFullscreenState fs=0 target=0");   // the host's HRESULT crosses
    g_gaps.clear(); r = e.call("IDXGISwapChain", 10, {sw_g, 1, 0x1234});   // an IDXGIOutput has no guest proxy
    HR(r, x86::D3D11_GAP_HRESULT); LOG();
    EXPECT(g_gaps.size() == 1 && g_gaps[0] == "x86 D3D11 gap: IDXGISwapChain::SetFullscreenState target output", "gap line %s", g_gaps.empty() ? "(none)" : g_gaps[0].c_str());
    {
        uint32_t fs = e.words({0xFFFF, 0xDEADBEEFu});
        g_fullscreen = FALSE;   // windowed: S_OK, FALSE, and a null target
        r = e.call("IDXGISwapChain", 11, {sw_g, fs, fs + 4}); HR(r, S_OK); LOG("GetFullscreenState fs=ptr target=ptr");
        EXPECT(e.mem.r32(fs) == 0 && e.mem.r32(fs + 4) == 0, "windowed: fullscreen %u target 0x%08x", e.mem.r32(fs), e.mem.r32(fs + 4));
        g_fullscreen = TRUE;    // fullscreen: a BOOL-only query is fine, one that asks for the output cannot be answered
        e.mem.w32(fs + 4, 0xDEADBEEFu);
        r = e.call("IDXGISwapChain", 11, {sw_g, fs, 0}); HR(r, S_OK); LOG("GetFullscreenState fs=ptr target=null");
        EXPECT(e.mem.r32(fs) == 1, "fullscreen %u", e.mem.r32(fs));
        g_gaps.clear(); g_output = nullptr;
        r = e.call("IDXGISwapChain", 11, {sw_g, fs, fs + 4}); HR(r, x86::D3D11_GAP_HRESULT); LOG("GetFullscreenState fs=ptr target=ptr");
        EXPECT(g_output && g_output->refs == 0, "the host output is released (refs %d)", g_output ? g_output->refs : -1);
        EXPECT(e.mem.r32(fs) == 1 && e.mem.r32(fs + 4) == 0, "fullscreen %u target 0x%08x", e.mem.r32(fs), e.mem.r32(fs + 4));
        EXPECT(g_gaps.size() == 1 && g_gaps[0] == "x86 D3D11 gap: IDXGISwapChain::GetFullscreenState target output", "gap line %s", g_gaps.empty() ? "(none)" : g_gaps[0].c_str());
        r = e.call("IDXGISwapChain", 11, {sw_g, 0, 0}); HR(r, S_OK); LOG("GetFullscreenState fs=ptr target=null");
        g_fullscreen = FALSE;
    }
    r = e.call("IDXGISwapChain", 14, {sw_g, e.words({1280, 720, 60, 1, 28, 0, 0})}); HR(r, S_OK); LOG("ResizeTarget 1280x720 fmt=28 refresh=60/1 scanline=0 scaling=0");
    r = e.call("IDXGISwapChain", 14, {sw_g, 0}); HR(r, E_POINTER); LOG();
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
    static_assert(sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC) == 24, "D3D11_DEPTH_STENCIL_VIEW_DESC is six DWORDs");
    static_assert(sizeof(D3D11_QUERY_DESC) == 8, "D3D11_QUERY_DESC is two DWORDs");
    static_assert(sizeof(D3D11_BOX) == 24, "D3D11_BOX is six UINTs");

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
        && audit_iface(host.com, "ID3D11DepthStencilView", x86::kD3D11ViewNames, x86::kD3D11ViewArgc)
        && audit_iface(host.com, "ID3D11Query", x86::kD3D11QueryNames, x86::kD3D11QueryArgc)
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
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 114, "FinishCommandList")
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 10, "SetFullscreenState")
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 11, "GetFullscreenState")
        && named("IDXGISwapChain", x86::kD3D11SwapChainNames, 14, "ResizeTarget")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 10, "CreateDepthStencilView")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 24, "CreateQuery")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 30, "CheckMultisampleQualityLevels")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 38, "GetCreationFlags")
        && named("ID3D11Device", x86::kD3D11DeviceNames, 39, "GetDeviceRemovedReason")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 20, "DrawIndexedInstanced")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 21, "DrawInstanced")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 25, "VSSetShaderResources")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 26, "VSSetSamplers")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 27, "Begin")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 28, "End")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 29, "GetData")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 46, "CopySubresourceRegion")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 47, "CopyResource")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 53, "ClearDepthStencilView")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 54, "GenerateMips")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 57, "ResolveSubresource")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 110, "ClearState")
        && named("ID3D11DeviceContext", x86::kD3D11ContextNames, 111, "Flush")
        && named("ID3D11Buffer", x86::kD3D11ResourceNames, 7, "GetType")
        && named("ID3D11Texture2D", x86::kD3D11ResourceNames, 10, "GetDesc")
        && named("ID3D11Query", x86::kD3D11QueryNames, 7, "GetDataSize")
        && named("ID3D11Query", x86::kD3D11QueryNames, 8, "GetDesc");
    if (!ok) return 1;
    test_routes();
    if (failures) { std::fprintf(stderr, "X86-D3D11 FAIL: %d route check(s) failed\n", failures); return 1; }
    std::puts("X86-D3D11 PASS: 16 interfaces match the SDK slot tables, bridged arities match, PE32 struct sizes hold, Phase 2 routes marshal");
    return 0;
}
