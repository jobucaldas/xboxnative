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

#define D3D12_DIAG_HOOK(Type,slot) auto call=reinterpret_cast<Type>(original(self,slot));if(!call)return E_FAIL;
#define D3D12_DIAG_FAILED(name,key,text) do{if(FAILED(hr))report(false,std::string(name)+" "+hex(hr)+" "+(key),std::string(name)+" hr="+hex(hr)+" "+(text));}while(0)

using DevicePtr=void*;
static HRESULT STDMETHODCALLTYPE createCommandQueue(DevicePtr self,const D3D12_COMMAND_QUEUE_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandQueue)HRESULT hr=call(self,d,id,out);
    if(d)D3D12_DIAG_FAILED("CreateCommandQueue",fmt("%d/%d/%d/%d",int(d->Type),int(d->Priority),int(d->Flags),int(d->NodeMask)),
        fmt("type=%d priority=%d flags=0x%x node=%u",int(d->Type),d->Priority,d->Flags,d->NodeMask));
    else D3D12_DIAG_FAILED("CreateCommandQueue","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommandAllocator(DevicePtr self,D3D12_COMMAND_LIST_TYPE type,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D12_COMMAND_LIST_TYPE,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandAllocator)HRESULT hr=call(self,type,id,out);
    D3D12_DIAG_FAILED("CreateCommandAllocator",fmt("%d",int(type)),fmt("type=%d",int(type)));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommandList(DevicePtr self,UINT node,D3D12_COMMAND_LIST_TYPE type,ID3D12CommandAllocator* allocator,
        ID3D12PipelineState* state,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandList)HRESULT hr=call(self,node,type,allocator,state,id,out);
    D3D12_DIAG_FAILED("CreateCommandList",fmt("%d/%d/%d/%d",int(type),node,allocator!=nullptr,state!=nullptr),
        fmt("type=%d node=%u allocator=%d state=%d",int(type),node,allocator!=nullptr,state!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createGraphicsPipelineState(DevicePtr self,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateGraphicsPipelineState)HRESULT hr=call(self,d,id,out);
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
inline std::string streamSubobjects(const void* stream,size_t bytes) {
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
        // The next subobject starts at the next pointer-aligned offset after
        // the payload: each one is an alignas(void*) struct in the real stream.
        p=typeStart+((payloadAt+e->size+sizeof(void*)-1)&~(sizeof(void*)-1));
        if(subs.size()>400){subs+="...";break;}
    }
    return fmt("bytes=%zu%s subobjects=[%s]",bytes,truncated?" truncated":"",subs.c_str());
}
static HRESULT STDMETHODCALLTYPE createPipelineStateStream(DevicePtr self,const D3D12_PIPELINE_STATE_STREAM_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,createPipelineStateStreamSlot)HRESULT hr=call(self,d,id,out);
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
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateComputePipelineState)HRESULT hr=call(self,d,id,out);
    if(d) {
        std::string key,cs=shaderText(d->CS.pShaderBytecode,d->CS.BytecodeLength,&key);
        D3D12_DIAG_FAILED("CreateComputePipelineState",key,fmt("cs[%s]",cs.c_str()));
    } else D3D12_DIAG_FAILED("CreateComputePipelineState","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createDescriptorHeap(DevicePtr self,const D3D12_DESCRIPTOR_HEAP_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_DESCRIPTOR_HEAP_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateDescriptorHeap)HRESULT hr=call(self,d,id,out);
    if(d)D3D12_DIAG_FAILED("CreateDescriptorHeap",fmt("%d/%u/%x",int(d->Type),d->NumDescriptors,d->Flags),
        fmt("type=%d descriptors=%u flags=0x%x node=%u",int(d->Type),d->NumDescriptors,d->Flags,d->NodeMask));
    else D3D12_DIAG_FAILED("CreateDescriptorHeap","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createRootSignature(DevicePtr self,UINT node,const void* blob,SIZE_T length,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,const void*,SIZE_T,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateRootSignature)HRESULT hr=call(self,node,blob,length,id,out);
    if(FAILED(hr)) {
        std::string key;std::string text=shaderText(blob,length,&key);
        D3D12_DIAG_FAILED("CreateRootSignature",key,fmt("node=%u %s",node,text.c_str()));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommittedResource(DevicePtr self,const D3D12_HEAP_PROPERTIES* heap,D3D12_HEAP_FLAGS heapFlags,
        const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommittedResource)HRESULT hr=call(self,heap,heapFlags,d,state,clear,id,out);
    if(FAILED(hr)) {
        D3D12_DIAG_FAILED("CreateCommittedResource",resourceKey(d)+fmt("/%d/%x",int(state),heapFlags),
            fmt("heap=%s heapflags=0x%x state=%s(%d) %s",heap?heapText(heap->Type).c_str():"null",heapFlags,
                resourceStateName(state),int(state),resourceDescText(d).c_str()));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createHeap(DevicePtr self,const D3D12_HEAP_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_HEAP_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateHeap)HRESULT hr=call(self,d,id,out);
    if(d)D3D12_DIAG_FAILED("CreateHeap",fmt("%llu/%x/%d",(unsigned long long)d->SizeInBytes,d->Flags,int(d->Properties.Type)),
        fmt("bytes=%llu flags=0x%x heap=%s align=%llu",(unsigned long long)d->SizeInBytes,d->Flags,
            heapText(d->Properties.Type).c_str(),(unsigned long long)d->Alignment));
    else D3D12_DIAG_FAILED("CreateHeap","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createPlacedResource(DevicePtr self,ID3D12Heap* heap,UINT64 offset,const D3D12_RESOURCE_DESC* d,
        D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Heap*,UINT64,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreatePlacedResource)HRESULT hr=call(self,heap,offset,d,state,clear,id,out);
    if(FAILED(hr))D3D12_DIAG_FAILED("CreatePlacedResource",resourceKey(d)+fmt("/%d",int(state)),
        fmt("heap=%d offset=%llu state=%s(%d) %s",heap!=nullptr,(unsigned long long)offset,resourceStateName(state),int(state),resourceDescText(d).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createReservedResource(DevicePtr self,const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES state,
        const D3D12_CLEAR_VALUE* clear,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateReservedResource)HRESULT hr=call(self,d,state,clear,id,out);
    if(FAILED(hr))D3D12_DIAG_FAILED("CreateReservedResource",resourceKey(d)+fmt("/%d",int(state)),
        fmt("state=%s(%d) %s",resourceStateName(state),int(state),resourceDescText(d).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createFence(DevicePtr self,UINT64 value,D3D12_FENCE_FLAGS flags,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT64,D3D12_FENCE_FLAGS,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateFence)HRESULT hr=call(self,value,flags,id,out);
    D3D12_DIAG_FAILED("CreateFence",fmt("%x",flags),fmt("value=%llu flags=0x%x",(unsigned long long)value,flags));
    return hr;
}
static HRESULT STDMETHODCALLTYPE createQueryHeap(DevicePtr self,const D3D12_QUERY_HEAP_DESC* d,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_QUERY_HEAP_DESC*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateQueryHeap)HRESULT hr=call(self,d,id,out);
    if(d)D3D12_DIAG_FAILED("CreateQueryHeap",fmt("%d/%u",int(d->Type),d->Count),fmt("type=%d count=%u node=%u",int(d->Type),d->Count,d->NodeMask));
    else D3D12_DIAG_FAILED("CreateQueryHeap","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCommandSignature(DevicePtr self,const D3D12_COMMAND_SIGNATURE_DESC* d,ID3D12RootSignature* root,REFIID id,void** out) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_COMMAND_SIGNATURE_DESC*,ID3D12RootSignature*,REFIID,void**);
    D3D12_DIAG_HOOK(Fn,CreateCommandSignature)HRESULT hr=call(self,d,root,id,out);
    if(d)D3D12_DIAG_FAILED("CreateCommandSignature",fmt("%u/%u",d->ByteStride,d->NumArgumentDescs),
        fmt("stride=%u args=%u root=%d",d->ByteStride,d->NumArgumentDescs,root!=nullptr));
    else D3D12_DIAG_FAILED("CreateCommandSignature","null","desc=null");
    return hr;
}
// Views share one shape: (resource, optional desc, dest handle). Only the
// description type differs.
template<int Slot,class Desc,class Describe>
static HRESULT viewCall(DevicePtr self,const char* name,ID3D12Resource* resource,const Desc* d,D3D12_CPU_DESCRIPTOR_HANDLE handle,Describe describe) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,const Desc*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK(Fn,Slot)HRESULT hr=call(self,resource,d,handle);
    if(FAILED(hr)) {
        std::string text,key;describe(d,text,key);
        D3D12_DIAG_FAILED(name,key+fmt("/%llx",(unsigned long long)handle.ptr),
            text+fmt(" handle=0x%llx res=%d",(unsigned long long)handle.ptr,resource!=nullptr));
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE createSrv(DevicePtr self,ID3D12Resource* r,const D3D12_SHADER_RESOURCE_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    return viewCall<CreateShaderResourceView>(self,"CreateShaderResourceView",r,d,h,[](const D3D12_SHADER_RESOURCE_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createUav(DevicePtr self,ID3D12Resource* r,const D3D12_UNORDERED_ACCESS_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    return viewCall<CreateUnorderedAccessView>(self,"CreateUnorderedAccessView",r,d,h,[](const D3D12_UNORDERED_ACCESS_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createRtv(DevicePtr self,ID3D12Resource* r,const D3D12_RENDER_TARGET_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    return viewCall<CreateRenderTargetView>(self,"CreateRenderTargetView",r,d,h,[](const D3D12_RENDER_TARGET_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d",formatText(v->Format).c_str(),int(v->ViewDimension));key=fmt("%d/%d",int(v->Format),int(v->ViewDimension));});
}
static HRESULT STDMETHODCALLTYPE createDsv(DevicePtr self,ID3D12Resource* r,const D3D12_DEPTH_STENCIL_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    return viewCall<CreateDepthStencilView>(self,"CreateDepthStencilView",r,d,h,[](const D3D12_DEPTH_STENCIL_VIEW_DESC* v,std::string& text,std::string& key){
        if(!v){text="view=default";key="default";return;}
        text=fmt("viewfmt=%s dim=%d flags=0x%x",formatText(v->Format).c_str(),int(v->ViewDimension),v->Flags);key=fmt("%d/%d/%x",int(v->Format),int(v->ViewDimension),v->Flags);});
}
static HRESULT STDMETHODCALLTYPE createSampler(DevicePtr self,const D3D12_SAMPLER_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_SAMPLER_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK(Fn,CreateSampler)HRESULT hr=call(self,d,h);
    if(d)D3D12_DIAG_FAILED("CreateSampler",fmt("%x/%d/%d/%d/%u",unsigned(d->Filter),int(d->AddressU),int(d->AddressV),int(d->AddressW),d->MaxAnisotropy),
        fmt("filter=0x%x address=%d/%d/%d aniso=%u cmp=%d lod=%g..%g",unsigned(d->Filter),int(d->AddressU),int(d->AddressV),int(d->AddressW),d->MaxAnisotropy,int(d->ComparisonFunc),double(d->MinLOD),double(d->MaxLOD)));
    else D3D12_DIAG_FAILED("CreateSampler","null","desc=null");
    return hr;
}
static HRESULT STDMETHODCALLTYPE createCbv(DevicePtr self,const D3D12_CONSTANT_BUFFER_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_CONSTANT_BUFFER_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
    D3D12_DIAG_HOOK(Fn,CreateConstantBufferView)HRESULT hr=call(self,d,h);
    if(d)D3D12_DIAG_FAILED("CreateConstantBufferView",fmt("%llu/%u",(unsigned long long)d->BufferLocation,d->SizeInBytes),
        fmt("gpuva=0x%llx bytes=%u",(unsigned long long)d->BufferLocation,d->SizeInBytes));
    else D3D12_DIAG_FAILED("CreateConstantBufferView","null","desc=null");
    return hr;
}

// ---- capability queries: the answers that pick a game's renderer path
static HRESULT STDMETHODCALLTYPE checkFeatureSupport(DevicePtr self,D3D12_FEATURE feature,void* data,UINT size) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D12_FEATURE,void*,UINT);
    D3D12_DIAG_HOOK(Fn,CheckFeatureSupport)HRESULT hr=call(self,feature,data,size);
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
static HRESULT STDMETHODCALLTYPE queueExecuteCommandLists(DevicePtr self,UINT count,ID3D12CommandList* const* lists) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D12CommandList* const*);
    D3D12_DIAG_HOOK(Fn,QueueExecuteCommandLists)HRESULT hr=call(self,count,lists);
    D3D12_DIAG_FAILED("ExecuteCommandLists",fmt("%u",count),fmt("lists=%u",count));
    return hr;
}
static HRESULT STDMETHODCALLTYPE queueSignal(DevicePtr self,ID3D12Fence* fence,UINT64 value) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Fence*,UINT64);
    D3D12_DIAG_HOOK(Fn,QueueSignal)HRESULT hr=call(self,fence,value);
    D3D12_DIAG_FAILED("CommandQueue::Signal",fmt("%d",fence!=nullptr),fmt("fence=%d value=%llu",fence!=nullptr,(unsigned long long)value));
    return hr;
}
static HRESULT STDMETHODCALLTYPE queueWait(DevicePtr self,ID3D12Fence* fence,UINT64 value) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Fence*,UINT64);
    D3D12_DIAG_HOOK(Fn,QueueWait)HRESULT hr=call(self,fence,value);
    D3D12_DIAG_FAILED("CommandQueue::Wait",fmt("%d",fence!=nullptr),fmt("fence=%d value=%llu",fence!=nullptr,(unsigned long long)value));
    return hr;
}

// ---- command list: Close/Reset decide whether a frame is ever submitted
static HRESULT STDMETHODCALLTYPE listClose(DevicePtr self) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr);
    D3D12_DIAG_HOOK(Fn,ListClose)HRESULT hr=call(self);
    D3D12_DIAG_FAILED("CommandList::Close","","");
    return hr;
}
static HRESULT STDMETHODCALLTYPE listReset(DevicePtr self,ID3D12CommandAllocator* allocator,ID3D12PipelineState* state) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12CommandAllocator*,ID3D12PipelineState*);
    D3D12_DIAG_HOOK(Fn,ListReset)HRESULT hr=call(self,allocator,state);
    D3D12_DIAG_FAILED("CommandList::Reset",fmt("%d/%d",allocator!=nullptr,state!=nullptr),fmt("allocator=%d state=%d",allocator!=nullptr,state!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listSetPipelineState(DevicePtr self,ID3D12PipelineState* state) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12PipelineState*);
    D3D12_DIAG_HOOK(Fn,ListSetPipelineState)HRESULT hr=call(self,state);
    D3D12_DIAG_FAILED("SetPipelineState",fmt("%d",state!=nullptr),fmt("state=%d",state!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listResourceBarrier(DevicePtr self,UINT count,const D3D12_RESOURCE_BARRIER* barriers) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,const D3D12_RESOURCE_BARRIER*);
    D3D12_DIAG_HOOK(Fn,ListResourceBarrier)HRESULT hr=call(self,count,barriers);
    if(FAILED(hr)) {
        std::string text="barriers="+u64(count);
        if(barriers&&count)text+=fmt(" first=%d %s->%s",int(barriers[0].Type),
            resourceStateName(barriers[0].Transition.StateBefore),resourceStateName(barriers[0].Transition.StateAfter));
        D3D12_DIAG_FAILED("ResourceBarrier",fmt("%u",count),text);
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE listSetDescriptorHeaps(DevicePtr self,UINT count,ID3D12DescriptorHeap* const* heaps) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,ID3D12DescriptorHeap* const*);
    D3D12_DIAG_HOOK(Fn,ListSetDescriptorHeaps)HRESULT hr=call(self,count,heaps);
    D3D12_DIAG_FAILED("SetDescriptorHeaps",fmt("%u",count),fmt("heaps=%u",count));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listDrawInstanced(DevicePtr self,UINT vertices,UINT instances,UINT startVertex,UINT startInstance) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT,UINT);
    D3D12_DIAG_HOOK(Fn,ListDrawInstanced)HRESULT hr=call(self,vertices,instances,startVertex,startInstance);
    D3D12_DIAG_FAILED("DrawInstanced",fmt("%u/%u",vertices,instances),fmt("vertices=%u instances=%u start=%u/%u",vertices,instances,startVertex,startInstance));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listDrawIndexedInstanced(DevicePtr self,UINT indices,UINT instances,UINT startIndex,INT baseVertex,UINT startInstance) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT,INT,UINT);
    D3D12_DIAG_HOOK(Fn,ListDrawIndexedInstanced)HRESULT hr=call(self,indices,instances,startIndex,baseVertex,startInstance);
    D3D12_DIAG_FAILED("DrawIndexedInstanced",fmt("%u/%u",indices,instances),fmt("indices=%u instances=%u start=%u base=%d",indices,instances,startIndex,baseVertex));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listDispatch(DevicePtr self,UINT x,UINT y,UINT z) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,UINT,UINT);
    D3D12_DIAG_HOOK(Fn,ListDispatch)HRESULT hr=call(self,x,y,z);
    D3D12_DIAG_FAILED("Dispatch",fmt("%u/%u/%u",x,y,z),fmt("groups=%u/%u/%u",x,y,z));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listCopyResource(DevicePtr self,ID3D12Resource* dst,ID3D12Resource* src) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,ID3D12Resource*);
    D3D12_DIAG_HOOK(Fn,ListCopyResource)HRESULT hr=call(self,dst,src);
    D3D12_DIAG_FAILED("CopyResource",fmt("%d/%d",dst!=nullptr,src!=nullptr),fmt("dst=%d src=%d",dst!=nullptr,src!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listCopyBufferRegion(DevicePtr self,ID3D12Resource* dst,UINT64 dstOffset,ID3D12Resource* src,UINT64 srcOffset,UINT64 bytes) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,UINT64,ID3D12Resource*,UINT64,UINT64);
    D3D12_DIAG_HOOK(Fn,ListCopyBufferRegion)HRESULT hr=call(self,dst,dstOffset,src,srcOffset,bytes);
    D3D12_DIAG_FAILED("CopyBufferRegion",fmt("%d/%d",dst!=nullptr,src!=nullptr),
        fmt("dst=%d@%llu src=%d@%llu bytes=%llu",dst!=nullptr,(unsigned long long)dstOffset,src!=nullptr,(unsigned long long)srcOffset,(unsigned long long)bytes));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listCopyTextureRegion(DevicePtr self,const D3D12_TEXTURE_COPY_LOCATION* dst,UINT dstSub,
        const D3D12_TEXTURE_COPY_LOCATION* src,const D3D12_BOX* box) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,const D3D12_TEXTURE_COPY_LOCATION*,UINT,const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*);
    D3D12_DIAG_HOOK(Fn,ListCopyTextureRegion)HRESULT hr=call(self,dst,dstSub,src,box);
    D3D12_DIAG_FAILED("CopyTextureRegion",fmt("%d/%d/%u",dst!=nullptr,src!=nullptr,dstSub),
        fmt("dst=%d sub=%u src=%d box=%d",dst!=nullptr,dstSub,src!=nullptr,box!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listResolveSubresource(DevicePtr self,ID3D12Resource* dst,UINT dstSub,ID3D12Resource* src,UINT srcSub,DXGI_FORMAT format) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12Resource*,UINT,ID3D12Resource*,UINT,DXGI_FORMAT);
    D3D12_DIAG_HOOK(Fn,ListResolveSubresource)HRESULT hr=call(self,dst,dstSub,src,srcSub,format);
    D3D12_DIAG_FAILED("ResolveSubresource",fmt("%d/%d/%u",dst!=nullptr,src!=nullptr,srcSub),
        fmt("dst=%d sub=%u src=%d sub=%u fmt=%s",dst!=nullptr,dstSub,src!=nullptr,srcSub,formatText(format).c_str()));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listOMSetRenderTargets(DevicePtr self,UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,BOOL single,const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*);
    D3D12_DIAG_HOOK(Fn,ListOMSetRenderTargets)HRESULT hr=call(self,count,rtvs,single,dsv);
    D3D12_DIAG_FAILED("OMSetRenderTargets",fmt("%u/%d",count,single),fmt("rtvs=%u contiguous=%d dsv=%d",count,single,dsv!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listClearRenderTargetView(DevicePtr self,D3D12_CPU_DESCRIPTOR_HANDLE rtv,const FLOAT colour[4],UINT rects,const D3D12_RECT* r) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D12_CPU_DESCRIPTOR_HANDLE,const FLOAT*,UINT,const D3D12_RECT*);
    D3D12_DIAG_HOOK(Fn,ListClearRenderTargetView)HRESULT hr=call(self,rtv,colour,rects,r);
    if(FAILED(hr))D3D12_DIAG_FAILED("ClearRenderTargetView",fmt("%llx",(unsigned long long)rtv.ptr),fmt("rtv=0x%llx rects=%u",(unsigned long long)rtv.ptr,rects));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listClearDepthStencilView(DevicePtr self,D3D12_CPU_DESCRIPTOR_HANDLE dsv,D3D12_CLEAR_FLAGS flags,FLOAT depth,UINT8 stencil,UINT rects,const D3D12_RECT* r) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS,FLOAT,UINT8,UINT,const D3D12_RECT*);
    D3D12_DIAG_HOOK(Fn,ListClearDepthStencilView)HRESULT hr=call(self,dsv,flags,depth,stencil,rects,r);
    if(FAILED(hr))D3D12_DIAG_FAILED("ClearDepthStencilView",fmt("%llx/%x",(unsigned long long)dsv.ptr,flags),fmt("dsv=0x%llx flags=0x%x rects=%u",(unsigned long long)dsv.ptr,flags,rects));
    return hr;
}
static HRESULT STDMETHODCALLTYPE listExecuteIndirect(DevicePtr self,ID3D12CommandSignature* signature,UINT maxCount,ID3D12Resource* args,UINT64 argsOffset,ID3D12Resource* count,UINT64 countOffset) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,ID3D12CommandSignature*,UINT,ID3D12Resource*,UINT64,ID3D12Resource*,UINT64);
    D3D12_DIAG_HOOK(Fn,ListExecuteIndirect)HRESULT hr=call(self,signature,maxCount,args,argsOffset,count,countOffset);
    D3D12_DIAG_FAILED("ExecuteIndirect",fmt("%u",maxCount),fmt("signature=%d max=%u args=%d count=%d",signature!=nullptr,maxCount,args!=nullptr,count!=nullptr));
    return hr;
}

// ---- fences and resources: the calls a frame's pacing and uploads go through
static HRESULT STDMETHODCALLTYPE fenceSetEventOnCompletion(DevicePtr self,UINT64 value,HANDLE event) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT64,HANDLE);
    D3D12_DIAG_HOOK(Fn,FenceSetEventOnCompletion)HRESULT hr=call(self,value,event);
    D3D12_DIAG_FAILED("Fence::SetEventOnCompletion",fmt("%llu/%d",(unsigned long long)value,event!=nullptr),
        fmt("value=%llu event=%d",(unsigned long long)value,event!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE fenceSignal(DevicePtr self,UINT64 value) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT64);
    D3D12_DIAG_HOOK(Fn,FenceSignal)HRESULT hr=call(self,value);
    D3D12_DIAG_FAILED("Fence::Signal",fmt("%llu",(unsigned long long)value),fmt("value=%llu",(unsigned long long)value));
    return hr;
}
static HRESULT STDMETHODCALLTYPE resourceMap(DevicePtr self,UINT sub,const D3D12_RANGE* read,void** data) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr,UINT,const D3D12_RANGE*,void**);
    D3D12_DIAG_HOOK(Fn,ResourceMap)HRESULT hr=call(self,sub,read,data);
    D3D12_DIAG_FAILED("Resource::Map",fmt("%u",sub),fmt("subresource=%u range=%d",sub,read!=nullptr));
    return hr;
}
static HRESULT STDMETHODCALLTYPE allocatorReset(DevicePtr self) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(DevicePtr);
    D3D12_DIAG_HOOK(Fn,AllocatorReset)HRESULT hr=call(self);
    D3D12_DIAG_FAILED("CommandAllocator::Reset","","");
    return hr;
}

// ---- the hooks each interface level gets
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
