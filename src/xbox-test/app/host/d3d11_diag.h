// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostics for the D3D11 device Play hands a guest. Games fall back or skip
// whole layers silently when a Create* call fails; nothing logged those. The
// entries of the device class's vtable that create resources, shaders and
// states (and CheckFormatSupport/CheckFeatureSupport, plus the immediate
// context's Map) are redirected to wrappers that call the original entry and
// then, on failure, log one line per distinct (method, hr, key parameters)
// through the probe sink, up to a cap. Results and arguments pass through
// unchanged. Like play_graphics.h the patch is per vtable (every device of a
// class shares it), so it is safe for several devices and for the d3d11.1+
// interfaces, whose vtables extend ID3D11Device's: slots beyond ID3D11Device
// are only written to vtables reached through a successful QueryInterface for
// that interface.
// The same classes are patched for the context slots that say what the guest
// actually draws: draws and dispatches bucketed by primitive topology and by
// whether hull/domain shaders are bound, successful shader creations, render
// target sets by format and size, clears, and deferred-context command lists.
// Those are counted with relaxed atomics (no per-call logging) and summarised
// through the sink every ~10 s for the first minutes.
// A draw can also be submitted without a count: DrawAuto, the indirect draws
// (the arguments live in a GPU buffer) and DispatchIndirect. Those carry the
// same topology and shader state, so they are bucketed the same way; a summary
// that only counts the direct draws cannot tell whether a tessellated layer
// was submitted at all. Render targets set together with UAVs
// (OMSetRenderTargetsAndUnorderedAccessViews), MSAA resolves and resource
// copies are counted for the same reason: a layer that renders into its own
// target and is then resolved/copied into the frame is invisible in a
// direct-draw-only summary. Draws are also bucketed by the format of the
// target they were submitted to. Null shader-resource entries are counted per
// stage, because a layer that samples a texture the guest never bound renders
// black with no failed call anywhere. The arguments of an indirect draw are
// read where the guest writes them (CreateBuffer's initial data,
// UpdateSubresource, and the Unmap of a write-only Map), so a zero-count
// submission is visible. Only the bytes the buffer holds are read: the
// argument layouts differ in size (20 for the indexed draws, 16 for
// DrawInstancedIndirect, 12 for DispatchIndirect), and a short upload is
// reported as raw words rather than named fields.
#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace d3d11_diag {
using Sink=void(*)(const char*);
inline Sink sink=nullptr;
constexpr size_t kMaxFailures=200;   // distinct failed calls logged
constexpr size_t kMaxProbes=160;     // distinct capability answers logged
constexpr int kSlots=128;            // ID3D11DeviceContext reaches slot 114
constexpr size_t kMaxClasses=16;

// ---- vtable slots (ID3D11Device, ID3D11Device1, ID3D11DeviceContext)
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
static_assert(CheckFeatureSupport==33&&CreateDeferredContext==27&&CreateSamplerState==23,"ID3D11Device slot layout");

// ID3D11DeviceContext slots (ID3D11DeviceChild's 3 come first). Only the ones
// the draw summary reads are named.
enum ContextSlot {
    PSSetShaderResources=8,DrawIndexed=12,Draw=13,Map=14,Unmap=15,DrawIndexedInstanced=20,DrawInstanced=21,IASetPrimitiveTopology=24,
    VSSetShaderResources=25,SetPredication=30,OMSetRenderTargets=33,
    OMSetRenderTargetsAndUnorderedAccessViews=34,DrawAuto=38,DrawIndexedInstancedIndirect=39,DrawInstancedIndirect=40,
    Dispatch=41,DispatchIndirect=42,CopySubresourceRegion=46,CopyResource=47,UpdateSubresource=48,
    ClearRenderTargetView=50,ClearDepthStencilView=53,ResolveSubresource=57,ExecuteCommandList=58,HSSetShaderResources=59,HSSetShader=60,
    DSSetShaderResources=63,DSSetShader=64,FinishCommandList=114
};
static_assert(ContextMap==14&&Unmap==15&&DrawIndexed==12&&DrawIndexedInstanced==20&&OMSetRenderTargets==33&&
    OMSetRenderTargetsAndUnorderedAccessViews==34&&DrawAuto==38&&DrawIndexedInstancedIndirect==39&&
    DrawInstancedIndirect==40&&DispatchIndirect==42&&UpdateSubresource==48&&
    ResolveSubresource==57&&ExecuteCommandList==58&&HSSetShader==60&&DSSetShader==64&&FinishCommandList==114,
    "ID3D11DeviceContext slot layout");

// ---- reporting: once per key, bounded
inline std::mutex reportM;
inline std::set<std::string> seen;
inline size_t failures=0,probes=0;
inline bool failuresCapped=false,probesCapped=false;
inline void emit(const std::string& text) {if(auto s=sink)s(text.c_str());}
inline void report(bool probe,const std::string& key,const std::string& text) {
    std::string line=text,note;
    {
        std::lock_guard<std::mutex> lock(reportM);
        size_t& used=probe?probes:failures;bool& capped=probe?probesCapped:failuresCapped;
        if(seen.count(key))return;
        if(used>=(probe?kMaxProbes:kMaxFailures)) {
            if(!capped){capped=true;note=probe?"further capability answers suppressed":"further failed calls suppressed";}
            else return;
            line.clear();
        } else {seen.insert(key);++used;}
    }
    if(!line.empty())emit(line);
    if(!note.empty())emit(note+" (cap reached)");
}
inline std::string hex(HRESULT hr){char b[16];snprintf(b,sizeof b,"0x%08lx",(unsigned long)hr);return b;}
inline std::string fmt(const char* f,...) {
    char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);return b;
}

// ---- formatting helpers
inline const char* formatName(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_UNKNOWN:return "UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:return "R32G32B32A32_FLOAT";case DXGI_FORMAT_R32G32B32_FLOAT:return "R32G32B32_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:return "R16G16B16A16_FLOAT";case DXGI_FORMAT_R16G16B16A16_UNORM:return "R16G16B16A16_UNORM";
    case DXGI_FORMAT_R32G32_FLOAT:return "R32G32_FLOAT";case DXGI_FORMAT_R10G10B10A2_UNORM:return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R11G11B10_FLOAT:return "R11G11B10_FLOAT";case DXGI_FORMAT_R8G8B8A8_TYPELESS:return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:return "R8G8B8A8_UNORM";case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R16G16_FLOAT:return "R16G16_FLOAT";case DXGI_FORMAT_R16G16_UNORM:return "R16G16_UNORM";
    case DXGI_FORMAT_R32_TYPELESS:return "R32_TYPELESS";case DXGI_FORMAT_D32_FLOAT:return "D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:return "R32_FLOAT";case DXGI_FORMAT_R32_UINT:return "R32_UINT";
    case DXGI_FORMAT_R24G8_TYPELESS:return "R24G8_TYPELESS";case DXGI_FORMAT_D24_UNORM_S8_UINT:return "D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return "R24_UNORM_X8_TYPELESS";case DXGI_FORMAT_R8G8_UNORM:return "R8G8_UNORM";
    case DXGI_FORMAT_R16_TYPELESS:return "R16_TYPELESS";case DXGI_FORMAT_R16_FLOAT:return "R16_FLOAT";
    case DXGI_FORMAT_D16_UNORM:return "D16_UNORM";case DXGI_FORMAT_R16_UNORM:return "R16_UNORM";
    case DXGI_FORMAT_R16_UINT:return "R16_UINT";case DXGI_FORMAT_R8_UNORM:return "R8_UNORM";
    case DXGI_FORMAT_A8_UNORM:return "A8_UNORM";case DXGI_FORMAT_BC1_UNORM:return "BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:return "BC1_UNORM_SRGB";case DXGI_FORMAT_BC2_UNORM:return "BC2_UNORM";
    case DXGI_FORMAT_BC3_UNORM:return "BC3_UNORM";case DXGI_FORMAT_BC3_UNORM_SRGB:return "BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_UNORM:return "BC4_UNORM";case DXGI_FORMAT_BC5_UNORM:return "BC5_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:return "B8G8R8A8_UNORM";case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_UF16:return "BC6H_UF16";case DXGI_FORMAT_BC7_UNORM:return "BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:return "BC7_UNORM_SRGB";
    default:return "";
    }
}
inline std::string formatText(DXGI_FORMAT f) {
    const char* n=formatName(f);return *n?fmt("%d(%s)",int(f),n):fmt("%d",int(f));
}
inline std::string supportText(UINT s) {
    static const struct {UINT bit;const char* name;} bits[]={
        {0x1,"BUFFER"},{0x2,"IA_VB"},{0x4,"IA_IB"},{0x10,"TEX1D"},{0x20,"TEX2D"},{0x40,"TEX3D"},{0x80,"CUBE"},{0x100,"LOAD"},
        {0x200,"SAMPLE"},{0x400,"SAMPLE_CMP"},{0x1000,"MIP"},{0x2000,"MIP_AUTOGEN"},{0x4000,"RT"},{0x8000,"BLEND"},
        {0x10000,"DS"},{0x20000,"CPU_LOCK"},{0x40000,"MS_RESOLVE"},{0x200000,"MS_RT"},{0x400000,"MS_LOAD"},{0x800000,"GATHER"},
        {0x1000000,"TYPED_UAV"}};
    std::string out;
    for(auto& b:bits) if(s&b.bit){if(!out.empty())out+=",";out+=b.name;}
    return out.empty()?"none":out;
}
inline uint64_t fnv(const void* data,size_t n,uint64_t h=1469598103934665603ull) {
    auto p=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
}

