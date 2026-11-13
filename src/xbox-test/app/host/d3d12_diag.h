// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostics for the D3D12 device Play hands a guest. A D3D12 game fails
// silently in ways a D3D11 one does not: a rejected feature query picks a
// different renderer, a failed root-signature or pipeline-state creation
// leaves the frame black, and a swap chain created with the wrong device
// argument is simply never presented. Nothing logged those. Like
// d3d11_diag.h, the vtable entries that create the device's objects, query
// features and submit work are redirected to wrappers that call the original
// entry and then log one line per distinct (method, hr, key parameters)
// through the probe sink, up to a cap. Results and arguments pass through
// unchanged.
// The patch is per vtable (every object of a class shares it), so several
// devices are safe. Only interfaces reached through a successful
// QueryInterface are patched at their level, so a vtable that stops at
// ID3D12Device never gets a slot its interface does not define.
//
// The swap chain is the other half of a D3D12 guest: DXGI requires the device
// argument of CreateSwapChainForHwnd to be the command queue, not the device,
// and the chain must be flip-model. play_graphics.h already routes a guest
// HWND to the CoreWindow and strips the frame-latency waitable object; the
// queue argument passes through untouched, so nothing here has to rewrite it.
#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>

namespace d3d12_diag {
using Sink=void(*)(const char*);
inline Sink sink=nullptr;
// Opt-in D3D12 debug layer: set once at boot from LocalState\d3d12debug.txt
// (launcher main.cpp). Off by default, so the guest's D3D12GetDebugInterface
// answers DXGI_ERROR_SDK_COMPONENT_MISSING and the layer is never enabled.
inline std::atomic<bool> debugLayerOptIn{false};
constexpr size_t kMaxFailures=200;   // distinct failed calls logged
constexpr size_t kMaxProbes=160;     // distinct capability answers logged
constexpr int kSlots=64;
constexpr size_t kMaxClasses=24;

// ---- vtable slots (ID3D12Device and the objects it creates)
enum DeviceSlot {
    GetNodeCount=7,CreateCommandQueue,CreateCommandAllocator,CreateGraphicsPipelineState,
    CreateComputePipelineState,CreateCommandList,CheckFeatureSupport,CreateDescriptorHeap,
    GetDescriptorHandleIncrementSize,CreateRootSignature,CreateConstantBufferView,
    CreateShaderResourceView,CreateUnorderedAccessView,CreateRenderTargetView,
    CreateDepthStencilView,CreateSampler,CopyDescriptors,CopyDescriptorsSimple,
    GetResourceAllocationInfo,GetCustomHeapProperties,CreateCommittedResource,CreateHeap,
    CreatePlacedResource,CreateReservedResource,CreateSharedHandle,OpenSharedHandle,
    OpenSharedHandleByName,MakeResident,Evict,CreateFence,GetDeviceRemovedReason,
    GetCopyableFootprints,CreateQueryHeap,SetStablePowerState,CreateCommandSignature,
    GetResourceTiling,GetAdapterLuid
};
enum QueueSlot {QueueExecuteCommandLists=10,QueueSetMarker,QueueBeginEvent,QueueEndEvent,QueueSignal,QueueWait};
enum ListSlot {ListClose=9,ListReset,ListClearState,ListDrawInstanced,ListDrawIndexedInstanced,
    ListDispatch,ListCopyBufferRegion,ListCopyTextureRegion,ListCopyResource,ListCopyTiles,
    ListResolveSubresource,ListIASetPrimitiveTopology,ListRSSetViewports,ListRSSetScissorRects,
    ListOMSetBlendFactor,ListOMSetStencilRef,ListSetPipelineState,ListResourceBarrier,
    ListExecuteBundle,ListSetDescriptorHeaps,ListSetComputeRootSignature,ListSetGraphicsRootSignature,
    ListSetComputeRootDescriptorTable,ListSetGraphicsRootDescriptorTable,ListSetComputeRoot32BitConstant,
    ListSetGraphicsRoot32BitConstant,ListSetComputeRoot32BitConstants,ListSetGraphicsRoot32BitConstants,
    ListSetComputeRootConstantBufferView,ListSetGraphicsRootConstantBufferView,
    ListSetComputeRootShaderResourceView,ListSetGraphicsRootShaderResourceView,
    ListSetComputeRootUnorderedAccessView,ListSetGraphicsRootUnorderedAccessView,
    ListIASetIndexBuffer,ListIASetVertexBuffers,ListSOSetTargets,ListOMSetRenderTargets,
    ListClearDepthStencilView,ListClearRenderTargetView,ListClearUnorderedAccessViewUint,
    ListClearUnorderedAccessViewFloat,ListDiscardResource,ListBeginQuery,ListEndQuery,
    ListResolveQueryData,ListSetPredication,ListExecuteIndirect};
enum FenceSlot {FenceGetCompletedValue=8,FenceSetEventOnCompletion,FenceSignal};
enum ResourceSlot {ResourceMap=8,ResourceUnmap,ResourceGetDesc,ResourceGetGPUVirtualAddress};
enum AllocatorSlot {AllocatorReset=8};
// CreatePipelineState (the stream path) is ID3D12Device2's method. Its slot is
// checked against the generated ID3D12Device2Vtbl by tests/d3d12-vtbl-layout.cpp,
// and that hook is only written through a pointer that implements ID3D12Device2.
constexpr int createPipelineStateStreamSlot=47;
static_assert(CreateCommandQueue==8&&CheckFeatureSupport==13&&CreateFence==36&&GetAdapterLuid==43,"ID3D12Device slot layout");
static_assert(createPipelineStateStreamSlot==47,"ID3D12Device2 CreatePipelineState slot");
static_assert(QueueExecuteCommandLists==10&&QueueSignal==14&&QueueWait==15,"ID3D12CommandQueue slot layout");
static_assert(ListClose==9&&ListResourceBarrier==26&&ListOMSetRenderTargets==46&&ListExecuteIndirect==56,"ID3D12GraphicsCommandList slot layout");
static_assert(FenceSetEventOnCompletion==9&&FenceSignal==10,"ID3D12Fence slot layout");
static_assert(ResourceMap==8&&AllocatorReset==8,"ID3D12Resource/ID3D12CommandAllocator slot layout");

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
    char b[1024];va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);return b;
}
inline std::string u64(unsigned long long v){return fmt("%llu",v);}
inline const char* formatName(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_UNKNOWN:return "UNKNOWN";
    case DXGI_FORMAT_R32G32B32A32_FLOAT:return "R32G32B32A32_FLOAT";case DXGI_FORMAT_R32G32B32_FLOAT:return "R32G32B32_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:return "R16G16B16A16_FLOAT";case DXGI_FORMAT_R16G16B16A16_UNORM:return "R16G16B16A16_UNORM";
    case DXGI_FORMAT_R32G32_FLOAT:return "R32G32_FLOAT";case DXGI_FORMAT_R10G10B10A2_UNORM:return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R11G11B10_FLOAT:return "R11G11B10_FLOAT";case DXGI_FORMAT_R8G8B8A8_TYPELESS:return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM:return "R8G8B8A8_UNORM";case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R16G16_FLOAT:return "R16G16_FLOAT";case DXGI_FORMAT_R16G16_UNORM:return "R16G16_UNORM";
    // Typeless formats share their value with the depth format of the same
    // footprint, so each pair gets one case.
    case DXGI_FORMAT_R32_TYPELESS:return "R32_TYPELESS_or_D32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT:return "R32_FLOAT";case DXGI_FORMAT_R32_UINT:return "R32_UINT";
    case DXGI_FORMAT_R24G8_TYPELESS:return "R24G8_TYPELESS_or_D24_UNORM_S8_UINT";
    case DXGI_FORMAT_R8G8_UNORM:return "R8G8_UNORM";
    case DXGI_FORMAT_R16_TYPELESS:return "R16_TYPELESS_or_D16_UNORM";
    case DXGI_FORMAT_R16_UNORM:return "R16_UNORM";case DXGI_FORMAT_R8_UNORM:return "R8_UNORM";
    case DXGI_FORMAT_A8_UNORM:return "A8_UNORM";case DXGI_FORMAT_BC1_UNORM:return "BC1_UNORM";
    case DXGI_FORMAT_BC3_UNORM:return "BC3_UNORM";case DXGI_FORMAT_BC5_UNORM:return "BC5_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM:return "B8G8R8A8_UNORM";case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_BC6H_UF16:return "BC6H_UF16";case DXGI_FORMAT_BC7_UNORM:return "BC7_UNORM";
    default:return "";
    }
}
inline std::string formatText(DXGI_FORMAT f) {
    const char* n=formatName(f);return *n?fmt("%d(%s)",int(f),n):fmt("%d",int(f));
}
inline const char* resourceStateName(D3D12_RESOURCE_STATES s) {
    if(s==0)return "COMMON_or_PRESENT";   // both are zero
    switch(s) {
    case D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:return "VB_CB";
    case D3D12_RESOURCE_STATE_INDEX_BUFFER:return "IB";
    case D3D12_RESOURCE_STATE_RENDER_TARGET:return "RT";
    case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:return "UAV";
    case D3D12_RESOURCE_STATE_DEPTH_WRITE:return "DS_WRITE";
    case D3D12_RESOURCE_STATE_DEPTH_READ:return "DS_READ";
    case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:return "SRV_NP";
    case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:return "SRV_P";
    case D3D12_RESOURCE_STATE_COPY_DEST:return "COPY_DEST";
    case D3D12_RESOURCE_STATE_COPY_SOURCE:return "COPY_SRC";
    default:return "?";
    }
}
inline const char* featureName(D3D12_FEATURE f) {
    switch(int(f)) {
    case D3D12_FEATURE_FEATURE_LEVELS:return "FEATURE_LEVELS";
    case D3D12_FEATURE_ARCHITECTURE:return "ARCHITECTURE";
    case D3D12_FEATURE_SHADER_MODEL:return "SHADER_MODEL";
    case D3D12_FEATURE_D3D12_OPTIONS:return "OPTIONS";
    case D3D12_FEATURE_D3D12_OPTIONS1:return "OPTIONS1";
    case D3D12_FEATURE_D3D12_OPTIONS2:return "OPTIONS2";
    case D3D12_FEATURE_D3D12_OPTIONS3:return "OPTIONS3";
    case D3D12_FEATURE_D3D12_OPTIONS4:return "OPTIONS4";
    case D3D12_FEATURE_D3D12_OPTIONS5:return "OPTIONS5";
    case D3D12_FEATURE_D3D12_OPTIONS6:return "OPTIONS6";
    case D3D12_FEATURE_D3D12_OPTIONS7:return "OPTIONS7";
    case D3D12_FEATURE_QUERY_META_COMMAND:return "QUERY_META_COMMAND";
    case D3D12_FEATURE_FORMAT_SUPPORT:return "FORMAT_SUPPORT";
    case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS:return "MULTISAMPLE_QUALITY_LEVELS";
    case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:return "GPU_VIRTUAL_ADDRESS_SUPPORT";
    default:return "?";
    }
}
inline uint64_t fnv(const void* data,size_t n,uint64_t h=1469598103934665603ull) {
    auto p=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}return h;
}

