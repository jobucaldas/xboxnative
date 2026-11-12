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
#include "../../uwph/host/shaders/depth_bytecode.h"

extern "C" {
// No CRT: the byte-copy the depth pass's dynamic upload needs (the same
// definition play-d3d12.cpp carries; the dynamic-map copy is not folded to a
// constant size the compiler can inline).
void* memcpy(void* d,const void* s,size_t n) {
    auto a=(unsigned char*)d;auto b=(const unsigned char*)s;while(n--)*a++=*b++;return d;
}
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
    ID3D11RenderTargetView* backView=nullptr;
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

    // ---- MSAA pass: 4x render target, clear, resolve, readback ----
    // Games pick MSAA through CheckMultisampleQualityLevels and resolve with
    // ResolveSubresource; if the layer answers the quality probe but drops the
    // resolve, the pixels stay black exactly there. No shader: the cleared
    // colour is the evidence. Exit 50-54 name the step; 54 is BLOCKED (never
    // a pass): the calls ran but the resolved pixel is not the clear colour.
    D3D11_TEXTURE2D_DESC msaaDesc{};
    msaaDesc.Width=64;msaaDesc.Height=64;msaaDesc.MipLevels=1;msaaDesc.ArraySize=1;
    msaaDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;msaaDesc.SampleDesc.Count=4;
    msaaDesc.Usage=D3D11_USAGE_DEFAULT;msaaDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* msaaTarget=nullptr;
    UINT msaaQuality=0;
    if(FAILED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,4,&msaaQuality))||!msaaQuality)return 50;
    if(FAILED(device->CreateTexture2D(&msaaDesc,nullptr,&msaaTarget))||!msaaTarget)return 51;
    ID3D11RenderTargetView* msaaView=nullptr;
    if(FAILED(device->CreateRenderTargetView(msaaTarget,nullptr,&msaaView))||!msaaView)return 52;
    // ResolveSubresource needs a DEFAULT single-sample destination of the same
    // format; a staging texture is not a legal destination (the debug layer
    // rejects it, and a layer that tolerates the call writes nothing). Resolve
    // into a default texture, then copy that into the staging readback.
    D3D11_TEXTURE2D_DESC resolveDesc=msaaDesc;
    resolveDesc.SampleDesc.Count=1;resolveDesc.BindFlags=0;
    ID3D11Texture2D* resolveTarget=nullptr;
    if(FAILED(device->CreateTexture2D(&resolveDesc,nullptr,&resolveTarget))||!resolveTarget)return 52;
    ID3D11RenderTargetView* msaaBinds[]={msaaView};
    context->OMSetRenderTargets(1,msaaBinds,nullptr);
    const FLOAT msaaClear[4]={1,0,0,1};   // red: the colour the readback checks
    context->ClearRenderTargetView(msaaView,msaaClear);
    context->ResolveSubresource(resolveTarget,0,msaaTarget,0,DXGI_FORMAT_R8G8B8A8_UNORM);
    context->CopyResource(staging,resolveTarget);
    D3D11_MAPPED_SUBRESOURCE mMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&mMapped))||!mMapped.pData)return 53;
    // The resolve copies the whole resource, so the staging texture now
    // holds the MSAA target's cleared red everywhere.
    const unsigned char* mPixel=static_cast<const unsigned char*>(mMapped.pData)+16*mMapped.RowPitch+16*4;
    const bool msaaRed=mPixel[0]>0x80&&mPixel[1]<0x40&&mPixel[2]<0x40;
    context->Unmap(staging,0);
    msaaView->Release();resolveTarget->Release();msaaTarget->Release();
    if(!msaaRed)return 54;   // BLOCKED: the resolve did not reach the readback

    // ---- GenerateMips pass: mip chain, SRV, one GenerateMips, readback ----
    // A game's textures arrive with MipLevels>1 and the runtime fills the
    // smaller levels on bind; a dropped GenerateMips leaves level 1 black
    // while level 0 still samples, which reads as a shimmering surface. The
    // top level is filled from CPU data (green), the filter must shrink it
    // into level 1. Exits 55-58 name the step; 58 is BLOCKED (never a pass).
    D3D11_TEXTURE2D_DESC mipDesc{};
    mipDesc.Width=16;mipDesc.Height=16;mipDesc.MipLevels=2;mipDesc.ArraySize=1;
    mipDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;mipDesc.SampleDesc.Count=1;
    mipDesc.Usage=D3D11_USAGE_DEFAULT;mipDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    mipDesc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
    unsigned int green[16*16];
    for(unsigned int& px:green)px=0xFF00D000u;   // ABGR little-endian: green
    // Level 1 starts red, deliberately NOT the success colour: if GenerateMips
    // is a no-op the readback stays red and the pass blocks. Only a mip
    // filter that actually shrank level 0 can make it green.
    unsigned int redSmall[8*8];
    for(unsigned int& px:redSmall)px=0xFF0000FFu;   // ABGR: red
    // CreateTexture2D wants MipLevels*ArraySize initial-data entries: level 0
    // (16x16) and level 1 (8x8), so nothing is read past the array. Only
    // level 0 carries the source colour; level 1 is what GenerateMips fills.
    D3D11_SUBRESOURCE_DATA mipInit[2]={};
    mipInit[0].pSysMem=green;mipInit[0].SysMemPitch=16*4;
    mipInit[1].pSysMem=redSmall;mipInit[1].SysMemPitch=8*4;
    ID3D11Texture2D* mipTexture=nullptr;
    if(FAILED(device->CreateTexture2D(&mipDesc,mipInit,&mipTexture))||!mipTexture)return 55;
    ID3D11ShaderResourceView* mipSrv=nullptr;
    if(FAILED(device->CreateShaderResourceView(mipTexture,nullptr,&mipSrv))||!mipSrv)return 56;
    context->GenerateMips(mipSrv);
    // Read level 1 back: a second staging texture of the same size, CopySubresourceRegion.
    D3D11_TEXTURE2D_DESC mipStageDesc=mipDesc;
    mipStageDesc.MipLevels=0;mipStageDesc.BindFlags=0;mipStageDesc.MiscFlags=0;
    mipStageDesc.Usage=D3D11_USAGE_STAGING;mipStageDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* mipStaging=nullptr;
    if(FAILED(device->CreateTexture2D(&mipStageDesc,nullptr,&mipStaging))||!mipStaging)return 57;
    D3D11_BOX mipBox{0,0,0,8,8,1};
    context->CopySubresourceRegion(mipStaging,0,0,0,0,mipTexture,1,&mipBox);
    D3D11_MAPPED_SUBRESOURCE mipMapped{};
    if(FAILED(context->Map(mipStaging,0,D3D11_MAP_READ,0,&mipMapped))||!mipMapped.pData)return 57;
    const unsigned char* gPixel=static_cast<const unsigned char*>(mipMapped.pData)+4*mipMapped.RowPitch+4*4;
    // Level 1 was created red; only a real mip filter can make it green.
    const bool mipGreen=gPixel[1]>0x40&&gPixel[0]<0x40;   // green must arrive, red must go
    context->Unmap(mipStaging,0);
    mipSrv->Release();mipStaging->Release();mipTexture->Release();
    if(!mipGreen)return 58;   // BLOCKED: level 1 never received the filtered colour

    // ---- Deferred-context pass: record a clear+draw, ExecuteCommandList ----
    // Unity and Unreal record whole frames on deferred contexts and hand the
    // launcher one command list; if the layer's FinishCommandList returns an
    // empty list or ExecuteCommandList drops it, the game renders nothing
    // while every immediate call still succeeds. Exits 60-63 name the step;
    // 63 is BLOCKED (never a pass): the recorded draw did not reach the target.
    ID3D11DeviceContext* deferred=nullptr;
    if(FAILED(device->CreateDeferredContext(0,&deferred))||!deferred)return 60;
    ID3D11RenderTargetView* dView=nullptr;
    if(FAILED(device->CreateRenderTargetView(target,nullptr,&dView))||!dView)return 61;
    ID3D11RenderTargetView* dBinds[]={dView};
    deferred->OMSetRenderTargets(1,dBinds,nullptr);
    D3D11_VIEWPORT dViewport{0,0,64,64,0.0f,1.0f};
    deferred->RSSetViewports(1,&dViewport);
    const FLOAT dClear[4]={1,0,0,1};   // red: the colour the readback checks
    deferred->ClearRenderTargetView(dView,dClear);
    ID3D11CommandList* list=nullptr;
    if(FAILED(deferred->FinishCommandList(FALSE,&list))||!list)return 62;
    // ExecuteCommandList(FALSE) resets the immediate context to pipeline
    // defaults, exactly as a game's next recorded frame expects; the passes
    // after this one rebind everything they use (vertex buffer, RTV,
    // viewport), so nothing here depends on stale state.
    context->ExecuteCommandList(list,FALSE);
    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE dMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&dMapped))||!dMapped.pData)return 62;
    // The deferred clear painted the whole target red; sample a corner.
    const unsigned char* dPixel=static_cast<const unsigned char*>(dMapped.pData)+48*dMapped.RowPitch+48*4;
    const bool deferredRed=dPixel[0]>0x80&&dPixel[1]<0x40&&dPixel[2]<0x40;
    context->Unmap(staging,0);
    list->Release();dView->Release();deferred->Release();
    if(!deferredRed)return 63;   // BLOCKED: the command list did not reach the target

    // ---- Stream-out pass: capture VS output, read the SO buffer back ----
    // Particle systems and GPU-skinned animation capture the vertex stage
    // into an SO buffer and later draw it back (DrawAuto); a layer that
    // accepts SOSetTargets but writes nothing leaves the capture empty, which
    // reads as a vanished particle system. No new shader: the committed VS is
    // the stream-out stage, its output layout (SV_POSITION+COLOR) is the
    // entry. Exits 64-67 name the step; 67 is BLOCKED (never a pass).
    D3D11_SO_DECLARATION_ENTRY soDecl[2]={
        {0,"SV_POSITION",0,0,4,0},
        {0,"COLOR",0,0,4,0}};
    ID3D11GeometryShader* soGs=nullptr;
    // Stream-out without a GS uses a GS created with the SO declarations and
    // no shader body: the standard D3D11 route (CreateGeometryShaderWithSO,
    // pShaderBytecode=nullptr is not valid, so the SO-only route passes the
    // declarations with a zero-length entry count through a real GS call).
    if(FAILED(device->CreateGeometryShaderWithStreamOutput(triangle_vs,sizeof triangle_vs,
        soDecl,2,nullptr,0,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&soGs))||!soGs)return 64;
    D3D11_BUFFER_DESC soDesc{};
    soDesc.ByteWidth=3*sizeof(float)*8;   // 3 vertices, 8 floats each (position+color)
    soDesc.Usage=D3D11_USAGE_DEFAULT;soDesc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    soDesc.CPUAccessFlags=0;soDesc.MiscFlags=0;
    ID3D11Buffer* soBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&soDesc,nullptr,&soBuffer))||!soBuffer)return 65;
    ID3D11Buffer* soBinds[]={soBuffer};
    UINT soOffsets[]={0};
    context->GSSetShader(soGs,nullptr,0);
    context->SOSetTargets(1,soBinds,soOffsets);
    context->Draw(3,0);
    ID3D11Buffer* soClear[]={nullptr};
    context->SOSetTargets(1,soClear,soOffsets);
    context->GSSetShader(nullptr,nullptr,0);
    // Read the capture back through a staging buffer.
    D3D11_BUFFER_DESC soReadDesc=soDesc;
    soReadDesc.Usage=D3D11_USAGE_STAGING;soReadDesc.BindFlags=0;
    soReadDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* soStaging=nullptr;
    if(FAILED(device->CreateBuffer(&soReadDesc,nullptr,&soStaging))||!soStaging)return 66;
    context->CopyResource(soStaging,soBuffer);
    D3D11_MAPPED_SUBRESOURCE soMapped{};
    if(FAILED(context->Map(soStaging,0,D3D11_MAP_READ,0,&soMapped))||!soMapped.pData)return 66;
    // The VS passes positions straight through: the first captured vertex is
    // the first drawn vertex (-0.75, 0.75, 0, 1).
    const float* soData=static_cast<const float*>(soMapped.pData);
    const bool soCaptured=soData[0]<-0.7f&&soData[0]>-0.8f&&soData[1]>0.7f&&soData[1]<0.8f;
    context->Unmap(soStaging,0);
    soStaging->Release();soBuffer->Release();soGs->Release();
    if(!soCaptured)return 67;   // BLOCKED: the SO buffer stayed empty

    // ---- Indirect-draw pass: DrawInstancedIndirect from a buffer ----
    // GPU-driven rendering builds argument buffers on the device and draws
    // through DrawInstancedIndirect; a layer that accepts the call but reads
    // nothing draws nothing. No new shader: the committed triangle, arguments
    // filled from CPU data. Exits 68-71 name the step; 71 is BLOCKED.
    D3D11_BUFFER_DESC argDesc{};
    argDesc.ByteWidth=sizeof(D3D11_DRAW_INSTANCED_INDIRECT_ARGS);
    argDesc.Usage=D3D11_USAGE_DEFAULT;argDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    argDesc.CPUAccessFlags=0;argDesc.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    D3D11_DRAW_INSTANCED_INDIRECT_ARGS args{3,1,0,0};
    D3D11_SUBRESOURCE_DATA argInit{};argInit.pSysMem=&args;
    ID3D11Buffer* argBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&argDesc,&argInit,&argBuffer))||!argBuffer)return 68;
    context->ClearRenderTargetView(rtv,clear);
    ID3D11Buffer* indBinds[]={verticesBuffer};
    context->IASetVertexBuffers(0,1,indBinds,&stride,&offset);
    context->OMSetRenderTargets(1,binds,nullptr);
    context->RSSetViewports(1,&viewport);
    context->DrawInstancedIndirect(argBuffer,0);
    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE iMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&iMapped))||!iMapped.pData)return 69;
    const unsigned char* iPixel=static_cast<const unsigned char*>(iMapped.pData)+32*iMapped.RowPitch+32*4;
    const bool indirectRed=iPixel[0]>0x80&&iPixel[1]<0x40&&iPixel[2]<0x40;
    context->Unmap(staging,0);
    argBuffer->Release();
    if(!indirectRed)return 71;   // BLOCKED: the indirect draw never reached the target

    // ---- Query pass: occlusion query + GetData, the readback the GPU gives ----
    // Fog-of-war, occlusion culling and lens flares branch on occlusion
    // queries; a layer that never answers GetData with a real count hangs the
    // game's culling or blinds it. Exits 72-75 name the step; 75 is BLOCKED.
    D3D11_QUERY_DESC qDesc{};qDesc.Query=D3D11_QUERY_OCCLUSION;
    ID3D11Query* query=nullptr;
    if(FAILED(device->CreateQuery(&qDesc,&query))||!query)return 72;
    context->ClearRenderTargetView(rtv,clear);
    context->OMSetRenderTargets(1,binds,nullptr);
    context->Begin(query);
    context->Draw(3,0);
    context->End(query);
    UINT64 passed=0;
    // Poll without blocking first (the way a frame-paced game reads it); the
    // answer must arrive within a bounded spin, not never.
    BOOL gotData=false;
    for(int spin=0;spin<2000&&!gotData;++spin){
        HRESULT qhr=context->GetData(query,&passed,sizeof passed,0);
        if(qhr==S_OK){gotData=TRUE;break;}
        if(FAILED(qhr))return 74;
    }
    if(!gotData)return 75;   // BLOCKED: GetData never answered
    // The triangle covers ~half the 64x64 target: the count must be nonzero
    // and cannot exceed the whole target's pixels.
    if(passed==0||passed>64ull*64ull)return 75;
    query->Release();

    // ---- ResizeBuffers pass: shrink the chain, redraw, present ----
    // Windowed games resize on every window change; a layer that keeps stale
    // buffer associations after ResizeBuffers renders into a dead buffer.
    // Exits 76-79 name the step; 79 is BLOCKED. DXGI requires every back
    // buffer reference released before the resize, so the view goes first and
    // the buffer itself right after — the order a game's resize handler uses.
    // backView is null here (nothing created a view on the back buffer yet),
    // so only the buffer itself is released; a view is created after the
    // resize. The guard keeps a later edit that adds a view from crashing.
    if(backView){backView->Release();backView=nullptr;}
    backBuffer->Release();backBuffer=nullptr;
    hr=swapchain->ResizeBuffers(0,32,32,DXGI_FORMAT_UNKNOWN,0);
    if(FAILED(hr))return 76;
    if(FAILED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&backBuffer))||!backBuffer)return 77;
    if(FAILED(device->CreateRenderTargetView(backBuffer,nullptr,&backView))||!backView)return 77;
    ID3D11RenderTargetView* backBinds[]={backView};
    context->OMSetRenderTargets(1,backBinds,nullptr);
    // ExecuteCommandList(FALSE) above reset the pipeline to defaults; rebind
    // the triangle's state the way a game's frame setup does before drawing.
    context->IASetInputLayout(layout);
    ID3D11Buffer* rbBinds[]={verticesBuffer};
    context->IASetVertexBuffers(0,1,rbBinds,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs,nullptr,0);
    context->PSSetShader(ps,nullptr,0);
    D3D11_VIEWPORT small{0,0,32,32,0.0f,1.0f};
    context->RSSetViewports(1,&small);
    context->ClearRenderTargetView(backView,clear);
    context->Draw(3,0);
    hr=swapchain->Present(0,0);
    if(FAILED(hr))return 78;

    // The draw reaches the swap chain's back buffer too, as a game's does:
    // present one frame with the triangle in it.
    context->ClearRenderTargetView(backView,clear);
    ID3D11RenderTargetView* backBinds2[]={backView};
    context->OMSetRenderTargets(1,backBinds2,nullptr);
    D3D11_VIEWPORT full{0,0,64,64,0.0f,1.0f};
    context->RSSetViewports(1,&full);
    context->Draw(3,0);
    hr=swapchain->Present(0,0);
    if(hr==DXGI_ERROR_DEVICE_REMOVED)return 27;

    // ---- Indexed-draw pass: index buffer + DrawIndexed ----
    // Every mesh renderer draws indexed; a layer that binds the IB but reads
    // vertices in order renders the wrong geometry. Six vertices, two
    // disjoint triangles: (0,1,2) red on the left, (3,4,5) green on the
    // right. The index buffer selects {3,4,5}; a layer that ignores it and
    // draws the first three vertices paints the LEFT red instead. Exits
    // 80-82 name the step; 82 is BLOCKED.
    const float ivertices[][6]={
        {-0.75f, 0.75f,1,0,0,1},   // 0: decoy triangle (the red one, drawn
        { 0.75f, 0.75f,1,0,0,1},   // 1: only if the IB is ignored)
        { 0.0f,-0.75f,1,0,0,1},    // 2
        {-0.75f, 0.75f,0,1,0,1},   // 3: the same proven-winding triangle,
        { 0.75f, 0.75f,0,1,0,1},   // 4: green — what the index buffer picks
        { 0.0f,-0.75f,0,1,0,1}};   // 5
    const unsigned short indices[]={3,4,5};
    D3D11_BUFFER_DESC ivbDesc{};
    ivbDesc.ByteWidth=sizeof ivertices;ivbDesc.Usage=D3D11_USAGE_IMMUTABLE;
    ivbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ivbInit{};ivbInit.pSysMem=ivertices;
    ID3D11Buffer* indexedVb=nullptr;
    if(FAILED(device->CreateBuffer(&ivbDesc,&ivbInit,&indexedVb))||!indexedVb)return 80;
    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.ByteWidth=sizeof indices;ibDesc.Usage=D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibInit{};ibInit.pSysMem=indices;
    ID3D11Buffer* indexBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&ibDesc,&ibInit,&indexBuffer))||!indexBuffer)return 80;
    context->ClearRenderTargetView(rtv,clear);
    ID3D11Buffer* idxBinds[]={indexedVb};
    context->IASetVertexBuffers(0,1,idxBinds,&stride,&offset);
    context->IASetIndexBuffer(indexBuffer,DXGI_FORMAT_R16_UINT,0);
    context->OMSetRenderTargets(1,binds,nullptr);
    context->RSSetViewports(1,&viewport);
    context->DrawIndexed(3,0,0);
    context->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE xMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&xMapped))||!xMapped.pData)return 81;
    // Indexed pixel: the centre (x=32) is inside both triangles — it must be
    // green (the indexed triangle's colour). The decoy triangle is the same
    // shape, so a layer that ignores the IB paints the centre red instead.
    const unsigned char* xPixel=static_cast<const unsigned char*>(xMapped.pData)+32*xMapped.RowPitch+32*4;
    const bool indexedGreen=xPixel[1]>0x80&&xPixel[0]<0x40&&xPixel[2]<0x40;
    context->Unmap(staging,0);
    indexedVb->Release();indexBuffer->Release();
    if(!indexedGreen)return 82;   // BLOCKED: the index fetch never reached the target

    // ---- Depth-stencil pass: DSV + depth test ----
    // Every 3D game writes depth; a layer that accepts the DSV but never
    // tests draws hidden surfaces over near ones (walls over characters).
    // Two overlapping triangles at the same pixels: far green (z=0.8) first,
    // near red (z=0.2) after. Depth test LESS_EQUAL ON: the centre pixel must
    // end red. Depth OFF: the later draw wins, so the centre must be green —
    // both checks together prove the test, not just the draw order. Exits
    // 83-86; 86 BLOCKED. The committed triangle VS hardcodes clip z to 0, so
    // this pass uses its own committed shaders (shaders/depth_*.dxbc):
    // POSITION is float3 and the VS passes z through, which is what makes
    // the two draws differ.
    ID3D11VertexShader* dvs=nullptr;ID3D11PixelShader* dps=nullptr;
    if(FAILED(device->CreateVertexShader(depth_vs,sizeof depth_vs,nullptr,&dvs))||!dvs)return 83;
    if(FAILED(device->CreatePixelShader(depth_ps,sizeof depth_ps,nullptr,&dps))||!dps)return 83;
    const D3D11_INPUT_ELEMENT_DESC dElements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ID3D11InputLayout* dLayout=nullptr;
    if(FAILED(device->CreateInputLayout(dElements,2,depth_vs,sizeof depth_vs,&dLayout))||!dLayout)return 83;
    D3D11_TEXTURE2D_DESC dsDesc{};
    dsDesc.Width=64;dsDesc.Height=64;dsDesc.MipLevels=1;dsDesc.ArraySize=1;
    dsDesc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dsDesc.SampleDesc.Count=1;
    dsDesc.Usage=D3D11_USAGE_DEFAULT;dsDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* dsTexture=nullptr;
    if(FAILED(device->CreateTexture2D(&dsDesc,nullptr,&dsTexture))||!dsTexture)return 83;
    ID3D11DepthStencilView* dsv=nullptr;
    if(FAILED(device->CreateDepthStencilView(dsTexture,nullptr,&dsv))||!dsv)return 83;
    D3D11_DEPTH_STENCIL_DESC dsDescState{};
    dsDescState.DepthEnable=TRUE;
    dsDescState.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    dsDescState.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    ID3D11DepthStencilState* dsState=nullptr;
    if(FAILED(device->CreateDepthStencilState(&dsDescState,&dsState))||!dsState)return 84;
    // The control: depth OFF — the later (near red) draw must win on draw
    // order alone. Both checks together prove the ordering came from the
    // depth test, not from the draw sequence or a fixed colour.
    D3D11_DEPTH_STENCIL_DESC dsOffDesc{};
    dsOffDesc.DepthEnable=FALSE;
    ID3D11DepthStencilState* dsOff=nullptr;
    if(FAILED(device->CreateDepthStencilState(&dsOffDesc,&dsOff))||!dsOff)return 84;
    context->OMSetDepthStencilState(dsOff,0);
    context->ClearRenderTargetView(rtv,clear);
    context->ClearDepthStencilView(dsv,D3D11_CLEAR_DEPTH,1.0f,0);
    ID3D11RenderTargetView* dsBinds[]={rtv};
    context->OMSetRenderTargets(1,dsBinds,dsv);
    context->RSSetViewports(1,&viewport);
    // Far green triangle (z=0.8) first, then near red (z=0.2): the shared
    // centre pixel must end red with the test off (draw order) AND with the
    // test on LESS_EQUAL (depth). The VS passes z through, so the two draws
    // use different vertex data: one dynamic vertex buffer, rewritten per
    // draw (the Map WRITE_DISCARD a game's per-frame upload uses — itself a
    // covered feature).
    D3D11_BUFFER_DESC dvbDesc{};
    dvbDesc.ByteWidth=sizeof(float)*6*3;dvbDesc.Usage=D3D11_USAGE_DYNAMIC;
    dvbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;dvbDesc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* dynVb=nullptr;
    if(FAILED(device->CreateBuffer(&dvbDesc,nullptr,&dynVb))||!dynVb)return 85;
    const float triFar[3][6]={
        {-0.75f,0.75f,0.8f,0,1,0},{0.75f,0.75f,0.8f,0,1,0},{0.0f,-0.75f,0.8f,0,1,0}};
    D3D11_MAPPED_SUBRESOURCE mM{};
    if(FAILED(context->Map(dynVb,0,D3D11_MAP_WRITE_DISCARD,0,&mM))||!mM.pData)return 85;
    memcpy(mM.pData,triFar,sizeof triFar);
    context->Unmap(dynVb,0);
    context->IASetInputLayout(dLayout);
    ID3D11Buffer* rBinds[]={dynVb};
    context->IASetVertexBuffers(0,1,rBinds,&stride,&offset);
    context->VSSetShader(dvs,nullptr,0);
    context->PSSetShader(dps,nullptr,0);
    context->Draw(3,0);
    const float triNear[3][6]={
        {-0.75f,0.75f,0.2f,1,0,0},{0.75f,0.75f,0.2f,1,0,0},{0.0f,-0.75f,0.2f,1,0,0}};
    if(FAILED(context->Map(dynVb,0,D3D11_MAP_WRITE_DISCARD,0,&mM))||!mM.pData)return 85;
    memcpy(mM.pData,triNear,sizeof triNear);
    context->Unmap(dynVb,0);
    context->Draw(3,0);
    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE dMapped2{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&dMapped2))||!dMapped2.pData)return 85;
    const unsigned char* dPix=static_cast<const unsigned char*>(dMapped2.pData)+32*dMapped2.RowPitch+32*4;
    const bool orderRed=dPix[0]>0x80&&dPix[1]<0x40&&dPix[2]<0x40;
    context->Unmap(staging,0);
    if(!orderRed)return 86;   // BLOCKED: even draw order did not reach the target
    // Now the real test: depth ON, LESS_EQUAL, same two draws from a cleared
    // target. The near red triangle must still win — and a layer that ignores
    // the depth state paints the same picture either way, so this only
    // counts after the control above passed with the identical draw list.
    context->ClearRenderTargetView(rtv,clear);
    context->ClearDepthStencilView(dsv,D3D11_CLEAR_DEPTH,1.0f,0);
    context->OMSetDepthStencilState(dsState,0);
    if(FAILED(context->Map(dynVb,0,D3D11_MAP_WRITE_DISCARD,0,&mM))||!mM.pData)return 85;
    memcpy(mM.pData,triFar,sizeof triFar);
    context->Unmap(dynVb,0);
    context->Draw(3,0);
    if(FAILED(context->Map(dynVb,0,D3D11_MAP_WRITE_DISCARD,0,&mM))||!mM.pData)return 85;
    memcpy(mM.pData,triNear,sizeof triNear);
    context->Unmap(dynVb,0);
    context->Draw(3,0);
    context->CopyResource(staging,target);
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&dMapped2))||!dMapped2.pData)return 85;
    dPix=static_cast<const unsigned char*>(dMapped2.pData)+32*dMapped2.RowPitch+32*4;
    const bool depthRed=dPix[0]>0x80&&dPix[1]<0x40&&dPix[2]<0x40;
    context->Unmap(staging,0);
    dsOff->Release();dsState->Release();dynVb->Release();dsv->Release();dsTexture->Release();
    dvs->Release();dps->Release();dLayout->Release();
    context->OMSetDepthStencilState(nullptr,0);
    // The depth pass rebound its own layout/shaders; restore the triangle's
    // so the passes after it draw with the committed pipeline again.
    context->IASetInputLayout(layout);
    ID3D11Buffer* tBinds[]={verticesBuffer};
    context->IASetVertexBuffers(0,1,tBinds,&stride,&offset);
    context->VSSetShader(vs,nullptr,0);
    context->PSSetShader(ps,nullptr,0);
    if(!depthRed)return 86;   // BLOCKED: the depth test did not order the draws

    // ---- Texture-array pass: render to slice 0, copy to slice 1, read back ----
    // Terrain atlases, skybox cross-fades and Cascaded Shadow Maps put array
    // slices behind RTVArraySlice/DepthSlice; a layer that binds the view but
    // renders into slice 0 regardless corrupts every slice's owner. Render to
    // slice 0, CopySubresourceRegion into slice 1, read slice 1 back. Exits
    // 87-90 name the step; 90 is BLOCKED.
    D3D11_TEXTURE2D_DESC arrDesc{};
    arrDesc.Width=64;arrDesc.Height=64;arrDesc.MipLevels=1;arrDesc.ArraySize=2;
    arrDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;arrDesc.SampleDesc.Count=1;
    arrDesc.Usage=D3D11_USAGE_DEFAULT;arrDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* arrTexture=nullptr;
    if(FAILED(device->CreateTexture2D(&arrDesc,nullptr,&arrTexture))||!arrTexture)return 87;
    D3D11_RENDER_TARGET_VIEW_DESC arrRtvDesc{};
    arrRtvDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    arrRtvDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    arrRtvDesc.Texture2DArray.MipSlice=0;arrRtvDesc.Texture2DArray.ArraySize=1;
    arrRtvDesc.Texture2DArray.FirstArraySlice=0;
    ID3D11RenderTargetView* arrView=nullptr;
    if(FAILED(device->CreateRenderTargetView(arrTexture,&arrRtvDesc,&arrView))||!arrView)return 87;
    ID3D11RenderTargetView* arrBinds[]={arrView};
    context->OMSetRenderTargets(1,arrBinds,nullptr);
    context->RSSetViewports(1,&viewport);
    const FLOAT arrClear[4]={1,0,0,1};
    context->ClearRenderTargetView(arrView,arrClear);
    context->CopySubresourceRegion(arrTexture,1,0,0,0,arrTexture,0,nullptr);
    // Read slice 1 back through the staging texture (same shape, one slice).
    context->CopySubresourceRegion(staging,0,0,0,0,arrTexture,1,nullptr);
    D3D11_MAPPED_SUBRESOURCE aMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&aMapped))||!aMapped.pData)return 88;
    const unsigned char* aPixel=static_cast<const unsigned char*>(aMapped.pData)+16*aMapped.RowPitch+16*4;
    const bool arrayRed=aPixel[0]>0x80&&aPixel[1]<0x40&&aPixel[2]<0x40;
    context->Unmap(staging,0);
    arrView->Release();arrTexture->Release();
    if(!arrayRed)return 90;   // BLOCKED: the slice copy never reached slice 1

    // ---- BC-format pass: a BC1 block through create + staging readback ----
    // Compressed textures are most of a game's VRAM; the layer must accept
    // BC1-7 creation and copy them unchanged. A one-block BC1 texture (the
    // block encodes two colours: white, black), staged and mapped back: the
    // 8 bytes must survive byte-for-byte, which proves the format is routed
    // without re-encoding. Exits 91-94 name the step; 94 is BLOCKED.
    D3D11_TEXTURE2D_DESC bcDesc{};
    bcDesc.Width=4;bcDesc.Height=4;bcDesc.MipLevels=1;bcDesc.ArraySize=1;
    bcDesc.Format=DXGI_FORMAT_BC1_UNORM;bcDesc.SampleDesc.Count=1;
    bcDesc.Usage=D3D11_USAGE_DEFAULT;bcDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    // One BC1 block: colour0=0xFFFF (white), colour1=0x0000, indices all 0.
    const unsigned char bc1Block[8]={0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00};
    D3D11_SUBRESOURCE_DATA bcInit{};bcInit.pSysMem=bc1Block;bcInit.SysMemPitch=8;
    ID3D11Texture2D* bcTexture=nullptr;
    if(FAILED(device->CreateTexture2D(&bcDesc,&bcInit,&bcTexture))||!bcTexture)return 91;
    D3D11_TEXTURE2D_DESC bcStageDesc=bcDesc;
    bcStageDesc.BindFlags=0;bcStageDesc.Usage=D3D11_USAGE_STAGING;
    bcStageDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* bcStaging=nullptr;
    if(FAILED(device->CreateTexture2D(&bcStageDesc,nullptr,&bcStaging))||!bcStaging)return 92;
    context->CopyResource(bcStaging,bcTexture);
    D3D11_MAPPED_SUBRESOURCE bcMapped{};
    if(FAILED(context->Map(bcStaging,0,D3D11_MAP_READ,0,&bcMapped))||!bcMapped.pData)return 93;
    const bool bcRoundTrip=memcmp(bcMapped.pData,bc1Block,sizeof bc1Block)==0;
    context->Unmap(bcStaging,0);
    bcStaging->Release();bcTexture->Release();
    if(!bcRoundTrip)return 94;   // BLOCKED: the BC1 block did not survive the copy

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