// ---- draw summary
// What the guest actually submits, counted with relaxed atomics on the hot
// path and printed as one line per bucket every kSummaryMs for the first
// kSummaryWindows windows. Nothing here changes a call's result.
constexpr ULONGLONG kSummaryMs=10000;
constexpr unsigned kSummaryWindows=7;   // ~70 s, then one line saying so
// Draw kinds the summary buckets. The last three take their counts from a GPU
// buffer (or, for DrawAuto, from the stream-output state), so a game whose
// terrain goes through them shows nothing in a direct-draw-only summary.
constexpr int kDrawKinds=7;
constexpr int kDrawAuto=4,kDrawIndexedIndirect=5,kDrawInstancedIndirect=6;
// Upper bound on the lines the summary can print: the fixed lines and up to
// kMaxRtvKinds render-target lines per window, one line per draw bucket
// (kDrawKinds x patchlist x hull/domain), one per described render target view,
// and the closing line.
constexpr size_t kMaxDrawBuckets=size_t(kDrawKinds)*4;
constexpr size_t kMaxRtvKinds=24;
constexpr size_t kMaxRtvViews=32;
constexpr size_t kFixedSummaryLines=16;   // topology, kDrawKinds, dispatch, lists, shaders, targets, UAVs, resources, formats, main target
constexpr size_t kMaxSummaryLines=kSummaryWindows*(kFixedSummaryLines+kMaxRtvKinds+kMaxDrawBuckets)+kMaxDrawBuckets+kMaxRtvViews+2;
inline std::atomic<bool> summaryStarted{false};
inline std::atomic<unsigned> summaryWindows{0};
inline std::atomic<ULONGLONG> summaryNext{0};
inline std::atomic<bool> summaryStopped{false};
// Per-context state: a deferred context records with its own topology and its
// own bound shaders, so the draw bucket must read the state of the context
// that made the call. Fixed table, lock-free, first match or first free slot.
// The format cache is direct-mapped: the guest rebinds the same few targets
// most of the time, and a collision costs one GetDesc, never a re-scan.
constexpr size_t kFormatCache=8;   // power of two
struct ContextState {
    std::atomic<void*> context{nullptr};
    std::atomic<unsigned> topology{0};
    std::atomic<unsigned> shaders{0};
    std::atomic<unsigned> rtFormat{0};   // format of the first render target bound
    std::atomic<void*> formatView[kFormatCache];   // view the cached format is for
    std::atomic<unsigned> formatValue[kFormatCache];
};
constexpr size_t kMaxContexts=32;
inline ContextState contextStates[kMaxContexts];
inline ContextState orphanState;                 // past kMaxContexts: counted, not bucketed by context
inline std::atomic<unsigned> lastTopology{0};    // last topology any context set, for the summary line
inline ContextState& stateOf(void* self) {
    static thread_local ContextState* last=nullptr;
    if(last&&last->context.load(std::memory_order_relaxed)==self)return *last;
    for(auto& s:contextStates) if(s.context.load(std::memory_order_acquire)==self){last=&s;return s;}
    for(auto& s:contextStates) {
        void* expected=nullptr;
        if(s.context.compare_exchange_strong(expected,self,std::memory_order_acq_rel)){last=&s;return s;}
    }
    return orphanState;
}
// A context created at an address an earlier one used must not inherit its
// state: patchContext resets the entry (a new context has no topology and no
// shaders bound).
inline void resetContext(void* self) {
    ContextState& s=stateOf(self);
    s.topology.store(0,std::memory_order_relaxed);s.shaders.store(0,std::memory_order_relaxed);
    s.rtFormat.store(0,std::memory_order_relaxed);
    for(size_t i=0;i<kFormatCache;++i)s.formatView[i].store(nullptr,std::memory_order_relaxed);
}
inline std::atomic<uint64_t> draws[kDrawKinds][2][2];   // [kind][patchlist][hull+domain]
// Draws per render-target format. A layer that renders into an off-screen
// target (deferred/HDR) and is then tonemapped into the backbuffer shows up
// here as draws into that format and none into the swapchain's; a layer that
// is drawn but never composited shows up as draws into a format the frame
// never receives.
constexpr size_t kMaxDrawFormats=16;
inline std::atomic<unsigned> drawFormatValue[kMaxDrawFormats];
inline std::atomic<uint64_t> drawFormatCount[kMaxDrawFormats];
inline std::atomic<unsigned> drawFormatUsed{0};
inline std::atomic<uint64_t> drawsNoTarget;   // draws with no render target bound
inline void countDrawFormat(unsigned format) {
    if(!format){drawsNoTarget.fetch_add(1,std::memory_order_relaxed);return;}
    unsigned used=drawFormatUsed.load(std::memory_order_relaxed);
    for(unsigned i=0;i<used&&i<kMaxDrawFormats;++i)
        if(drawFormatValue[i].load(std::memory_order_relaxed)==format) {
            drawFormatCount[i].fetch_add(1,std::memory_order_relaxed);return;
        }
    for(unsigned i=0;i<kMaxDrawFormats;++i) {
        unsigned expected=0;
        if(drawFormatValue[i].compare_exchange_strong(expected,format,std::memory_order_acq_rel)) {
            drawFormatCount[i].fetch_add(1,std::memory_order_relaxed);
            unsigned n=drawFormatUsed.load(std::memory_order_relaxed);
            while(n<i+1&&!drawFormatUsed.compare_exchange_weak(n,i+1,std::memory_order_acq_rel)) {}
            return;
        }
    }
}
inline std::atomic<uint64_t> zeroCounts[kDrawKinds];    // draws with 0 index/vertex/instance count
inline std::atomic<uint64_t> dispatches,dispatchZero,dispatchIndirects,commandLists,executedLists;
inline std::atomic<uint64_t> shaderCreates[6];   // vs gs ps hs ds cs, successful
inline std::atomic<uint64_t> clears[2];          // render target, depth stencil
inline std::atomic<uint64_t> rtvSets;            // OMSetRenderTargets with a render target
inline std::atomic<uint64_t> rtvUnknown;         // ... whose view or resource could not be read
inline std::atomic<uint64_t> rtvUncached;        // ... beyond the kMaxRtvViews table (counted, not described)
inline std::atomic<uint64_t> rtvUavSets;         // OMSetRenderTargetsAndUnorderedAccessViews with a view
inline std::atomic<uint64_t> uavSets;            // ... and with at least one UAV
inline std::atomic<uint64_t> resolves;           // ResolveSubresource (MSAA -> single sample)
inline std::atomic<uint64_t> copies;             // CopyResource + CopySubresourceRegion
inline std::atomic<uint64_t> srvSets[4];         // SetShaderResources calls, vs ps hs ds
inline std::atomic<uint64_t> srvNulls[4];        // ... with at least one null entry (an unbound texture)
inline std::atomic<uint64_t> predicated;         // SetPredication with a predicate object
// Buffers the guest created for indirect draw arguments, so a later
// UpdateSubresource/Map of one can be read without a GetDesc per call. The
// mapped pointer is kept per buffer because a write-only Map is read back
// after Unmap, when the guest has finished writing.
constexpr size_t kMaxIndirectBuffers=64;
inline std::atomic<void*> indirectBuffers[kMaxIndirectBuffers];
inline std::atomic<void*> indirectMapped[kMaxIndirectBuffers];
inline std::atomic<unsigned> indirectBytes[kMaxIndirectBuffers];   // ByteWidth: the read at Unmap must not pass it
inline void noteIndirectBuffer(void* buffer,unsigned bytes) {
    for(size_t i=0;i<kMaxIndirectBuffers;++i) {
        void* expected=nullptr;
        if(indirectBuffers[i].compare_exchange_strong(expected,buffer,std::memory_order_acq_rel)) {
            indirectBytes[i].store(bytes,std::memory_order_relaxed);return;
        }
        if(expected==buffer)return;
    }
}
inline int indirectIndex(void* buffer) {
    for(size_t i=0;i<kMaxIndirectBuffers;++i) if(indirectBuffers[i].load(std::memory_order_relaxed)==buffer)return int(i);
    return -1;
}
inline bool isIndirectBuffer(void* buffer) {return indirectIndex(buffer)>=0;}
inline unsigned indirectBufferBytes(void* buffer) {
    int i=indirectIndex(buffer);
    return i<0?0:indirectBytes[i].load(std::memory_order_relaxed);
}
// The words of an indirect draw's arguments (index/vertex count, instances,
// start index, base vertex, start instance). A zero count submits no work:
// that is what a silently missing layer looks like when the submission is
// indirect. Only the bytes the guest actually supplied are read: a
// DrawInstancedIndirect's arguments are 16 bytes and a DispatchIndirect's 12,
// and a tightly sized buffer must not be read past its end. A short upload is
// reported as raw words rather than named fields, because the field layout
// depends on which call consumes it.
inline std::string indirectArgsText(const void* data,size_t bytes,const char* first="count") {
    if(!data)return "args=null";
    if(bytes<4)return fmt("args=%zuB",bytes);
    const UINT* a=static_cast<const UINT*>(data);
    size_t words=std::min<size_t>(bytes/4,5);
    if(words<5) {
        std::string text="words=";
        for(size_t i=0;i<words;++i)text+=fmt(i?",%u":"%u",a[i]);
        return text+fmt(" (%zu bytes)",bytes);
    }
    static const char* rest[4]={"instances","startIndex","baseVertex","startInstance"};
    std::string text=fmt("%s=%u",first,a[0]);
    for(size_t i=1;i<5;++i)text+=fmt(" %s=%u",rest[i-1],a[i]);
    return text;
}
inline std::mutex rtvM;
inline std::vector<std::string> rtvKinds;        // distinct "fmt WxH" bound as render target
inline std::atomic<void*> rtvViews[kMaxRtvViews];   // views already described: no GetDesc per frame
inline std::atomic<unsigned> mainWidth{0},mainHeight{0};   // largest render target bound
inline std::atomic<unsigned> chainWidth{0},chainHeight{0}; // guest swapchain buffers
inline std::atomic<unsigned> chainFormat{0},chainBuffers{0};
inline std::atomic<unsigned> contextsPatched{0};
inline std::atomic<unsigned> deferredContexts{0};
inline std::vector<void**> contextVtables;   // guarded by patchM
inline std::atomic<bool> contextSeen{false}; // no patch attempt while nothing is adopted

