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
    unsigned int greenSmall[8*8];
    for(unsigned int& px:greenSmall)px=0xFF00D000u;
    // CreateTexture2D wants MipLevels*ArraySize initial-data entries: level 0
    // (16x16) and level 1 (8x8), both filled so nothing is read past the array.
    D3D11_SUBRESOURCE_DATA mipInit[2]={};
    mipInit[0].pSysMem=green;mipInit[0].SysMemPitch=16*4;
    mipInit[1].pSysMem=greenSmall;mipInit[1].SysMemPitch=8*4;
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
    const bool mipGreen=gPixel[1]>0x40;   // the filter may darken; green must survive
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
    backView->Release();backView=nullptr;
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
    // vertices in order renders scrambled geometry. Draw the triangle's three
    // vertices in rotated index order (1,2,0) — same triangle, so the pixel
    // test is identical, but only a real index fetch can produce it. Exits
    // 80-82 name the step; 82 is BLOCKED.
    const unsigned short indices[]={1,2,0};
    D3D11_BUFFER_DESC ibDesc{};
    ibDesc.ByteWidth=sizeof indices;ibDesc.Usage=D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibInit{};ibInit.pSysMem=indices;
    ID3D11Buffer* indexBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&ibDesc,&ibInit,&indexBuffer))||!indexBuffer)return 80;
    context->ClearRenderTargetView(rtv,clear);
    ID3D11Buffer* idxBinds[]={verticesBuffer};
    context->IASetVertexBuffers(0,1,idxBinds,&stride,&offset);
    context->IASetIndexBuffer(indexBuffer,DXGI_FORMAT_R16_UINT,0);
    context->OMSetRenderTargets(1,binds,nullptr);
    context->RSSetViewports(1,&viewport);
    context->DrawIndexed(3,0,0);
    context->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE xMapped{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&xMapped))||!xMapped.pData)return 81;
    const unsigned char* xPixel=static_cast<const unsigned char*>(xMapped.pData)+32*xMapped.RowPitch+32*4;
    const bool indexedRed=xPixel[0]>0x80&&xPixel[1]<0x40&&xPixel[2]<0x40;
    context->Unmap(staging,0);
    indexBuffer->Release();
    if(!indexedRed)return 82;   // BLOCKED: the indexed draw never reached the target

    // ---- Depth-stencil pass: DSV + depth test ----
    // Every 3D game writes depth; a layer that accepts the DSV but never
    // tests draws hidden surfaces over near ones (walls over characters). A
    // far red triangle drawn first, a near green triangle after, depth test
    // LESS_EQUAL: the centre pixel must stay green. Exits 83-86; 86 BLOCKED.
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
    context->OMSetDepthStencilState(dsState,0);
    context->ClearRenderTargetView(rtv,clear);
    context->ClearDepthStencilView(dsv,D3D11_CLEAR_DEPTH,1.0f,0);
    ID3D11RenderTargetView* dsBinds[]={rtv};
    context->OMSetRenderTargets(1,dsBinds,dsv);
    context->RSSetViewports(1,&viewport);
    // Far red triangle (z=0.8), then near green (z=0.2): the shared centre
    // pixel must end green. The VS passes z through, so the two draws use
    // different vertex data: one dynamic vertex buffer, rewritten per draw
    // (the Map WRITE_DISCARD a game's per-frame upload uses — itself a
    // covered feature).
    D3D11_BUFFER_DESC dvbDesc{};
    dvbDesc.ByteWidth=sizeof(float)*6*3;dvbDesc.Usage=D3D11_USAGE_DYNAMIC;
    dvbDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;dvbDesc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* dynVb=nullptr;
    if(FAILED(device->CreateBuffer(&dvbDesc,nullptr,&dynVb))||!dynVb)return 85;
    const float triRed[3][6]={
        {-0.75f,0.75f,0.8f,1,0,0},{0.75f,0.75f,0.8f,1,0,0},{0.0f,-0.75f,0.8f,1,0,0}};
    D3D11_MAPPED_SUBRESOURCE mM{};
    if(FAILED(context->Map(dynVb,0,D3D11_MAP_WRITE_DISCARD,0,&mM))||!mM.pData)return 85;
    memcpy(mM.pData,triRed,sizeof triRed);
    context->Unmap(dynVb,0);
    ID3D11Buffer* rBinds[]={dynVb};
    context->IASetVertexBuffers(0,1,rBinds,&stride,&offset);
    context->Draw(3,0);
    const float triGreen[3][6]={
        {-0.75f,0.75f,0.2f,0,1,0},{0.75f,0.75f,0.2f,0,1,0},{0.0f,-0.75f,0.2f,0,1,0}};
    if(FAILED(context->Map(dynVb,0,D3D11_MAP_WRITE_DISCARD,0,&mM))||!mM.pData)return 85;
    memcpy(mM.pData,triGreen,sizeof triGreen);
    context->Unmap(dynVb,0);
    context->Draw(3,0);
    context->OMSetRenderTargets(1,binds,nullptr);
    context->CopyResource(staging,target);
    D3D11_MAPPED_SUBRESOURCE dMapped2{};
    if(FAILED(context->Map(staging,0,D3D11_MAP_READ,0,&dMapped2))||!dMapped2.pData)return 85;
    const unsigned char* dPix=static_cast<const unsigned char*>(dMapped2.pData)+32*dMapped2.RowPitch+32*4;
    const bool depthGreen=dPix[1]>0x80&&dPix[0]<0x40&&dPix[2]<0x40;
    context->Unmap(staging,0);
    dsState->Release();dynVb->Release();dsv->Release();dsTexture->Release();
    context->OMSetDepthStencilState(nullptr,0);
    if(!depthGreen)return 86;   // BLOCKED: the depth test did not order the draws

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
