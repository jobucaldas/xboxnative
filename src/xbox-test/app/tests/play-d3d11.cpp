// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that draws one D3D11 triangle the way a game's
// first frame does, through Play's routes. It creates a device through
// D3D11CreateDevice, a swap chain on a guest HWND (which Play's factory maps
// to the CoreWindow adapter), builds the pipeline from precompiled DXBC
// (shaders/triangle_bytecode.h, committed beside its HLSL; nothing is
// compiled at run time and no shader binary is produced at build time),
// draws, then reads the pixels back through a staging texture and checks
// them. Exit code 42 = the triangle rendered where it should be and the
// rejected calls after it failed the way the device reports; anything else
// names the step. The log is the diagnostics evidence.
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include "../../uwph/host/shaders/triangle_bytecode.h"

extern "C" {
int d3d_anchor;
void* d3d_relocation=&d3d_anchor;

// One guest window, created through the same user32 record core a game uses:
// Play's factory validates it and maps it to the launcher's CoreWindow.
static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d11";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d11",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

DWORD d3d_entry() {
    // ---- the draw: device, factory, swap chain, triangle, readback ----
    HWND window=guestWindow();
    if(!window)return 10;

    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL got{};
    const D3D_FEATURE_LEVEL asked[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,asked,2,D3D11_SDK_VERSION,&device,&got,&context);
    if(FAILED(hr))   // an implementation without 11_1 rejects the list outright
        hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,asked+1,1,D3D11_SDK_VERSION,&device,&got,&context);
    if(FAILED(hr)||!device)return 11;

    // The swap chain goes through the guest device's factory, whose HWND
    // entries Play redirects to the CoreWindow adapter.
    IDXGIDevice* dxgi=nullptr;
    if(FAILED(device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi))||!dxgi)return 12;
    IDXGIAdapter* adapter=nullptr;
    if(FAILED(dxgi->GetAdapter(&adapter))||!adapter)return 13;
    IDXGIFactory2* factory=nullptr;
    if(FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),(void**)&factory))||!factory)return 14;
    adapter->Release();dxgi->Release();

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=64;desc.Height=64;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling=DXGI_SCALING_STRETCH;
    IDXGISwapChain1* swapchain=nullptr;
    hr=factory->CreateSwapChainForHwnd(device,window,&desc,nullptr,nullptr,&swapchain);
    factory->Release();
    if(FAILED(hr)||!swapchain)return 15;

    // Offscreen target and a CPU-readable staging copy: the pair a game uses
    // for a screenshot, and the only way to see the draw.
    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width=64;targetDesc.Height=64;targetDesc.MipLevels=1;targetDesc.ArraySize=1;
    targetDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;targetDesc.SampleDesc.Count=1;
    targetDesc.Usage=D3D11_USAGE_DEFAULT;targetDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* target=nullptr;
    if(FAILED(device->CreateTexture2D(&targetDesc,nullptr,&target))||!target)return 16;
    targetDesc.BindFlags=0;targetDesc.Usage=D3D11_USAGE_STAGING;
    targetDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging=nullptr;
    if(FAILED(device->CreateTexture2D(&targetDesc,nullptr,&staging))||!staging)return 17;

    ID3D11Texture2D* backBuffer=nullptr;
    if(FAILED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&backBuffer))||!backBuffer)return 18;

    // Pipeline from the committed DXBC: the same shaders the host draw test
    // uses, so no compiler and no generated blob are involved.
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    if(FAILED(device->CreateVertexShader(triangle_vs,sizeof triangle_vs,nullptr,&vs))||!vs)return 19;
    if(FAILED(device->CreatePixelShader(triangle_ps,sizeof triangle_ps,nullptr,&ps))||!ps)return 20;
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ID3D11InputLayout* layout=nullptr;
    if(FAILED(device->CreateInputLayout(elements,2,triangle_vs,sizeof triangle_vs,&layout))||!layout)return 21;

    // Clip-space vertices (the HLSL writes the position straight through) over
    // the left of the target, all red: x=-0.75 maps to pixel 8, x=0.75 to 56
    // through the 64-wide viewport.
    const float vertices[][6]={
        {-0.75f, 0.75f,1,0,0,1},
        { 0.75f, 0.75f,1,0,0,1},
        { 0.0f,-0.75f,1,0,0,1}};
    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth=sizeof vertices;vb.Usage=D3D11_USAGE_IMMUTABLE;vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=vertices;
    ID3D11Buffer* verticesBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&vb,&initial,&verticesBuffer))||!verticesBuffer)return 22;

    D3D11_VIEWPORT viewport{0,0,64,64,0.0f,1.0f};
    context->RSSetViewports(1,&viewport);
    context->IASetInputLayout(layout);
    UINT stride=sizeof vertices[0],offset=0;
    ID3D11Buffer* bound=verticesBuffer;
    context->IASetVertexBuffers(0,1,&bound,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs,nullptr,0);
    context->PSSetShader(ps,nullptr,0);

    ID3D11RenderTargetView* rtv=nullptr;
    if(FAILED(device->CreateRenderTargetView(target,nullptr,&rtv))||!rtv)return 23;
    const FLOAT clear[4]={0,0,0,1};
    context->ClearRenderTargetView(rtv,clear);
    ID3D11RenderTargetView* binds[]={rtv};
    context->OMSetRenderTargets(1,binds,nullptr);
    context->Draw(3,0);

    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped))||!mapped.pData)return 24;
    const unsigned char* inside=static_cast<const unsigned char*>(mapped.pData)+32*mapped.RowPitch+32*4;
    const unsigned char* outside=static_cast<const unsigned char*>(mapped.pData)+4*mapped.RowPitch+4*4;
    const bool insideRed=inside[0]>0x80&&inside[1]<0x40&&inside[2]<0x40;
    const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
    context->Unmap(staging,0);
    // 43 is BLOCKED (never a pass): the pipeline ran but the pixels did not
    // read back as drawn, the same outcome the opengl fixture reports when
    // the host drew nothing the guest could read.
    if(!insideRed||!outsideBlack)return 43;

    // ---- the geometry-shader pass: one point expanded through the GS ----
    // The shape Halo Wars' terrain/minimap layer uses (pointlist input, a GS
    // that appends vertices): if the host's GS route drops the expansion the
    // pixels stay black exactly as the game's terrain does. Same target, a
    // fresh clear, one point at the center expanded to a green triangle.
    ID3D11GeometryShader* gs=nullptr;
    if(FAILED(device->CreateGeometryShader(triangle_gs,sizeof triangle_gs,nullptr,&gs))||!gs)return 44;
    // The GS's input is the VS output: one vertex carrying POSITION (the VS
    // passes it through) plus COLOR for the PS. Feed the same stride the
    // triangle pass uses so the input layout applies unchanged.
    const float pointV[6]={0.0f,0.0f,0,1,0,1};   // POSITION + COLOR, green
    D3D11_BUFFER_DESC pvb{};
    pvb.ByteWidth=sizeof pointV;pvb.Usage=D3D11_USAGE_IMMUTABLE;pvb.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA pvInit{};pvInit.pSysMem=pointV;
    ID3D11Buffer* pointFullBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&pvb,&pvInit,&pointFullBuffer))||!pointFullBuffer)return 45;
    context->ClearRenderTargetView(rtv,clear);
    ID3D11Buffer* pointBinds[]={pointFullBuffer};
    context->IASetVertexBuffers(0,1,pointBinds,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    context->GSSetShader(gs,nullptr,0);
    context->Draw(1,0);
    context->GSSetShader(nullptr,nullptr,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* triBinds[]={verticesBuffer};
    context->IASetVertexBuffers(0,1,triBinds,&stride,&offset);

    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE gMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&gMapped))||!gMapped.pData)return 46;
    // The GS expands the center point to a triangle spanning y+0.4, y-0.4,
    // x±0.4: pixel (32,32) lands inside it, green through the PS.
    const unsigned char* gInside=static_cast<const unsigned char*>(gMapped.pData)+32*gMapped.RowPitch+32*4;
    const bool insideGreen=gInside[1]>0x80&&gInside[0]<0x40&&gInside[2]<0x40;
    context->Unmap(staging,0);
    if(!insideGreen)return 47;   // BLOCKED: the GS expansion did not reach the target

    // The draw reaches the swap chain's back buffer too, as a game's does:
    // present one frame with the triangle in it.
    ID3D11RenderTargetView* backView=nullptr;
    if(FAILED(device->CreateRenderTargetView(backBuffer,nullptr,&backView))||!backView)return 26;
    context->ClearRenderTargetView(backView,clear);
    ID3D11RenderTargetView* backBinds[]={backView};
    context->OMSetRenderTargets(1,backBinds,nullptr);
    D3D11_VIEWPORT full{0,0,64,64,0.0f,1.0f};
    context->RSSetViewports(1,&full);
    context->Draw(3,0);
    hr=swapchain->Present(0,0);
    if(hr==DXGI_ERROR_DEVICE_REMOVED)return 27;

    // ---- the diagnostics: calls the device must reject, logged once ----
    D3D11_TEXTURE2D_DESC texture{};texture.Width=256;texture.Height=256;texture.MipLevels=1;texture.ArraySize=1;
    texture.Format=DXGI_FORMAT_UNKNOWN;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* created=nullptr;
    if(!FAILED(device->CreateTexture2D(&texture,nullptr,&created)))return 28;
    // The same failure again must not be logged twice; a different one is.
    texture.Width=64;
    if(!FAILED(device->CreateTexture2D(&texture,nullptr,&created)))return 29;
    D3D11_BUFFER_DESC buffer{};buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;   // zero bytes
    ID3D11Buffer* made=nullptr;
    if(!FAILED(device->CreateBuffer(&buffer,nullptr,&made)))return 30;
    UINT support=0;
    if(!FAILED(device->CheckFormatSupport(DXGI_FORMAT(0x7fff),&support))&&support)return 31;

    backView->Release();backBuffer->Release();
    rtv->Release();pointFullBuffer->Release();gs->Release();
    verticesBuffer->Release();layout->Release();
    ps->Release();vs->Release();
    staging->Release();target->Release();
    swapchain->Release();context->Release();device->Release();
    DestroyWindow(window);
    return 42;
}
}