// ---- vtable patching (same contract as d3d11_diag.h)
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
    DWORD old=0;
    if(!VirtualProtect(vtable+low,sizeof(void*)*size_t(high-low+1),PAGE_READWRITE,&old)) {
        emit(fmt("diagnostics cannot patch a D3D12 vtable (error %lu)",GetLastError()));return false;
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
inline std::string heapText(D3D12_HEAP_TYPE t) {
    switch(t){case D3D12_HEAP_TYPE_DEFAULT:return "default";case D3D12_HEAP_TYPE_UPLOAD:return "upload";
    case D3D12_HEAP_TYPE_READBACK:return "readback";case D3D12_HEAP_TYPE_CUSTOM:return "custom";default:return "?";}
}
inline std::string resourceDescText(const D3D12_RESOURCE_DESC* d) {
    if(!d)return "desc=null";
    std::string extent=d->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D
        ?fmt("%llux%ux%u",(unsigned long long)d->Width,d->Height,d->DepthOrArraySize)
        :fmt("%llux%u",(unsigned long long)d->Width,d->Height);
    return fmt("dim=%d fmt=%s %s mips=%u samples=%u flags=0x%llx layout=%d",
        int(d->Dimension),formatText(d->Format).c_str(),extent.c_str(),d->MipLevels,d->SampleDesc.Count,
        (unsigned long long)d->Flags,int(d->Layout));
}
// The key drops the flags a game varies freely (RT/DS usage) so one texture
// failing at several sizes logs once, while a different format or dimension
// logs again.
inline std::string resourceKey(const D3D12_RESOURCE_DESC* d) {
    if(!d)return "null";
    return fmt("%d/%d/%u/%u/%u/%llx/%d",int(d->Dimension),int(d->Format),d->SampleDesc.Count,d->MipLevels,d->DepthOrArraySize,
        (unsigned long long)(d->Flags&~(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)),int(d->Layout));
}
// A DXIL container: "DXIL" magic, a fourcc hash naming the program type and a
// content hash, so a rejected pipeline state can be matched to the game's file.
inline std::string shaderText(const void* code,SIZE_T length,std::string* key=nullptr) {
    if(!code||!length){if(key)*key="none";return "bytecode=none";}
    auto bytes=static_cast<const unsigned char*>(code);
    uint64_t hash=fnv(code,size_t(std::min<SIZE_T>(length,16u<<20)));
    std::string kind="not-dxil";
    if(length>=8&&!memcmp(bytes,"DXIL",4)) kind=fmt("dxil version=%u.%u",bytes[4],bytes[5]);
    else if(length>=4&&!memcmp(bytes,"DXBC",4)) kind="dxbc";
    if(key)*key=fmt("%016llx",(unsigned long long)hash);
    return fmt("%s bytes=%llu hash=%016llx",kind.c_str(),(unsigned long long)size_t(length),(unsigned long long)hash);
}

// ---- opt-in call trace (LocalState\d3d12trace.txt, read once at the first
// adopt; default off, so a run without the file logs exactly what it did
// before). The ring keeps the last kTraceRing hooked calls by slot name and
// hr; a call still in flight stays "pending". flushTrace() runs from the fault
// handler, so the last entry is the call the fault came from.
constexpr size_t kTraceRing=64;      // the newest calls, by sequence number
constexpr size_t kTraceInFlight=32;  // calls begun and not yet returned (kept until they end)
// Result states of a traced call.
enum TraceState : int { TracePending=0, TraceHr=1, TraceVoid=2 };
struct TraceEntry { uint64_t seq=0; const char* name=nullptr; long hr=0; int state=TracePending; char detail[512]{}; };
// Memory ordering: every access to the ring, the in-flight list and the sequence
// counter holds traceMutex, so an entry is always read and written whole. A
// ticket is the call's sequence number; an end is applied only to the ring entry
// that still carries that number, so a newer call that reused the slot is never
// overwritten. Pending calls stay in the in-flight list until they end.
inline std::mutex traceMutex;
inline uint64_t traceNextSeq=1;
inline TraceEntry traceRing[kTraceRing];
inline TraceEntry traceInFlight[kTraceInFlight];
inline std::atomic<bool> traceOn{false};
inline std::atomic<bool> traceChecked{false};
inline bool (*set_trace_requested)()=nullptr;
// Returns the ticket (0 when the trace is off).
inline uint64_t traceBegin(const char* name) {
    if(!traceOn.load(std::memory_order_acquire)) return 0;
    std::lock_guard<std::mutex> lock(traceMutex);
    uint64_t seq=traceNextSeq++;
    traceRing[seq%kTraceRing]=TraceEntry{seq,name,0,TracePending};
    for(auto& f:traceInFlight) if(f.seq==0) {f=TraceEntry{seq,name,0,TracePending};break;}
    return seq;
}
inline void traceFinish(uint64_t ticket,int state,long hr) {
    if(!ticket) return;
    std::lock_guard<std::mutex> lock(traceMutex);
    TraceEntry& e=traceRing[ticket%kTraceRing];
    if(e.seq==ticket) {e.state=state;e.hr=hr;}   // overwritten by a newer call: drop the update
    for(auto& f:traceInFlight) if(f.seq==ticket) {f.seq=0;break;}
}
inline void traceEnd(uint64_t ticket,HRESULT hr) { traceFinish(ticket,TraceHr,long(hr)); }
// A void call has no HRESULT: the entry records "void", not a made-up hr.
inline void traceEndVoid(uint64_t ticket) { traceFinish(ticket,TraceVoid,0); }
// Runs from the fault handler. The faulting call is not inside traceMutex, so a
// try-lock only fails if another guest thread is mid-update; then it says so.
inline void flushTrace() {
    if(!traceOn.load(std::memory_order_acquire)) return;
    std::unique_lock<std::mutex> lock(traceMutex,std::try_to_lock);
    if(!lock.owns_lock()) {emit("trace: busy, not flushed");return;}
    for(const auto& f:traceInFlight)
        if(f.seq) emit(fmt("trace in-flight #%llu %s pending",(unsigned long long)f.seq,f.name?f.name:"?"));
    uint64_t first=traceNextSeq>kTraceRing?traceNextSeq-kTraceRing:1;
    emit(fmt("trace: last %llu hooked calls, oldest first",(unsigned long long)(traceNextSeq-first)));
    for(uint64_t s=first;s<traceNextSeq;++s) {
        const TraceEntry& e=traceRing[s%kTraceRing];
        if(e.seq!=s) continue;
        if(e.detail[0]) emit(fmt("trace #%llu pso %s",(unsigned long long)s,e.detail));
        else {
            const char* result=e.state==TracePending?"pending":e.state==TraceVoid?"void":nullptr;
            emit(fmt("trace #%llu %s %s",(unsigned long long)s,e.name?e.name:"?",result?result:hex(HRESULT(e.hr)).c_str()));
        }
    }
}

// nameStr is the name the trace records: a string literal, never a stringified
// template parameter (that stringifies the parameter's token, e.g. "Slot").
#define D3D12_DIAG_HOOK_AS(Type,slot,nameStr) auto call=reinterpret_cast<Type>(original(self,slot));if(!call)return E_FAIL; const uint64_t traceAt=traceBegin(nameStr);
#define D3D12_DIAG_HOOK(Type,slot) D3D12_DIAG_HOOK_AS(Type,slot,#slot)
// The same for a void call: the hook cannot return E_FAIL from a void function.
#define D3D12_DIAG_HOOK_VOID_AS(Type,slot,nameStr) auto call=reinterpret_cast<Type>(original(self,slot));if(!call)return; const uint64_t traceAt=traceBegin(nameStr);
#define D3D12_DIAG_HOOK_VOID(Type,slot) D3D12_DIAG_HOOK_VOID_AS(Type,slot,#slot)
#define D3D12_DIAG_FAILED(name,key,text) do{if(FAILED(hr))report(false,std::string(name)+" "+hex(hr)+" "+(key),std::string(name)+" hr="+hex(hr)+" "+(text));}while(0)

using DevicePtr=void*;
static HRESULT STDMETHODCALLTYPE createCommandQueue(DevicePtr self,const D3D12_COMMAND_QUEUE_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandQueue)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    if(d)D3D12_DIAG_FAILED("CreateCommandQueue",fmt("%d/%d/%d/%d",int(d->Type),int(d->Priority),int(d->Flags),int(d->NodeMask)),
        fmt("type=%d priority=%d flags=0x%x node=%u",int(d->Type),d->Priority,d->Flags,d->NodeMask));
    else D3D12_DIAG_FAILED("CreateCommandQueue","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommandAllocator(DevicePtr self,D3D12_COMMAND_LIST_TYPE type,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D12_COMMAND_LIST_TYPE,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandAllocator)HRESULT hr=call(self,type,id,out); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CreateCommandAllocator",fmt("%d",int(type)),fmt("type=%d",int(type)));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommandList(DevicePtr self,UINT node,D3D12_COMMAND_LIST_TYPE type,ID3D12CommandAllocator* allocator,
        ID3D12PipelineState* state,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandList)HRESULT hr=call(self,node,type,allocator,state,id,out); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CreateCommandList",fmt("%d/%d/%d/%d",int(type),node,allocator!=nullptr,state!=nullptr),
        fmt("type=%d node=%u allocator=%d state=%d",int(type),node,allocator!=nullptr,state!=nullptr));
    return hr;
}
// ---- opt-in PSO description (same switch as the call trace). Logged BEFORE the
// call, so a fault inside the driver still leaves the description in the log.
// The digest field is reported as set/null; container=valid means the DXBC
// header, chunk table and DXIL chunk are structurally in bounds (not checksum validation).
constexpr size_t kMaxPsoLines=64;
inline std::atomic<size_t> psoLines{0};
inline void tracePso(const std::string& text) {
    if(!traceOn.load(std::memory_order_acquire)||psoLines.fetch_add(1)>=kMaxPsoLines) return;
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        uint64_t seq=traceNextSeq++;
        TraceEntry& e=traceRing[seq%kTraceRing];
        e=TraceEntry{seq,"PSO",0,TraceVoid};
        std::snprintf(e.detail,sizeof e.detail,"%s",text.c_str());
    }
    emit("pso "+text);
}
inline const char* dxilContainerState(const unsigned char* bytes,size_t length) {
    if(length<4||memcmp(bytes,"DXBC",4)!=0) return "not-dxbc";
    if(length<32) return "invalid";
    uint32_t total=0,chunks=0;
    memcpy(&total,bytes+24,sizeof total);memcpy(&chunks,bytes+28,sizeof chunks);
    if(total<32||total>length||chunks>(total-32)/4) return "invalid";
    bool hasDxil=false;
    for(uint32_t i=0;i<chunks;++i) {
        uint32_t offset=0;memcpy(&offset,bytes+32+size_t(i)*4,sizeof offset);
        if(offset<32+size_t(chunks)*4||offset>total||total-offset<8) return "invalid";
        uint32_t chunkBytes=0;memcpy(&chunkBytes,bytes+offset+4,sizeof chunkBytes);
        if(chunkBytes>total-offset-8) return "invalid";
        if(memcmp(bytes+offset,"DXIL",4)==0) {
            if(chunkBytes<8) return "invalid";
            hasDxil=true;
        }
    }
    return hasDxil?"valid":"not-dxil";
}
inline std::string containerText(const void* code,SIZE_T length) {
    if(!code||!length) return "none";
    auto bytes=static_cast<const unsigned char*>(code);
    if(length<4||memcmp(bytes,"DXBC",4)!=0) return fmt("not-dxbc bytes=%llu",(unsigned long long)length);
    bool nullDigest=true;
    if(length>=20) for(int i=4;i<20;++i) if(bytes[i]) nullDigest=false;
    return fmt("dxbc bytes=%llu digest=%s container=%s hash=%016llx",(unsigned long long)length,
        length<20?"unavailable":nullDigest?"null(unsigned)":"set",dxilContainerState(bytes,size_t(length)),
        (unsigned long long)fnv(code,size_t(std::min<SIZE_T>(length,16u<<20))));
}
inline std::string bytecodeText(const D3D12_SHADER_BYTECODE& b) {
    return containerText(b.pShaderBytecode,b.BytecodeLength);
}
inline std::string inputLayoutText(const D3D12_INPUT_LAYOUT_DESC& l) {
    std::string text=fmt("inputs=%u[",l.NumElements);
    for(UINT i=0;i<l.NumElements&&i<8;++i) {
        const auto& e=l.pInputElementDescs[i];
        text+=fmt("%s%s:%s",i?" ":"",e.SemanticName?e.SemanticName:"?",formatText(e.Format).c_str());
    }
    return text+"]";
}
inline std::string rtvText(const D3D12_RT_FORMAT_ARRAY& r) {
    std::string text=fmt("rtv=%u[",r.NumRenderTargets);
    for(UINT i=0;i<r.NumRenderTargets&&i<8;++i) text+=fmt("%s%s",i?" ":"",formatText(r.RTFormats[i]).c_str());
    return text+"]";
}
inline const char* rootParameterName(D3D12_ROOT_PARAMETER_TYPE type) {
    switch(type) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:return "DESCRIPTOR_TABLE";
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:return "32BIT_CONSTANTS";
    case D3D12_ROOT_PARAMETER_TYPE_CBV:return "CBV";
    case D3D12_ROOT_PARAMETER_TYPE_SRV:return "SRV";
    case D3D12_ROOT_PARAMETER_TYPE_UAV:return "UAV";
    default:return "UNKNOWN";
    }
}
inline std::string rootParametersText(const D3D12_ROOT_SIGNATURE_DESC* desc) {
    if(!desc)return "params=unavailable";
    std::string text=fmt("params=%u[",desc->NumParameters);
    for(UINT i=0;i<desc->NumParameters&&i<16;++i) {
        if(i)text+=" ";
        const D3D12_ROOT_PARAMETER& p=desc->pParameters[i];
        text+=rootParameterName(p.ParameterType);
        if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)text+=fmt("(ranges=%u)",p.DescriptorTable.NumDescriptorRanges);
    }
    if(desc->NumParameters>16)text+=" ...";
    return text+"]";
}
inline std::string rootParametersText(const D3D12_ROOT_SIGNATURE_DESC1* desc) {
    if(!desc)return "params=unavailable";
    std::string text=fmt("params=%u[",desc->NumParameters);
    for(UINT i=0;i<desc->NumParameters&&i<16;++i) {
        const auto& p=desc->pParameters[i];text+=fmt("%s%s",i?" ":"",rootParameterName(p.ParameterType));
        if(p.ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            text+=fmt("(ranges=%u[",p.DescriptorTable.NumDescriptorRanges);
            for(UINT j=0;j<p.DescriptorTable.NumDescriptorRanges&&j<8;++j) {const auto& r=p.DescriptorTable.pDescriptorRanges[j];
                text+=fmt("%s%s:%u reg=%u space=%u flags=0x%x",j?" ":"",r.RangeType==D3D12_DESCRIPTOR_RANGE_TYPE_SRV?"SRV":r.RangeType==D3D12_DESCRIPTOR_RANGE_TYPE_UAV?"UAV":r.RangeType==D3D12_DESCRIPTOR_RANGE_TYPE_CBV?"CBV":"SAMPLER",r.NumDescriptors,r.BaseShaderRegister,r.RegisterSpace,unsigned(r.Flags));}
            if(p.DescriptorTable.NumDescriptorRanges>8)text+=" ...";
            text+="])";
        }
    }
    if(desc->NumParameters>16)text+=" ...";
    return text+"]";
}
#if defined(D3D12_SDK_VERSION) && D3D12_SDK_VERSION >= 618
static_assert(std::is_same_v<decltype(D3D12_ROOT_SIGNATURE_DESC2::pParameters),decltype(D3D12_ROOT_SIGNATURE_DESC1::pParameters)> &&
    offsetof(D3D12_ROOT_SIGNATURE_DESC2,NumParameters)==offsetof(D3D12_ROOT_SIGNATURE_DESC1,NumParameters) &&
    offsetof(D3D12_ROOT_SIGNATURE_DESC2,pParameters)==offsetof(D3D12_ROOT_SIGNATURE_DESC1,pParameters) &&
    sizeof(D3D12_ROOT_SIGNATURE_DESC2)==sizeof(D3D12_ROOT_SIGNATURE_DESC1),"1.2 root parameters must match 1.1 layout");
