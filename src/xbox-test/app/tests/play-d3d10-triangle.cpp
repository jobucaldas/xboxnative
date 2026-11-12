// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that draws with D3D10 the way a game's first
// frames do, through Play's D3D10 shim onto the console's D3D11. It statically
// imports d3d10.dll (served from the launcher's runtime directory), creates a
// device, and then exercises the draw surface a D3D10 game uses, each pass
// checked by reading pixels (or vertices) back:
//   1. input layout + vs_4_0/ps_4_0 + Draw, red triangle readback
//   2. DrawIndexed over an index buffer, green triangle readback
//   3. geometry shader: one point expanded to a green triangle (the shape
//      Halo Wars' terrain/minimap layer uses)
//   4. stream-out: a GS with a SO declaration writes vertices a Map reads back
//   5. cubemap texture + GenerateMips, mip readback
//   6. ResolveSubresource from a 4x MSAA target, readback
// Exit code 42 = every readback matched; 43 = the pipeline ran but the pixels
// did not read back as drawn (BLOCKED, never a pass); anything else names the
// step that failed. No console is contacted and nothing here is a game.
#include <windows.h>
#include <d3d10.h>
#include <d3d10_1.h>
#include <dxgi.h>
#include "d3d10_bytecode.h"

extern "C" {
int d3d10tri_anchor;
void* d3d10tri_relocation=&d3d10tri_anchor;

static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d10-tri";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d10-tri",WS_OVERLAPPEDWINDOW,
        0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
}

// A 64x64 R8G8B8A8 target plus its staging copy: the pair every pass reads
// through. Created once, cleared between passes.
static bool makeTarget(ID3D10Device* device,ID3D10Texture2D** target,
                       ID3D10Texture2D** staging,ID3D10RenderTargetView** view) {
    D3D10_TEXTURE2D_DESC desc{};
    desc.Width=64;desc.Height=64;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D10_USAGE_DEFAULT;desc.BindFlags=D3D10_BIND_RENDER_TARGET;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,target))||!*target)return false;
    desc.BindFlags=0;desc.Usage=D3D10_USAGE_STAGING;desc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,staging))||!*staging)return false;
    if(FAILED(device->CreateRenderTargetView(*target,nullptr,view))||!*view)return false;
    return true;
}
// Map a staging texture that already holds the data and answer the pixel at
// (x,y). The caller does the copy, so any staging texture works (the render
// target's 64x64 one, a cubemap mip's 8x8 one, a resolve's copy).
static bool readStaged(ID3D10Texture2D* staging,unsigned x,unsigned y,unsigned char out[4]) {
    D3D10_MAPPED_TEXTURE2D mapped{};
    if(FAILED(staging->Map(0,D3D10_MAP_READ,0,&mapped))||!mapped.pData)return false;
    const unsigned char* pixel=static_cast<const unsigned char*>(mapped.pData)
        +y*mapped.RowPitch+x*4;
    out[0]=pixel[0];out[1]=pixel[1];out[2]=pixel[2];out[3]=pixel[3];
    staging->Unmap(0);
    return true;
}
// Copy the target into the staging copy and answer the pixel at (x,y).
static bool readPixel(ID3D10Device* device,ID3D10Texture2D* target,
                      ID3D10Texture2D* staging,unsigned x,unsigned y,
                      unsigned char out[4]) {
    device->CopyResource(staging,target);
    return readStaged(staging,x,y,out);
}
// The triangle pass's shared state: pipeline, vertex buffer, viewport.
static const float vertices[][6]={
    {-0.75f, 0.75f,1,0,0,1},
    { 0.75f, 0.75f,1,0,0,1},
    { 0.0f,-0.75f,1,0,0,1}};