inline std::string topologyName(unsigned t) {
    switch(t) {
    case 0:return "undefined";
    case 1:return "pointlist";
    case 2:return "linelist";case 3:return "linestrip";case 4:return "trianglelist";case 5:return "trianglestrip";
    case 10:return "linelist-adj";case 11:return "linestrip-adj";case 12:return "trianglelist-adj";case 13:return "trianglestrip-adj";
    default:break;
    }
    if(t>=33&&t<=64)return fmt("patchlist-%u",t-32);   // 1..32 control points
    return "?";
}
// Patch lists are 1..32 control points (33..64 in D3D11): the game's terrain
// effect tessellates, so this is the bucket that says whether it ran.
inline bool patchList(unsigned t){return t>=33&&t<=64;}
inline const char* drawKindName(int k){
    static const char* names[kDrawKinds]={"DrawIndexed","Draw","DrawIndexedInstanced","DrawInstanced",
        "DrawAuto","DrawIndexedInstancedIndirect","DrawInstancedIndirect"};return names[k];
}
inline std::string rtvText(ID3D11RenderTargetView* view) {
    if(!view)return "null";
    D3D11_RENDER_TARGET_VIEW_DESC v{};
    view->GetDesc(&v);   // void: a view always has a description
    ID3D11Resource* resource=nullptr;view->GetResource(&resource);
    if(!resource)return "no-resource";
    D3D11_RESOURCE_DIMENSION dim=D3D11_RESOURCE_DIMENSION_UNKNOWN;resource->GetType(&dim);
    std::string text="unknown";
    if(dim==D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC t{};static_cast<ID3D11Texture2D*>(resource)->GetDesc(&t);
        text=fmt("fmt=%s %ux%u bind=0x%x ms=%u",formatText(t.Format).c_str(),t.Width,t.Height,t.BindFlags,t.SampleDesc.Count);
        if(t.BindFlags&D3D11_BIND_RENDER_TARGET) {
            unsigned w=mainWidth.load(std::memory_order_relaxed),h=mainHeight.load(std::memory_order_relaxed);
            if(size_t(t.Width)*t.Height>size_t(w)*h){mainWidth.store(t.Width,std::memory_order_relaxed);mainHeight.store(t.Height,std::memory_order_relaxed);}
        }
    }
    resource->Release();
    return text;
}
// Adds a distinct key to a capped list; returns whether it was new.
inline bool noteKind(std::vector<std::string>& list,const std::string& key,size_t cap) {
    for(auto& k:list) if(k==key)return false;
    if(list.size()>=cap)return false;
    list.push_back(key);return true;
}
inline void summary() {
    if(summaryStarted.exchange(true,std::memory_order_acq_rel))return;
    summaryNext.store(GetTickCount64()+kSummaryMs,std::memory_order_relaxed);
}
inline void printSummary(unsigned window);
// Called from the hooks after they counted; cheap until the next window.
inline void tick() {
    if(!summaryStarted.load(std::memory_order_acquire))return;
    ULONGLONG now=GetTickCount64(),next=summaryNext.load(std::memory_order_relaxed);
    if(now<next)return;
    if(!summaryNext.compare_exchange_strong(next,now+kSummaryMs))return;
    unsigned window=summaryWindows.load(std::memory_order_relaxed);
    if(window>=kSummaryWindows) {
        if(!summaryStopped.exchange(true))emit(fmt("draw summary: %u windows done, counters stay live but are no longer printed",kSummaryWindows));
        return;
    }
    summaryWindows.store(window+1,std::memory_order_relaxed);
    printSummary(window+1);
}
// One window's lines. Called from tick() under the window's CAS, so two
// threads never print the same window.
inline void printSummary(unsigned window) {
    unsigned t=lastTopology.load(std::memory_order_relaxed);
    emit(fmt("draw summary %u/%u: last topology=%u(%s) contexts=%u deferred=%u",window,kSummaryWindows,t,topologyName(t).c_str(),
        contextsPatched.load(),deferredContexts.load()));
    for(int k=0;k<kDrawKinds;++k) {
        uint64_t total=0,patched=0,withHull=0,zero=zeroCounts[k].load(std::memory_order_relaxed);
        for(int p=0;p<2;++p)for(int h=0;h<2;++h) {
            uint64_t n=draws[k][p][h].load(std::memory_order_relaxed);
            total+=n;if(p)patched+=n;if(h)withHull+=n;
        }
        if(!total)continue;
        emit(fmt("draw summary %u/%u: %s calls=%llu patchlist=%llu hull-domain=%llu zero-count=%llu",window,kSummaryWindows,drawKindName(k),
            (unsigned long long)total,(unsigned long long)patched,(unsigned long long)withHull,(unsigned long long)zero));
    }
    uint64_t d=dispatches.load(std::memory_order_relaxed),di=dispatchIndirects.load(std::memory_order_relaxed);
    if(d||di||dispatchZero.load(std::memory_order_relaxed))
        emit(fmt("draw summary %u/%u: Dispatch calls=%llu indirect=%llu zero-groups=%llu",window,kSummaryWindows,
            (unsigned long long)d,(unsigned long long)di,(unsigned long long)dispatchZero.load()));
    emit(fmt("draw summary %u/%u: command lists finish=%llu execute=%llu predicated=%llu",window,kSummaryWindows,
        (unsigned long long)commandLists.load(),(unsigned long long)executedLists.load(),(unsigned long long)predicated.load()));
    emit(fmt("draw summary %u/%u: shader creates vs=%llu gs=%llu ps=%llu hs=%llu ds=%llu cs=%llu",window,kSummaryWindows,
        (unsigned long long)shaderCreates[0].load(),(unsigned long long)shaderCreates[1].load(),(unsigned long long)shaderCreates[2].load(),
        (unsigned long long)shaderCreates[3].load(),(unsigned long long)shaderCreates[4].load(),(unsigned long long)shaderCreates[5].load()));
    emit(fmt("draw summary %u/%u: render targets sets=%llu unreadable=%llu uncached=%llu clears=rt %llu/ds %llu",window,kSummaryWindows,
        (unsigned long long)rtvSets.load(),(unsigned long long)rtvUnknown.load(),(unsigned long long)rtvUncached.load(),
        (unsigned long long)clears[0].load(),(unsigned long long)clears[1].load()));
    // A layer that renders into its own target and is then resolved or copied
    // into the frame never appears as a draw into the swapchain.
    emit(fmt("draw summary %u/%u: targets with UAVs sets=%llu uav=%llu resolves=%llu copies=%llu",window,kSummaryWindows,
        (unsigned long long)rtvUavSets.load(),(unsigned long long)uavSets.load(),
        (unsigned long long)resolves.load(),(unsigned long long)copies.load()));
    // Null entries are the textures the guest never bound: a black layer with
    // no failed call is what that looks like.
    emit(fmt("draw summary %u/%u: shader resources vs=%llu/%llu ps=%llu/%llu hs=%llu/%llu ds=%llu/%llu (null sets)",
        window,kSummaryWindows,(unsigned long long)srvSets[0].load(),(unsigned long long)srvNulls[0].load(),
        (unsigned long long)srvSets[1].load(),(unsigned long long)srvNulls[1].load(),(unsigned long long)srvSets[2].load(),
        (unsigned long long)srvNulls[2].load(),(unsigned long long)srvSets[3].load(),(unsigned long long)srvNulls[3].load()));
    // Draws per target format: the format the swapchain presents vs the ones
    // the guest renders into and may or may not composite. Draws with no
    // render target bound are counted separately: they write nothing visible.
    unsigned formatUsed=drawFormatUsed.load(std::memory_order_relaxed);
    std::string formats;
    for(unsigned i=0;i<formatUsed&&i<kMaxDrawFormats;++i) {
        unsigned f=drawFormatValue[i].load(std::memory_order_relaxed);
        if(!formats.empty())formats+=" ";
        formats+=fmt("%s=%llu",formatText(DXGI_FORMAT(f)).c_str(),(unsigned long long)drawFormatCount[i].load(std::memory_order_relaxed));
    }
    uint64_t noTarget=drawsNoTarget.load(std::memory_order_relaxed);
    if(noTarget)formats+=fmt("%sno-target=%llu",formats.empty()?"":" ",(unsigned long long)noTarget);
    if(!formats.empty())emit(fmt("draw summary %u/%u: draws by target format %s",window,kSummaryWindows,formats.c_str()));
    std::vector<std::string> kinds;
    {std::lock_guard<std::mutex> lock(rtvM);kinds=rtvKinds;}
    for(auto& k:kinds)emit(fmt("draw summary %u/%u: render target %s",window,kSummaryWindows,k.c_str()));
    unsigned w=mainWidth.load(std::memory_order_relaxed),h=mainHeight.load(std::memory_order_relaxed);
    unsigned cw=chainWidth.load(std::memory_order_relaxed),ch=chainHeight.load(std::memory_order_relaxed);
    if(w||cw)emit(fmt("draw summary %u/%u: main render target %ux%u swapchain %ux%u fmt=%s buffers=%u",window,kSummaryWindows,w,h,cw,ch,
        formatText(DXGI_FORMAT(chainFormat.load())).c_str(),chainBuffers.load()));
}
// The guest's own swapchain creation (play_graphics.h) says the size the
// guest asked for; the CoreWindow route may substitute its own.
inline void noteSwapChain(UINT width,UINT height,DXGI_FORMAT format,UINT buffers) {
    chainWidth.store(width,std::memory_order_relaxed);chainHeight.store(height,std::memory_order_relaxed);
    chainFormat.store(unsigned(format),std::memory_order_relaxed);chainBuffers.store(buffers,std::memory_order_relaxed);
}

// ---- vtable patching
struct Patch {int slot;void* hook;};
struct Class {void** vtable=nullptr;bool patched[kSlots]={};void* original[kSlots]={};};
inline Class classes[kMaxClasses];
inline std::atomic<size_t> classCount{0};
inline std::mutex patchM;
inline void* original(void* self,int slot) {
    void** vtable=*static_cast<void***>(self);
    size_t n=classCount.load(std::memory_order_acquire);
    for(size_t i=0;i<n;++i) if(classes[i].vtable==vtable) return classes[i].original[slot];
    return nullptr;
}
// Writes the hooks missing from this vtable; returns whether the vtable now
// carries them. Callers pass only slots the vtable's interface defines.
inline bool patchClass(void** vtable,const Patch* patches,size_t count) {
    std::lock_guard<std::mutex> lock(patchM);
    size_t n=classCount.load();Class* c=nullptr;
    for(size_t i=0;i<n;++i) if(classes[i].vtable==vtable)c=&classes[i];
    if(!c) {
        if(n>=kMaxClasses)return false;
        c=&classes[n];c->vtable=vtable;
    }
    int low=kSlots,high=-1;
    for(size_t i=0;i<count;++i) if(!c->patched[patches[i].slot]) {low=std::min(low,patches[i].slot);high=std::max(high,patches[i].slot);}
    if(high<0)return true;
    if(high>=kSlots){emit(fmt("diagnostics cannot patch vtable slot %d (beyond %d)",high,kSlots));return false;}
    DWORD old=0;
    if(!VirtualProtect(vtable+low,sizeof(void*)*size_t(high-low+1),PAGE_READWRITE,&old)) {
        emit(fmt("diagnostics cannot patch a vtable (error %lu)",GetLastError()));return false;
    }
    for(size_t i=0;i<count;++i) {
        int s=patches[i].slot;if(c->patched[s])continue;
        c->original[s]=vtable[s];vtable[s]=patches[i].hook;c->patched[s]=true;
    }
    DWORD ignored=0;VirtualProtect(vtable+low,sizeof(void*)*size_t(high-low+1),old,&ignored);
    if(c==&classes[n])classCount.store(n+1,std::memory_order_release);
    return true;
}