inline std::string rootParametersText(const D3D12_ROOT_SIGNATURE_DESC2* desc) {
    if(!desc)return "params=unavailable";
    D3D12_ROOT_SIGNATURE_DESC1 parameters{};parameters.NumParameters=desc->NumParameters;parameters.pParameters=desc->pParameters;
    return rootParametersText(&parameters);
}
#endif
inline std::string rootParametersText(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc) {
    if(!desc)return "params=unavailable";
    switch(desc->Version) {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:return rootParametersText(&desc->Desc_1_0);
    case D3D_ROOT_SIGNATURE_VERSION_1_1:return rootParametersText(&desc->Desc_1_1);
#if defined(D3D12_SDK_VERSION) && D3D12_SDK_VERSION >= 618
    case D3D_ROOT_SIGNATURE_VERSION_1_2:return rootParametersText(&desc->Desc_1_2);
#endif
    default:return fmt("version=%u params=unsupported-version",unsigned(desc->Version));
    }
}
using RootSignatureDescriber=std::string(*)(const void*,SIZE_T);
inline std::string describeRootSignature(const void* blob,SIZE_T length) {
    using Fn=HRESULT(WINAPI*)(LPCVOID,SIZE_T,REFIID,void**);
    HMODULE module=GetModuleHandleW(L"d3d12.dll");auto fn=module?reinterpret_cast<Fn>(GetProcAddress(module,"D3D12CreateVersionedRootSignatureDeserializer")):nullptr;
    if(fn) {ID3D12VersionedRootSignatureDeserializer* d=nullptr;
        if(SUCCEEDED(fn(blob,length,__uuidof(ID3D12VersionedRootSignatureDeserializer),reinterpret_cast<void**>(&d)))&&d) {
            std::string text=rootParametersText(d->GetUnconvertedRootSignatureDesc());d->Release();if(text!="params=deserialize-failed")return text;}}
    ID3D12RootSignatureDeserializer* d=nullptr;
    if(FAILED(D3D12CreateRootSignatureDeserializer(blob,length,__uuidof(ID3D12RootSignatureDeserializer),reinterpret_cast<void**>(&d)))||!d)return "params=deserialize-failed";
    std::string text=rootParametersText(d->GetRootSignatureDesc());d->Release();return text;
}
inline RootSignatureDescriber rootSignatureDescriber=&describeRootSignature;
constexpr size_t kMaxRootSignatures=64;
struct RootSignatureEntry {const void* signature=nullptr;std::string parameters;};
inline RootSignatureEntry rootSignatures[kMaxRootSignatures];
inline size_t rootSignatureNext=0;
inline std::mutex rootSignatureMutex;
inline void rememberRootSignature(const void* signature,const void* blob,SIZE_T length) {
    if(!signature||!traceOn.load(std::memory_order_acquire)||!rootSignatureDescriber)return;
    std::string parameters=rootSignatureDescriber(blob,length);
    std::lock_guard<std::mutex> lock(rootSignatureMutex);
    for(auto& entry:rootSignatures)if(entry.signature==signature){entry.parameters=std::move(parameters);return;}
    RootSignatureEntry& entry=rootSignatures[rootSignatureNext++%kMaxRootSignatures];
    entry.signature=signature;entry.parameters=std::move(parameters);
}
inline std::string rootSignatureText(const void* signature) {
    if(!signature)return "null";
    std::lock_guard<std::mutex> lock(rootSignatureMutex);
    for(const auto& entry:rootSignatures)if(entry.signature==signature)return "set "+entry.parameters;
    return "set params=unavailable";
}
// Stream entries that matter for a description: shaders, input layout, formats.
// Payloads are read with memcpy (the walker hands over unaligned pointers).
inline void describeStreamEntry(int type,const unsigned char* payload,size_t size,void* ctx) {
    auto* text=static_cast<std::string*>(ctx);
    switch(type) {
    case 0: {
        ID3D12RootSignature* root=nullptr;memcpy(&root,payload,sizeof root);
        *text+=" rootsig="+rootSignatureText(root);
        break;
    }
    case 1: case 2: case 6: {
        D3D12_SHADER_BYTECODE b{};memcpy(&b,payload,sizeof b);
        *text+=fmt(" %s:%s",type==1?"VS":type==2?"PS":"CS",bytecodeText(b).c_str());
        break;
    }
    case 12: {
        D3D12_INPUT_LAYOUT_DESC l{};memcpy(&l,payload,sizeof l);
        *text+=" "+inputLayoutText(l);
        break;
    }
    case 15: {
        D3D12_RT_FORMAT_ARRAY r{};memcpy(&r,payload,sizeof r);
        *text+=" "+rtvText(r);
        break;
    }
    case 16: {
        DXGI_FORMAT f{};memcpy(&f,payload,sizeof f);
        *text+=" dsv="+formatText(f);
        break;
    }
    case 29: {
#if defined(D3D12_SDK_VERSION) && D3D12_SDK_VERSION >= 618
        D3D12_SERIALIZED_ROOT_SIGNATURE_DESC d{};memcpy(&d,payload,sizeof d);
        *text+=" rootsig="+(rootSignatureDescriber?rootSignatureDescriber(d.pSerializedBlob,d.SerializedBlobSizeInBytes):"params=unavailable");
#else
        *text+=" rootsig=serialized params=unavailable";
#endif
        break;
    }
    default: break;
    }
    (void)size;
}
inline void traceStreamEntry(int type,const unsigned char* payload,size_t size,void*) {
    std::string text;
    describeStreamEntry(type,payload,size,&text);
    if(!text.empty()) tracePso("stream"+text);
}
inline void traceGraphicsDesc(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& d) {
    tracePso("v0 VS:"+bytecodeText(d.VS));
    tracePso("v0 PS:"+bytecodeText(d.PS));
    tracePso(fmt("v0 %s dsv=%s samples=%u rootsig=%s %s",
        rtvText({{d.RTVFormats[0],d.RTVFormats[1],d.RTVFormats[2],d.RTVFormats[3],d.RTVFormats[4],d.RTVFormats[5],d.RTVFormats[6],d.RTVFormats[7]},d.NumRenderTargets}).c_str(),
        formatText(d.DSVFormat).c_str(),d.SampleDesc.Count,rootSignatureText(d.pRootSignature).c_str(),inputLayoutText(d.InputLayout).c_str()));
}

