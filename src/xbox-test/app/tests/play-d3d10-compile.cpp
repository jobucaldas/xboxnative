// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that compiles a shader through Play's D3D10 shim
// the way a game's first shader build does. The statically imported d3d10.dll
// is Play's shim (served from the runtime directory); its D3D10CompileShader
// loads d3dcompiler_47.dll by name through Play's loader, which serves the
// probed Wine donor (donor_pe) — the same bridge launch.h wires for a game.
// The compiled shader must then run: the device draws with it and the pixels
// are read back.
//
// This fixture exists because the full d3d10-triangle fixture's pass 5
// (cubemap GenerateMips) reads black under Wine's d3d11 — a pre-existing
// environment limitation (the committed six-pass fixture returns 43 there
// too). The console runs d3d10-triangle with all seven passes; this fixture
// proves the compiler path off-device without the GenerateMips dependency.
//
// Exit codes: 42 = every step succeeded; 43 = drew but the pixels did not
// read back (BLOCKED, never a pass); 44/45/46/47 name the compile step.
#include <windows.h>
#include <d3d10.h>
#include <d3d10_1.h>
#include <dxgi.h>

extern "C" {
int d3d10compile_anchor;
void* d3d10compile_relocation = &d3d10compile_anchor;

static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d10-compile";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d10-compile",WS_OVERLAPPEDWINDOW,
        0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
}

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

static bool readPixel(ID3D10Device* device,ID3D10Texture2D* target,
                      ID3D10Texture2D* staging,unsigned x,unsigned y,
                      unsigned char out[4]) {
    device->CopyResource(staging,target);
    D3D10_MAPPED_TEXTURE2D mapped{};
    if(FAILED(staging->Map(0,D3D10_MAP_READ,0,&mapped))||!mapped.pData)return false;
    const unsigned char* pixel=static_cast<const unsigned char*>(mapped.pData)
        +y*mapped.RowPitch+x*4;
    out[0]=pixel[0];out[1]=pixel[1];out[2]=pixel[2];out[3]=pixel[3];
    staging->Unmap(0);
    return true;
}

DWORD d3d10compile_entry() {
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
    device->OMSetRenderTargets(1,&view,nullptr);

    // ---- the compile: D3D10CompileShader through the shim's by-name
    // d3dcompiler_47 load, which Play serves from the probed donor set ----
    static const char compileSource[]=
        "struct VsIn { float4 pos:POSITION; float4 col:COLOR; };\n"
        "struct VsOut { float4 pos:SV_Position; float4 col:COLOR; };\n"
        "VsOut main(VsIn i) { VsOut o; o.pos=i.pos; o.col=i.col; return o; }\n";
    ID3D10Blob* compiled=nullptr;ID3D10Blob* compileErrors=nullptr;
    HRESULT chr=D3D10CompileShader(compileSource,sizeof compileSource-1,nullptr,
        nullptr,nullptr,"main","vs_4_0",0,&compiled,&compileErrors);
    if(compileErrors)compileErrors->Release();
    if(FAILED(chr)||!compiled)return 44;
    // The blob must be a real SM4 container ('DXBC'), not an empty success.
    // The blob stays alive until the layout and vertex shader are built: the
    // device reads the bytecode during those creates, so an early Release is
    // a use-after-free (this fixture died exactly that way once).
    const unsigned char* bits=static_cast<const unsigned char*>(compiled->GetBufferPointer());
    bool dxbc=compiled->GetBufferSize()>4&&bits[0]=='D'&&bits[1]=='X'&&bits[2]=='B'&&bits[3]=='C';
    SIZE_T size=compiled->GetBufferSize();
    if(!dxbc){compiled->Release();return 45;}

    // The compiled shader must run: a triangle whose vertex colors make the
    // center red only if this shader (not a fallback) drew.
    struct V { float x,y;float r,g,b,a; };
    static const V vertices[]={
        {-0.75f, 0.75f,1,0,0,1},
        { 0.75f, 0.75f,1,0,0,1},
        { 0.00f,-0.75f,1,0,0,1}};
    D3D10_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D10_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,8,D3D10_INPUT_PER_VERTEX_DATA,0}};
    ID3D10InputLayout* layout=nullptr;
    if(FAILED(device->CreateInputLayout(elements,2,bits,size,&layout))||!layout)return 46;
    D3D10_BUFFER_DESC vb{};
    vb.ByteWidth=sizeof vertices;vb.Usage=D3D10_USAGE_IMMUTABLE;vb.BindFlags=D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA init{};init.pSysMem=vertices;
    ID3D10Buffer* vertexBuffer=nullptr;
    if(FAILED(device->CreateBuffer(&vb,&init,&vertexBuffer))||!vertexBuffer)return 47;
    ID3D10VertexShader* compiledVs=nullptr;
    if(FAILED(device->CreateVertexShader(bits,size,&compiledVs))||!compiledVs)return 48;
    compiled->Release();   // the layout and shader hold their own copies now

    // A pixel shader too, from the same donor compiler: without one the draw's
    // output is undefined (the first run of this fixture read back black for
    // exactly that reason). It forwards the interpolated vertex colour.
    static const char psSource[]=
        "struct PsIn { float4 pos:SV_Position; float4 col:COLOR; };\n"
        "float4 main(PsIn i):SV_Target { return i.col; }\n";
    ID3D10Blob* psCompiled=nullptr;ID3D10Blob* psErrors=nullptr;
    HRESULT pchr=D3D10CompileShader(psSource,sizeof psSource-1,nullptr,
        nullptr,nullptr,"main","ps_4_0",0,&psCompiled,&psErrors);
    if(psErrors)psErrors->Release();
    if(FAILED(pchr)||!psCompiled)return 51;
    const unsigned char* psBits=static_cast<const unsigned char*>(psCompiled->GetBufferPointer());
    ID3D10PixelShader* compiledPs=nullptr;
    HRESULT phr=device->CreatePixelShader(psBits,psCompiled->GetBufferSize(),&compiledPs);
    psCompiled->Release();
    if(FAILED(phr)||!compiledPs)return 52;

    device->RSSetViewports(1,&viewport);
    device->IASetInputLayout(layout);
    UINT stride=sizeof vertices[0],offset=0;
    device->IASetVertexBuffers(0,1,&vertexBuffer,&stride,&offset);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device->VSSetShader(compiledVs);
    device->PSSetShader(compiledPs);
    device->OMSetRenderTargets(1,&view,nullptr);

    const FLOAT clear[4]={0,0,0,1};
    device->ClearRenderTargetView(view,clear);
    device->Draw(3,0);
    unsigned char pixel[4];
    if(!readPixel(device,target,staging,32,32,pixel))return 49;
    if(!(pixel[0]>0x80&&pixel[1]<0x40&&pixel[2]<0x40))return 43;   // BLOCKED
    if(!readPixel(device,target,staging,4,4,pixel))return 50;
    if(!(pixel[0]<0x20&&pixel[1]<0x20&&pixel[2]<0x20))return 43;   // BLOCKED

    compiledVs->Release();compiledPs->Release();vertexBuffer->Release();layout->Release();
    view->Release();staging->Release();target->Release();
    device->Release();
    return 42;
}
}