// ---- description of the arguments of a failed call
inline std::string resourceText(ID3D11Resource* r) {
    if(!r)return "res=null";
    D3D11_RESOURCE_DIMENSION d=D3D11_RESOURCE_DIMENSION_UNKNOWN;r->GetType(&d);
    switch(d) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:{D3D11_BUFFER_DESC b{};static_cast<ID3D11Buffer*>(r)->GetDesc(&b);
        return fmt("res=buffer bytes=%u bind=0x%x usage=%d misc=0x%x stride=%u",b.ByteWidth,b.BindFlags,int(b.Usage),b.MiscFlags,b.StructureByteStride);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:{D3D11_TEXTURE1D_DESC t{};static_cast<ID3D11Texture1D*>(r)->GetDesc(&t);
        return fmt("res=tex1d fmt=%s %u mips=%u arr=%u bind=0x%x usage=%d",formatText(t.Format).c_str(),t.Width,t.MipLevels,t.ArraySize,t.BindFlags,int(t.Usage));}
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:{D3D11_TEXTURE2D_DESC t{};static_cast<ID3D11Texture2D*>(r)->GetDesc(&t);
        return fmt("res=tex2d fmt=%s %ux%u mips=%u arr=%u ms=%u bind=0x%x usage=%d misc=0x%x",formatText(t.Format).c_str(),t.Width,t.Height,t.MipLevels,t.ArraySize,t.SampleDesc.Count,t.BindFlags,int(t.Usage),t.MiscFlags);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:{D3D11_TEXTURE3D_DESC t{};static_cast<ID3D11Texture3D*>(r)->GetDesc(&t);
        return fmt("res=tex3d fmt=%s %ux%ux%u mips=%u bind=0x%x usage=%d",formatText(t.Format).c_str(),t.Width,t.Height,t.Depth,t.MipLevels,t.BindFlags,int(t.Usage));}
    default:return "res=unknown";
    }
}
// Shape of a resource without its sizes: what decides whether a call is a
// new kind of failure.
inline std::string resourceKey(ID3D11Resource* r) {
    if(!r)return "null";
    D3D11_RESOURCE_DIMENSION d=D3D11_RESOURCE_DIMENSION_UNKNOWN;r->GetType(&d);
    switch(d) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:{D3D11_BUFFER_DESC b{};static_cast<ID3D11Buffer*>(r)->GetDesc(&b);return fmt("b%x/%d/%x/%u",b.BindFlags,int(b.Usage),b.MiscFlags,b.StructureByteStride);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:{D3D11_TEXTURE1D_DESC t{};static_cast<ID3D11Texture1D*>(r)->GetDesc(&t);return fmt("1d%d/%x/%d",int(t.Format),t.BindFlags,int(t.Usage));}
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D:{D3D11_TEXTURE2D_DESC t{};static_cast<ID3D11Texture2D*>(r)->GetDesc(&t);return fmt("2d%d/%x/%d/%x/%u/%u",int(t.Format),t.BindFlags,int(t.Usage),t.MiscFlags,t.SampleDesc.Count,t.ArraySize);}
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:{D3D11_TEXTURE3D_DESC t{};static_cast<ID3D11Texture3D*>(r)->GetDesc(&t);return fmt("3d%d/%x/%d",int(t.Format),t.BindFlags,int(t.Usage));}
    default:return "?";
    }
}
// Shader model, program type and a content hash of a DXBC container, so a
// failed shader can be told apart and matched to the game's own files.
inline std::string shaderText(const void* code,SIZE_T length,std::string* key=nullptr) {
    if(!code||!length){if(key)*key="none";return "bytecode=none";}
    auto bytes=static_cast<const unsigned char*>(code);
    uint64_t hash=fnv(code,size_t(std::min<SIZE_T>(length,16u<<20)));
    std::string model="?";
    auto word=[&](size_t at)->uint32_t{uint32_t v=0;if(at+4<=length)memcpy(&v,bytes+at,4);return v;};
    if(length>=32&&!memcmp(bytes,"DXBC",4)) {
        uint32_t chunks=word(28);
        for(uint32_t i=0;i<chunks&&i<64;++i) {
            size_t at=word(32+4*i);
            if(at+16>length)break;
            if(!memcmp(bytes+at,"SHDR",4)||!memcmp(bytes+at,"SHEX",4)) {
                uint32_t token=word(at+8);
                static const char* types[]={"ps","vs","gs","hs","ds","cs"};
                unsigned type=token>>16;
                model=fmt("%s_%u_%u",type<6?types[type]:"?",(token>>4)&0xf,token&0xf);
                break;
            }
        }
    } else model="not-dxbc";
    if(key)*key=fmt("%016llx",(unsigned long long)hash);
    return fmt("model=%s bytes=%zu hash=%016llx",model.c_str(),size_t(length),(unsigned long long)hash);
}

#define D3D11_DIAG_HOOK(Type,slot) auto call=reinterpret_cast<Type>(original(self,slot));if(!call)return E_FAIL;
#define D3D11_DIAG_FAILED(name,key,text) do{if(FAILED(hr))report(false,std::string(name)+" "+hex(hr)+" "+(key),std::string(name)+" hr="+hex(hr)+" "+(text));}while(0)

using DevicePtr=void*;
static HRESULT STDMETHODCALLTYPE createBuffer(DevicePtr self,const D3D11_BUFFER_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Buffer** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer**);
    D3D11_DIAG_HOOK(Fn,CreateBuffer)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateBuffer",fmt("%x/%d/%x/%x/%u/%d",d->BindFlags,int(d->Usage),d->MiscFlags,d->CPUAccessFlags,d->StructureByteStride,init!=nullptr),
        fmt("bytes=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x stride=%u init=%d",d->ByteWidth,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,d->StructureByteStride,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateBuffer","null","desc=null");
    // An indirect draw's arguments live in a buffer. When the guest supplies
    // them at creation the counts are visible here; a zero index/vertex count
    // is a draw that submits no work, which is what a silently missing layer
    // looks like when the submission is indirect.
    if(SUCCEEDED(hr)&&d&&(d->MiscFlags&D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)) {
        if(out&&*out)noteIndirectBuffer(*out,d->ByteWidth);
        std::string words="no-initial-data (written by the GPU or later)";
        if(init&&init->pSysMem) words=indirectArgsText(init->pSysMem,d->ByteWidth,"index");
        report(true,fmt("indirect args buffer %u/%x/%d/%s",d->ByteWidth,d->BindFlags,int(d->Usage),words.c_str()),
            fmt("indirect args buffer bytes=%u bind=0x%x usage=%d %s",d->ByteWidth,d->BindFlags,int(d->Usage),words.c_str()));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createTexture1D(DevicePtr self,const D3D11_TEXTURE1D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture1D** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_TEXTURE1D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture1D**);
    D3D11_DIAG_HOOK(Fn,CreateTexture1D)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateTexture1D",fmt("%d/%x/%d/%x/%x/%d",int(d->Format),d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr),
        fmt("fmt=%s width=%u mips=%u arr=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x init=%d",formatText(d->Format).c_str(),d->Width,d->MipLevels,d->ArraySize,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateTexture1D","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createTexture2D(DevicePtr self,const D3D11_TEXTURE2D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture2D** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
    D3D11_DIAG_HOOK(Fn,CreateTexture2D)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateTexture2D",fmt("%d/%x/%d/%x/%x/%u/%u/%d",int(d->Format),d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,d->SampleDesc.Count,d->ArraySize,init!=nullptr),
        fmt("fmt=%s %ux%u mips=%u arr=%u ms=%u/%u bind=0x%x usage=%d cpu=0x%x misc=0x%x init=%d",formatText(d->Format).c_str(),d->Width,d->Height,d->MipLevels,d->ArraySize,d->SampleDesc.Count,d->SampleDesc.Quality,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateTexture2D","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createTexture3D(DevicePtr self,const D3D11_TEXTURE3D_DESC* d,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture3D** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_TEXTURE3D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture3D**);
    D3D11_DIAG_HOOK(Fn,CreateTexture3D)HRESULT hr=call(self,d,init,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateTexture3D",fmt("%d/%x/%d/%x/%x/%d",int(d->Format),d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr),
        fmt("fmt=%s %ux%ux%u mips=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x init=%d",formatText(d->Format).c_str(),d->Width,d->Height,d->Depth,d->MipLevels,d->BindFlags,int(d->Usage),d->CPUAccessFlags,d->MiscFlags,init!=nullptr));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateTexture3D","null","desc=null");
    return hr;
}
// Views share one shape: (resource, optional desc, out). Only the view
// description differs.
template<int Slot,class Desc,class Out,class Describe>
static HRESULT viewCall(DevicePtr self,const char* name,ID3D11Resource* resource,const Desc* d,Out** out,Describe describe) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,const Desc*,Out**);
    D3D11_DIAG_HOOK(Fn,Slot)HRESULT hr=call(self,resource,d,out);
    if(FAILED(hr)) {
        std::string view,viewKey;describe(d,view,viewKey);
        D3D11_DIAG_FAILED(name,viewKey+"|"+resourceKey(resource),view+" "+resourceText(resource));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createSrv(DevicePtr self,ID3D11Resource* r,const D3D11_SHADER_RESOURCE_VIEW_DESC* d,ID3D11ShaderResourceView** out) {
    return viewCall<CreateShaderResourceView>(self,"CreateShaderResourceView",r,d,out,[](const D3D11_SHADER_RESOURCE_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createUav(DevicePtr self,ID3D11Resource* r,const D3D11_UNORDERED_ACCESS_VIEW_DESC* d,ID3D11UnorderedAccessView** out) {
    return viewCall<CreateUnorderedAccessView>(self,"CreateUnorderedAccessView",r,d,out,[](const D3D11_UNORDERED_ACCESS_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createRtv(DevicePtr self,ID3D11Resource* r,const D3D11_RENDER_TARGET_VIEW_DESC* d,ID3D11RenderTargetView** out) {
    return viewCall<CreateRenderTargetView>(self,"CreateRenderTargetView",r,d,out,[](const D3D11_RENDER_TARGET_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createDsv(DevicePtr self,ID3D11Resource* r,const D3D11_DEPTH_STENCIL_VIEW_DESC* d,ID3D11DepthStencilView** out) {
    return viewCall<CreateDepthStencilView>(self,"CreateDepthStencilView",r,d,out,[](const D3D11_DEPTH_STENCIL_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d flags=0x%x",formatText(v->Format).c_str(),int(v->ViewDimension),v->Flags);key=fmt("%d/%d/%x",int(v->Format),int(v->ViewDimension),v->Flags);});
}
static HRESULT STDMETHODCALLTYPE createInputLayout(DevicePtr self,const D3D11_INPUT_ELEMENT_DESC* elements,UINT count,const void* code,SIZE_T length,ID3D11InputLayout** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
    D3D11_DIAG_HOOK(Fn,CreateInputLayout)HRESULT hr=call(self,elements,count,code,length,out);
    if(FAILED(hr)) {
        std::string list,key;
        for(UINT i=0;i<count&&elements;++i) {
            if(i<12)list+=fmt("%s%u:%d@%u ",elements[i].SemanticName?elements[i].SemanticName:"?",elements[i].SemanticIndex,int(elements[i].Format),elements[i].InputSlot);
            key+=fmt("%s%u:%d:%u:%u,",elements[i].SemanticName?elements[i].SemanticName:"?",elements[i].SemanticIndex,int(elements[i].Format),elements[i].InputSlot,elements[i].AlignedByteOffset);
        }
        std::string shaderKey,shader=shaderText(code,length,&shaderKey);
        D3D11_DIAG_FAILED("CreateInputLayout",fmt("%016llx",(unsigned long long)fnv(key.data(),key.size()))+"/"+shaderKey,fmt("elements=%u [%s] %s",count,list.c_str(),shader.c_str()));
    }
    return hr;
}
template<int Slot>
static HRESULT STDMETHODCALLTYPE createShader(DevicePtr self,const void* code,SIZE_T length,ID3D11ClassLinkage* linkage,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const void*,SIZE_T,ID3D11ClassLinkage*,void**);
    D3D11_DIAG_HOOK(Fn,Slot)HRESULT hr=call(self,code,length,linkage,out);
    if(FAILED(hr)) {
        static const struct {int slot;const char* name;} names[]={{CreateVertexShader,"CreateVertexShader"},{CreateGeometryShader,"CreateGeometryShader"},
            {CreatePixelShader,"CreatePixelShader"},{CreateHullShader,"CreateHullShader"},{CreateDomainShader,"CreateDomainShader"},{CreateComputeShader,"CreateComputeShader"}};
        const char* name="CreateShader";for(auto& n:names) if(n.slot==Slot)name=n.name;
        std::string key,text=shaderText(code,length,&key);
        D3D11_DIAG_FAILED(name,key,text+fmt(" linkage=%d",linkage!=nullptr));
    } else {
        // 0 vs 1 gs 2 ps 3 hs 4 ds 5 cs: what the guest can actually create
        // says whether a missing terrain layer never had its shaders.
        static const struct {int slot;int index;} created[]={{CreateVertexShader,0},{CreateGeometryShader,1},{CreatePixelShader,2},
            {CreateHullShader,3},{CreateDomainShader,4},{CreateComputeShader,5}};
        for(auto& c:created) if(c.slot==Slot){shaderCreates[c.index].fetch_add(1,std::memory_order_relaxed);break;}
        tick();
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createGeometryShaderWithStreamOutput(DevicePtr self,const void* code,SIZE_T length,const D3D11_SO_DECLARATION_ENTRY* entries,UINT entryCount,
        const UINT* strides,UINT strideCount,UINT rasterized,ID3D11ClassLinkage* linkage,ID3D11GeometryShader** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**);
    D3D11_DIAG_HOOK(Fn,CreateGeometryShaderWithStreamOutput)HRESULT hr=call(self,code,length,entries,entryCount,strides,strideCount,rasterized,linkage,out);
    if(FAILED(hr)){std::string key,text=shaderText(code,length,&key);D3D11_DIAG_FAILED("CreateGeometryShaderWithStreamOutput",key,text+fmt(" so-entries=%u strides=%u rasterized=%u",entryCount,strideCount,rasterized));}
    return hr;
}
static HRESULT STDMETHODCALLTYPE createClassLinkage(DevicePtr self,ID3D11ClassLinkage** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D11ClassLinkage**);
    D3D11_DIAG_HOOK(Fn,CreateClassLinkage)HRESULT hr=call(self,out);
    D3D11_DIAG_FAILED("CreateClassLinkage","","");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createBlendState(DevicePtr self,const D3D11_BLEND_DESC* d,ID3D11BlendState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_BLEND_DESC*,ID3D11BlendState**);
    D3D11_DIAG_HOOK(Fn,CreateBlendState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d) {
        auto& t=d->RenderTarget[0];
        D3D11_DIAG_FAILED("CreateBlendState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
            fmt("a2c=%d independent=%d rt0:enable=%d src=%d dst=%d op=%d srca=%d dsta=%d opa=%d mask=0x%x",d->AlphaToCoverageEnable,d->IndependentBlendEnable,t.BlendEnable,
                int(t.SrcBlend),int(t.DestBlend),int(t.BlendOp),int(t.SrcBlendAlpha),int(t.DestBlendAlpha),int(t.BlendOpAlpha),t.RenderTargetWriteMask));
    } else if(FAILED(hr))D3D11_DIAG_FAILED("CreateBlendState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDepthStencilState(DevicePtr self,const D3D11_DEPTH_STENCIL_DESC* d,ID3D11DepthStencilState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_DEPTH_STENCIL_DESC*,ID3D11DepthStencilState**);
    D3D11_DIAG_HOOK(Fn,CreateDepthStencilState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateDepthStencilState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("depth=%d write=%d func=%d stencil=%d",d->DepthEnable,int(d->DepthWriteMask),int(d->DepthFunc),d->StencilEnable));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateDepthStencilState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createRasterizerState(DevicePtr self,const D3D11_RASTERIZER_DESC* d,ID3D11RasterizerState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_RASTERIZER_DESC*,ID3D11RasterizerState**);
    D3D11_DIAG_HOOK(Fn,CreateRasterizerState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateRasterizerState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("fill=%d cull=%d depthclip=%d scissor=%d multisample=%d aa-line=%d bias=%d",int(d->FillMode),int(d->CullMode),d->DepthClipEnable,d->ScissorEnable,d->MultisampleEnable,d->AntialiasedLineEnable,d->DepthBias));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateRasterizerState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createSamplerState(DevicePtr self,const D3D11_SAMPLER_DESC* d,ID3D11SamplerState** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_SAMPLER_DESC*,ID3D11SamplerState**);
    D3D11_DIAG_HOOK(Fn,CreateSamplerState)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateSamplerState",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("filter=0x%x address=%d/%d/%d aniso=%u cmp=%d lod=%g..%g",unsigned(d->Filter),int(d->AddressU),int(d->AddressV),int(d->AddressW),d->MaxAnisotropy,int(d->ComparisonFunc),d->MinLOD,d->MaxLOD));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateSamplerState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createBlendState1(DevicePtr self,const D3D11_BLEND_DESC1* d,ID3D11BlendState1** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_BLEND_DESC1*,ID3D11BlendState1**);
    D3D11_DIAG_HOOK(Fn,CreateBlendState1)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d) {
        auto& t=d->RenderTarget[0];
        D3D11_DIAG_FAILED("CreateBlendState1",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
            fmt("a2c=%d independent=%d rt0:enable=%d logicop=%d/%d src=%d dst=%d mask=0x%x",d->AlphaToCoverageEnable,d->IndependentBlendEnable,t.BlendEnable,t.LogicOpEnable,int(t.LogicOp),int(t.SrcBlend),int(t.DestBlend),t.RenderTargetWriteMask));
    } else if(FAILED(hr))D3D11_DIAG_FAILED("CreateBlendState1","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createRasterizerState1(DevicePtr self,const D3D11_RASTERIZER_DESC1* d,ID3D11RasterizerState1** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_RASTERIZER_DESC1*,ID3D11RasterizerState1**);
    D3D11_DIAG_HOOK(Fn,CreateRasterizerState1)HRESULT hr=call(self,d,out);
    if(FAILED(hr)&&d)D3D11_DIAG_FAILED("CreateRasterizerState1",fmt("%016llx",(unsigned long long)fnv(d,sizeof *d)),
        fmt("fill=%d cull=%d depthclip=%d scissor=%d forcedsamples=%u",int(d->FillMode),int(d->CullMode),d->DepthClipEnable,d->ScissorEnable,d->ForcedSampleCount));
    else if(FAILED(hr))D3D11_DIAG_FAILED("CreateRasterizerState1","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createQuery(DevicePtr self,const D3D11_QUERY_DESC* d,ID3D11Query** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_QUERY_DESC*,ID3D11Query**);
    D3D11_DIAG_HOOK(Fn,CreateQuery)HRESULT hr=call(self,d,out);
    if(FAILED(hr))D3D11_DIAG_FAILED("CreateQuery",d?fmt("%d/%x",int(d->Query),d->MiscFlags):"null",d?fmt("query=%d misc=0x%x",int(d->Query),d->MiscFlags):"desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createPredicate(DevicePtr self,const D3D11_QUERY_DESC* d,ID3D11Predicate** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D11_QUERY_DESC*,ID3D11Predicate**);
    D3D11_DIAG_HOOK(Fn,CreatePredicate)HRESULT hr=call(self,d,out);
    if(FAILED(hr))D3D11_DIAG_FAILED("CreatePredicate",d?fmt("%d/%x",int(d->Query),d->MiscFlags):"null",d?fmt("query=%d misc=0x%x",int(d->Query),d->MiscFlags):"desc=null");
    return hr;
}

// ---- immediate/deferred context: Map, the call with the most undocumented
// driver restrictions (NO_OVERWRITE on non-buffers, staging use), plus the
// calls that say what the guest draws. The draw hooks only count: they call
// the original entry with the arguments they were given and return its result.
inline bool patchContext(ID3D11DeviceContext* context);
static HRESULT STDMETHODCALLTYPE contextMap(DevicePtr self,ID3D11Resource* r,UINT subresource,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE* mapped) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
    auto call=reinterpret_cast<Fn>(original(self,ContextMap));if(!call)return E_FAIL;
    // A write-only Map of an indirect-arguments buffer is the guest uploading
    // the counts: remember the pointer and read it back at Unmap.
    int indirect=indirectIndex(r);
    HRESULT hr=call(self,r,subresource,type,flags,mapped);
    if(SUCCEEDED(hr)&&indirect>=0&&mapped&&mapped->pData&&
            (type==D3D11_MAP_WRITE||type==D3D11_MAP_WRITE_DISCARD||type==D3D11_MAP_WRITE_NO_OVERWRITE))
        indirectMapped[indirect].store(mapped->pData,std::memory_order_relaxed);
    if(FAILED(hr)&&hr!=DXGI_ERROR_WAS_STILL_DRAWING)   // expected with D3D11_MAP_FLAG_DO_NOT_WAIT
        D3D11_DIAG_FAILED("Map",fmt("%d/%x/%s",int(type),flags,resourceKey(r).c_str()),fmt("type=%d flags=0x%x subresource=%u %s",int(type),flags,subresource,resourceText(r).c_str()));
    return hr;
}
static void STDMETHODCALLTYPE contextUnmap(DevicePtr self,ID3D11Resource* r,UINT subresource) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,UINT);
    auto call=reinterpret_cast<Fn>(original(self,Unmap));if(!call)return;
    int indirect=indirectIndex(r);
    if(indirect>=0) {
        void* data=indirectMapped[indirect].exchange(nullptr,std::memory_order_relaxed);
        if(data) {
            // Read only what the buffer holds: a 16-byte DrawInstancedIndirect
            // or 12-byte DispatchIndirect upload must not be read as 20.
            std::string text=indirectArgsText(data,indirectBytes[indirect].load(std::memory_order_relaxed));
            report(true,fmt("indirect args upload %s",text.c_str()),fmt("indirect args upload %s",text.c_str()));
        }
    }
    call(self,r,subresource);
}
// One counter per draw, bucketed by the topology last set on this context and
// by whether a hull and a domain shader are bound there. A bucket seen for the
// first time is named once, so a summary that shows a kind the game never
// reaches is not silently missing. No allocation and no lock on the hot path.
inline void countDraw(void* self,int kind,unsigned count,unsigned instances) {
    ContextState& state=stateOf(self);
    unsigned t=state.topology.load(std::memory_order_relaxed);
    unsigned s=state.shaders.load(std::memory_order_relaxed);
    bool patch=patchList(t),hull=(s&3)==3;
    draws[kind][patch?1:0][hull?1:0].fetch_add(1,std::memory_order_relaxed);
    countDrawFormat(state.rtFormat.load(std::memory_order_relaxed));
    if(!count||(kind>=2&&kind<4&&!instances))zeroCounts[kind].fetch_add(1,std::memory_order_relaxed);
    static std::atomic<bool> named[kDrawKinds][2][2];
    if(!named[kind][patch?1:0][hull?1:0].exchange(true,std::memory_order_relaxed))
        emit(fmt("draw summary: first %s topology=%u(%s) hs/ds=%s",drawKindName(kind),t,topologyName(t).c_str(),
            hull?"both":((s&1)?"hs":((s&2)?"ds":"none"))));
    tick();
}
// The indirect kinds and DrawAuto carry no counts on the CPU side: the
// arguments live in a GPU buffer (or in the stream-output state), so the
// bucket is all that can be said.
inline void countDrawUnknown(void* self,int kind) {
    ContextState& state=stateOf(self);
    unsigned t=state.topology.load(std::memory_order_relaxed);
    unsigned s=state.shaders.load(std::memory_order_relaxed);
    bool patch=patchList(t),hull=(s&3)==3;
    draws[kind][patch?1:0][hull?1:0].fetch_add(1,std::memory_order_relaxed);
    countDrawFormat(state.rtFormat.load(std::memory_order_relaxed));
    static std::atomic<bool> named[kDrawKinds][2][2];
    if(!named[kind][patch?1:0][hull?1:0].exchange(true,std::memory_order_relaxed))
        emit(fmt("draw summary: first %s topology=%u(%s) hs/ds=%s",drawKindName(kind),t,topologyName(t).c_str(),
            hull?"both":((s&1)?"hs":((s&2)?"ds":"none"))));
    tick();
}
static void STDMETHODCALLTYPE contextDrawIndexed(DevicePtr self,UINT indexCount,UINT startIndex,INT baseVertex) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,INT);
    auto call=reinterpret_cast<Fn>(original(self,DrawIndexed));if(!call)return;
    countDraw(self,0,indexCount,1);call(self,indexCount,startIndex,baseVertex);
}
static void STDMETHODCALLTYPE contextDraw(DevicePtr self,UINT vertexCount,UINT startVertex) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT);
    auto call=reinterpret_cast<Fn>(original(self,Draw));if(!call)return;
    countDraw(self,1,vertexCount,1);call(self,vertexCount,startVertex);
}
static void STDMETHODCALLTYPE contextDrawIndexedInstanced(DevicePtr self,UINT indexCount,UINT instances,UINT startIndex,INT baseVertex,UINT startInstance) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT,INT,UINT);
    auto call=reinterpret_cast<Fn>(original(self,DrawIndexedInstanced));if(!call)return;
    countDraw(self,2,indexCount,instances);call(self,indexCount,instances,startIndex,baseVertex,startInstance);
}
static void STDMETHODCALLTYPE contextDrawInstanced(DevicePtr self,UINT vertexCount,UINT instances,UINT startVertex,UINT startInstance) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT,UINT);
    auto call=reinterpret_cast<Fn>(original(self,DrawInstanced));if(!call)return;
    countDraw(self,3,vertexCount,instances);call(self,vertexCount,instances,startVertex,startInstance);
}
// Draws whose counts the guest keeps on the GPU. They are the paths a
// tessellated or GPU-generated layer uses, and the direct-draw counters above
// cannot see them.
static void STDMETHODCALLTYPE contextDrawAuto(DevicePtr self) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr);
    auto call=reinterpret_cast<Fn>(original(self,DrawAuto));if(!call)return;
    countDrawUnknown(self,kDrawAuto);call(self);
}
static void STDMETHODCALLTYPE contextDrawIndexedInstancedIndirect(DevicePtr self,ID3D11Buffer* buffer,UINT offset) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Buffer*,UINT);
    auto call=reinterpret_cast<Fn>(original(self,DrawIndexedInstancedIndirect));if(!call)return;
    countDrawUnknown(self,kDrawIndexedIndirect);call(self,buffer,offset);
}
static void STDMETHODCALLTYPE contextDrawInstancedIndirect(DevicePtr self,ID3D11Buffer* buffer,UINT offset) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Buffer*,UINT);
    auto call=reinterpret_cast<Fn>(original(self,DrawInstancedIndirect));if(!call)return;
    countDrawUnknown(self,kDrawInstancedIndirect);call(self,buffer,offset);
}
static void STDMETHODCALLTYPE contextDispatchIndirect(DevicePtr self,ID3D11Buffer* buffer,UINT offset) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Buffer*,UINT);
    auto call=reinterpret_cast<Fn>(original(self,DispatchIndirect));if(!call)return;
    dispatchIndirects.fetch_add(1,std::memory_order_relaxed);
    call(self,buffer,offset);tick();
}
static void STDMETHODCALLTYPE contextIASetPrimitiveTopology(DevicePtr self,D3D11_PRIMITIVE_TOPOLOGY t) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,D3D11_PRIMITIVE_TOPOLOGY);
    auto call=reinterpret_cast<Fn>(original(self,IASetPrimitiveTopology));if(!call)return;
    stateOf(self).topology.store(unsigned(t),std::memory_order_relaxed);
    lastTopology.store(unsigned(t),std::memory_order_relaxed);
    call(self,t);
}
static void STDMETHODCALLTYPE contextHSSetShader(DevicePtr self,ID3D11HullShader* shader,ID3D11ClassInstance* const* instances,UINT count) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11HullShader*,ID3D11ClassInstance* const*,UINT);
    auto call=reinterpret_cast<Fn>(original(self,HSSetShader));if(!call)return;
    auto& s=stateOf(self).shaders;
    if(shader)s.fetch_or(1,std::memory_order_relaxed);else s.fetch_and(~1u,std::memory_order_relaxed);
    call(self,shader,instances,count);
}
static void STDMETHODCALLTYPE contextDSSetShader(DevicePtr self,ID3D11DomainShader* shader,ID3D11ClassInstance* const* instances,UINT count) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11DomainShader*,ID3D11ClassInstance* const*,UINT);
    auto call=reinterpret_cast<Fn>(original(self,DSSetShader));if(!call)return;
    auto& s=stateOf(self).shaders;
    if(shader)s.fetch_or(2,std::memory_order_relaxed);else s.fetch_and(~2u,std::memory_order_relaxed);
    call(self,shader,instances,count);
}
// Describes the render target views of an OM set once per distinct view. The
// view table is fixed: once it is full, later views are counted but not
// described, and nothing is emitted for them. (Emitting on every unseen view
// instead flooded a real run with 35k lines once the table filled.)
inline void noteRenderTargets(UINT count,ID3D11RenderTargetView* const* views) {
    if(!count||!views)return;
    rtvSets.fetch_add(1,std::memory_order_relaxed);
    for(UINT i=0;i<count;++i) {
        ID3D11RenderTargetView* view=views[i];if(!view)continue;
        bool known=false;
        for(auto& seen:rtvViews) if(seen.load(std::memory_order_relaxed)==view){known=true;break;}
        if(known)continue;
        std::string text=rtvText(view);
        if(text=="unknown"||text=="no-resource"){rtvUnknown.fetch_add(1,std::memory_order_relaxed);continue;}
        bool registered=false;
        for(auto& seen:rtvViews) {
            void* expected=nullptr;
            if(seen.compare_exchange_strong(expected,view,std::memory_order_acq_rel)){registered=true;break;}
            if(expected==view){registered=true;break;}   // another thread stored it first
        }
        if(!registered){rtvUncached.fetch_add(1,std::memory_order_relaxed);continue;}
        bool fresh=false;
        {
            std::lock_guard<std::mutex> lock(rtvM);
            fresh=noteKind(rtvKinds,text,kMaxRtvKinds);
        }
        if(fresh)emit(fmt("draw summary: first render target %s",text.c_str()));
    }
}
// The first view's format is what the draw-format counter buckets by: it is
// the target the layer writes, and a summary that shows no draws into the
// swapchain's format says the layer never reaches the frame.
inline void noteRenderTargetFormat(void* self,UINT count,ID3D11RenderTargetView* const* views) {
    ContextState& state=stateOf(self);
    // An unbind (no view, or a null first slot) leaves no target: the next
    // draws must not be bucketed as the format that was bound before, or the
    // summary would report swapchain-format work that never reached it.
    if(!count||!views||!views[0]){state.rtFormat.store(0,std::memory_order_relaxed);return;}
    ID3D11RenderTargetView* view=views[0];
    size_t slot=(reinterpret_cast<uintptr_t>(view)>>4)&(kFormatCache-1);
    unsigned format=0;
    if(state.formatView[slot].load(std::memory_order_relaxed)!=view) {
        D3D11_RENDER_TARGET_VIEW_DESC v{};
        view->GetDesc(&v);
        format=unsigned(v.Format);
        state.formatValue[slot].store(format,std::memory_order_relaxed);
        state.formatView[slot].store(view,std::memory_order_release);
    } else format=state.formatValue[slot].load(std::memory_order_relaxed);
    state.rtFormat.store(format,std::memory_order_relaxed);
}
static void STDMETHODCALLTYPE contextOMSetRenderTargets(DevicePtr self,UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
    auto call=reinterpret_cast<Fn>(original(self,OMSetRenderTargets));if(!call)return;
    noteRenderTargets(count,views);
    noteRenderTargetFormat(self,count,views);
    call(self,count,views,depth);
}
// The same bind with UAVs: a layer that writes through UAVs (or renders into a
// target it later reads back) binds its targets this way, not through
// OMSetRenderTargets.
static void STDMETHODCALLTYPE contextOMSetRenderTargetsAndUnorderedAccessViews(DevicePtr self,UINT count,ID3D11RenderTargetView* const* views,
        ID3D11DepthStencilView* depth,UINT uavStart,UINT uavCount,ID3D11UnorderedAccessView* const* uavs,const UINT* initialCounts) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
    auto call=reinterpret_cast<Fn>(original(self,OMSetRenderTargetsAndUnorderedAccessViews));if(!call)return;
    rtvUavSets.fetch_add(1,std::memory_order_relaxed);
    if(uavCount&&uavs&&uavCount!=D3D11_KEEP_UNORDERED_ACCESS_VIEWS)uavSets.fetch_add(1,std::memory_order_relaxed);
    // D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL (0xffffffff) means "leave the
    // bound targets alone": it is not a view count, and walking the array that
    // many times would read far past it. Binding UAVs while keeping the
    // targets is exactly the path a UAV-writing layer takes.
    if(count!=D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) {
        noteRenderTargets(count,views);
        noteRenderTargetFormat(self,count,views);
    }
    call(self,count,views,depth,uavStart,uavCount,uavs,initialCounts);tick();
}
// Null entries in a shader-resource bind are textures the guest never set: a
// layer that samples one renders black without any call failing.
template<int Slot,int Stage>
static void STDMETHODCALLTYPE contextSetShaderResources(DevicePtr self,UINT start,UINT count,ID3D11ShaderResourceView* const* views) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,ID3D11ShaderResourceView* const*);
    auto call=reinterpret_cast<Fn>(original(self,Slot));if(!call)return;
    srvSets[Stage].fetch_add(1,std::memory_order_relaxed);
    if(!views||!count)srvNulls[Stage].fetch_add(1,std::memory_order_relaxed);
    else for(UINT i=0;i<count;++i) if(!views[i]){srvNulls[Stage].fetch_add(1,std::memory_order_relaxed);break;}
    call(self,start,count,views);
}
static void STDMETHODCALLTYPE contextDispatch(DevicePtr self,UINT x,UINT y,UINT z) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT);
    auto call=reinterpret_cast<Fn>(original(self,Dispatch));if(!call)return;
    dispatches.fetch_add(1,std::memory_order_relaxed);
    if(!x||!y||!z)dispatchZero.fetch_add(1,std::memory_order_relaxed);
    call(self,x,y,z);tick();
}
static void STDMETHODCALLTYPE contextClearRenderTargetView(DevicePtr self,ID3D11RenderTargetView* view,const FLOAT color[4]) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11RenderTargetView*,const FLOAT*);
    auto call=reinterpret_cast<Fn>(original(self,ClearRenderTargetView));if(!call)return;
    clears[0].fetch_add(1,std::memory_order_relaxed);call(self,view,color);
}
static void STDMETHODCALLTYPE contextClearDepthStencilView(DevicePtr self,ID3D11DepthStencilView* view,UINT flags,FLOAT depth,UINT8 stencil) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
    auto call=reinterpret_cast<Fn>(original(self,ClearDepthStencilView));if(!call)return;
    clears[1].fetch_add(1,std::memory_order_relaxed);call(self,view,flags,depth,stencil);
}
// A layer rendered at one size and resolved/copied into the frame is the other
// way a target's content reaches the screen without a direct draw into it.
static void STDMETHODCALLTYPE contextResolveSubresource(DevicePtr self,ID3D11Resource* dst,UINT dstSub,ID3D11Resource* src,UINT srcSub,DXGI_FORMAT format) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,UINT,ID3D11Resource*,UINT,DXGI_FORMAT);
    auto call=reinterpret_cast<Fn>(original(self,ResolveSubresource));if(!call)return;
    resolves.fetch_add(1,std::memory_order_relaxed);call(self,dst,dstSub,src,srcSub,format);
}
static void STDMETHODCALLTYPE contextCopyResource(DevicePtr self,ID3D11Resource* dst,ID3D11Resource* src) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,ID3D11Resource*);
    auto call=reinterpret_cast<Fn>(original(self,CopyResource));if(!call)return;
    copies.fetch_add(1,std::memory_order_relaxed);call(self,dst,src);
}
static void STDMETHODCALLTYPE contextCopySubresourceRegion(DevicePtr self,ID3D11Resource* dst,UINT dstSub,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT srcSub,const D3D11_BOX* box) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
    auto call=reinterpret_cast<Fn>(original(self,CopySubresourceRegion));if(!call)return;
    copies.fetch_add(1,std::memory_order_relaxed);call(self,dst,dstSub,x,y,z,src,srcSub,box);
}
// A draw under a predicate object is skipped when the predicate is false: a
// layer that is culled this way submits no work at all.
static void STDMETHODCALLTYPE contextSetPredication(DevicePtr self,ID3D11Predicate* predicate,BOOL value) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Predicate*,BOOL);
    auto call=reinterpret_cast<Fn>(original(self,SetPredication));if(!call)return;
    if(predicate)predicated.fetch_add(1,std::memory_order_relaxed);
    call(self,predicate,value);
}
// An indirect draw's arguments are uploaded, not passed: reading them where
// the guest writes them is the only way to tell a zero-count submission (a
// layer that draws nothing) from a real one.
static void STDMETHODCALLTYPE contextUpdateSubresource(DevicePtr self,ID3D11Resource* resource,UINT subresource,const D3D11_BOX* box,const void* data,UINT rowPitch,UINT depthPitch) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
    auto call=reinterpret_cast<Fn>(original(self,UpdateSubresource));if(!call)return;
    if(data&&isIndirectBuffer(resource)) {
        // The source is only as large as the buffer it fills (or the box it
        // fills), so read no more than that.
        size_t bytes=indirectBufferBytes(resource);
        if(box&&box->right>box->left) bytes=std::min<size_t>(bytes,size_t(box->right-box->left));
        std::string text=indirectArgsText(data,bytes);
        report(true,fmt("indirect args upload %s",text.c_str()),fmt("indirect args upload %s",text.c_str()));
    }
    call(self,resource,subresource,box,data,rowPitch,depthPitch);
}
static HRESULT STDMETHODCALLTYPE contextFinishCommandList(DevicePtr self,BOOL restore,ID3D11CommandList** list) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,BOOL,ID3D11CommandList**);
    auto call=reinterpret_cast<Fn>(original(self,FinishCommandList));if(!call)return E_FAIL;
    HRESULT hr=call(self,restore,list);
    if(SUCCEEDED(hr))commandLists.fetch_add(1,std::memory_order_relaxed);
    D3D11_DIAG_FAILED("FinishCommandList",fmt("%d",restore!=0),fmt("restore=%d",restore!=0));
    return hr;
}
// Executed lists carry draws recorded on a deferred context: the draw hooks
// already counted them at recording time, so this only reports the plumbing.
static void STDMETHODCALLTYPE contextExecuteCommandList(DevicePtr self,ID3D11CommandList* list,BOOL restore) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D11CommandList*,BOOL);
    auto call=reinterpret_cast<Fn>(original(self,ExecuteCommandList));if(!call)return;
    executedLists.fetch_add(1,std::memory_order_relaxed);call(self,list,restore);tick();
}
static HRESULT STDMETHODCALLTYPE createDeferredContext(DevicePtr self,UINT flags,ID3D11DeviceContext** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D11DeviceContext**);
    D3D11_DIAG_HOOK(Fn,CreateDeferredContext)HRESULT hr=call(self,flags,out);
    if(SUCCEEDED(hr)&&out&&*out){deferredContexts.fetch_add(1,std::memory_order_relaxed);patchContext(*out);}
    D3D11_DIAG_FAILED("CreateDeferredContext",fmt("%x",flags),fmt("flags=0x%x",flags));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDeferredContext1(DevicePtr self,UINT flags,ID3D11DeviceContext1** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D11DeviceContext1**);
    D3D11_DIAG_HOOK(Fn,CreateDeferredContext1)HRESULT hr=call(self,flags,out);
    if(SUCCEEDED(hr)&&out&&*out){deferredContexts.fetch_add(1,std::memory_order_relaxed);patchContext(*out);}
    D3D11_DIAG_FAILED("CreateDeferredContext1",fmt("%x",flags),fmt("flags=0x%x",flags));
    return hr;
}
inline const Patch* contextPatches(size_t& count){
    static const Patch table[]={
        {ContextMap,(void*)&contextMap},{Unmap,(void*)&contextUnmap},{DrawIndexed,(void*)&contextDrawIndexed},{Draw,(void*)&contextDraw},
        {DrawIndexedInstanced,(void*)&contextDrawIndexedInstanced},{DrawInstanced,(void*)&contextDrawInstanced},
        {DrawAuto,(void*)&contextDrawAuto},{DrawIndexedInstancedIndirect,(void*)&contextDrawIndexedInstancedIndirect},
        {DrawInstancedIndirect,(void*)&contextDrawInstancedIndirect},{DispatchIndirect,(void*)&contextDispatchIndirect},
        {IASetPrimitiveTopology,(void*)&contextIASetPrimitiveTopology},{HSSetShader,(void*)&contextHSSetShader},
        {DSSetShader,(void*)&contextDSSetShader},{OMSetRenderTargets,(void*)&contextOMSetRenderTargets},
        {OMSetRenderTargetsAndUnorderedAccessViews,(void*)&contextOMSetRenderTargetsAndUnorderedAccessViews},
        {PSSetShaderResources,(void*)&contextSetShaderResources<PSSetShaderResources,1>},
        {VSSetShaderResources,(void*)&contextSetShaderResources<VSSetShaderResources,0>},
        {HSSetShaderResources,(void*)&contextSetShaderResources<HSSetShaderResources,2>},
        {DSSetShaderResources,(void*)&contextSetShaderResources<DSSetShaderResources,3>},
        {SetPredication,(void*)&contextSetPredication},{UpdateSubresource,(void*)&contextUpdateSubresource},
        {CopyResource,(void*)&contextCopyResource},{CopySubresourceRegion,(void*)&contextCopySubresourceRegion},
        {ResolveSubresource,(void*)&contextResolveSubresource},
        {Dispatch,(void*)&contextDispatch},{ClearRenderTargetView,(void*)&contextClearRenderTargetView},
        {ClearDepthStencilView,(void*)&contextClearDepthStencilView},{FinishCommandList,(void*)&contextFinishCommandList},
        {ExecuteCommandList,(void*)&contextExecuteCommandList}};
    count=sizeof table/sizeof *table;return table;
}
inline bool patchContext(ID3D11DeviceContext* context) {
    if(!context||!contextSeen.load(std::memory_order_acquire))return false;
    void** vtable=*reinterpret_cast<void***>(context);
    {
        std::lock_guard<std::mutex> lock(patchM);
        bool known=false;
        for(auto* v:contextVtables) known|=v==vtable;
        if(!known&&contextVtables.size()<kMaxClasses){contextVtables.push_back(vtable);contextsPatched.fetch_add(1,std::memory_order_relaxed);}
    }
    resetContext(context);
    size_t count=0;const Patch* patches=contextPatches(count);
    if(!patchClass(vtable,patches,count))return false;
    summary();
    return true;
}