static HRESULT STDMETHODCALLTYPE createGraphicsPipelineState(DevicePtr self,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**);
    if(d&&traceOn.load(std::memory_order_acquire)) traceGraphicsDesc(*d);
    D3D12_DIAG_HOOK(Fn,CreateGraphicsPipelineState)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    if(d) {
        std::string vsKey,psKey,vs=shaderText(d->VS.pShaderBytecode,d->VS.BytecodeLength,&vsKey);
        std::string ps=shaderText(d->PS.pShaderBytecode,d->PS.BytecodeLength,&psKey);
        D3D12_DIAG_FAILED("CreateGraphicsPipelineState",vsKey+"/"+psKey,
            fmt("vs[%s] ps[%s] rtvfmt=%s dsvfmt=%s samples=%u nodes=%u",vs.c_str(),ps.c_str(),
                formatText(d->RTVFormats[0]).c_str(),formatText(d->DSVFormat).c_str(),d->SampleDesc.Count,d->NodeMask));
    } else D3D12_DIAG_FAILED("CreateGraphicsPipelineState","null","desc=null");
    return hr;
}
// One line describing a PSO subobject stream: the named subobjects in order
// (with truncation/unknown markers), bounded to ~400 chars. Exposed for the
// d3d12-diag test, which feeds synthetic streams built from the real layout.
// The stream is a sequence of CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT entries
// (alignas(void*), a 4-byte type, then the payload at its own alignment), so
// walk with a real size and alignment table: a wrong stride would misname the
// refused subobject or read past the stream — exactly what this diagnostic
// exists to name. Sizes and alignments come from sizeof/alignof on the real
// d3d12.h structs (checked against DirectX-Headers v1.618.1).
// visit, when given, sees each subobject's type, payload and size in stream order.
using StreamVisit=void(*)(int type,const unsigned char* payload,size_t size,void* ctx);
inline std::string streamSubobjects(const void* stream,size_t bytes,StreamVisit visit=nullptr,void* ctx=nullptr) {
    static const struct{int type;size_t size;size_t align;const char* name;} subobjects[]={
        {0,sizeof(ID3D12RootSignature*),alignof(ID3D12RootSignature*),        "ROOT_SIGNATURE"},
        {1,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),       "VS"},
        {2,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),       "PS"},
        {3,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),       "DS"},
        {4,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),       "HS"},
        {5,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),       "GS"},
        {6,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),       "CS"},
        {7,sizeof(D3D12_STREAM_OUTPUT_DESC),alignof(D3D12_STREAM_OUTPUT_DESC),    "STREAM_OUTPUT"},
        {8,sizeof(D3D12_BLEND_DESC),alignof(D3D12_BLEND_DESC),            "BLEND"},
        {9,sizeof(UINT),alignof(UINT),                        "SAMPLE_MASK"},
        {10,sizeof(D3D12_RASTERIZER_DESC),alignof(D3D12_RASTERIZER_DESC),      "RASTERIZER"},
        {11,sizeof(D3D12_DEPTH_STENCIL_DESC),alignof(D3D12_DEPTH_STENCIL_DESC),   "DEPTH_STENCIL"},
        {12,sizeof(D3D12_INPUT_LAYOUT_DESC),alignof(D3D12_INPUT_LAYOUT_DESC),    "INPUT_LAYOUT"},
        {13,sizeof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE),alignof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE),"IB_STRIP_CUT"},
        {14,sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE),alignof(D3D12_PRIMITIVE_TOPOLOGY_TYPE),"PRIMITIVE_TOPOLOGY"},
        {15,sizeof(D3D12_RT_FORMAT_ARRAY),alignof(D3D12_RT_FORMAT_ARRAY),      "RTV_FORMATS"},
        {16,sizeof(DXGI_FORMAT),alignof(DXGI_FORMAT),                "DSV_FORMAT"},
        {17,sizeof(DXGI_SAMPLE_DESC),alignof(DXGI_SAMPLE_DESC),           "SAMPLE_DESC"},
        {18,sizeof(UINT),alignof(UINT),                       "NODE_MASK"},
        {19,sizeof(D3D12_CACHED_PIPELINE_STATE),alignof(D3D12_CACHED_PIPELINE_STATE),"CACHED_PSO"},
        {20,sizeof(D3D12_PIPELINE_STATE_FLAGS),alignof(D3D12_PIPELINE_STATE_FLAGS), "FLAGS"},
        {21,sizeof(D3D12_DEPTH_STENCIL_DESC1),alignof(D3D12_DEPTH_STENCIL_DESC1),  "DEPTH_STENCIL1"},
        {22,sizeof(D3D12_VIEW_INSTANCING_DESC),alignof(D3D12_VIEW_INSTANCING_DESC), "VIEW_INSTANCING"},
        {24,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),      "AS"},
        {25,sizeof(D3D12_SHADER_BYTECODE),alignof(D3D12_SHADER_BYTECODE),      "MS"},
