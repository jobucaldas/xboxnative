// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that draws with D3D9 the way a game with its own
// shaders does: a vertex buffer with a vertex declaration, a game-compiled
// vs_1_1 and ps_1_1, a texture, a sampler and a transform. Play must serve the
// D3D9 shim from the runtime directory, the shim must translate the bytecode to
// DXBC with the translator served beside it, and the draw must reach the
// console's D3D11 device.
//
// The shaders are hand-written shader model 1.1 tokens, so the fixture does not
// depend on a shader compiler being installed. The vertex shader transforms by
// a matrix the fixture sets; the pixel shader samples the texture and multiplies
// by the vertex colour. The result is read back and checked, so a draw that
// silently did nothing fails here rather than looking like a black screen.
//
// Exit code 42 = the textured, shaded triangle rendered where it should be;
// anything else names the step that failed. No console is contacted.
#include <windows.h>
#include <d3d9.h>

extern "C" {
int d3d9_textured_anchor;
void* d3d9_textured_relocation=&d3d9_textured_anchor;

static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d9-textured";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d9-textured",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

// D3D9 token builders. The instruction length in bits 24-27 is the operand
// count: without it the translator reads the next token as an operand.
static DWORD op(DWORD code,unsigned operands) { return code|(operands<<24); }
static DWORD dst(unsigned reg,DWORD type,DWORD mask=0xF) {
    return (reg&0x7FF)|((type&7)<<28)|((mask&0xF)<<16);
}
static DWORD src(unsigned reg,DWORD type,DWORD swizzle=0xE4) {
    return (reg&0x7FF)|((type&7)<<28)|((swizzle&0xFF)<<16);
}
enum { Temp=0,Input=1,Const=2,Texture=3,RastOut=4,AttrOut=5 };
// D3DSIO_* values from d3d9types.h: m4x4 is 20, not the 0x18 a reader might
// guess from the neighbouring matrix opcodes.
enum { Mov=1,Mul=5,M4x4=20,Dcl=0x1F,Tex=0x42,End=0xFFFF };

struct Vertex { float x,y,z; DWORD colour; float u,v; };

DWORD d3d9_textured_entry() {
    HWND window=guestWindow();
    if(!window)return 10;

    IDirect3D9Ex* d3d9=nullptr;
    if(FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3d9))||!d3d9)return 11;

    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth=640;present.BackBufferHeight=480;
    present.BackBufferFormat=D3DFMT_X8R8G8B8;present.BackBufferCount=1;
    present.MultiSampleType=D3DMULTISAMPLE_NONE;
    present.SwapEffect=D3DSWAPEFFECT_DISCARD;present.hDeviceWindow=window;present.Windowed=TRUE;
    present.EnableAutoDepthStencil=TRUE;present.AutoDepthStencilFormat=D3DFMT_D24S8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* device=nullptr;
    if(FAILED(d3d9->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &present,nullptr,&device))||!device)return 12;

    // vs_1_1: dcl_position v0; dcl_color v1; dcl_texcoord v2;
    //        m4x4 oPos, v0, c0; mov oD0, v1; mov oT0, v2
    const DWORD vs[]={
        0xFFFE0101,
        // The usage token is D3DDECLUSAGE in bits 0-4 and its index in bits
        // 16-19, so these are POSITION 0, COLOR 0 and TEXCOORD 0.
        op(Dcl,2),0x00000000,dst(0,Input),                    // dcl_position v0
        op(Dcl,2),0x0000000A,dst(1,Input),                    // dcl_color    v1
        op(Dcl,2),0x00000005,dst(2,Input),                    // dcl_texcoord v2
        op(M4x4,3),dst(0,RastOut),src(0,Input),src(0,Const),  // m4x4 oPos, v0, c0
        op(Mov,2),dst(0,AttrOut),src(1,Input),                // mov oD0, v1
        op(Mov,2),dst(0,Texture),src(2,Input),                // mov oT0, v2
        End,
    };
    IDirect3DVertexShader9* vertexShader=nullptr;
    if(FAILED(device->CreateVertexShader(vs,&vertexShader))||!vertexShader)return 13;

    // ps_1_1: tex t0; mul r0, t0, v0
    const DWORD ps[]={
        0xFFFF0101,
        op(Tex,1),dst(0,Texture),                             // tex t0
        op(Mul,3),dst(0,Temp),src(0,Texture),src(0,Input),    // mul r0, t0, v0
        End,
    };
    IDirect3DPixelShader9* pixelShader=nullptr;
    if(FAILED(device->CreatePixelShader(ps,&pixelShader))||!pixelShader)return 14;

    const D3DVERTEXELEMENT9 declaration[]={
        {0,0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,   0},
        {0,16,D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9* layout=nullptr;
    if(FAILED(device->CreateVertexDeclaration(declaration,&layout))||!layout)return 15;

    IDirect3DVertexBuffer9* buffer=nullptr;
    if(FAILED(device->CreateVertexBuffer(3*sizeof(Vertex),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
        &buffer,nullptr))||!buffer)return 16;
    Vertex* vertices=nullptr;
    if(FAILED(buffer->Lock(0,0,reinterpret_cast<void**>(&vertices),0))||!vertices)return 17;
    // A triangle in clip space covering the middle of the target, white so the
    // texture's own colour is what the readback sees.
    vertices[0]=Vertex{-0.75f,-0.75f,0.5f,0xffffffff,0.0f,0.0f};
    vertices[1]=Vertex{ 0.75f,-0.75f,0.5f,0xffffffff,1.0f,0.0f};
    vertices[2]=Vertex{ 0.00f, 0.75f,0.5f,0xffffffff,0.5f,1.0f};
    if(FAILED(buffer->Unlock()))return 18;

    // A 2x2 texture, all green: the pixel shader multiplies it by the white
    // vertex colour, so the triangle must come out green.
    IDirect3DTexture9* texture=nullptr;
    if(FAILED(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr))
        ||!texture)return 19;
    D3DLOCKED_RECT locked{};
    if(FAILED(texture->LockRect(0,&locked,nullptr,0))||!locked.pBits)return 20;
    for(unsigned y=0;y<2;++y) {
        DWORD* row=reinterpret_cast<DWORD*>(static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch);
        row[0]=0xff00ff00u;row[1]=0xff00ff00u;
    }
    texture->UnlockRect(0);

    const UINT size=64;
    IDirect3DSurface9* target=nullptr;
    if(FAILED(device->CreateRenderTarget(size,size,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
        &target,nullptr))||!target)return 21;
    IDirect3DSurface9* staging=nullptr;
    if(FAILED(device->CreateOffscreenPlainSurface(size,size,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
        &staging,nullptr))||!staging)return 22;

    // The transform the vertex shader reads from c0..c3: identity, so the clip
    // space vertices above are what the rasteriser sees.
    const float transform[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    if(FAILED(device->SetVertexShaderConstantF(0,&transform[0][0],4)))return 23;
    if(FAILED(device->SetVertexDeclaration(layout)))return 24;
    if(FAILED(device->SetStreamSource(0,buffer,0,sizeof(Vertex))))return 25;
    if(FAILED(device->SetVertexShader(vertexShader)))return 26;
    if(FAILED(device->SetPixelShader(pixelShader)))return 27;
    if(FAILED(device->SetTexture(0,texture)))return 28;
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);

    if(FAILED(device->SetRenderTarget(0,target)))return 29;
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 30;
    D3DVIEWPORT9 viewport{0,0,size,size,0.0f,1.0f};
    if(FAILED(device->SetViewport(&viewport)))return 31;

    if(FAILED(device->BeginScene()))return 32;
    if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 33;
    if(FAILED(device->EndScene()))return 34;
    if(FAILED(device->GetRenderTargetData(target,staging)))return 35;

    if(FAILED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))||!locked.pBits)return 36;
    const unsigned char* inside=static_cast<const unsigned char*>(locked.pBits)
        +32*locked.Pitch+32*4;
    const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
        +4*locked.Pitch+4*4;
    const bool insideGreen=inside[1]>0x80&&inside[0]<0x40&&inside[2]<0x40;
    const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
    staging->UnlockRect();
    if(!insideGreen||!outsideBlack)return 37;

    // And the frame reaches the swap chain, as a game's does.
    IDirect3DSurface9* backBuffer=nullptr;
    if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer))||!backBuffer)return 38;
    if(FAILED(device->SetRenderTarget(0,backBuffer)))return 39;
    D3DVIEWPORT9 full{0,0,640,480,0.0f,1.0f};
    device->SetViewport(&full);
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 40;
    if(FAILED(device->BeginScene()))return 41;
    if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 52;
    if(FAILED(device->EndScene()))return 53;
    device->Present(nullptr,nullptr,nullptr,nullptr);

    backBuffer->Release();
    staging->Release();target->Release();
    texture->Release();layout->Release();buffer->Release();
    pixelShader->Release();vertexShader->Release();
    device->Release();d3d9->Release();
    DestroyWindow(window);
    return 42;
}
}