// ---- capability queries
inline std::string bytesText(const void* data,UINT size) {
    std::string out;auto p=static_cast<const unsigned char*>(data);
    for(UINT i=0;i<size&&i<32;++i)out+=fmt("%02x",p[i]);
    return out.empty()?"-":out;
}
static HRESULT STDMETHODCALLTYPE checkFormatSupport(DevicePtr self,DXGI_FORMAT format,UINT* support) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,DXGI_FORMAT,UINT*);
    D3D11_DIAG_HOOK(Fn,CheckFormatSupport)HRESULT hr=call(self,format,support);
    UINT bits=SUCCEEDED(hr)&&support?*support:0;
    if(FAILED(hr)||!bits)D3D11_DIAG_FAILED("CheckFormatSupport",fmt("%d/%x",int(format),bits),fmt("fmt=%s support=0x%x UNSUPPORTED",formatText(format).c_str(),bits));
    else report(true,fmt("CheckFormatSupport %d/%x",int(format),bits),fmt("CheckFormatSupport fmt=%s support=0x%x [%s]",formatText(format).c_str(),bits,supportText(bits).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE checkMultisampleQualityLevels(DevicePtr self,DXGI_FORMAT format,UINT samples,UINT* levels) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,DXGI_FORMAT,UINT,UINT*);
    D3D11_DIAG_HOOK(Fn,CheckMultisampleQualityLevels)HRESULT hr=call(self,format,samples,levels);
    UINT n=SUCCEEDED(hr)&&levels?*levels:0;
    if(FAILED(hr)||!n)D3D11_DIAG_FAILED("CheckMultisampleQualityLevels",fmt("%d/%u",int(format),samples),fmt("fmt=%s samples=%u levels=%u UNSUPPORTED",formatText(format).c_str(),samples,n));
    return hr;
}
static HRESULT STDMETHODCALLTYPE checkFeatureSupport(DevicePtr self,D3D11_FEATURE feature,void* data,UINT size) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D11_FEATURE,void*,UINT);
    D3D11_DIAG_HOOK(Fn,CheckFeatureSupport)HRESULT hr=call(self,feature,data,size);
    if(FAILED(hr))D3D11_DIAG_FAILED("CheckFeatureSupport",fmt("%d/%u",int(feature),size),fmt("feature=%d size=%u",int(feature),size));
    else report(true,fmt("CheckFeatureSupport %d/%u/%s",int(feature),size,bytesText(data,size).c_str()),
        fmt("CheckFeatureSupport feature=%d size=%u data=%s",int(feature),size,bytesText(data,size).c_str()));
    return hr;
}