#if defined(D3D12_SDK_VERSION) && D3D12_SDK_VERSION >= 610
        {26,sizeof(D3D12_DEPTH_STENCIL_DESC2),alignof(D3D12_DEPTH_STENCIL_DESC2),  "DEPTH_STENCIL2"},
        {27,sizeof(D3D12_RASTERIZER_DESC1),alignof(D3D12_RASTERIZER_DESC1),     "RASTERIZER1"},
        {28,sizeof(D3D12_RASTERIZER_DESC2),alignof(D3D12_RASTERIZER_DESC2),     "RASTERIZER2"},
#else
        // SDK < 610 headers lack the structs; the sizes below are sizeof of
        // the real structs under SDK 610+ headers (D3D12_DEPTH_STENCIL_DESC2:
        // 7 fields ending in BOOL after two D3D12_DEPTH_STENCILOP_DESC1;
        // RASTERIZER_DESC1/2: the 11/10-field float/int variants), verified
        // against SDK 616 and 618 headers. The console's Mesa (SDK 618) can
        // emit all three.
        {26,60,4,"DEPTH_STENCIL2"},   // 6*4 + 4 + 2*D3D12_DEPTH_STENCILOP_DESC1(16) + 4 + 4, packed to 60
        {27,44,4,"RASTERIZER1"},      // same layout as D3D12_RASTERIZER_DESC with INT DepthBias
        {28,40,4,"RASTERIZER2"},      // RASTERIZER_DESC1 with the INT DepthBias dropped
#endif
#if defined(D3D12_SDK_VERSION) && D3D12_SDK_VERSION >= 618
        {29,sizeof(D3D12_SERIALIZED_ROOT_SIGNATURE_DESC),alignof(D3D12_SERIALIZED_ROOT_SIGNATURE_DESC),"SERIALIZED_ROOT_SIGNATURE"},
#else
        // The struct arrives in SDK 618 (d3d12.h: two pointer members:
        // pSerializedBlob + SerializedBlobSizeInBytes); older SDKs never see
        // the subobject, but the console's Mesa (SDK 618 headers) can emit it.
        {29,2*sizeof(void*),alignof(void*),"SERIALIZED_ROOT_SIGNATURE"},
