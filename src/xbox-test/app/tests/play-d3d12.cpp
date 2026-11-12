// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that renders D3D12 frames through Play's
// routes, the way a D3D12 game starts up. It creates a device through
// D3D12CreateDevice, a direct command queue, a factory, a flip-model swap
// chain on a guest HWND with the queue as the device argument, render-target
// views on the back buffers, a fence and an event; then it draws one
// triangle with a minimal graphics pipeline (the committed SM 5.0 DXBC
// shaders, an upload-heap vertex buffer, no textures) into an offscreen
// target, reads the pixels back through a READBACK heap and checks them,
// so a draw that silently did nothing fails here rather than looking like
// a black screen; and it clears each back buffer and presents. Exit code
// 42 = every step succeeded, the triangle is where it should be and the
// GPU finished every frame; anything else names the step that failed. The
// log is the evidence.
//
// Coverage additions (research/D3D12-COVERAGE.md): root signature 1.1 through
// D3D12SerializeVersionedRootSignature, a compute PSO with a root constant that
// Dispatches over an RWByteAddressBuffer UAV and reads the result back (exit 42
// only when the GPU wrote the expected values), a UAV barrier, and ResizeBuffers
// on the live flip-model chain. Same exit-code conventions.
//
// Device-creation attempts (console evidence, PR #160: the null-adapter path
// fails there with DXGI_ERROR_DEVICE_RESET while d3d11 passes): the fixture
// tries, in order, null adapter at FL 11_0, then the factory's adapter 0
// (Play wraps it; the host route unwraps it) at FL 11_0, then that adapter at
// FL 12_0. Exit codes: 10 window, 11 no device at all (null adapter and the
// fallback factory/enum steps failed with their own codes 81/82 first), 81
// factory create failed, 82 EnumAdapters1 failed, 83 every D3D12CreateDevice
// attempt failed. 12 CheckFeatureSupport, 13 queue, 14 factory, 15 swap chain,
// 16 RTV heap, 17 GetBuffer, 18-22 allocator/list/fence/event, 40-55 the
// triangle pipeline, 56 debug-layer errors, 57/58 root signature 1.1,
// 59-69/79/80 the compute pass (43 = readback mismatch, BLOCKED, never a
// pass), 70-78 ResizeBuffers. The log is the evidence.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "../../uwph/host/shaders/triangle_bytecode.h"
#include "compute_bytecode.h"