DWORD d3d10tri_entry() {
    HWND window=guestWindow();
    if(!window)return 10;

    ID3D10Device* device=nullptr;
    if(FAILED(D3D10CreateDevice(nullptr,D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,
        D3D10_SDK_VERSION,&device))||!device)return 11;

    ID3D10Texture2D* target=nullptr;ID3D10Texture2D* staging=nullptr;
    ID3D10RenderTargetView* view=nullptr;
    if(!makeTarget(device,&target,&staging,&view))return 12;
    D3D10_VIEWPORT viewport{0,0,64,64,0.0f,1.0f};
    device->RSSetViewports(1,&viewport);

    // Pipeline from the committed SM4 bytecode: the shaders a D3D10 device
    // runs. Nothing is compiled at run time.
    ID3D10VertexShader* vs=nullptr;ID3D10PixelShader* ps=nullptr;
    if(FAILED(device->CreateVertexShader(d3d10_vs,sizeof d3d10_vs,&vs))||!vs)return 13;
    if(FAILED(device->CreatePixelShader(d3d10_ps,sizeof d3d10_ps,&ps))||!ps)return 14;
    const D3D10_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D10_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D10_INPUT_PER_VERTEX_DATA,0}};
    ID3D10InputLayout* layout=nullptr;
    if(FAILED(device->CreateInputLayout(elements,2,d3d10_vs,sizeof d3d10_vs,&layout))||!layout)return 15;
    D3D10_BUFFER_DESC vb{};
    vb.ByteWidth=sizeof vertices;vb.Usage=D3D10_USAGE_IMMUTABLE;vb.BindFlags=D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA init{};init.pSysMem=vertices;
    ID3D10Buffer* vertexBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&vb,&init,&vertexBuffer))||!vertexBuffer)return 16;

    device->IASetInputLayout(layout);
    UINT stride=sizeof vertices[0],offset=0;
    device->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device->VSSetShader(vs);
    device->PSSetShader(ps);
    device->OMSetRenderTargets(1,&view,nullptr);

    // ---- pass 1: Draw, red triangle at the left, clear colour elsewhere ----
    const FLOAT clear[4]={0,0,0,1};
    device->ClearRenderTargetView(view,clear);
    device->Draw(3,0);
    unsigned char pixel[4];
    if(!readPixel(device,target,staging,32,32,pixel))return 17;
    if(!(pixel[0]>0x80&&pixel[1]<0x40&&pixel[2]<0x40))return 43;   // BLOCKED
    if(!readPixel(device,target,staging,4,4,pixel))return 18;
    if(!(pixel[0]<0x20&&pixel[1]<0x20&&pixel[2]<0x20))return 43;   // BLOCKED

    // ---- pass 2: DrawIndexed over an index buffer, green triangle ----
    const UINT indices[]={0,1,2};
    D3D10_BUFFER_DESC ib{};
    ib.ByteWidth=sizeof indices;ib.Usage=D3D10_USAGE_IMMUTABLE;ib.BindFlags=D3D10_BIND_INDEX_BUFFER;
    D3D10_SUBRESOURCE_DATA ibInit{};ibInit.pSysMem=indices;
    ID3D10Buffer* indexBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&ib,&ibInit,&indexBuffer))||!indexBuffer)return 19;
    const float green[][6]={
        {-0.75f, 0.75f,0,1,0,1},
        { 0.75f, 0.75f,0,1,0,1},
        { 0.0f,-0.75f,0,1,0,1}};
    D3D10_BUFFER_DESC gb{};
    gb.ByteWidth=sizeof green;gb.Usage=D3D10_USAGE_IMMUTABLE;gb.BindFlags=D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA gbInit{};gbInit.pSysMem=green;
    ID3D10Buffer* greenBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&gb,&gbInit,&greenBuffer))||!greenBuffer)return 20;
    device->ClearRenderTargetView(view,clear);
    device->IASetIndexBuffer(indexBuffer,DXGI_FORMAT_R32_UINT,0);
    device->IASetVertexBuffers(0,1,&greenBuffer,&stride,&offset);
    device->DrawIndexed(3,0,0);
    if(!readPixel(device,target,staging,32,32,pixel))return 21;
    if(!(pixel[1]>0x80&&pixel[0]<0x40&&pixel[2]<0x40))return 43;   // BLOCKED
    device->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
    device->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);

    // ---- pass 3: geometry shader expands one point to a green triangle ----
    ID3D10GeometryShader* gs=nullptr;
    if(FAILED(device->CreateGeometryShader(d3d10_gs,sizeof d3d10_gs,&gs))||!gs)return 22;
    const float pointV[6]={0.0f,0.0f,0,1,0,1};   // POSITION + COLOR, green
    D3D10_BUFFER_DESC pvb{};
    pvb.ByteWidth=sizeof pointV;pvb.Usage=D3D10_USAGE_IMMUTABLE;pvb.BindFlags=D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA pvInit{};pvInit.pSysMem=pointV;
    ID3D10Buffer* pointBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&pvb,&pvInit,&pointBuffer))||!pointBuffer)return 23;
    device->ClearRenderTargetView(view,clear);
    device->IASetVertexBuffers(0,1,&pointBuffer,&stride,&offset);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
    device->GSSetShader(gs);
    device->Draw(1,0);
    device->GSSetShader(nullptr);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
    if(!readPixel(device,target,staging,32,32,pixel))return 24;
    if(!(pixel[1]>0x80&&pixel[0]<0x40&&pixel[2]<0x40))return 43;   // BLOCKED

    // ---- pass 4: stream-out writes the GS's vertices a Map reads back ----
    // The GS's own output declaration, as CreateGeometryShaderWithStreamOutput
    // takes it: the two attributes the geometry shader appends per vertex.
    // D3D10's entry has no stream field (one output stream) and its field order
    // is SemanticName, SemanticIndex, StartComponent, ComponentCount, OutputSlot.
    D3D10_SO_DECLARATION_ENTRY soDecl[]={
        {"SV_POSITION",0,0,4,0},
        {"COLOR",0,0,4,0}};
    // With a declaration, the bytecode must carry the geometry shader's output
    // signature: the same DXBC the pass above ran.
    ID3D10GeometryShader* soShader=nullptr;
    if(FAILED(device->CreateGeometryShaderWithStreamOutput(d3d10_gs,sizeof d3d10_gs,
        soDecl,2,32,&soShader))||!soShader)return 25;
    D3D10_BUFFER_DESC soBuffer{};
    soBuffer.ByteWidth=3*32;soBuffer.Usage=D3D10_USAGE_DEFAULT;
    soBuffer.BindFlags=D3D10_BIND_STREAM_OUTPUT;
    ID3D10Buffer* soTarget=nullptr;
    if(FAILED(device->CreateBuffer(&soBuffer,nullptr,&soTarget))||!soTarget)return 26;
    // A STREAM_OUTPUT buffer cannot be mapped: the readback goes through a
    // staging copy, as a game's GPU-vertex capture does.
    D3D10_BUFFER_DESC soStageDesc{};
    soStageDesc.ByteWidth=3*32;soStageDesc.Usage=D3D10_USAGE_STAGING;
    soStageDesc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
    ID3D10Buffer* soStaging=nullptr;
    if(FAILED(device->CreateBuffer(&soStageDesc,nullptr,&soStaging))||!soStaging)return 35;
    const UINT soOffset=0;
    // The primitive count comes from an SO_STATISTICS query issued around the
    // draw, as a game's vertex-capture path reads it.
    D3D10_QUERY_DESC soStatsDesc{D3D10_QUERY_SO_STATISTICS,D3D10_QUERY_MISC_FLAG(0)};
    ID3D10Query* soStats=nullptr;
    if(FAILED(device->CreateQuery(&soStatsDesc,&soStats))||!soStats)return 36;
    device->SOSetTargets(1,&soTarget,&soOffset);
    device->IASetVertexBuffers(0,1,&pointBuffer,&stride,&offset);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
    device->GSSetShader(soShader);
    soStats->Begin();
    device->Draw(1,0);
    soStats->End();
    device->GSSetShader(nullptr);
    device->SOSetTargets(0,nullptr,&soOffset);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D10_QUERY_DATA_SO_STATISTICS stats{};
    // Not a FLAG_NONE query: a blocking read is legal and must answer. With
    // flags 0 GetData returns S_FALSE (neither FAILED nor S_OK) until the GPU
    // catches up, so poll until it answers or the budget runs out; giving up
    // is a failure (37), never a pass.
    bool soAnswered=false;
    for(unsigned poll=0;poll<1000;++poll) {
        HRESULT qr=soStats->GetData(&stats,sizeof stats,0);
        if(qr==S_OK) {soAnswered=true;break;}
        if(FAILED(qr))return 37;
        Sleep(1);
    }
    if(!soAnswered)return 37;
    soStats->Release();
    // The GS expands one point to one triangle: one primitive written, one
    // needed. Zero written means the SO path silently dropped the draw.
    if(stats.NumPrimitivesWritten!=1||stats.PrimitivesStorageNeeded!=1)return 43;   // BLOCKED
    device->CopyResource(soStaging,soTarget);
    void* mappedSo=nullptr;
    if(FAILED(soStaging->Map(D3D10_MAP_READ,0,&mappedSo))||!mappedSo)return 27;
    // The GS wrote the three expanded vertices, each POSITION then COLOR: top
    // (0,+0.4), bottom-right (+0.4,-0.4), bottom-left (-0.4,-0.4), all green.
    static const float expected[3][8]={
        {0.0f, 0.4f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f},
        {0.4f,-0.4f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f},
        {-0.4f,-0.4f,0.0f,1.0f, 0.0f,1.0f,0.0f,1.0f}};
    const float* written=static_cast<const float*>(mappedSo);
    bool soVertices=true;
    for(unsigned v=0;v<3;++v)
        for(unsigned c=0;c<8;++c)
            if(written[v*8+c]!=expected[v][c])soVertices=false;
    soStaging->Unmap();
    if(!soVertices)return 43;   // BLOCKED: the stream-out wrote the wrong vertices

    // ---- pass 5: a cubemap, GenerateMips, and the generated mip read back ----
    D3D10_TEXTURE2D_DESC cubeDesc{};
    cubeDesc.Width=16;cubeDesc.Height=16;cubeDesc.MipLevels=2;cubeDesc.ArraySize=6;
    cubeDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;cubeDesc.SampleDesc.Count=1;
    cubeDesc.Usage=D3D10_USAGE_DEFAULT;
    cubeDesc.BindFlags=D3D10_BIND_SHADER_RESOURCE|D3D10_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags=D3D10_RESOURCE_MISC_TEXTURECUBE;
    ID3D10Texture2D* cube=nullptr;
    if(FAILED(device->CreateTexture2D(&cubeDesc,nullptr,&cube))||!cube)return 28;
    // Every face's mip 0 is opaque blue, so mip 1 must average to it. One
    // UpdateSubresource per face: mip 0 is the source, mip 1 gets the same
    // pixels so a failed GenerateMips cannot be hidden by pre-filled data.
    static unsigned char faceData[6][16*16*4];
    for(unsigned f=0;f<6;++f) {
        for(unsigned i=0;i<16*16;++i) {
            faceData[f][i*4+0]=0;faceData[f][i*4+1]=0;faceData[f][i*4+2]=0xff;faceData[f][i*4+3]=0xff;
        }
        device->UpdateSubresource(cube,D3D10CalcSubresource(0,f,2),nullptr,faceData[f],16*4,0);
        // Mip 1 is deliberately NOT filled: only GenerateMips can put blue
        // there, so the readback below proves the generation ran.
    }
    D3D10_SHADER_RESOURCE_VIEW_DESC1 srv{};
    srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension=D3D10_SRV_DIMENSION_TEXTURECUBE;
    srv.TextureCube.MostDetailedMip=0;srv.TextureCube.MipLevels=2;
    ID3D10ShaderResourceView1* cubeView=nullptr;
    // CreateShaderResourceView1 is the ID3D10Device1 method; the cube descriptor
    // is a D3D10.1 feature, so the call goes through the device1 interface.
    ID3D10Device1* device1=nullptr;
    if(FAILED(device->QueryInterface(__uuidof(ID3D10Device1),(void**)&device1))||!device1)return 34;
    if(FAILED(device1->CreateShaderResourceView1(cube,&srv,&cubeView))||!cubeView){device1->Release();return 29;}
    device1->Release();
    device->GenerateMips(cubeView);
    // The generated mip is read through its own staging texture, sized to the
    // 8x8 mip and mapped before anything else touches the shared staging copy.
    // The subresource index is mip + face*mipLevels (D3D10CalcSubresource).
    D3D10_TEXTURE2D_DESC mipDesc{};
    mipDesc.Width=8;mipDesc.Height=8;mipDesc.MipLevels=1;mipDesc.ArraySize=1;
    mipDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;mipDesc.SampleDesc.Count=1;
    mipDesc.Usage=D3D10_USAGE_STAGING;mipDesc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
    ID3D10Texture2D* mipStaging=nullptr;
    if(FAILED(device->CreateTexture2D(&mipDesc,nullptr,&mipStaging))||!mipStaging)return 38;
    device->CopySubresourceRegion(mipStaging,0,0,0,0,cube,D3D10CalcSubresource(1,0,2),nullptr);
    if(!readStaged(mipStaging,0,0,pixel))return 30;
    if(!(pixel[2]>0xc0&&pixel[0]<0x40))return 43;   // BLOCKED
    mipStaging->Release();
    cubeView->Release();cube->Release();

    // ---- pass 6: ResolveSubresource from a 4x MSAA target ----
    D3D10_TEXTURE2D_DESC msDesc{};
    msDesc.Width=64;msDesc.Height=64;msDesc.MipLevels=1;msDesc.ArraySize=1;
    msDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;msDesc.SampleDesc.Count=4;
    msDesc.Usage=D3D10_USAGE_DEFAULT;msDesc.BindFlags=D3D10_BIND_RENDER_TARGET;
    ID3D10Texture2D* msTarget=nullptr;
    if(FAILED(device->CreateTexture2D(&msDesc,nullptr,&msTarget))||!msTarget)return 31;
    ID3D10RenderTargetView* msView=nullptr;
    if(FAILED(device->CreateRenderTargetView(msTarget,nullptr,&msView))||!msView)return 39;
    // A known, distinct colour goes into the MSAA target first, so the resolve
    // has something real to move (an undrawn target proves nothing).
    const FLOAT msColour[4]={0.75f,0.25f,0.5f,1.0f};
    device->ClearRenderTargetView(msView,msColour);
    msView->Release();
    // The resolve lands in its own non-MSAA texture, never in a target another
    // pass clears afterwards, and its staging copy is read before anything
    // else touches it.
    D3D10_TEXTURE2D_DESC resolvedDesc=msDesc;
    resolvedDesc.SampleDesc.Count=1;resolvedDesc.BindFlags=0;
    resolvedDesc.Usage=D3D10_USAGE_STAGING;resolvedDesc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
    ID3D10Texture2D* resolved=nullptr;
    if(FAILED(device->CreateTexture2D(&resolvedDesc,nullptr,&resolved))||!resolved)return 40;
    // The console's CoreWindow adapter has no resolve layer, but an offscreen
    // resolve through the device is the path a game's post-process takes.
    D3D10_TEXTURE2D_DESC resolveDefault=msDesc;
    resolveDefault.SampleDesc.Count=1;resolveDefault.BindFlags=0;
    ID3D10Texture2D* resolveTarget=nullptr;
    if(FAILED(device->CreateTexture2D(&resolveDefault,nullptr,&resolveTarget))||!resolveTarget)return 41;
    device->ResolveSubresource(resolveTarget,0,msTarget,0,DXGI_FORMAT_R8G8B8A8_UNORM);
    device->CopyResource(resolved,resolveTarget);
    if(!readStaged(resolved,32,32,pixel))return 32;
    // The MSAA target's clear colour must have crossed the resolve untouched.
    const bool resolvedColour=pixel[0]>0xb0&&pixel[0]<=0xc0&&pixel[1]>0x30&&pixel[1]<0x50
        &&pixel[2]>0x70&&pixel[2]<0x90&&pixel[3]==0xff;
    if(!resolvedColour)return 43;   // BLOCKED
    resolveTarget->Release();
    resolved->Release();
    msTarget->Release();

    // The present path still works after the draw passes, as a game's frame does.
    device->ClearRenderTargetView(view,clear);
    device->OMSetRenderTargets(1,&view,nullptr);
    device->Draw(3,0);

    soTarget->Release();soStaging->Release();
    pointBuffer->Release();gs->Release();
    greenBuffer->Release();indexBuffer->Release();
    vertexBuffer->Release();layout->Release();
    ps->Release();vs->Release();
    view->Release();staging->Release();target->Release();
    device->Release();
    DestroyWindow(window);
    return 42;
}
}