#endif
    };
    auto base=static_cast<const unsigned char*>(stream);
    const unsigned char* end=base+bytes;
    std::string subs;
    bool truncated=false;
    for(auto p=base;p<end;) {
        if(size_t(end-p)<sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)){truncated=true;break;}
        auto typeStart=p;
        auto type=*reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(p);
        const auto* e=subobjects;
        for(;e<subobjects+sizeof(subobjects)/sizeof(subobjects[0])&&e->type!=int(type);++e) {}
        if(e==subobjects+sizeof(subobjects)/sizeof(subobjects[0])) {
            subs+=fmt("unknown(type=%d@offset=%zu) ",int(type),size_t(typeStart-base));
            break;
        }
        // The payload sits at its own alignment after the 4-byte type (offset 4
        // for UINT, enums and 4-aligned structs, 8 for pointer-holding ones).
        const size_t payloadAt=(sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)+e->align-1)&~(e->align-1);
        if(size_t(end-typeStart)<payloadAt+e->size){truncated=true;subs+=fmt("%s(truncated) ",e->name);break;}
        subs+=fmt("%s ",e->name);
        if(visit) visit(int(type),typeStart+payloadAt,e->size,ctx);
        // The next subobject starts at the next pointer-aligned offset after
        // the payload: each one is an alignas(void*) struct in the real stream.
        p=typeStart+((payloadAt+e->size+sizeof(void*)-1)&~(sizeof(void*)-1));
        if(subs.size()>400){subs+="...";break;}
    }
    return fmt("bytes=%zu%s subobjects=[%s]",bytes,truncated?" truncated":"",subs.c_str());
}
static HRESULT STDMETHODCALLTYPE createPipelineStateStream(DevicePtr self,const D3D12_PIPELINE_STATE_STREAM_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
    if(d&&d->pPipelineStateSubobjectStream&&traceOn.load(std::memory_order_acquire))
        streamSubobjects(d->pPipelineStateSubobjectStream,size_t(d->SizeInBytes),&traceStreamEntry,nullptr);
    D3D12_DIAG_HOOK(Fn,createPipelineStateStreamSlot)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    // The stream path (ID3D12Device2::CreatePipelineState) is what Mesa's
    // d3d12 driver takes when the device reports
    // opts14.IndependentFrontAndBackStencilRefMaskSupported — the console's
    // does. A refused subobject type or value is what E_INVALIDARG names
    // here, where the v0desc path's fields would be.
    if(d&&d->pPipelineStateSubobjectStream) {
        int depthStencils=0,rasterizers=0,depthStencil2s=0,rasterizer2s=0;
        const std::string text=streamSubobjects(d->pPipelineStateSubobjectStream,size_t(d->SizeInBytes));
        for(size_t pos=text.find("DEPTH_STENCIL2 ");pos!=std::string::npos;pos=text.find("DEPTH_STENCIL2 ",pos+1))++depthStencil2s;
        for(size_t pos=text.find("RASTERIZER2 ");pos!=std::string::npos;pos=text.find("RASTERIZER2 ",pos+1))++rasterizer2s;
        for(size_t pos=text.find("RASTERIZER ");pos!=std::string::npos;pos=text.find("RASTERIZER ",pos+1))++rasterizers;
        for(size_t pos=text.find("DEPTH_STENCIL ");pos!=std::string::npos;pos=text.find("DEPTH_STENCIL ",pos+1))++depthStencils;
        D3D12_DIAG_FAILED("CreatePipelineState(stream)",fmt("%llu/%d/%d/%d/%d",(unsigned long long)d->SizeInBytes,depthStencils,depthStencil2s,rasterizers,rasterizer2s),text);
    } else D3D12_DIAG_FAILED("CreatePipelineState(stream)","null","desc=null or empty stream");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createComputePipelineState(DevicePtr self,const D3D12_COMPUTE_PIPELINE_STATE_DESC* d,REFIID id,void** out) {
    if(d&&traceOn.load(std::memory_order_acquire)) {
        tracePso("compute CS:"+bytecodeText(d->CS));
        tracePso("compute rootsig="+rootSignatureText(d->pRootSignature));
    }
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateComputePipelineState)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    if(d) {
        std::string key,cs=shaderText(d->CS.pShaderBytecode,d->CS.BytecodeLength,&key);
        D3D12_DIAG_FAILED("CreateComputePipelineState",key,fmt("cs[%s]",cs.c_str()));
    } else D3D12_DIAG_FAILED("CreateComputePipelineState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDescriptorHeap(DevicePtr self,const D3D12_DESCRIPTOR_HEAP_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_DESCRIPTOR_HEAP_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateDescriptorHeap)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    if(d)D3D12_DIAG_FAILED("CreateDescriptorHeap",fmt("%d/%u/%x",int(d->Type),d->NumDescriptors,d->Flags),
        fmt("type=%d descriptors=%u flags=0x%x node=%u",int(d->Type),d->NumDescriptors,d->Flags,d->NodeMask));
    else D3D12_DIAG_FAILED("CreateDescriptorHeap","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createRootSignature(DevicePtr self,UINT node,const void* blob,SIZE_T length,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,const void*,SIZE_T,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateRootSignature)HRESULT hr=call(self,node,blob,length,id,out); traceEnd(traceAt,hr);
    if(SUCCEEDED(hr)&&out&&*out)rememberRootSignature(*out,blob,length);
    if(FAILED(hr)) {
        std::string key;std::string text=shaderText(blob,length,&key);
        D3D12_DIAG_FAILED("CreateRootSignature",key,fmt("node=%u %s",node,text.c_str()));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommittedResource(DevicePtr self,const D3D12_HEAP_PROPERTIES* heap,D3D12_HEAP_FLAGS heapFlags,
        const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommittedResource)HRESULT hr=call(self,heap,heapFlags,d,state,clear,id,out); traceEnd(traceAt,hr);
    if(FAILED(hr)) {
        D3D12_DIAG_FAILED("CreateCommittedResource",resourceKey(d)+fmt("/%d/%x",int(state),heapFlags),
            fmt("heap=%s heapflags=0x%x state=%s(%d) %s",heap?heapText(heap->Type).c_str():"null",heapFlags,
                resourceStateName(state),int(state),resourceDescText(d).c_str()));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createHeap(DevicePtr self,const D3D12_HEAP_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_HEAP_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateHeap)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    if(d)D3D12_DIAG_FAILED("CreateHeap",fmt("%llu/%x/%d",(unsigned long long)d->SizeInBytes,d->Flags,int(d->Properties.Type)),
        fmt("bytes=%llu flags=0x%x heap=%s align=%llu",(unsigned long long)d->SizeInBytes,d->Flags,
            heapText(d->Properties.Type).c_str(),(unsigned long long)d->Alignment));
    else D3D12_DIAG_FAILED("CreateHeap","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createPlacedResource(DevicePtr self,ID3D12Heap* heap,UINT64 offset,const D3D12_RESOURCE_DESC* d,
        D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Heap*,UINT64,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreatePlacedResource)HRESULT hr=call(self,heap,offset,d,state,clear,id,out); traceEnd(traceAt,hr);
    if(FAILED(hr))D3D12_DIAG_FAILED("CreatePlacedResource",resourceKey(d)+fmt("/%d",int(state)),
        fmt("heap=%d offset=%llu state=%s(%d) %s",heap!=nullptr,(unsigned long long)offset,resourceStateName(state),int(state),resourceDescText(d).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createReservedResource(DevicePtr self,const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES state,
        const D3D12_CLEAR_VALUE* clear,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateReservedResource)HRESULT hr=call(self,d,state,clear,id,out); traceEnd(traceAt,hr);
    if(FAILED(hr))D3D12_DIAG_FAILED("CreateReservedResource",resourceKey(d)+fmt("/%d",int(state)),
        fmt("state=%s(%d) %s",resourceStateName(state),int(state),resourceDescText(d).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createFence(DevicePtr self,UINT64 value,D3D12_FENCE_FLAGS flags,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT64,D3D12_FENCE_FLAGS,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateFence)HRESULT hr=call(self,value,flags,id,out); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CreateFence",fmt("%x",flags),fmt("value=%llu flags=0x%x",(unsigned long long)value,flags));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createQueryHeap(DevicePtr self,const D3D12_QUERY_HEAP_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_QUERY_HEAP_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateQueryHeap)HRESULT hr=call(self,d,id,out); traceEnd(traceAt,hr);
    if(d)D3D12_DIAG_FAILED("CreateQueryHeap",fmt("%d/%u",int(d->Type),d->Count),fmt("type=%d count=%u node=%u",int(d->Type),d->Count,d->NodeMask));
    else D3D12_DIAG_FAILED("CreateQueryHeap","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommandSignature(DevicePtr self,const D3D12_COMMAND_SIGNATURE_DESC* d,ID3D12RootSignature* root,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_COMMAND_SIGNATURE_DESC*,ID3D12RootSignature*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandSignature)HRESULT hr=call(self,d,root,id,out); traceEnd(traceAt,hr);
    if(d)D3D12_DIAG_FAILED("CreateCommandSignature",fmt("%u/%u",d->ByteStride,d->NumArgumentDescs),
        fmt("stride=%u args=%u root=%d",d->ByteStride,d->NumArgumentDescs,root!=nullptr));
    else D3D12_DIAG_FAILED("CreateCommandSignature","null","desc=null");
    return hr;
}
// Views share one shape: (resource, optional desc, dest handle). Only the
// description type differs. The device's view creators return void, so there is
// no status to report: the call is traced as "void" and never logs a failure.
template<int Slot,class Desc>
static void viewCall(DevicePtr self,const char* name,ID3D12Resource* resource,const Desc* d,D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,const Desc*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK_VOID_AS(Fn,Slot,name)call(self,resource,d,handle); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE createSrv(DevicePtr self,ID3D12Resource* r,const D3D12_SHADER_RESOURCE_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    viewCall<CreateShaderResourceView>(self,"CreateShaderResourceView",r,d,h);
}
// CreateUnorderedAccessView takes a counter resource before the description, so
// it cannot share viewCall's (resource, desc, handle) shape: forwarding that
// shape dropped the counter and shifted the description into the handle.
static void STDMETHODCALLTYPE createUav(DevicePtr self,ID3D12Resource* r,ID3D12Resource* counter,const D3D12_UNORDERED_ACCESS_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK_VOID(Fn,CreateUnorderedAccessView)call(self,r,counter,d,h); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE createRtv(DevicePtr self,ID3D12Resource* r,const D3D12_RENDER_TARGET_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    viewCall<CreateRenderTargetView>(self,"CreateRenderTargetView",r,d,h);
}
static void STDMETHODCALLTYPE createDsv(DevicePtr self,ID3D12Resource* r,const D3D12_DEPTH_STENCIL_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    viewCall<CreateDepthStencilView>(self,"CreateDepthStencilView",r,d,h);
}
static void STDMETHODCALLTYPE createSampler(DevicePtr self,const D3D12_SAMPLER_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_SAMPLER_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK_VOID(Fn,CreateSampler)call(self,d,h); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE createCbv(DevicePtr self,const D3D12_CONSTANT_BUFFER_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_CONSTANT_BUFFER_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK_VOID(Fn,CreateConstantBufferView)call(self,d,h); traceEndVoid(traceAt);
}

// ---- capability queries: the answers that pick a game's renderer path
static HRESULT STDMETHODCALLTYPE checkFeatureSupport(DevicePtr self,D3D12_FEATURE feature,void* data,UINT size) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D12_FEATURE,void*,UINT);
    D3D12_DIAG_HOOK(Fn,CheckFeatureSupport)HRESULT hr=call(self,feature,data,size); traceEnd(traceAt,hr);
    std::string detail=fmt("size=%u",size);
    // The answers a D3D12 game branches on before anything else: the shader
    // model it may use, whether the adapter is UMA, and the highest feature
    // level. Each is a plain field of the caller's struct.
    if(SUCCEEDED(hr)&&data) {
        if(feature==D3D12_FEATURE_SHADER_MODEL&&size>=sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))
            detail=fmt("highest=0x%x",unsigned(static_cast<D3D12_FEATURE_DATA_SHADER_MODEL*>(data)->HighestShaderModel));
        else if(feature==D3D12_FEATURE_ARCHITECTURE&&size>=sizeof(D3D12_FEATURE_DATA_ARCHITECTURE)) {
            auto a=static_cast<D3D12_FEATURE_DATA_ARCHITECTURE*>(data);
            detail=fmt("node=%u tile=%d uma=%d ccnuma=%d",a->NodeIndex,a->TileBasedRenderer,a->UMA,a->CacheCoherentUMA);
        } else if(feature==D3D12_FEATURE_FEATURE_LEVELS&&size>=sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)) {
            auto f=static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(data);
            detail=fmt("max=0x%x",unsigned(f->MaxSupportedFeatureLevel));
        } else if(feature==D3D12_FEATURE_D3D12_OPTIONS5&&size>=sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5)) {
            auto o=static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS5*>(data);
            detail=fmt("renderpasses=%d raytracing=%d",int(o->RenderPassesTier),int(o->RaytracingTier));
        } else if(feature==D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT&&size>=sizeof(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT)) {
            auto g=static_cast<D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT*>(data);
            detail=fmt("bits/resource=%u bits/process=%u",g->MaxGPUVirtualAddressBitsPerResource,g->MaxGPUVirtualAddressBitsPerProcess);
        }
    }
    std::string text=fmt("feature=%d(%s) size=%u %s",int(feature),featureName(feature),size,detail.c_str());
    if(FAILED(hr))report(false,fmt("CheckFeatureSupport %d/%u %s",int(feature),size,hex(hr).c_str()),"CheckFeatureSupport hr="+hex(hr)+" "+text);
    else report(true,fmt("CheckFeatureSupport %d/%u %s",int(feature),size,detail.c_str()),"CheckFeatureSupport "+text);
    return hr;
}

// ---- command queue: the calls whose failure leaves a frame never submitted
static void STDMETHODCALLTYPE queueExecuteCommandLists(DevicePtr self,UINT count,ID3D12CommandList* const* lists) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D12CommandList* const*);
    D3D12_DIAG_HOOK_VOID(Fn,QueueExecuteCommandLists)call(self,count,lists); traceEndVoid(traceAt);
}
static HRESULT STDMETHODCALLTYPE queueSignal(DevicePtr self,ID3D12Fence* fence,UINT64 value) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Fence*,UINT64);
    D3D12_DIAG_HOOK(Fn,QueueSignal)HRESULT hr=call(self,fence,value); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CommandQueue::Signal",fmt("%d",fence!=nullptr),fmt("fence=%d value=%llu",fence!=nullptr,(unsigned long long)value));
    return hr;
}
static HRESULT STDMETHODCALLTYPE queueWait(DevicePtr self,ID3D12Fence* fence,UINT64 value) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Fence*,UINT64);
    D3D12_DIAG_HOOK(Fn,QueueWait)HRESULT hr=call(self,fence,value); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CommandQueue::Wait",fmt("%d",fence!=nullptr),fmt("fence=%d value=%llu",fence!=nullptr,(unsigned long long)value));
    return hr;
}

