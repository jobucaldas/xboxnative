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
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include "../../uwph/host/shaders/triangle_bytecode.h"

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
    HWND window=guestWindow();
    if(!window)return 10;
    // The adapter argument is optional; a D3D12 game that picks one passes the
    // factory's adapter, which Play wraps. Default (null) exercises the same
    // path a game takes when it lets the runtime choose.
    ID3D12Device* device=nullptr;
    HRESULT hr=D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),(void**)&device);
    if(FAILED(hr)||!device)return 11;
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
    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* target=nullptr;
    if(FAILED(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&targetDesc,
            D3D12_RESOURCE_STATE_COMMON,nullptr,__uuidof(ID3D12Resource),(void**)&target))||!target)return 40;
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type=D3D12_HEAP_TYPE_READBACK;
    ID3D12Resource* readback=nullptr;
    if(FAILED(device->CreateCommittedResource(&readbackHeap,D3D12_HEAP_FLAG_NONE,&targetDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,__uuidof(ID3D12Resource),(void**)&readback))||!readback)return 41;

    // The committed SM 5.0 DXBC: a D3D12 PSO takes shader model 5.0 bytecode
    // (it is the same DXBC container 6.x compiles into).
    D3D12_SHADER_BYTECODE vsBytecode{triangle_vs,sizeof triangle_vs};
    D3D12_SHADER_BYTECODE psBytecode{triangle_ps,sizeof triangle_ps};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
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
    vbDesc.Width=sizeof vertices;vbDesc.DepthOrArraySize=1;vbDesc.MipLevels=1;
    vbDesc.SampleDesc.Count=1;vbDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* vertexBuffer=nullptr;
    if(FAILED(device->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,__uuidof(ID3D12Resource),(void**)&vertexBuffer))||!vertexBuffer)return 43;
    void* mappedVertices=nullptr;
    D3D12_RANGE readNone{0,0};
    if(FAILED(vertexBuffer->Map(0,&readNone,&mappedVertices))||!mappedVertices)return 44;
    memcpy(mappedVertices,vertices,sizeof vertices);
    vertexBuffer->Unmap(0,nullptr);
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    vertexView.BufferLocation=vertexBuffer->GetGPUVirtualAddress();
    vertexView.StrideInBytes=sizeof vertices[0];
    vertexView.SizeInBytes=sizeof vertices;

    // A root signature with no parameters: the shaders read nothing beyond
    // their vertex input.
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rootBlob=nullptr;ID3DBlob* rootErrors=nullptr;
    if(FAILED(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBlob,&rootErrors)))return 45;
    ID3D12RootSignature* rootSignature=nullptr;
    hr=device->CreateRootSignature(0,rootBlob->GetBufferPointer(),rootBlob->GetBufferSize(),
        __uuidof(ID3D12RootSignature),(void**)&rootSignature);
    rootBlob->Release();if(rootErrors)rootErrors->Release();
    if(FAILED(hr)||!rootSignature)return 46;

    // Draw into the offscreen target, copy it to the readback heap, wait. The
    // RTV heap's first kFrames slots belong to the back buffers; the target
    // gets its own view in the slot after them.
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
    D3D12_VIEWPORT drawViewport{0,0,64,64,0.0f,1.0f};
    list->RSSetViewports(1,&drawViewport);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->SetGraphicsRootSignature(rootSignature);
    list->IASetVertexBuffers(0,1,&vertexView);
    list->DrawInstanced(3,1,0,0);
    D3D12_RESOURCE_BARRIER toCommon=toDraw;
    toCommon.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCommon.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1,&toCommon);
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource=target;
    toCopy.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toCopy.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
    toCopy.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1,&toCopy);
    list->CopyResource(readback,target);
    D3D12_RESOURCE_BARRIER backToCommon=toCopy;
    backToCommon.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
    backToCommon.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1,&backToCommon);
    if(FAILED(list->Close()))return 49;
    {ID3D12CommandList* drawLists[]={list};
     queue->ExecuteCommandLists(1,drawLists);}
    if(FAILED(queue->Signal(fence,100)))return 50;
    if(FAILED(fence->SetEventOnCompletion(100,event)))return 51;
    if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 52;

    // Check the pixels: the triangle covers the middle-left, the rest is the
    // clear colour. A 64x64 RGBA8 target reads back tightly packed.
    {
        void* mappedRead=nullptr;
        D3D12_RANGE readWhole{0,64*64*4};
        if(FAILED(readback->Map(0,&readWhole,&mappedRead))||!mappedRead)return 53;
        const unsigned char* row=static_cast<const unsigned char*>(mappedRead);
        const unsigned char* inside=row+32*64*4+32*4;   // 32nd row, 32nd pixel
        const unsigned char* outside=row+4*64*4+4*4;    // 4th row, 4th pixel
        const bool insideRed=inside[0]>0x80&&inside[1]<0x40&&inside[2]<0x40;
        const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
        D3D12_RANGE writtenNone{0,0};
        readback->Unmap(0,&writtenNone);
        // 43 is BLOCKED (never a pass): the pipeline ran but the pixels did
        // not read back as drawn, the same outcome the opengl fixture reports
        // when the host drew nothing the guest could read.
        if(!insideRed||!outsideBlack)return 43;
    }

    // Each frame: reset, barrier to RENDER_TARGET, clear, barrier back to
    // PRESENT, close, execute, present, then wait for the GPU to finish.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT frame=0;frame<kFrames;++frame) {
        if(FAILED(allocator->Reset()))return 23;
        if(FAILED(list->Reset(allocator,nullptr)))return 24;
        D3D12_RESOURCE_BARRIER toTarget{};
        toTarget.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toTarget.Transition.pResource=targets[frame];
        toTarget.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toTarget.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
        toTarget.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&toTarget);
        const FLOAT colour[4]={0.10f,0.45f,0.85f,1.0f};
        list->ClearRenderTargetView(rtv,colour,0,nullptr);
        D3D12_RESOURCE_BARRIER toPresent=toTarget;
        toPresent.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&toPresent);
        if(FAILED(list->Close()))return 25;
        ID3D12CommandList* lists[]={list};
        queue->ExecuteCommandLists(1,lists);
        if(FAILED(chain->Present(1,0)))return 26;
        if(FAILED(queue->Signal(fence,frame+1)))return 27;
        if(FAILED(fence->SetEventOnCompletion(frame+1,event)))return 28;
        if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return 29;
        rtv.ptr+=stride;
    }
    // A call the device must reject, so Play's diagnostics are exercised too.
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
    chain->Release();queue->Release();device->Release();
    return 42;
}
}
