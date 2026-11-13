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
#include <thread>
#include <tuple>
#include <utility>
#include <type_traits>
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
static void makeDxilContainer(unsigned char* bytes,bool signedContainer) {
    const uint32_t total=52,chunks=1,offset=36,chunkBytes=8;
    memset(bytes,0,total);memcpy(bytes,"DXBC",4);
    if(signedContainer)bytes[4]=1;
    memcpy(bytes+24,&total,4);memcpy(bytes+28,&chunks,4);memcpy(bytes+32,&offset,4);
    memcpy(bytes+offset,"DXIL",4);memcpy(bytes+offset+4,&chunkBytes,4);
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
// ---- argument forwarding: every hook passes each argument to the right position ----
// Each argument gets a distinct sentinel (a distinct buffer for pointers, a distinct
// value otherwise). A recording original stores what it received; the check compares
// position by position. Pointer sentinels point at zeroed memory, so a hook that
// reads through them sees a null-like description and does not fault.
alignas(16) static unsigned char fwdBytes[16][256];
template<class T> static T fwdSentinel(int i) {
    using U=std::remove_cv_t<T>;
    if constexpr(std::is_pointer_v<U>) return reinterpret_cast<U>(fwdBytes[i]);
    else if constexpr(std::is_same_v<U,D3D12_CPU_DESCRIPTOR_HANDLE>) return D3D12_CPU_DESCRIPTOR_HANDLE{SIZE_T(0x7000+i)};
    else if constexpr(std::is_same_v<U,IID>) { IID g{}; g.Data1=UINT(0x1000+i); return g; }
    else return static_cast<U>(i+1);   // enums and arithmetic types
}
template<class T> static bool fwdSame(const T& a,const T& b) {
    using U=std::remove_cv_t<T>;
    if constexpr(std::is_same_v<U,D3D12_CPU_DESCRIPTOR_HANDLE>) return a.ptr==b.ptr;
    else if constexpr(std::is_same_v<U,IID>) return memcmp(&a,&b,sizeof a)==0;
    else return a==b;
}
// A method's arguments, from its C++ declaration in d3d12.h, with a recording original.
template<class M> struct FwdMethod;
template<class C,class R,class...A> struct FwdMethod<R(C::*)(A...)> {
    using ret=R;
    using Args=std::tuple<std::decay_t<A>...>;
    static inline Args seen{};
    static R STDMETHODCALLTYPE recorder(void*,A... a) {
        seen=Args(std::decay_t<A>(a)...);
        if constexpr(std::is_void_v<R>) return; else return R(S_OK);
    }
    template<size_t...I> static Args sentinels(std::index_sequence<I...>) {
        return Args(fwdSentinel<std::decay_t<A>>(int(I))...);
    }
    template<size_t...I> static bool same(const Args& a,const Args& b,std::index_sequence<I...>) {
        return (true && ... && fwdSame(std::get<I>(a),std::get<I>(b)));
    }
    // Calls the hook with the sentinel arguments, then checks the recorder got them in order.
    static bool forwards(void* hook,void* object) {
        using Idx=std::index_sequence_for<A...>;
        Args want=sentinels(Idx{});
        seen=Args{};
        using Fn=R(STDMETHODCALLTYPE*)(void*,A...);
        std::apply([&](auto&... a){ (void)reinterpret_cast<Fn>(hook)(object,a...); },want);
        return same(seen,want,Idx{});
    }
};
// One hook, through its fake class: each argument must reach the original in its
// own position (FwdMethod), the recorded kind must be the one the header gives the
// method (void or hr, in the trace), and a void method must never log "<name> hr=".
template<class M>
static void runHook(void** vt,void** obj,const char* method,const char* slotName,int slot,void* hook,bool traceMode) {
    using T=FwdMethod<M>;
    vt[slot]=reinterpret_cast<void*>(&T::recorder);
    d3d12_diag::Patch patch{slot,hook};
    d3d12_diag::patchClass(vt,&patch,1);
    const size_t before=lines.size();
    const bool forwarded=T::forwards(vt[slot],obj);
    if(!forwarded) printf("  forwarding mismatch: %s\n",method);
    CHECK(forwarded);
    constexpr bool isVoid=std::is_void_v<typename T::ret>;
    if(traceMode) {
        // The newest ring entry for this slot must carry the kind the header gives it.
        int state=-1;bool found=false;
        const uint64_t newest=d3d12_diag::traceNextSeq-1;
        for(uint64_t s=newest;s>=1&&s+d3d12_diag::kTraceRing>newest;--s) {
            const auto& e=d3d12_diag::traceRing[s%d3d12_diag::kTraceRing];
            if(e.seq==s&&e.name&&strcmp(e.name,slotName)==0){state=e.state;found=true;break;}
        }
        CHECK(found&&state==(isVoid?d3d12_diag::TraceVoid:d3d12_diag::TraceHr));
    }
    if(isVoid) {
        const std::string needle=std::string(method)+" hr=";
        size_t n=0;
        for(size_t i=before;i<lines.size();++i) if(lines[i].find(needle)!=std::string::npos) ++n;
        CHECK(n==0);
    }
}

int main(int argc,char** argv) {
    // "trace" runs the same contracts with LocalState\d3d12trace.txt present (the
    // probe is read at the first adopt). Default runs must log no trace at all.
    const bool traceMode=argc>1&&!strcmp(argv[1],"trace");
    if(traceMode) d3d12_diag::set_trace_requested=[]{return true;};
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
    unsigned char dxbc[48]{};memcpy(dxbc,"DXBC",4);dxbc[24]=48;dxbc[28]=1;dxbc[32]=36;memcpy(dxbc+36,"DXIL",4);dxbc[40]=4;
    CHECK(d3d12_diag::dxbcHasDxilChunk(dxbc,sizeof dxbc));
    CHECK(d3d12_diag::shaderText(dxbc,sizeof dxbc).find("dxbc-container dxil-chunk=yes")!=std::string::npos);
    dxbc[32]=47;
    CHECK(!d3d12_diag::dxbcHasDxilChunk(dxbc,sizeof dxbc));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.InputLayout.NumElements=3;state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.StreamOutput.NumEntries=2;state.StreamOutput.NumStrides=1;state.SampleMask=0x12345678;
    state.NumRenderTargets=1;state.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;
    state.DepthStencilState.DepthEnable=TRUE;state.RasterizerState.CullMode=D3D12_CULL_MODE_BACK;
    std::string stateText=d3d12_diag::graphicsStateText(&state);
    CHECK(stateText.find("topology=3 so_entries=2 so_strides=1 sample_mask=0x12345678")!=std::string::npos);
    CHECK(stateText.find("depth=1 stencil=0 cull=")!=std::string::npos);
    CHECK(stateText.find(" fill=")!=std::string::npos);

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
        auto callVoid=[&](int slot,auto... args){reinterpret_cast<void(STDMETHODCALLTYPE*)(void*,decltype(args)...)>(vtable[slot])(self,args...);};
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
            "CreateRootSignature hr=",
            "CreateCommittedResource hr=","CreateHeap hr=",
            "CreatePlacedResource hr=","CreateReservedResource hr=","CreateFence hr=",
            "CreateQueryHeap hr=","CreateCommandSignature hr="};
        size_t beforeNames[sizeof names/sizeof *names];
        for(size_t i=0;i<sizeof names/sizeof *names;++i)beforeNames[i]=count(names[i]);
        CHECK(call(d3d12_diag::CreateCommandQueue,&qd,__uuidof(ID3D12CommandQueue),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateCommandAllocator,D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateGraphicsPipelineState,&gd,__uuidof(ID3D12PipelineState),&out)==E_INVALIDARG);
        CHECK(count("vs-kind=none ps-kind=none nodes=0 topology=0") == 1);
        CHECK(count("CreateGraphicsPipelineState hr=0x80070057 vs-kind=none ps-kind=none bytes=")==0);
        CHECK(call(d3d12_diag::CreateComputePipelineState,&cd,__uuidof(ID3D12PipelineState),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateCommandList,0u,D3D12_COMMAND_LIST_TYPE_DIRECT,(ID3D12CommandAllocator*)nullptr,(ID3D12PipelineState*)nullptr,__uuidof(ID3D12GraphicsCommandList),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateDescriptorHeap,&hd,__uuidof(ID3D12DescriptorHeap),&out)==E_INVALIDARG);
        CHECK(call(d3d12_diag::CreateRootSignature,0u,(const void*)"junk",size_t(4),__uuidof(ID3D12RootSignature),&out)==E_INVALIDARG);
        callVoid(d3d12_diag::CreateConstantBufferView,&cbv,handle);
        callVoid(d3d12_diag::CreateSampler,&sd,handle);
        CHECK(count("CreateConstantBufferView hr=")==0&&count("CreateSampler hr=")==0);   // void: never a failure line
        callVoid(d3d12_diag::CreateShaderResourceView,(ID3D12Resource*)nullptr,(const D3D12_SHADER_RESOURCE_VIEW_DESC*)nullptr,handle);
        callVoid(d3d12_diag::CreateUnorderedAccessView,(ID3D12Resource*)nullptr,(ID3D12Resource*)nullptr,(const D3D12_UNORDERED_ACCESS_VIEW_DESC*)nullptr,handle);
        callVoid(d3d12_diag::CreateRenderTargetView,(ID3D12Resource*)nullptr,(const D3D12_RENDER_TARGET_VIEW_DESC*)nullptr,handle);
        callVoid(d3d12_diag::CreateDepthStencilView,(ID3D12Resource*)nullptr,(const D3D12_DEPTH_STENCIL_VIEW_DESC*)nullptr,handle);
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
        // The void view creators never log a failure line, whatever the arguments.
        CHECK(count("CreateShaderResourceView hr=")==0&&count("CreateUnorderedAccessView hr=")==0);
        CHECK(count("CreateRenderTargetView hr=")==0&&count("CreateDepthStencilView hr=")==0);
        unsigned char dxilContainer[52]{};makeDxilContainer(dxilContainer,true);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC dxilState{};dxilState.VS={dxilContainer,sizeof dxilContainer};
        CHECK(call(d3d12_diag::CreateGraphicsPipelineState,&dxilState,__uuidof(ID3D12PipelineState),&out)==E_INVALIDARG);
        CHECK(count("CreateGraphicsPipelineState hr=0x80070057 vs-kind=dxbc-container dxil-chunk=yes")==1);
        CHECK(count("CreateGraphicsPipelineState hr=0x80070057 vs-kind=dxbc-container dxil-chunk=yes bytes=")==0);
        CHECK(count("topology=0 so_entries=0 so_strides=0 sample_mask=0x00000000")>=2);
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
    // The UAV hook forwards the counter and the description: a UAV on a
    // default-heap buffer with a valid description must succeed through the hook.
    // (Before the fix the counter was dropped and the description landed in the
    // handle argument, so the create failed.)
    D3D12_HEAP_PROPERTIES defaultHeap{};defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC uavBuffer{};
    uavBuffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;uavBuffer.Width=1024;uavBuffer.Height=1;
    uavBuffer.DepthOrArraySize=1;uavBuffer.MipLevels=1;uavBuffer.SampleDesc.Count=1;
    uavBuffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;uavBuffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* uavRes=nullptr;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&uavBuffer,
        D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&uavRes))&&uavRes);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heapDesc.NumDescriptors=4;
    ID3D12DescriptorHeap* uavHeap=nullptr;
    CHECK(SUCCEEDED(device->CreateDescriptorHeap(&heapDesc,__uuidof(ID3D12DescriptorHeap),(void**)&uavHeap))&&uavHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE start=uavHeap?uavHeap->GetCPUDescriptorHandleForHeapStart():D3D12_CPU_DESCRIPTOR_HANDLE{};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format=DXGI_FORMAT_UNKNOWN;uavDesc.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements=16;uavDesc.Buffer.StructureByteStride=16;
    // CreateUnorderedAccessView returns void on the device, so the forwarding is
    // checked on a fake class: the patched UAV slot must hand its original the
    // same counter, description and handle it was called with. The sentinel
    // counter is never dereferenced; the old hook shifted every argument.
    {
        static D3D12_CPU_DESCRIPTOR_HANDLE seenHandle{};
        static ID3D12Resource* seenCounter=nullptr;
        static const D3D12_UNORDERED_ACCESS_VIEW_DESC* seenDesc=nullptr;
        static ID3D12Resource* seenResource=nullptr;
        struct Recorder {
            static void STDMETHODCALLTYPE original(void*,ID3D12Resource* r,ID3D12Resource* c,const D3D12_UNORDERED_ACCESS_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h) {
                seenResource=r;seenCounter=c;seenDesc=d;seenHandle=h;
            }
        };
        using UavFn=void(STDMETHODCALLTYPE*)(void*,ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
        void* vtable[64]{};
        vtable[d3d12_diag::CreateUnorderedAccessView]=reinterpret_cast<void*>(&Recorder::original);
        size_t patchCount=0;const d3d12_diag::Patch* patches=d3d12_diag::devicePatches(patchCount);
        void* object[1]={vtable};
        d3d12_diag::patchClass(vtable,patches,patchCount);
        ID3D12Resource* sentinelCounter=reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1234));
        D3D12_CPU_DESCRIPTOR_HANDLE sentinelHandle{uintptr_t(0x5678)};
        // The real slot is reached through the patched table: the hook runs, then calls the recorder.
        reinterpret_cast<UavFn>(vtable[d3d12_diag::CreateUnorderedAccessView])(object,uavRes,sentinelCounter,&uavDesc,sentinelHandle);
        CHECK(seenResource==uavRes&&seenCounter==sentinelCounter&&seenDesc==&uavDesc&&seenHandle.ptr==sentinelHandle.ptr);
    }
    // Keep #205's per-call PSO trace records; failure lines add only missing fields.
    {
        struct Rec {
            static HRESULT STDMETHODCALLTYPE v0(void*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**) {return S_OK;}
            static HRESULT STDMETHODCALLTYPE compute(void*,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**) {return S_OK;}
            static HRESULT STDMETHODCALLTYPE stream(void*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**) {return S_OK;}
        };
        void* vt[64]{};
        vt[d3d12_diag::CreateGraphicsPipelineState]=reinterpret_cast<void*>(&Rec::v0);
        vt[d3d12_diag::CreateComputePipelineState]=reinterpret_cast<void*>(&Rec::compute);
        vt[d3d12_diag::createPipelineStateStreamSlot]=reinterpret_cast<void*>(&Rec::stream);
        size_t patchCount=0;const d3d12_diag::Patch* patches=d3d12_diag::devicePatches(patchCount);
        d3d12_diag::patchClass(vt,patches,patchCount);
        const d3d12_diag::Patch streamPatch{d3d12_diag::createPipelineStateStreamSlot,reinterpret_cast<void*>(&d3d12_diag::createPipelineStateStream)};
        d3d12_diag::patchClass(vt,&streamPatch,1);
        void* object[1]={vt};void* out=nullptr;
        const uint64_t traceBefore=d3d12_diag::traceNextSeq;
        unsigned char vsBytes[52]{};makeDxilContainer(vsBytes,true);
        unsigned char psBytes[52]{};makeDxilContainer(psBytes,false);
        D3D12_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC gd{};gd.VS={vsBytes,sizeof vsBytes};gd.PS={psBytes,sizeof psBytes};
        gd.NumRenderTargets=1;gd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;gd.InputLayout={&element,1};
        reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,REFIID,void**)>(vt[d3d12_diag::CreateGraphicsPipelineState])(object,&gd,__uuidof(ID3D12PipelineState),&out);
        D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
        reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**)>(vt[d3d12_diag::CreateComputePipelineState])(object,&cd,__uuidof(ID3D12PipelineState),&out);
        std::vector<unsigned char> stream;
        D3D12_INPUT_LAYOUT_DESC layout{&element,1};
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,(ID3D12RootSignature*)nullptr);
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,D3D12_SHADER_BYTECODE{vsBytes,sizeof vsBytes});
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,D3D12_SHADER_BYTECODE{psBytes,sizeof psBytes});
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,layout);
        D3D12_RT_FORMAT_ARRAY rtv{};rtv.NumRenderTargets=1;rtv.RTFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        appendSubobject(stream,D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,rtv);
        D3D12_PIPELINE_STATE_STREAM_DESC sd{stream.size(),stream.data()};
        reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**)>(vt[d3d12_diag::createPipelineStateStreamSlot])(object,&sd,__uuidof(ID3D12PipelineState),&out);
        if(traceMode) {
            CHECK(d3d12_diag::traceNextSeq==traceBefore+13);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+0)%d3d12_diag::kTraceRing].detail,"v0 VS:dxbc")!=nullptr);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+1)%d3d12_diag::kTraceRing].detail,"v0 PS:dxbc")!=nullptr);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+2)%d3d12_diag::kTraceRing].detail,"v0 rtv=1[")!=nullptr);
            CHECK(strcmp(d3d12_diag::traceRing[(traceBefore+4)%d3d12_diag::kTraceRing].detail,"compute CS:none")==0);
            CHECK(strcmp(d3d12_diag::traceRing[(traceBefore+5)%d3d12_diag::kTraceRing].detail,"compute rootsig=null")==0);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+8)%d3d12_diag::kTraceRing].detail,"stream VS:dxbc")!=nullptr);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+9)%d3d12_diag::kTraceRing].detail,"stream PS:dxbc")!=nullptr);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+10)%d3d12_diag::kTraceRing].detail,"inputs=1[POSITION:16(R32G32_FLOAT)]")!=nullptr);
            CHECK(strstr(d3d12_diag::traceRing[(traceBefore+11)%d3d12_diag::kTraceRing].detail,"rtv=1[28(R8G8B8A8_UNORM)]")!=nullptr);
            CHECK(d3d12_diag::traceRing[(traceBefore+3)%d3d12_diag::kTraceRing].name&&strcmp(d3d12_diag::traceRing[(traceBefore+3)%d3d12_diag::kTraceRing].name,"CreateGraphicsPipelineState")==0);
            CHECK(d3d12_diag::traceRing[(traceBefore+6)%d3d12_diag::kTraceRing].name&&strcmp(d3d12_diag::traceRing[(traceBefore+6)%d3d12_diag::kTraceRing].name,"CreateComputePipelineState")==0);
            CHECK(d3d12_diag::traceRing[(traceBefore+12)%d3d12_diag::kTraceRing].name&&strcmp(d3d12_diag::traceRing[(traceBefore+12)%d3d12_diag::kTraceRing].name,"createPipelineStateStreamSlot")==0);
            size_t flushed=lines.size();d3d12_diag::flushTrace();
            auto since=[&](const std::string& needle){size_t n=0;for(size_t i=flushed;i<lines.size();++i)if(lines[i].find(needle)!=std::string::npos)++n;return n;};
            CHECK(since("trace #"+std::to_string(traceBefore+3)+" CreateGraphicsPipelineState 0x00000000")==1);
            CHECK(since("trace #"+std::to_string(traceBefore+6)+" CreateComputePipelineState 0x00000000")==1);
            CHECK(since("trace #"+std::to_string(traceBefore+12)+" createPipelineStateStreamSlot 0x00000000")==1);
        }
    }

    // Every hooked method: the recorded kind (void or hr) must be the one the
    // header gives it, and a void method must never log a failure line. The
    // header side is compile-time (DIAG_PIN_SLOT); this runs each hook once.
    // Patched through one fake class per interface, so each check is independent.
    {
        static void* vtDev[64]{};static void* objDev[1]{vtDev};
        static void* vtQueue[64]{};static void* objQueue[1]{vtQueue};
        static void* vtList[64]{};static void* objList[1]{vtList};
        static void* vtFence[64]{};static void* objFence[1]{vtFence};
        static void* vtAlloc[64]{};static void* objAlloc[1]{vtAlloc};
        static void* vtRes[64]{};static void* objRes[1]{vtRes};
#define CHECK_HOOK(VT,OBJ,Iface,Method,Hook,Slot) runHook<decltype(&Iface::Method)>(VT,OBJ,#Method,#Slot,int(d3d12_diag::Slot),reinterpret_cast<void*>(&d3d12_diag::Hook),traceMode)
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateCommandQueue,createCommandQueue,CreateCommandQueue);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateCommandAllocator,createCommandAllocator,CreateCommandAllocator);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateCommandList,createCommandList,CreateCommandList);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateGraphicsPipelineState,createGraphicsPipelineState,CreateGraphicsPipelineState);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateComputePipelineState,createComputePipelineState,CreateComputePipelineState);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CheckFeatureSupport,checkFeatureSupport,CheckFeatureSupport);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateDescriptorHeap,createDescriptorHeap,CreateDescriptorHeap);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateRootSignature,createRootSignature,CreateRootSignature);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateConstantBufferView,createCbv,CreateConstantBufferView);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateShaderResourceView,createSrv,CreateShaderResourceView);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateUnorderedAccessView,createUav,CreateUnorderedAccessView);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateRenderTargetView,createRtv,CreateRenderTargetView);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateDepthStencilView,createDsv,CreateDepthStencilView);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateSampler,createSampler,CreateSampler);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateCommittedResource,createCommittedResource,CreateCommittedResource);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateHeap,createHeap,CreateHeap);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreatePlacedResource,createPlacedResource,CreatePlacedResource);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateReservedResource,createReservedResource,CreateReservedResource);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateFence,createFence,CreateFence);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateQueryHeap,createQueryHeap,CreateQueryHeap);
        CHECK_HOOK(vtDev,objDev,ID3D12Device,CreateCommandSignature,createCommandSignature,CreateCommandSignature);
        CHECK_HOOK(vtDev,objDev,ID3D12Device2,CreatePipelineState,createPipelineStateStream,createPipelineStateStreamSlot);
        CHECK_HOOK(vtQueue,objQueue,ID3D12CommandQueue,ExecuteCommandLists,queueExecuteCommandLists,QueueExecuteCommandLists);
        CHECK_HOOK(vtQueue,objQueue,ID3D12CommandQueue,Signal,queueSignal,QueueSignal);
        CHECK_HOOK(vtQueue,objQueue,ID3D12CommandQueue,Wait,queueWait,QueueWait);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,Close,listClose,ListClose);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,Reset,listReset,ListReset);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,SetPipelineState,listSetPipelineState,ListSetPipelineState);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,ResourceBarrier,listResourceBarrier,ListResourceBarrier);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,SetDescriptorHeaps,listSetDescriptorHeaps,ListSetDescriptorHeaps);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,DrawInstanced,listDrawInstanced,ListDrawInstanced);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,DrawIndexedInstanced,listDrawIndexedInstanced,ListDrawIndexedInstanced);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,Dispatch,listDispatch,ListDispatch);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,CopyResource,listCopyResource,ListCopyResource);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,CopyBufferRegion,listCopyBufferRegion,ListCopyBufferRegion);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,CopyTextureRegion,listCopyTextureRegion,ListCopyTextureRegion);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,ResolveSubresource,listResolveSubresource,ListResolveSubresource);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,OMSetRenderTargets,listOMSetRenderTargets,ListOMSetRenderTargets);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,ClearRenderTargetView,listClearRenderTargetView,ListClearRenderTargetView);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,ClearDepthStencilView,listClearDepthStencilView,ListClearDepthStencilView);
        CHECK_HOOK(vtList,objList,ID3D12GraphicsCommandList,ExecuteIndirect,listExecuteIndirect,ListExecuteIndirect);
        CHECK_HOOK(vtFence,objFence,ID3D12Fence,SetEventOnCompletion,fenceSetEventOnCompletion,FenceSetEventOnCompletion);
        CHECK_HOOK(vtFence,objFence,ID3D12Fence,Signal,fenceSignal,FenceSignal);
        CHECK_HOOK(vtAlloc,objAlloc,ID3D12CommandAllocator,Reset,allocatorReset,AllocatorReset);
        CHECK_HOOK(vtRes,objRes,ID3D12Resource,Map,resourceMap,ResourceMap);