// ---- command list: Close/Reset decide whether a frame is ever submitted
static HRESULT STDMETHODCALLTYPE listClose(DevicePtr self) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr);
    D3D12_DIAG_HOOK(Fn,ListClose)HRESULT hr=call(self); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CommandList::Close","","");
    return hr;
}
static HRESULT STDMETHODCALLTYPE listReset(DevicePtr self,ID3D12CommandAllocator* allocator,ID3D12PipelineState* state) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12CommandAllocator*,ID3D12PipelineState*);
    D3D12_DIAG_HOOK(Fn,ListReset)HRESULT hr=call(self,allocator,state); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CommandList::Reset",fmt("%d/%d",allocator!=nullptr,state!=nullptr),fmt("allocator=%d state=%d",allocator!=nullptr,state!=nullptr));
    return hr;
}
static void STDMETHODCALLTYPE listSetPipelineState(DevicePtr self,ID3D12PipelineState* state) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12PipelineState*);
    D3D12_DIAG_HOOK_VOID(Fn,ListSetPipelineState)call(self,state); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listResourceBarrier(DevicePtr self,UINT count,const D3D12_RESOURCE_BARRIER* barriers) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,const D3D12_RESOURCE_BARRIER*);
    D3D12_DIAG_HOOK_VOID(Fn,ListResourceBarrier)call(self,count,barriers); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listSetDescriptorHeaps(DevicePtr self,UINT count,ID3D12DescriptorHeap* const* heaps) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D12DescriptorHeap* const*);
    D3D12_DIAG_HOOK_VOID(Fn,ListSetDescriptorHeaps)call(self,count,heaps); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listDrawInstanced(DevicePtr self,UINT vertices,UINT instances,UINT startVertex,UINT startInstance) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT,UINT);
    D3D12_DIAG_HOOK_VOID(Fn,ListDrawInstanced)call(self,vertices,instances,startVertex,startInstance); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listDrawIndexedInstanced(DevicePtr self,UINT indices,UINT instances,UINT startIndex,INT baseVertex,UINT startInstance) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT,INT,UINT);
    D3D12_DIAG_HOOK_VOID(Fn,ListDrawIndexedInstanced)call(self,indices,instances,startIndex,baseVertex,startInstance); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listDispatch(DevicePtr self,UINT x,UINT y,UINT z) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT);
    D3D12_DIAG_HOOK_VOID(Fn,ListDispatch)call(self,x,y,z); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listCopyResource(DevicePtr self,ID3D12Resource* dst,ID3D12Resource* src) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,ID3D12Resource*);
    D3D12_DIAG_HOOK_VOID(Fn,ListCopyResource)call(self,dst,src); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listCopyBufferRegion(DevicePtr self,ID3D12Resource* dst,UINT64 dstOffset,ID3D12Resource* src,UINT64 srcOffset,UINT64 bytes) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,UINT64,ID3D12Resource*,UINT64,UINT64);
    D3D12_DIAG_HOOK_VOID(Fn,ListCopyBufferRegion)call(self,dst,dstOffset,src,srcOffset,bytes); traceEndVoid(traceAt);
}
// CopyTextureRegion(pDst, DstX, DstY, DstZ, pSrc, pSrcBox): the destination
// position is three UINTs. The wrapper took one (a subresource index) before.
static void STDMETHODCALLTYPE listCopyTextureRegion(DevicePtr self,const D3D12_TEXTURE_COPY_LOCATION* dst,UINT dstX,UINT dstY,UINT dstZ,
        const D3D12_TEXTURE_COPY_LOCATION* src,const D3D12_BOX* box) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_TEXTURE_COPY_LOCATION*,UINT,UINT,UINT,const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*);
    D3D12_DIAG_HOOK_VOID(Fn,ListCopyTextureRegion)call(self,dst,dstX,dstY,dstZ,src,box); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listResolveSubresource(DevicePtr self,ID3D12Resource* dst,UINT dstSub,ID3D12Resource* src,UINT srcSub,DXGI_FORMAT format) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,UINT,ID3D12Resource*,UINT,DXGI_FORMAT);
    D3D12_DIAG_HOOK_VOID(Fn,ListResolveSubresource)call(self,dst,dstSub,src,srcSub,format); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listOMSetRenderTargets(DevicePtr self,UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,BOOL single,const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*);
    D3D12_DIAG_HOOK_VOID(Fn,ListOMSetRenderTargets)call(self,count,rtvs,single,dsv); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listClearRenderTargetView(DevicePtr self,D3D12_CPU_DESCRIPTOR_HANDLE rtv,const FLOAT colour[4],UINT rects,const D3D12_RECT* r) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,D3D12_CPU_DESCRIPTOR_HANDLE,const FLOAT*,UINT,const D3D12_RECT*);
    D3D12_DIAG_HOOK_VOID(Fn,ListClearRenderTargetView)call(self,rtv,colour,rects,r); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listClearDepthStencilView(DevicePtr self,D3D12_CPU_DESCRIPTOR_HANDLE dsv,D3D12_CLEAR_FLAGS flags,FLOAT depth,UINT8 stencil,UINT rects,const D3D12_RECT* r) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS,FLOAT,UINT8,UINT,const D3D12_RECT*);
    D3D12_DIAG_HOOK_VOID(Fn,ListClearDepthStencilView)call(self,dsv,flags,depth,stencil,rects,r); traceEndVoid(traceAt);
}
static void STDMETHODCALLTYPE listExecuteIndirect(DevicePtr self,ID3D12CommandSignature* signature,UINT maxCount,ID3D12Resource* args,UINT64 argsOffset,ID3D12Resource* count,UINT64 countOffset) {
    using Fn=void(STDMETHODCALLTYPE*)(DevicePtr,ID3D12CommandSignature*,UINT,ID3D12Resource*,UINT64,ID3D12Resource*,UINT64);
    D3D12_DIAG_HOOK_VOID(Fn,ListExecuteIndirect)call(self,signature,maxCount,args,argsOffset,count,countOffset); traceEndVoid(traceAt);
}

// ---- fences and resources: the calls a frame's pacing and uploads go through
static HRESULT STDMETHODCALLTYPE fenceSetEventOnCompletion(DevicePtr self,UINT64 value,HANDLE event) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT64,HANDLE);
    D3D12_DIAG_HOOK(Fn,FenceSetEventOnCompletion)HRESULT hr=call(self,value,event); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("Fence::SetEventOnCompletion",fmt("%llu/%d",(unsigned long long)value,event!=nullptr),
        fmt("value=%llu event=%d",(unsigned long long)value,event!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE fenceSignal(DevicePtr self,UINT64 value) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT64);
    D3D12_DIAG_HOOK(Fn,FenceSignal)HRESULT hr=call(self,value); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("Fence::Signal",fmt("%llu",(unsigned long long)value),fmt("value=%llu",(unsigned long long)value));
    return hr;
}
static HRESULT STDMETHODCALLTYPE resourceMap(DevicePtr self,UINT sub,const D3D12_RANGE* read,void** data) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,const D3D12_RANGE*,void**);
    D3D12_DIAG_HOOK(Fn,ResourceMap)HRESULT hr=call(self,sub,read,data); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("Resource::Map",fmt("%u",sub),fmt("subresource=%u range=%d",sub,read!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE allocatorReset(DevicePtr self) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr);
    D3D12_DIAG_HOOK(Fn,AllocatorReset)HRESULT hr=call(self); traceEnd(traceAt,hr);
    D3D12_DIAG_FAILED("CommandAllocator::Reset","","");
    return hr;
}

// ---- the header decides each hooked slot's return kind ----
// Every hook's declared return type must equal the return type of the interface
// method it replaces in d3d12.h (C++ member declarations). A mismatch is a compile
// error, so a void method cannot be traced as HRESULT, and the reverse.
template<class M> struct MethodReturn;
template<class C,class R,class...A> struct MethodReturn<R(C::*)(A...)> { using type=R; };
template<class F> struct FunctionReturn;
template<class R,class...A> struct FunctionReturn<R(*)(A...)> { using type=R; };

// ---- the hooks each interface level gets
// ---- each hook matches its method in d3d12.h: arguments and return kind ----
// A hook takes (self, the method's arguments in order). Argument order, count and
// type are compared as types, so a wrapper that drops or swaps an argument is a
// compile error. The return type is not checked here.
template<class M> struct MethodArgs;
template<class C,class R,class...A> struct MethodArgs<R(C::*)(A...)> { using type=std::tuple<A...>; };
template<class F> struct HookArgs;
template<class R,class...A> struct HookArgs<R(*)(A...)> { using type=std::tuple<A...>; };
template<class T> struct DropSelf;
template<class H,class...T> struct DropSelf<std::tuple<H,T...>> { using type=std::tuple<T...>; using self=H; };
#define DIAG_PIN(Iface,Method,Hook) static_assert(\
    std::is_same<typename DropSelf<typename HookArgs<decltype(&Hook)>::type>::type,typename MethodArgs<decltype(&Iface::Method)>::type>::value&&\
    std::is_same<typename DropSelf<typename HookArgs<decltype(&Hook)>::type>::self,void*>::value&&\
    std::is_same<typename MethodReturn<decltype(&Iface::Method)>::type,typename FunctionReturn<decltype(&Hook)>::type>::value,\
    #Hook " must match " #Iface "::" #Method " in d3d12.h (arguments and return)")