extern "C" {
int d3d12_anchor;
void* d3d12_relocation=&d3d12_anchor;

// No CRT: the compiler lowers struct copies to these libc names (the shims'
// base.h carries the same definitions).
void* memset(void* d,int c,size_t n) {
    auto p=(unsigned char*)d;while(n--)*p++=(unsigned char)c;return d;
}
void* memcpy(void* d,const void* s,size_t n) {
    auto a=(unsigned char*)d;auto b=(const unsigned char*)s;while(n--)*a++=*b++;return d;
}

static const UINT kFrames=3;
static const DXGI_FORMAT kFormat=DXGI_FORMAT_R8G8B8A8_UNORM;

// One guest window, created through the same user32 record core a game uses:
// Play's factory validates it and maps it to the launcher's CoreWindow.
static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d12";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d12",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

DWORD d3d12_entry() {
    // The debug layer, where the platform has it: enabled before the device
    // so API misuse is recorded, and the recorded errors are counted before
    // the run reports success. Optional — a host without the debug layer
    // simply skips the check.
    bool debugLayer=false;
    {ID3D12Debug1* debug=nullptr;
     if(SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug1),(void**)&debug))&&debug){
         debug->EnableDebugLayer();debug->Release();debugLayer=true;}}
    HWND window=guestWindow();
    if(!window)return 10;
    // The adapter argument is optional; a D3D12 game that picks one passes the
    // factory's adapter, which Play wraps. Default (null) exercises the same
    // path a game takes when it lets the runtime choose.
    // Console 0.5.303 evidence: the null-adapter default path fails there with
    // DXGI_ERROR_DEVICE_RESET (0x887a0007) while d3d11 on the same stack
    // succeeds — and that d3d11 layers on a D3D12 device, so one can exist
    // in-process. Fall back the way a game that picks an adapter would:
    // enumerate the factory's adapter 0 and hand it over explicitly, at
    // 11_0 then 12_0. The host route logs each attempt separately
    // (adapter=default vs adapter=given), so the console log names the
    // working path.
    ID3D12Device* device=nullptr;
    HRESULT hr=D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),(void**)&device);
    if(FAILED(hr)||!device) {
        IDXGIFactory4* factory4=nullptr;
        if(FAILED(CreateDXGIFactory2(0,__uuidof(IDXGIFactory4),(void**)&factory4))||!factory4)return 81;
        IDXGIAdapter1* chosen=nullptr;
        if(FAILED(factory4->EnumAdapters1(0,&chosen))||!chosen){factory4->Release();return 82;}
        hr=D3D12CreateDevice(chosen,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),(void**)&device);
        if(FAILED(hr)||!device)
            hr=D3D12CreateDevice(chosen,D3D_FEATURE_LEVEL_12_0,__uuidof(ID3D12Device),(void**)&device);
        chosen->Release();factory4->Release();
        if(FAILED(hr)||!device)return 83;
    }
    // Shader model: a D3D12 game reads this before choosing its shader set.
    D3D12_FEATURE_DATA_SHADER_MODEL model{};
    model.HighestShaderModel=D3D_SHADER_MODEL_6_6;
    if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof model)))return 12;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue=nullptr;
    if(FAILED(device->CreateCommandQueue(&queueDesc,__uuidof(ID3D12CommandQueue),(void**)&queue))||!queue)return 13;

    IDXGIFactory2* factory=nullptr;
    if(FAILED(CreateDXGIFactory2(0,__uuidof(IDXGIFactory2),(void**)&factory))||!factory)return 14;
    // DXGI requires the command queue, not the device, as the swap chain's
    // device argument for D3D12, and a flip model.
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=640;desc.Height=480;desc.Format=kFormat;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=3;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    IDXGISwapChain1* chain=nullptr;
    hr=factory->CreateSwapChainForHwnd(queue,window,&desc,nullptr,nullptr,&chain);
    factory->Release();
    if(FAILED(hr)||!chain)return 15;

    // One RTV descriptor heap with a view per back buffer, plus one for the
    // offscreen readback target the draw lands in.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heapDesc.NumDescriptors=kFrames+1;
    ID3D12DescriptorHeap* heap=nullptr;
    if(FAILED(device->CreateDescriptorHeap(&heapDesc,__uuidof(ID3D12DescriptorHeap),(void**)&heap))||!heap)return 16;
    UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle=heap->GetCPUDescriptorHandleForHeapStart();
    ID3D12Resource* targets[kFrames]{};
    for(UINT i=0;i<kFrames;++i) {
        if(FAILED(chain->GetBuffer(i,__uuidof(ID3D12Resource),(void**)&targets[i]))||!targets[i])return 17;
        device->CreateRenderTargetView(targets[i],nullptr,handle);
        handle.ptr+=stride;
    }

    ID3D12CommandAllocator* allocator=nullptr;
    if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),(void**)&allocator))||!allocator)return 18;
    ID3D12GraphicsCommandList* list=nullptr;
    if(FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,__uuidof(ID3D12GraphicsCommandList),(void**)&list))||!list)return 19;
    if(FAILED(list->Close()))return 20;

    ID3D12Fence* fence=nullptr;
    if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),(void**)&fence))||!fence)return 21;
    HANDLE event=CreateEventExW(nullptr,nullptr,0,EVENT_MODIFY_STATE|SYNCHRONIZE);
    if(!event)return 22;

    // ---- the draw: one triangle through a minimal PSO, read back ----
    // An offscreen target and a READBACK copy: the pair a game uses for a
    // screenshot, and the only way to see a draw.
    D3D12_RESOURCE_DESC targetDesc{};
    targetDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    targetDesc.Width=64;targetDesc.Height=64;targetDesc.DepthOrArraySize=1;
    targetDesc.MipLevels=1;targetDesc.Format=kFormat;targetDesc.SampleDesc.Count=1;
    // A render-target view on the texture needs this flag, and the optimized
    // clear value matches what the list clears it to.
    targetDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format=kFormat;clearValue.Color[3]=1.0f;
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* target=nullptr;
    if(FAILED(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&targetDesc,
            D3D12_RESOURCE_STATE_COMMON,&clearValue,__uuidof(ID3D12Resource),(void**)&target))||!target)return 40;
    // The readback side is a BUFFER on a READBACK heap: only buffers may live
    // there, and the copy must follow the layout GetCopyableFootprints reports
    // (its row pitch is 256-aligned, wider than the 64 pixels a row carries).
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readbackFootprint{};
    UINT64 readbackSize=0;
    device->GetCopyableFootprints(&targetDesc,0,1,0,&readbackFootprint,nullptr,nullptr,&readbackSize);
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width=readbackSize;readbackDesc.Height=1;   // buffers are Height 1
    readbackDesc.DepthOrArraySize=1;readbackDesc.MipLevels=1;
    readbackDesc.SampleDesc.Count=1;readbackDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* readback=nullptr;
    if(FAILED(device->CreateCommittedResource(&readbackHeap,D3D12_HEAP_FLAG_NONE,&readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),(void**)&readback))||!readback)return 41;

    // The committed SM 5.0 DXBC: a D3D12 PSO takes shader model 5.0 bytecode
    // (it is the same DXBC container 6.x compiles into).
    D3D12_SHADER_BYTECODE vsBytecode{triangle_vs,sizeof triangle_vs};
    D3D12_SHADER_BYTECODE psBytecode{triangle_ps,sizeof triangle_ps};
    // A root signature with no parameters: the shaders read nothing beyond
    // their vertex input. It is created first — the PSO requires it set.
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rootBlob=nullptr;ID3DBlob* rootErrors=nullptr;
    if(FAILED(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBlob,&rootErrors)))return 45;
    ID3D12RootSignature* rootSignature=nullptr;
    hr=device->CreateRootSignature(0,rootBlob->GetBufferPointer(),rootBlob->GetBufferSize(),
        __uuidof(ID3D12RootSignature),(void**)&rootSignature);
    rootBlob->Release();if(rootErrors)rootErrors->Release();
    if(FAILED(hr)||!rootSignature)return 46;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=rootSignature;
    pso.VS=vsBytecode;pso.PS=psBytecode;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask=UINT_MAX;
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets=1;pso.RTVFormats[0]=kFormat;
    pso.SampleDesc.Count=1;
    // The input layout the VS signature asks for: POSITION (rg32f) then COLOR
    // (rgba32f), the same elements the D3D9 shim's translated DXBC carries.
    static const D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    pso.InputLayout={layout,2};
    ID3D12PipelineState* pipeline=nullptr;
    if(FAILED(device->CreateGraphicsPipelineState(&pso,__uuidof(ID3D12PipelineState),(void**)&pipeline))||!pipeline)return 55;

    // Clip-space vertices (the HLSL passes the position straight through) in
    // one upload-heap buffer, all red, over the left of the target: x=-0.75
    // maps to pixel 8, x=0.75 to 56 through the 64-wide viewport.
    const float vertices[][6]={
        {-0.75f, 0.75f,1,0,0,1},
        { 0.75f, 0.75f,1,0,0,1},
        { 0.0f,-0.75f,1,0,0,1}};
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC vbDesc{};
    vbDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width=sizeof vertices;vbDesc.Height=1;   // buffers are Height 1
    vbDesc.DepthOrArraySize=1;vbDesc.MipLevels=1;
    vbDesc.SampleDesc.Count=1;vbDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* vertexBuffer=nullptr;
    if(FAILED(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),(void**)&vertexBuffer))||!vertexBuffer)return 54;
    void* mappedVertices=nullptr;
    D3D12_RANGE readNone{0,0};
    if(FAILED(vertexBuffer->Map(0,&readNone,&mappedVertices))||!mappedVertices)return 44;
    memcpy(mappedVertices,vertices,sizeof vertices);
    vertexBuffer->Unmap(0,nullptr);
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    vertexView.BufferLocation=vertexBuffer->GetGPUVirtualAddress();
    vertexView.StrideInBytes=sizeof vertices[0];
    vertexView.SizeInBytes=sizeof vertices;

    // Draw into the offscreen target, copy it to the readback buffer, wait.
    // The RTV heap's first kFrames slots belong to the back buffers; the
    // target gets its own view in the slot after them.
    D3D12_CPU_DESCRIPTOR_HANDLE targetRtv=heap->GetCPUDescriptorHandleForHeapStart();
    targetRtv.ptr+=kFrames*stride;
    device->CreateRenderTargetView(target,nullptr,targetRtv);
    if(FAILED(allocator->Reset()))return 47;
    if(FAILED(list->Reset(allocator,pipeline)))return 48;
    D3D12_RESOURCE_BARRIER toDraw{};
    toDraw.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toDraw.Transition.pResource=target;
    toDraw.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toDraw.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
    toDraw.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1,&toDraw);
    D3D12_CPU_DESCRIPTOR_HANDLE targetHandles[]={targetRtv};
    list->OMSetRenderTargets(1,targetHandles,FALSE,nullptr);
    // The clear makes the exterior deterministically black before the draw.
    const FLOAT clearBlack[4]={0.0f,0.0f,0.0f,1.0f};
    list->ClearRenderTargetView(targetRtv,clearBlack,0,nullptr);
    D3D12_VIEWPORT drawViewport{0,0,64,64,0.0f,1.0f};
    list->RSSetViewports(1,&drawViewport);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->SetGraphicsRootSignature(rootSignature);
    list->IASetVertexBuffers(0,1,&vertexView);
    list->DrawInstanced(3,1,0,0);
    // Straight to COPY_SOURCE: the target is done being rendered to, and the
    // copy needs it as a source.
    D3D12_RESOURCE_BARRIER toCopy=toDraw;
    toCopy.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopy.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&toCopy);
    // Texture-to-buffer copy at the placed footprint the device reported.
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource=target;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource=readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=readbackFootprint;
    D3D12_BOX copyBox{0,0,0,64,64,1};
    list->CopyTextureRegion(&dst,0,0,0,&src,&copyBox);
    D3D12_RESOURCE_BARRIER backToCommon=toCopy;
    backToCommon.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
    backToCommon.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1,&backToCommon);
    if(FAILED(list->Close()))return 49;
    {ID3D12CommandList* drawLists[]={list};
     queue->ExecuteCommandLists(1,drawLists);}
    // One strictly increasing counter for every Signal, so a wait can never
    // pass on a value the queue already signalled past.
    UINT64 fenceValue=0;
    ++fenceValue;
    if(FAILED(queue->Signal(fence,fenceValue)))return 50;
    if(FAILED(fence->SetEventOnCompletion(fenceValue,event)))return 51;
    if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 52;

    // Check the pixels: the triangle covers the middle-left, the rest is the
    // clear colour. Rows sit at the 256-aligned pitch GetCopyableFootprints
    // reported, so each row is indexed through it, not the tight 64*4 stride.
    {
        void* mappedRead=nullptr;
        D3D12_RANGE readWhole{0,(SIZE_T)readbackSize};
        if(FAILED(readback->Map(0,&readWhole,&mappedRead))||!mappedRead)return 53;
        const unsigned char* base=static_cast<const unsigned char*>(mappedRead);
        const UINT pitch=(UINT)readbackFootprint.Footprint.RowPitch;
        const unsigned char* inside=base+32*pitch+32*4;   // 32nd row, 32nd pixel
        const unsigned char* outside=base+4*pitch+4*4;    // 4th row, 4th pixel
        const bool insideRed=inside[0]>0x80&&inside[1]<0x40&&inside[2]<0x40;
        const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
        D3D12_RANGE writtenNone{0,0};
        readback->Unmap(0,&writtenNone);
        // 43 is BLOCKED (never a pass): the pipeline ran but the pixels did
        // not read back as drawn, the same outcome the opengl fixture reports
        // when the host drew nothing the guest could read.
        if(!insideRed||!outsideBlack)return 43;
    }

    // ---- coverage additions (research/D3D12-COVERAGE.md) ----
    // Root signature 1.1: the versioned serialize entry point every newer
    // engine calls, with the two descriptors the compute pass reads and
    // writes: the cbuffer Args (b0) as a root CBV and the RWByteAddressBuffer
    // dst (u0) as a root UAV. A signature that declared only the CBV would
    // leave u0 unbound and the dispatch would prove nothing.
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned{};
    versioned.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned.Desc_1_1.Flags=D3D12_ROOT_SIGNATURE_FLAG_NONE;
    D3D12_ROOT_PARAMETER1 params[2]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;   // cbuffer Args (b0)
    params[0].Descriptor.ShaderRegister=0;params[0].Descriptor.RegisterSpace=0;
    params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;   // RWByteAddressBuffer dst (u0)
    params[1].Descriptor.ShaderRegister=0;params[1].Descriptor.RegisterSpace=0;
    params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    versioned.Desc_1_1.NumParameters=2;
    versioned.Desc_1_1.pParameters=params;
    ID3DBlob* versionedBlob=nullptr;ID3DBlob* versionedErrors=nullptr;
    if(FAILED(D3D12SerializeVersionedRootSignature(&versioned,&versionedBlob,&versionedErrors)))return 57;
    ID3D12RootSignature* rootSignature11=nullptr;
    hr=device->CreateRootSignature(0,versionedBlob->GetBufferPointer(),versionedBlob->GetBufferSize(),
        __uuidof(ID3D12RootSignature),(void**)&rootSignature11);
    versionedBlob->Release();if(versionedErrors)versionedErrors->Release();
    if(FAILED(hr)||!rootSignature11)return 58;

    // Compute: a PSO from the committed SM 5.0 DXBC (dst.Store(i*4, i+addend)
    // over an RWByteAddressBuffer), a Dispatch with the addend as a root
    // constant, a UAV barrier, and the same READBACK proof the triangle uses:
    // exit 42 only when the GPU wrote the expected four values, so a dispatch
    // that silently did nothing fails here.
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
    compute.pRootSignature=rootSignature11;
    compute.CS={compute_cs,sizeof compute_cs};
    ID3D12PipelineState* computePipeline=nullptr;
    if(FAILED(device->CreateComputePipelineState(&compute,__uuidof(ID3D12PipelineState),
            (void**)&computePipeline))||!computePipeline)return 59;

    // The UAV storage (DEFAULT heap) and its readback side (READBACK heap), the
    // pair a game uses for a compute pass it wants to see.
    D3D12_RESOURCE_DESC uavDesc{};
    uavDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    uavDesc.Width=16;   // four uint32 elements
    uavDesc.Height=1;   // buffers are Height 1
    uavDesc.DepthOrArraySize=1;uavDesc.MipLevels=1;
    uavDesc.SampleDesc.Count=1;uavDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uavDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* uavBuffer=nullptr;
    if(FAILED(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&uavDesc,
            D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&uavBuffer))||!uavBuffer)return 60;
    D3D12_RESOURCE_DESC computeReadbackDesc=uavDesc;
    computeReadbackDesc.Flags=D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* computeReadback=nullptr;
    if(FAILED(device->CreateCommittedResource(&readbackHeap,D3D12_HEAP_FLAG_NONE,&computeReadbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),(void**)&computeReadback))||!computeReadback)return 61;

    if(FAILED(allocator->Reset()))return 62;
    if(FAILED(list->Reset(allocator,computePipeline)))return 63;
    list->SetComputeRootSignature(rootSignature11);
    // The addend as a root CBV: a 4-byte upload-heap buffer holding one uint,
    // the way an engine passes per-draw constants to compute.
    const UINT addend=7;
    D3D12_RESOURCE_DESC cbDesc{};
    cbDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width=4;cbDesc.Height=1;   // buffers are Height 1
    cbDesc.DepthOrArraySize=1;cbDesc.MipLevels=1;
    cbDesc.SampleDesc.Count=1;cbDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* cbBuffer=nullptr;
    if(FAILED(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),(void**)&cbBuffer))||!cbBuffer)return 79;
    void* mappedCb=nullptr;
    D3D12_RANGE cbReadNone{0,0};
    if(FAILED(cbBuffer->Map(0,&cbReadNone,&mappedCb))||!mappedCb)return 80;
    memcpy(mappedCb,&addend,sizeof addend);
    cbBuffer->Unmap(0,nullptr);
    list->SetComputeRootConstantBufferView(0,cbBuffer->GetGPUVirtualAddress());
    // A compute dispatch needs the UAV in a descriptor heap the list can point
    // at: one CBV_SRV_UAV heap with a single element, the smallest a game's
    // compute pass creates.
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
    cbvHeapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.NumDescriptors=1;
    cbvHeapDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* cbvHeap=nullptr;
    if(FAILED(device->CreateDescriptorHeap(&cbvHeapDesc,__uuidof(ID3D12DescriptorHeap),(void**)&cbvHeap))||!cbvHeap)return 64;
    D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle=cbvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavView{};
    uavView.Format=DXGI_FORMAT_R32_TYPELESS;
    uavView.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    uavView.Buffer.FirstElement=0;uavView.Buffer.NumElements=4;
    uavView.Buffer.CounterOffsetInBytes=0;uavView.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_NONE;
    device->CreateUnorderedAccessView(uavBuffer,nullptr,&uavView,cbvHandle);
    ID3D12DescriptorHeap* visibleHeaps[]={cbvHeap};
    list->SetDescriptorHeaps(1,visibleHeaps);
    // The UAV as the root signature's second parameter (u0): the GPU virtual
    // address of the storage the dispatch writes through. The heap view above
    // keeps a debuggable copy; the shader reads the root binding.
    list->SetComputeRootUnorderedAccessView(1,uavBuffer->GetGPUVirtualAddress());
    // The buffer was created COMMON; a dispatch writes it as a UAV.
    D3D12_RESOURCE_BARRIER toUav{};
    toUav.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toUav.Transition.pResource=uavBuffer;
    toUav.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toUav.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
    toUav.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1,&toUav);
    list->Dispatch(1,1,1);   // one group of 4 threads: writes elements 0..3
    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource=uavBuffer;
    list->ResourceBarrier(1,&uavBarrier);
    D3D12_RESOURCE_BARRIER toCopyUav{};
    toCopyUav.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopyUav.Transition.pResource=uavBuffer;
    toCopyUav.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopyUav.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toCopyUav.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&toCopyUav);
    list->CopyBufferRegion(computeReadback,0,uavBuffer,0,16);
    if(FAILED(list->Close()))return 65;
    {ID3D12CommandList* computeLists[]={list};
     queue->ExecuteCommandLists(1,computeLists);}
    ++fenceValue;
    if(FAILED(queue->Signal(fence,fenceValue)))return 66;
    if(FAILED(fence->SetEventOnCompletion(fenceValue,event)))return 67;
    if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 68;
    {
        void* mappedCompute=nullptr;
        D3D12_RANGE readCompute{0,16};
        if(FAILED(computeReadback->Map(0,&readCompute,&mappedCompute))||!mappedCompute)return 69;
        const unsigned* values=static_cast<const unsigned*>(mappedCompute);
        bool computeOk=values[0]==0+addend&&values[1]==1+addend&&values[2]==2+addend&&values[3]==3+addend;
        D3D12_RANGE writtenCompute{0,0};
        computeReadback->Unmap(0,&writtenCompute);
        // Same convention as the triangle: the pipeline ran but the GPU wrote
        // nothing the guest can read is BLOCKED, never a pass.
        if(!computeOk)return 43;
    }

    // ResizeBuffers on the live flip-model chain: a game's window-size change
    // goes through this, and it must come back with buffers the RTV heap can
    // still view. The GPU is idle here (every submit waited on above).
    for(UINT i=0;i<kFrames;++i)targets[i]->Release();
    hr=chain->ResizeBuffers(0,320,240,kFormat,0);   // 0 keeps the current buffer count
    if(FAILED(hr))return 70;
    D3D12_CPU_DESCRIPTOR_HANDLE resizedHandle=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<kFrames;++i) {
        if(FAILED(chain->GetBuffer(i,__uuidof(ID3D12Resource),(void**)&targets[i]))||!targets[i])return 71;
        device->CreateRenderTargetView(targets[i],nullptr,resizedHandle);
        resizedHandle.ptr+=stride;
    }
    // One more presented frame on the resized buffers, so a resize that only
    // looked right still fails. Flip-model buffer indices advance per Present:
    // the RTV, barrier and clear must all target the buffer
    // GetCurrentBackBufferIndex names, not a fixed slot.
    const UINT resizedIndex=static_cast<IDXGISwapChain3*>(chain)->GetCurrentBackBufferIndex();
    if(FAILED(allocator->Reset()))return 72;
    if(FAILED(list->Reset(allocator,nullptr)))return 73;
    D3D12_RESOURCE_BARRIER resizedToTarget{};
    resizedToTarget.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resizedToTarget.Transition.pResource=targets[resizedIndex];
    resizedToTarget.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resizedToTarget.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
    resizedToTarget.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1,&resizedToTarget);
    const FLOAT resizedColour[4]={0.2f,0.7f,0.3f,1.0f};
    D3D12_CPU_DESCRIPTOR_HANDLE resizedRtv=heap->GetCPUDescriptorHandleForHeapStart();
    resizedRtv.ptr+=resizedIndex*stride;
    list->ClearRenderTargetView(resizedRtv,resizedColour,0,nullptr);
    D3D12_RESOURCE_BARRIER resizedToPresent=resizedToTarget;
    resizedToPresent.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    resizedToPresent.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1,&resizedToPresent);
    if(FAILED(list->Close()))return 74;
    {ID3D12CommandList* resizedLists[]={list};
     queue->ExecuteCommandLists(1,resizedLists);}
    if(FAILED(chain->Present(1,0)))return 75;
    ++fenceValue;
    if(FAILED(queue->Signal(fence,fenceValue)))return 76;
    if(FAILED(fence->SetEventOnCompletion(fenceValue,event)))return 77;
    if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 78;

    // With the debug layer on, anything it recorded as an error up to here
    // (device creation through the readback) fails the run even where the
    // pixels happened to look right. The deliberate invalid calls further
    // down run after this check: they are meant to be rejected.
    if(debugLayer){
        ID3D12InfoQueue* info=nullptr;
        if(SUCCEEDED(device->QueryInterface(__uuidof(ID3D12InfoQueue),(void**)&info))&&info){
            static char buffer[2048];
            UINT64 stored=info->GetNumStoredMessages();
            for(UINT64 i=0;i<stored;++i){
                SIZE_T size=0;
                info->GetMessage(i,nullptr,&size);
                if(!size||size>sizeof buffer)continue;
                auto message=reinterpret_cast<D3D12_MESSAGE*>(buffer);
                if(SUCCEEDED(info->GetMessage(i,message,&size))&&
                        message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
                    info->Release();return 56;
                }
            }
            info->Release();
        }
    }

    // Each frame: reset, barrier to RENDER_TARGET, clear, barrier back to
    // PRESENT, close, execute, present, then wait for the GPU to finish.
    // The RTV and barrier follow GetCurrentBackBufferIndex, so the loop stays
    // correct no matter how many presents ran before it (the resize pass
    // above advanced the flip index too).
    for(UINT frame=0;frame<kFrames;++frame) {
        const UINT backIndex=static_cast<IDXGISwapChain3*>(chain)->GetCurrentBackBufferIndex();
        if(FAILED(allocator->Reset()))return 23;
        if(FAILED(list->Reset(allocator,nullptr)))return 24;
        D3D12_RESOURCE_BARRIER toTarget{};
        toTarget.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toTarget.Transition.pResource=targets[backIndex];
        toTarget.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toTarget.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
        toTarget.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&toTarget);
        const FLOAT colour[4]={0.10f,0.45f,0.85f,1.0f};
        D3D12_CPU_DESCRIPTOR_HANDLE frameRtv=heap->GetCPUDescriptorHandleForHeapStart();
        frameRtv.ptr+=backIndex*stride;
        list->ClearRenderTargetView(frameRtv,colour,0,nullptr);
        D3D12_RESOURCE_BARRIER toPresent=toTarget;
        toPresent.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&toPresent);
        if(FAILED(list->Close()))return 25;
        ID3D12CommandList* lists[]={list};
        queue->ExecuteCommandLists(1,lists);
        if(FAILED(chain->Present(1,0)))return 26;
        ++fenceValue;
        if(FAILED(queue->Signal(fence,fenceValue)))return 27;
        if(FAILED(fence->SetEventOnCompletion(fenceValue,event)))return 28;
        if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 29;
    }
    // A call the device must reject, so Play's diagnostics are exercised too.
    // These run after the error-count check above: they are meant to fail.
    D3D12_RESOURCE_DESC bad{};
    bad.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;bad.Width=64;bad.Height=64;
    bad.DepthOrArraySize=1;bad.MipLevels=1;bad.Format=DXGI_FORMAT_UNKNOWN;bad.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type=D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* rejected=nullptr;
    if(!FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&bad,
            D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&rejected)))return 30;
    // The same failure again must not be logged twice.
    bad.Width=32;
    if(!FAILED(device->CreateCommittedResource(&heapProps,D3D12_HEAP_FLAG_NONE,&bad,
            D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&rejected)))return 31;

    CloseHandle(event);
    for(UINT i=0;i<kFrames;++i)targets[i]->Release();
    list->Release();allocator->Release();fence->Release();heap->Release();
    rootSignature->Release();vertexBuffer->Release();pipeline->Release();
    readback->Release();target->Release();
    cbvHeap->Release();computePipeline->Release();rootSignature11->Release();
    uavBuffer->Release();computeReadback->Release();cbBuffer->Release();
    chain->Release();queue->Release();device->Release();
    return 42;
}
}