// The hooks every ID3D11Device vtable gets, and the ones only an
// ID3D11Device1-or-later vtable (a superset) may get.
inline const Patch* basePatches(size_t& count){
    static const Patch table[]={
        {CreateBuffer,(void*)&createBuffer},{CreateTexture1D,(void*)&createTexture1D},{CreateTexture2D,(void*)&createTexture2D},
        {CreateTexture3D,(void*)&createTexture3D},{CreateShaderResourceView,(void*)&createSrv},{CreateUnorderedAccessView,(void*)&createUav},
        {CreateRenderTargetView,(void*)&createRtv},{CreateDepthStencilView,(void*)&createDsv},{CreateInputLayout,(void*)&createInputLayout},
        {CreateVertexShader,(void*)&createShader<CreateVertexShader>},{CreateGeometryShader,(void*)&createShader<CreateGeometryShader>},
        {CreateGeometryShaderWithStreamOutput,(void*)&createGeometryShaderWithStreamOutput},{CreatePixelShader,(void*)&createShader<CreatePixelShader>},
        {CreateHullShader,(void*)&createShader<CreateHullShader>},{CreateDomainShader,(void*)&createShader<CreateDomainShader>},
        {CreateComputeShader,(void*)&createShader<CreateComputeShader>},{CreateClassLinkage,(void*)&createClassLinkage},
        {CreateBlendState,(void*)&createBlendState},{CreateDepthStencilState,(void*)&createDepthStencilState},
        {CreateRasterizerState,(void*)&createRasterizerState},{CreateSamplerState,(void*)&createSamplerState},{CreateQuery,(void*)&createQuery},
        {CreatePredicate,(void*)&createPredicate},{CreateDeferredContext,(void*)&createDeferredContext},{CheckFormatSupport,(void*)&checkFormatSupport},
        {CheckMultisampleQualityLevels,(void*)&checkMultisampleQualityLevels},{CheckFeatureSupport,(void*)&checkFeatureSupport}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* extendedPatches(size_t& count){
    static const Patch table[]={
        {CreateDeferredContext1,(void*)&createDeferredContext1},{CreateBlendState1,(void*)&createBlendState1},{CreateRasterizerState1,(void*)&createRasterizerState1}};
    count=sizeof table/sizeof *table;return table;
}
// ---- entry points
inline void configure(Sink s){sink=s;}
// Called with every device Play gives a guest. Patches the device class and
// the immediate context; logs the adapter the guest can see and the
// capability answers that pick engine code paths.
inline void adopt(ID3D11Device* device) {
    if(!device||!sink)return;
    size_t baseCount=0,extendedCount=0;
    const Patch* base=basePatches(baseCount);const Patch* extended=extendedPatches(extendedCount);
    bool ok=patchClass(*reinterpret_cast<void***>(device),base,baseCount);
    // ID3D11Device1's vtable continues ID3D11Device's: the base hooks plus the
    // 11.1 creators go on every vtable QueryInterface returns for it.
    ID3D11Device1* device1=nullptr;
    if(SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(&device1)))&&device1) {
        void** vtable=*reinterpret_cast<void***>(device1);
        patchClass(vtable,base,baseCount);patchClass(vtable,extended,extendedCount);
        device1->Release();
    }
    ID3D11DeviceContext* context=nullptr;device->GetImmediateContext(&context);
    if(context){contextSeen.store(true,std::memory_order_release);patchContext(context);context->Release();}
    if(!ok)return;
    emit(fmt("diagnostics on: failed Create*/Check*/Map calls are logged once per distinct call (caps %zu/%zu); draws are summarised every %llu s",
        kMaxFailures,kMaxProbes,(unsigned long long)(kSummaryMs/1000)));
    IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;
    if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi)))&&dxgi&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&adapter) {
        DXGI_ADAPTER_DESC d{};
        if(SUCCEEDED(adapter->GetDesc(&d))) {
            char name[128]{};WideCharToMultiByte(CP_UTF8,0,d.Description,-1,name,sizeof name-1,nullptr,nullptr);
            report(true,fmt("adapter %04x/%04x",d.VendorId,d.DeviceId),fmt("adapter vendor=0x%04x device=0x%04x rev=%u name=\"%s\" vram=%zuMB sysmem=%zuMB shared=%zuMB",
                d.VendorId,d.DeviceId,d.Revision,name,size_t(d.DedicatedVideoMemory>>20),size_t(d.DedicatedSystemMemory>>20),size_t(d.SharedSystemMemory>>20)));
        }
    }
    if(adapter)adapter->Release();
    if(dxgi)dxgi->Release();
    report(true,"device feature level",fmt("device feature level=0x%x creation flags=0x%x",unsigned(device->GetFeatureLevel()),device->GetCreationFlags()));
    // Capability answers games branch on. Unsupported ones log as failures.
    D3D11_FEATURE_DATA_THREADING threading{};device->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof threading);
    D3D11_FEATURE_DATA_DOUBLES doubles{};device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES,&doubles,sizeof doubles);
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS tenx{};device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS,&tenx,sizeof tenx);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof options);
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO architecture{};device->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO,&architecture,sizeof architecture);
    D3D11_FEATURE_DATA_D3D9_OPTIONS nine{};device->CheckFeatureSupport(D3D11_FEATURE_D3D9_OPTIONS,&nine,sizeof nine);
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 options1{};device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1,&options1,sizeof options1);
    // Formats terrain/height-map and compressed-texture paths probe for.
    static const DXGI_FORMAT formats[]={DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,
        DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC3_UNORM,DXGI_FORMAT_BC5_UNORM,
        DXGI_FORMAT_BC6H_UF16,DXGI_FORMAT_BC7_UNORM,DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_R8_UNORM};
    for(auto format:formats){UINT s=0;device->CheckFormatSupport(format,&s);}
}
#undef D3D11_DIAG_HOOK
#undef D3D11_DIAG_FAILED
}
