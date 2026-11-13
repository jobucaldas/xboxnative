// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contracts of the D3D12 failure diagnostics (host/d3d12_diag.h) on
// a native Wine d3d12 device: failed calls log once per distinct call, results
// pass through unchanged, several devices and the created objects are safe, the
// log is bounded. No rendering claim.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../host/d3d12_diag.h"
static unsigned checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)
static std::vector<std::string> lines;
static void sinkLine(const char* text){lines.push_back(text);}
static size_t count(const char* needle){size_t n=0;for(auto& l:lines)if(l.find(needle)!=std::string::npos)++n;return n;}
static HRESULT STDMETHODCALLTYPE alwaysInvalid(void*,void*,void*,void*){return E_INVALIDARG;}
// The fake object's IUnknown slots for the stream-hook guard test.
static HRESULT STDMETHODCALLTYPE noInterface(IUnknown*,REFIID,void** out){if(out)*out=nullptr;return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE refCount(IUnknown*){return 1;}
static ID3D12Device* make() {
    ID3D12Device* device=nullptr;
    if(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),(void**)&device)))return device;
    return nullptr;
}

// A stream builder with the layout CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT has
// (DirectX-Headers v1.618.1, d3dx12_pipeline_state_stream.h): an alignas(void*)
// entry holding the 4-byte type, then the payload at its own alignment. Each
// entry starts at a pointer-aligned offset; the payload of a UINT or enum sits
// at offset 4, not 8, which is what the walker has to get right.
template<typename T> struct alignas(void*) StreamEntry { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; T payload; };
template<typename T>
static void appendSubobject(std::vector<unsigned char>& stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type,const T& payload) {
    size_t at=(stream.size()+sizeof(void*)-1)&~size_t(sizeof(void*)-1);
    stream.resize(at+sizeof(StreamEntry<T>));
    StreamEntry<T> entry{};
    entry.type=type;
    entry.payload=payload;
    memcpy(&stream[at],&entry,sizeof entry);
}
static void appendUnknown(std::vector<unsigned char>& stream,int type) {
    size_t at=stream.size();
    stream.resize(at+sizeof(void*)+sizeof(void*));
    *reinterpret_cast<int*>(&stream[at])=type;
}
int main() {
    // The stream walker on synthetic streams built with the real struct
    // layout. These need no device: the walker is a pure function.
    {
        // A graphics stream like Mesa's: root signature, VS, PS, blend,
        // rasterizer, DSV format, RTV formats.
        std::vector<unsigned char> stream;
        ID3D12RootSignature* rs=nullptr;
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,rs);
        D3D12_SHADER_BYTECODE vs{nullptr,128};
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,vs);
        D3D12_SHADER_BYTECODE ps{nullptr,256};
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,ps);
        D3D12_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=0xf;
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,blend);
        D3D12_RASTERIZER_DESC raster{};
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,raster);
        DXGI_FORMAT dsv=DXGI_FORMAT_D32_FLOAT;
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,dsv);
        D3D12_RT_FORMAT_ARRAY rt{};rt.RTFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;rt.NumRenderTargets=1;
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,rt);
        std::string text=d3d12_diag::streamSubobjects(stream.data(),stream.size());
        CHECK(text.find("ROOT_SIGNATURE ")!=std::string::npos);
        CHECK(text.find("VS ")!=std::string::npos&&text.find("PS ")!=std::string::npos);
        CHECK(text.find("BLEND ")!=std::string::npos&&text.find("RASTERIZER ")!=std::string::npos);
        CHECK(text.find("DSV_FORMAT ")!=std::string::npos&&text.find("RTV_FORMATS ")!=std::string::npos);
        // The walk consumes the whole stream: no truncation, no unknown.
        CHECK(text.find("truncated")==std::string::npos&&text.find("unknown(")==std::string::npos);
        // The order Mesa's CD3DX12_PIPELINE_STATE_STREAM3 emits for a graphics
        // PSO (member order of the template). FLAGS and NODE_MASK are the 4-byte
        // entries that a pointer-aligned walk misreads: the walk must name all
        // sixteen in order and consume the stream exactly.
        {
            std::vector<unsigned char> mesa;
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,D3D12_PIPELINE_STATE_FLAG_NONE);
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK,UINT(0));
            ID3D12RootSignature* root=nullptr;
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,root);
            D3D12_INPUT_LAYOUT_DESC layout{};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,layout);
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED);
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            D3D12_SHADER_BYTECODE vs{nullptr,128};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,vs);
            D3D12_STREAM_OUTPUT_DESC so{};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT,so);
            D3D12_BLEND_DESC blend{};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,blend);
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,UINT(0xffffffff));
            D3D12_RASTERIZER_DESC raster{};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,raster);
            D3D12_DEPTH_STENCIL_DESC depth{};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,depth);
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,DXGI_FORMAT_UNKNOWN);
            D3D12_RT_FORMAT_ARRAY rtv{};rtv.NumRenderTargets=1;rtv.RTFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,rtv);
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,DXGI_SAMPLE_DESC{1,0});
            D3D12_CACHED_PIPELINE_STATE cached{};
            appendSubobject(mesa,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO,cached);
            std::string text=d3d12_diag::streamSubobjects(mesa.data(),mesa.size());
            CHECK(text.find("subobjects=[FLAGS NODE_MASK ROOT_SIGNATURE INPUT_LAYOUT IB_STRIP_CUT PRIMITIVE_TOPOLOGY VS "
                "STREAM_OUTPUT BLEND SAMPLE_MASK RASTERIZER DEPTH_STENCIL DSV_FORMAT RTV_FORMATS SAMPLE_DESC CACHED_PSO ]")!=std::string::npos);
        }
        // Compute-style stream: root signature + CS.
        std::vector<unsigned char> compute;
        appendSubobject(compute,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,rs);
        D3D12_SHADER_BYTECODE cs{nullptr,64};
        appendSubobject(compute,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,cs);
        text=d3d12_diag::streamSubobjects(compute.data(),compute.size());
        CHECK(text.find("CS ")!=std::string::npos&&text.find("PS ")==std::string::npos);
        CHECK(text.find("truncated")==std::string::npos);
        // A stream with an unknown subobject type: named with its type and
        // offset, and the walk stops instead of guessing.
        std::vector<unsigned char> unknown;
        appendSubobject(unknown,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,rs);
        appendUnknown(unknown,99);
        appendSubobject(unknown,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,vs);
        text=d3d12_diag::streamSubobjects(unknown.data(),unknown.size());
        CHECK(text.find("unknown(type=99@offset=")!=std::string::npos);
        CHECK(text.find("VS ")==std::string::npos);   // walk stopped at the unknown
        // A truncated stream: the subobject that does not fit is marked.
        std::vector<unsigned char> truncated(stream.begin(),stream.begin()+stream.size()-4);
        text=d3d12_diag::streamSubobjects(truncated.data(),truncated.size());
        CHECK(text.find("truncated")!=std::string::npos);
        // A size too small for even one subobject type is reported as-is.
        text=d3d12_diag::streamSubobjects(stream.data(),3);
        CHECK(text.find("bytes=3")!=std::string::npos);
    }

    ID3D12Device* device=make();
    if(!device){printf("OFF-DEVICE no d3d12 device available\n");return 2;}

    // A DXIL container: the model/hash parser reads the magic and hashes the
    // container; the device rejects it as a shader.
    unsigned char dxil[64]{};memcpy(dxil,"DXIL",4);dxil[4]=1;dxil[5]=6;
    std::string key,text=d3d12_diag::shaderText(dxil,sizeof dxil,&key);
    CHECK(text.find("dxil version=1.6")!=std::string::npos&&key.size()==16);
    CHECK(d3d12_diag::shaderText("junk",4).find("not-dxil")!=std::string::npos);
    CHECK(d3d12_diag::shaderText(nullptr,0)=="bytecode=none");
    unsigned char dxbc[8]{};memcpy(dxbc,"DXBC",4);
    CHECK(d3d12_diag::shaderText(dxbc,sizeof dxbc).find("dxbc")!=std::string::npos);

    // Nothing is logged, and nothing is patched, before the sink is set.
    d3d12_diag::adopt(device);
    CHECK(lines.empty()&&d3d12_diag::classCount.load()==0);
    d3d12_diag::configure(sinkLine);
    d3d12_diag::adopt(device);
    size_t classesAfterFirst=d3d12_diag::classCount.load();
    CHECK(classesAfterFirst==1);
    // The stream hook sits in the table of the ID3D12Device2 pointer the device
    // answers (on this Wine runtime that table is shared with ID3D12Device).
    {
        ID3D12Device2* device2=nullptr;
        CHECK(SUCCEEDED(device->QueryInterface(__uuidof(ID3D12Device2),(void**)&device2))&&device2);
        if(device2) {
            CHECK((*reinterpret_cast<void***>(device2))[47]==reinterpret_cast<void*>(&d3d12_diag::createPipelineStateStream));
            device2->Release();
        }
    }
    // The stream hook is written only through a pointer that implements
    // ID3D12Device2. A fake with a 44-slot table and no ID3D12Device2 must keep
    // the words after its table (where slot 47 would land) untouched.
    {
        void* table[48]{};
        table[0]=reinterpret_cast<void*>(&noInterface);table[1]=reinterpret_cast<void*>(&refCount);table[2]=reinterpret_cast<void*>(&refCount);
        for(int i=44;i<48;++i) table[i]=reinterpret_cast<void*>(uintptr_t(0x1000+i));
        void** object[1]={table};
        d3d12_diag::adoptStreamPath(reinterpret_cast<IUnknown*>(object));
        bool untouched=true;
        for(int i=44;i<48;++i) untouched=untouched&&table[i]==reinterpret_cast<void*>(uintptr_t(0x1000+i));
        CHECK(untouched);
        CHECK(count("has no ID3D12Device2")==1);
    }
    CHECK(count("diagnostics on")==1&&count("device nodes=")==1);
    // The capability answers a game branches on are logged once each.
    CHECK(count("CheckFeatureSupport feature=")>=4);
    CHECK(count("SHADER_MODEL")==1&&count("ARCHITECTURE")==1);

    // Failed creations: same results as the device gives, one line each.
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bad{};
    bad.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;bad.Width=64;bad.Height=64;
    bad.DepthOrArraySize=1;bad.MipLevels=1;bad.Format=DXGI_FORMAT_UNKNOWN;bad.SampleDesc.Count=1;
    ID3D12Resource* resource=nullptr;
    HRESULT hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bad,
        D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&resource);
    CHECK(FAILED(hr)&&!resource);
    CHECK(count("CreateCommittedResource hr=")==1&&count("fmt=0(UNKNOWN)")==1&&count("64x64")==1);
    bad.Width=128;bad.Height=32;   // same kind of failure at another size: not logged again
    CHECK(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bad,
        D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&resource)));
    CHECK(count("CreateCommittedResource hr=")==1);
    // A different kind of failure: a format that cannot be multisampled.
    bad.Format=DXGI_FORMAT_R8G8B8A8_UNORM;bad.SampleDesc.Count=2;
    CHECK(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bad,
        D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&resource)));
    CHECK(count("CreateCommittedResource hr=")==2);
    // A success is untouched and silent.
    D3D12_HEAP_PROPERTIES upload{};upload.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=1;
    buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;
    buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    size_t before=lines.size();
    ID3D12Resource* made=nullptr;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),(void**)&made))&&made);
    CHECK(lines.size()==before);

    // The objects a device creates are patched at their own level.
    ID3D12CommandQueue* queue=nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    CHECK(SUCCEEDED(device->CreateCommandQueue(&queueDesc,__uuidof(ID3D12CommandQueue),(void**)&queue))&&queue);
    d3d12_diag::adoptQueue(queue);
    ID3D12CommandAllocator* allocator=nullptr;
    CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),(void**)&allocator))&&allocator);
    d3d12_diag::adoptAllocator(allocator);
    ID3D12GraphicsCommandList* list=nullptr;
    CHECK(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,
        __uuidof(ID3D12GraphicsCommandList),(void**)&list))&&list);
    d3d12_diag::adoptList(list);
    ID3D12Fence* fence=nullptr;
    CHECK(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),(void**)&fence))&&fence);
    d3d12_diag::adoptFence(fence);
    d3d12_diag::adoptResource(made);
    // Each class is patched once; adopting the same objects again is a no-op.
    size_t classesAfterObjects=d3d12_diag::classCount.load();
    CHECK(classesAfterObjects==classesAfterFirst+5);
    d3d12_diag::adoptQueue(queue);d3d12_diag::adoptList(list);d3d12_diag::adoptFence(fence);
    d3d12_diag::adoptAllocator(allocator);d3d12_diag::adoptResource(made);
    CHECK(d3d12_diag::classCount.load()==classesAfterObjects);

    // Wine's list is created closed: the first Close succeeds silently, the
    // second is rejected and logged once; a third identical failure is not.
    before=lines.size();
    CHECK(SUCCEEDED(list->Close()));
    CHECK(lines.size()==before);
    CHECK(FAILED(list->Close()));
    CHECK(count("CommandList::Close hr=")==1);
    CHECK(FAILED(list->Close()));
    CHECK(count("CommandList::Close hr=")==1);
    // Reset with a null allocator is rejected by the device and logged once.
    CHECK(FAILED(list->Reset(nullptr,nullptr)));
    CHECK(count("CommandList::Reset hr=")==1);
    CHECK(FAILED(list->Reset(nullptr,nullptr)));
    CHECK(count("CommandList::Reset hr=")==1);
    // A fence signal to a value the device accepts is silent.
    before=lines.size();
    CHECK(SUCCEEDED(fence->Signal(1)));
    CHECK(lines.size()==before);
    // Map on a default-heap resource is rejected; the call is logged once.
    ID3D12Resource* placed=nullptr;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,
        D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&placed))&&placed);
    d3d12_diag::adoptResource(placed);
    void* mapped=nullptr;D3D12_RANGE read{0,0};
    CHECK(FAILED(placed->Map(0,&read,&mapped))&&!mapped);
    CHECK(count("Resource::Map hr=")==1);
    CHECK(FAILED(placed->Map(0,&read,&mapped)));
    CHECK(count("Resource::Map hr=")==1);
    placed->Release();

    // Every other hook, against a stand-in whose entries all fail (Wine accepts
    // many invalid descriptions): installed through the same patch tables.
    {
        static void* vtable[d3d12_diag::kSlots];
        for(auto& entry:vtable)entry=reinterpret_cast<void*>(&alwaysInvalid);
        struct Fake {void** vtable;} fake{vtable};
        size_t deviceCount=0,queueCount=0,listCount=0,fenceCount=0,resourceCount=0,allocatorCount=0;
        auto deviceHooks=d3d12_diag::devicePatches(deviceCount);
        d3d12_diag::queuePatches(queueCount);
        d3d12_diag::listPatches(listCount);
        d3d12_diag::fencePatches(fenceCount);
        d3d12_diag::resourcePatches(resourceCount);
        d3d12_diag::allocatorPatches(allocatorCount);
        CHECK(deviceCount>0&&queueCount>0&&listCount>0&&fenceCount>0&&resourceCount>0&&allocatorCount>0);
        CHECK(d3d12_diag::patchClass(vtable,deviceHooks,deviceCount));
        CHECK(d3d12_diag::patchClass(vtable,deviceHooks,deviceCount));   // idempotent
        for(size_t i=0;i<deviceCount;++i)CHECK(vtable[deviceHooks[i].slot]==deviceHooks[i].hook);
        void* self=&fake;
        auto call=[&](int slot,auto... args)->HRESULT{return reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,decltype(args)...)>(vtable[slot])(self,args...);};
        D3D12_COMMAND_QUEUE_DESC qd{};D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{};D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
        D3D12_HEAP_DESC hpd{};D3D12_QUERY_HEAP_DESC qhd{};D3D12_COMMAND_SIGNATURE_DESC csd{};
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};D3D12_SAMPLER_DESC sd{};
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        void* out=nullptr;
        // Each hook logs its own name once; the counts are compared against the
        // lines already present, since some names ran earlier in this test.
        const char* names[]={"CreateCommandQueue hr=","CreateCommandAllocator hr=","CreateGraphicsPipelineState hr=",
            "CreateComputePipelineState hr=","CreateCommandList hr=","CreateDescriptorHeap hr=",
            "CreateRootSignature hr=","CreateConstantBufferView hr=","CreateSampler hr=",
            "CreateShaderResourceView hr=","CreateUnorderedAccessView hr=","CreateRenderTargetView hr=",
            "CreateDepthStencilView hr=","CreateCommittedResource hr=","CreateHeap hr=",
            "CreatePlacedResource hr=","CreateReservedResource hr=","CreateFence hr=",
            "CreateQueryHeap hr=","CreateCommandSignature hr="};
        size_t beforeNames[sizeof names/sizeof *names];
        for(size_t i=0;i<sizeof names/sizeof *names;++i)beforeNames[i]=count(names[i]);
        CHECK(call(d3d12_diag::CreateCommandQueue,&qd,__uuidof(ID3D12CommandQueue),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateCommandAllocator,D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateGraphicsPipelineState,&gd,__uuidof(ID3D12PipelineState),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateComputePipelineState,&cd,__uuidof(ID3D12PipelineState),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateCommandList,0u,D3D12_COMMAND_LIST_TYPE_DIRECT,(ID3D12CommandAllocator*)nullptr,(ID3D12PipelineState*)nullptr,__uuidof(ID3D12GraphicsCommandList),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateDescriptorHeap,&hd,__uuidof(ID3D12DescriptorHeap),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateRootSignature,0u,(const void*)"junk",size_t(4),__uuidof(ID3D12RootSignature),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateConstantBufferView,&cbv,handle)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateSampler,&sd,handle)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateShaderResourceView,(ID3D12Resource*)nullptr,(const D3D12_SHADER_RESOURCE_VIEW_DESC*)nullptr,handle)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateUnorderedAccessView,(ID3D12Resource*)nullptr,(const D3D12_UNORDERED_ACCESS_VIEW_DESC*)nullptr,handle)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateRenderTargetView,(ID3D12Resource*)nullptr,(const D3D12_RENDER_TARGET_VIEW_DESC*)nullptr,handle)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateDepthStencilView,(ID3D12Resource*)nullptr,(const D3D12_DEPTH_STENCIL_VIEW_DESC*)nullptr,handle)==E_INVALIDARG);
        // A description no earlier call used, so this hook's line is new.
        D3D12_RESOURCE_DESC fakeBad=bad;fakeBad.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        CHECK(call(d3d12_diag::CreateCommittedResource,&heap,D3D12_HEAP_FLAG_NONE,&fakeBad,D3D12_RESOURCE_STATE_COMMON,(const D3D12_CLEAR_VALUE*)nullptr,__uuidof(ID3D12Resource),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateHeap,&hpd,__uuidof(ID3D12Heap),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreatePlacedResource,(ID3D12Heap*)nullptr,0ull,&bad,D3D12_RESOURCE_STATE_COMMON,(const D3D12_CLEAR_VALUE*)nullptr,__uuidof(ID3D12Resource),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateReservedResource,&bad,D3D12_RESOURCE_STATE_COMMON,(const D3D12_CLEAR_VALUE*)nullptr,__uuidof(ID3D12Resource),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateFence,0ull,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateQueryHeap,&qhd,__uuidof(ID3D12QueryHeap),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateCommandSignature,&csd,(ID3D12RootSignature*)nullptr,__uuidof(ID3D12CommandSignature),&out)==E_INVALIDARG);
        for(size_t i=0;i<sizeof names/sizeof *names;++i)
            CHECK(count(names[i])==beforeNames[i]+1);
    }

    // A second device shares the class: no new patch, same dedupe.
    size_t classesBeforeSecond=d3d12_diag::classCount.load();
    size_t committedBefore=count("CreateCommittedResource hr=");
    ID3D12Device* second=make();
    CHECK(second!=nullptr);
    if(second) {
        d3d12_diag::adopt(second);
        CHECK(d3d12_diag::classCount.load()==classesBeforeSecond);
        ID3D12Resource* again=nullptr;bad.Format=DXGI_FORMAT_UNKNOWN;bad.SampleDesc.Count=1;
        CHECK(FAILED(second->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bad,
            D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&again)));
        CHECK(count("CreateCommittedResource hr=")==committedBefore);   // already logged through the first device
        second->Release();
    }

    // The log is bounded; one line says so. The array size is part of the key,
    // so each iteration is a distinct failure.
    bad.Format=DXGI_FORMAT_UNKNOWN;bad.SampleDesc.Count=1;
    for(UINT i=0;i<400;++i){D3D12_RESOURCE_DESC d=bad;d.DepthOrArraySize=UINT16(i+1);ID3D12Resource* t=nullptr;
        device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&t);}
    CHECK(lines.size()<=d3d12_diag::kMaxFailures+d3d12_diag::kMaxProbes+4);
    CHECK(count("further failed calls suppressed")==1);
    if(failures)for(auto& l:lines)printf("  %s\n",l.c_str());
    if(made)made->Release();
    if(fence)fence->Release();
    if(list)list->Release();
    if(allocator)allocator->Release();
    if(queue)queue->Release();
    device->Release();
    printf("OFF-DEVICE D3D12 failure diagnostics checks=%u failures=%u lines=%zu; no rendering claim\n",checks,failures,lines.size());
    return failures?1:0;
}