#undef CHECK_HOOK
    }

    // The trace: silent unless the probe file was present at the first adopt.
    if(traceMode) {
        CHECK(count("trace on (LocalState")==1);
        // A call that never returns stays in flight, is named as pending, and is
        // not overwritten by 70 newer calls. When it finally ends, the entries of
        // the newer calls keep their own results (no clobbering).
        {
            const uint64_t hung=d3d12_diag::traceBegin("HungCall");
            size_t b=lines.size();
            d3d12_diag::flushTrace();
            size_t inFlight=0;
            for(size_t i=b;i<lines.size();++i) if(lines[i].find("trace in-flight")!=std::string::npos&&lines[i].find("HungCall pending")!=std::string::npos) ++inFlight;
            CHECK(inFlight==1);
            for(int i=0;i<70;++i) {const uint64_t t=d3d12_diag::traceBegin("Filler");d3d12_diag::traceEnd(t,S_OK);}
            b=lines.size();
            d3d12_diag::flushTrace();
            inFlight=0;
            for(size_t i=b;i<lines.size();++i) if(lines[i].find("HungCall pending")!=std::string::npos) ++inFlight;
            CHECK(inFlight==1);
            d3d12_diag::traceEnd(hung,S_OK);   // the hung call ends now, 70 calls late
            bool clobbered=false;size_t fillers=0;
            const uint64_t newest=d3d12_diag::traceNextSeq-1;
            for(uint64_t s=newest;s+d3d12_diag::kTraceRing>newest&&s>=1;--s) {
                const auto& e=d3d12_diag::traceRing[s%d3d12_diag::kTraceRing];
                if(e.seq!=s) continue;
                if(strcmp(e.name,"HungCall")==0) clobbered=true;
                if(strcmp(e.name,"Filler")==0&&e.state==d3d12_diag::TraceHr&&e.hr==S_OK) ++fillers;
            }
            CHECK(!clobbered);
            CHECK(fillers==d3d12_diag::kTraceRing);   // the ring is the last 64 calls, all fillers
            bool stillInFlight=false;
            for(const auto& f:d3d12_diag::traceInFlight) if(f.seq==hung) stillInFlight=true;
            CHECK(!stillInFlight);
        }
        // Concurrent begin/end from four guest threads: every ring entry that
        // holds a result must carry the result of its own call (hr = its seq).
        {
            std::vector<std::thread> threads;
            for(int t=0;t<4;++t) threads.emplace_back([]{
                for(int i=0;i<2000;++i) {
                    const uint64_t s=d3d12_diag::traceBegin("Stress");
                    d3d12_diag::traceEnd(s,HRESULT(s));
                }
            });
            for(auto& th:threads) th.join();
            bool consistent=true;size_t settled=0;
            const uint64_t newest=d3d12_diag::traceNextSeq-1;
            for(uint64_t s=newest;s+d3d12_diag::kTraceRing>newest&&s>=1;--s) {
                const auto& e=d3d12_diag::traceRing[s%d3d12_diag::kTraceRing];
                if(e.seq!=s) continue;
                if(e.state==d3d12_diag::TraceHr) {++settled;if(e.hr!=long(HRESULT(s)))consistent=false;}
            }
            CHECK(consistent&&settled>0);
            bool anyInFlight=false;
            for(const auto& f:d3d12_diag::traceInFlight) if(f.seq) anyInFlight=true;
            CHECK(!anyInFlight);
        }
        // One view of each kind through its hook: each must trace under its own name.
        UINT inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE srvAt=start;srvAt.ptr+=SIZE_T(inc);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format=DXGI_FORMAT_UNKNOWN;srvDesc.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements=16;srvDesc.Buffer.StructureByteStride=16;
        device->CreateShaderResourceView(uavRes,&srvDesc,srvAt);
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};rtvDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rtvDesc.NumDescriptors=1;
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};dsvDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;dsvDesc.NumDescriptors=1;
        ID3D12DescriptorHeap* rtvHeap=nullptr;ID3D12DescriptorHeap* dsvHeap=nullptr;
        device->CreateDescriptorHeap(&rtvDesc,__uuidof(ID3D12DescriptorHeap),(void**)&rtvHeap);
        device->CreateDescriptorHeap(&dsvDesc,__uuidof(ID3D12DescriptorHeap),(void**)&dsvHeap);
        // A buffer is not a render target or depth target, so these two fail;
        // the trace still names them, which is what this checks.
        if(rtvHeap) device->CreateRenderTargetView(uavRes,nullptr,rtvHeap->GetCPUDescriptorHandleForHeapStart());
        if(dsvHeap) device->CreateDepthStencilView(uavRes,nullptr,dsvHeap->GetCPUDescriptorHandleForHeapStart());
        size_t viewLines=0;
        d3d12_diag::flushTrace();
        for(auto& l:lines) if(l.rfind("trace #",0)==0) {
            for(const char* n:{" CreateShaderResourceView "," CreateUnorderedAccessView "," CreateRenderTargetView "," CreateDepthStencilView "})
                if(l.find(n)!=std::string::npos&&l.size()>=5&&l.compare(l.size()-5,5," void")==0) ++viewLines;
        }
        // Each view entry is "void" (no hr) and no view logged a failure line.
        CHECK(viewLines>=4);
        CHECK(count(" Slot ")==0);
        CHECK(count("CreateShaderResourceView hr=")==0&&count("CreateRenderTargetView hr=")==0);
        if(rtvHeap) rtvHeap->Release();
        if(dsvHeap) dsvHeap->Release();
        size_t before=lines.size();
        d3d12_diag::flushTrace();
        CHECK(lines.size()-before==1+std::min(size_t(d3d12_diag::traceNextSeq-1),d3d12_diag::kTraceRing));
        // Every completed entry carries its hr (the ring holds the newest calls only).
        size_t traced=0,withHr=0;
        size_t pendingNow=0;
        for(size_t i=before;i<lines.size();++i) {
            const std::string& l=lines[i];
            if(l.rfind("trace #",0)==0) {++traced;if(l.find(" 0x")!=std::string::npos||(l.size()>=5&&l.compare(l.size()-5,5," void")==0))++withHr;}
            if(l.find(" pending")!=std::string::npos) ++pendingNow;
        }
        CHECK(traced>0&&traced==withHr);
        CHECK(pendingNow==0);   // nothing in flight outside a call
        // More calls than the ring holds: the flush keeps only the newest entries.
        if(fence) for(UINT64 v=2;v<=100;++v) fence->Signal(v);
        before=lines.size();
        d3d12_diag::flushTrace();
        CHECK(lines.size()-before==1+d3d12_diag::kTraceRing);
        CHECK(count(" FenceSignal 0x00000000")>0);
    } else {
        size_t before=lines.size();
        d3d12_diag::flushTrace();
        CHECK(lines.size()==before);
        CHECK(count("trace on")==0&&count("trace #")==0);
    }
    if(failures)for(auto& l:lines)printf("  %s\n",l.c_str());
    if(made)made->Release();
    if(uavRes)uavRes->Release();
    if(uavHeap)uavHeap->Release();
    if(fence)fence->Release();
    if(list)list->Release();
    if(allocator)allocator->Release();
    if(queue)queue->Release();
    device->Release();
    printf("OFF-DEVICE D3D12 failure diagnostics checks=%u failures=%u lines=%zu; no rendering claim\n",checks,failures,lines.size());
    return failures?1:0;
}
