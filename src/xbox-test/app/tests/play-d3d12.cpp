// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that renders one D3D12 frame through Play's
// routes, the way a D3D12 game starts up. It creates a device through
// D3D12CreateDevice, a direct command queue, a factory, a flip-model swap
// chain on a guest HWND with the queue as the device argument, render-target
// views on the back buffers, a fence and an event, then clears each back
// buffer to a colour and presents. Exit code 42 = every step succeeded and the
// GPU finished the frame; anything else names the step that failed. No
// rendering claim: the log is the evidence.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

extern "C" {
int d3d12_anchor;
void* d3d12_relocation=&d3d12_anchor;

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

    // One RTV descriptor heap with a view per back buffer.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heapDesc.NumDescriptors=kFrames;
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
    chain->Release();queue->Release();device->Release();
    return 42;
}
}