DIAG_PIN(ID3D12Device,CreateCommandQueue,createCommandQueue);
DIAG_PIN(ID3D12Device,CreateCommandAllocator,createCommandAllocator);
DIAG_PIN(ID3D12Device,CreateCommandList,createCommandList);
DIAG_PIN(ID3D12Device,CreateGraphicsPipelineState,createGraphicsPipelineState);
DIAG_PIN(ID3D12Device,CreateComputePipelineState,createComputePipelineState);
DIAG_PIN(ID3D12Device,CheckFeatureSupport,checkFeatureSupport);
DIAG_PIN(ID3D12Device,CreateDescriptorHeap,createDescriptorHeap);
DIAG_PIN(ID3D12Device,CreateRootSignature,createRootSignature);
DIAG_PIN(ID3D12Device,CreateConstantBufferView,createCbv);
DIAG_PIN(ID3D12Device,CreateShaderResourceView,createSrv);
DIAG_PIN(ID3D12Device,CreateUnorderedAccessView,createUav);
DIAG_PIN(ID3D12Device,CreateRenderTargetView,createRtv);
DIAG_PIN(ID3D12Device,CreateDepthStencilView,createDsv);
DIAG_PIN(ID3D12Device,CreateSampler,createSampler);
DIAG_PIN(ID3D12Device,CreateCommittedResource,createCommittedResource);
DIAG_PIN(ID3D12Device,CreateHeap,createHeap);
DIAG_PIN(ID3D12Device,CreatePlacedResource,createPlacedResource);
DIAG_PIN(ID3D12Device,CreateReservedResource,createReservedResource);
DIAG_PIN(ID3D12Device,CreateFence,createFence);
DIAG_PIN(ID3D12Device,CreateQueryHeap,createQueryHeap);
DIAG_PIN(ID3D12Device,CreateCommandSignature,createCommandSignature);
DIAG_PIN(ID3D12Device2,CreatePipelineState,createPipelineStateStream);
DIAG_PIN(ID3D12CommandQueue,ExecuteCommandLists,queueExecuteCommandLists);
DIAG_PIN(ID3D12CommandQueue,Signal,queueSignal);
DIAG_PIN(ID3D12CommandQueue,Wait,queueWait);
DIAG_PIN(ID3D12GraphicsCommandList,Close,listClose);
DIAG_PIN(ID3D12GraphicsCommandList,Reset,listReset);
DIAG_PIN(ID3D12GraphicsCommandList,SetPipelineState,listSetPipelineState);
DIAG_PIN(ID3D12GraphicsCommandList,ResourceBarrier,listResourceBarrier);
DIAG_PIN(ID3D12GraphicsCommandList,SetDescriptorHeaps,listSetDescriptorHeaps);
DIAG_PIN(ID3D12GraphicsCommandList,DrawInstanced,listDrawInstanced);
DIAG_PIN(ID3D12GraphicsCommandList,DrawIndexedInstanced,listDrawIndexedInstanced);
DIAG_PIN(ID3D12GraphicsCommandList,Dispatch,listDispatch);
DIAG_PIN(ID3D12GraphicsCommandList,CopyResource,listCopyResource);
DIAG_PIN(ID3D12GraphicsCommandList,CopyBufferRegion,listCopyBufferRegion);
DIAG_PIN(ID3D12GraphicsCommandList,CopyTextureRegion,listCopyTextureRegion);
DIAG_PIN(ID3D12GraphicsCommandList,ResolveSubresource,listResolveSubresource);
DIAG_PIN(ID3D12GraphicsCommandList,OMSetRenderTargets,listOMSetRenderTargets);
DIAG_PIN(ID3D12GraphicsCommandList,ClearRenderTargetView,listClearRenderTargetView);
DIAG_PIN(ID3D12GraphicsCommandList,ClearDepthStencilView,listClearDepthStencilView);
DIAG_PIN(ID3D12GraphicsCommandList,ExecuteIndirect,listExecuteIndirect);
DIAG_PIN(ID3D12Fence,SetEventOnCompletion,fenceSetEventOnCompletion);
DIAG_PIN(ID3D12Fence,Signal,fenceSignal);
DIAG_PIN(ID3D12CommandAllocator,Reset,allocatorReset);
DIAG_PIN(ID3D12Resource,Map,resourceMap);

inline const Patch* devicePatches(size_t& count){
    static const Patch table[]={
        {CreateCommandQueue,(void*)&createCommandQueue},{CreateCommandAllocator,(void*)&createCommandAllocator},
        {CreateGraphicsPipelineState,(void*)&createGraphicsPipelineState},
        {CreateComputePipelineState,(void*)&createComputePipelineState},{CreateCommandList,(void*)&createCommandList},{CheckFeatureSupport,(void*)&checkFeatureSupport},
        {CreateDescriptorHeap,(void*)&createDescriptorHeap},{CreateRootSignature,(void*)&createRootSignature},
        {CreateConstantBufferView,(void*)&createCbv},{CreateShaderResourceView,(void*)&createSrv},
        {CreateUnorderedAccessView,(void*)&createUav},{CreateRenderTargetView,(void*)&createRtv},
        {CreateDepthStencilView,(void*)&createDsv},{CreateSampler,(void*)&createSampler},
        {CreateCommittedResource,(void*)&createCommittedResource},{CreateHeap,(void*)&createHeap},
        {CreatePlacedResource,(void*)&createPlacedResource},{CreateReservedResource,(void*)&createReservedResource},
        {CreateFence,(void*)&createFence},{CreateQueryHeap,(void*)&createQueryHeap},
        {CreateCommandSignature,(void*)&createCommandSignature}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* queuePatches(size_t& count){
    static const Patch table[]={{QueueExecuteCommandLists,(void*)&queueExecuteCommandLists},
        {QueueSignal,(void*)&queueSignal},{QueueWait,(void*)&queueWait}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* listPatches(size_t& count){
    static const Patch table[]={{ListClose,(void*)&listClose},{ListReset,(void*)&listReset},
        {ListSetPipelineState,(void*)&listSetPipelineState},{ListResourceBarrier,(void*)&listResourceBarrier},
        {ListSetDescriptorHeaps,(void*)&listSetDescriptorHeaps},{ListDrawInstanced,(void*)&listDrawInstanced},
        {ListDrawIndexedInstanced,(void*)&listDrawIndexedInstanced},{ListDispatch,(void*)&listDispatch},
        {ListCopyResource,(void*)&listCopyResource},{ListCopyBufferRegion,(void*)&listCopyBufferRegion},
        {ListCopyTextureRegion,(void*)&listCopyTextureRegion},{ListResolveSubresource,(void*)&listResolveSubresource},
        {ListOMSetRenderTargets,(void*)&listOMSetRenderTargets},{ListClearRenderTargetView,(void*)&listClearRenderTargetView},
        {ListClearDepthStencilView,(void*)&listClearDepthStencilView},{ListExecuteIndirect,(void*)&listExecuteIndirect}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* fencePatches(size_t& count){
    static const Patch table[]={{FenceSetEventOnCompletion,(void*)&fenceSetEventOnCompletion},{FenceSignal,(void*)&fenceSignal}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* resourcePatches(size_t& count){
    static const Patch table[]={{ResourceMap,(void*)&resourceMap}};
    count=sizeof table/sizeof *table;return table;
}
inline const Patch* allocatorPatches(size_t& count){
    static const Patch table[]={{AllocatorReset,(void*)&allocatorReset}};
    count=sizeof table/sizeof *table;return table;
}
// Patch the object class a successful creation returned, at the interface
// level the caller asked for. A creation the guest later QIs to a higher level
// is patched then, never by writing a slot the lower vtable lacks.
template<class T> inline void adoptObject(T* object,const Patch* patches,size_t count) {
    if(object&&patches&&count)patchClass(*reinterpret_cast<void***>(object),patches,count);
}
// The table is fetched before the call: passing queuePatches(n) and n in one
// argument list leaves the order unspecified, and the count read first is 0.
inline void adoptQueue(ID3D12CommandQueue* queue) {
    if(!queue)return;
    size_t n=0;const Patch* patches=queuePatches(n);adoptObject(queue,patches,n);
}
inline void adoptList(ID3D12GraphicsCommandList* list) {
    if(!list)return;
    size_t n=0;const Patch* patches=listPatches(n);adoptObject(list,patches,n);
}
inline void adoptFence(ID3D12Fence* fence) {
    if(!fence)return;
    size_t n=0;const Patch* patches=fencePatches(n);adoptObject(fence,patches,n);
}
inline void adoptAllocator(ID3D12CommandAllocator* allocator) {
    if(!allocator)return;
    size_t n=0;const Patch* patches=allocatorPatches(n);adoptObject(allocator,patches,n);
}
inline void adoptResource(ID3D12Resource* resource) {
    if(!resource)return;
    size_t n=0;const Patch* patches=resourcePatches(n);adoptObject(resource,patches,n);
}
// ---- entry points
inline void configure(Sink s){sink=s;}
// The stream-path hook goes only through the vtable of a pointer that really
// implements ID3D12Device2. ID3D12Device has 44 methods (slots 0-43), so slot 47
// is not a slot of its table; a device without ID3D12Device2 is skipped, logged.
inline void adoptStreamPath(IUnknown* device) {
    ID3D12Device2* device2=nullptr;
    if(FAILED(device->QueryInterface(__uuidof(ID3D12Device2),reinterpret_cast<void**>(&device2)))||!device2) {
        emit("diagnostics: device has no ID3D12Device2; CreatePipelineState stream hook not installed");
        return;
    }
    const Patch stream{createPipelineStateStreamSlot,(void*)&createPipelineStateStream};
    patchClass(*reinterpret_cast<void***>(device2),&stream,1);
    device2->Release();
}
// Called with every device Play gives a guest. Logs the answers that pick a
// game's renderer path: shader model (SM6 availability), UMA/tile-based
// architecture, feature levels, and the adapter LUID.
inline void adopt(ID3D12Device* device) {
    if(!device||!sink)return;
    if(!traceChecked.exchange(true,std::memory_order_acq_rel)) {
        traceOn.store(set_trace_requested&&set_trace_requested(),std::memory_order_release);
        if(traceOn.load(std::memory_order_acquire))
            emit("trace on (LocalState\\d3d12trace.txt): the last hooked calls are kept and flushed after a fault");
    }
    size_t count=0;const Patch* patches=devicePatches(count);
    if(!patchClass(*reinterpret_cast<void***>(device),patches,count))return;
    adoptStreamPath(device);
    emit(fmt("diagnostics on: failed D3D12 create/query/submit calls are logged once per distinct call (caps %llu/%llu)",
        (unsigned long long)kMaxFailures,(unsigned long long)kMaxProbes));
    report(true,"device node count",fmt("device nodes=%u",device->GetNodeCount()));
    // Shader model decides whether a game may use SM6.x and mesh shaders; the
    // architecture answer (UMA, tile-based) picks the memory strategy.
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
    shaderModel.HighestShaderModel=D3D_SHADER_MODEL_6_6;
    device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shaderModel,sizeof shaderModel);
    D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
    device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE,&architecture,sizeof architecture);
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS,&options,sizeof options);
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options5,sizeof options5);
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpuVa{};
    device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT,&gpuVa,sizeof gpuVa);
    report(true,"device removed reason",fmt("device removed reason=0x%08lx",(unsigned long)device->GetDeviceRemovedReason()));
    LUID luid=device->GetAdapterLuid();
    report(true,"adapter luid",fmt("adapter luid=%08lx:%08lx",(unsigned long)luid.HighPart,(unsigned long)luid.LowPart));
}
#undef D3D12_DIAG_HOOK
#undef D3D12_DIAG_FAILED
}
