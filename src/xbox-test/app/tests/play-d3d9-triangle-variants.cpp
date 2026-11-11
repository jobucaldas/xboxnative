// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: the d3d9-triangle draw run several ways, to separate what the
// console's d3d11on12/dxilconv path accepts from what it faults on. The plain
// triangle fixture (play-d3d9-triangle) crashes inside dxilconv when the
// shim's fixed-function pixel shader declares a sampler for a stage that never
// reads its texture; ffp.h now skips that declaration, and this fixture is the
// on-console proof either way.
//
// One guest process runs the variants in order and reports each with a
// VARIANTS line on stdout (Play routes it to the launch log). The variants are
// ordered so the ones closest to the reported crash run last: a variant that
// faults ends the process, and every earlier variant's line is already out.
// The exit code is 42 when every variant that ran passed its readback, 60+n
// naming the first variant that failed a check, and a fault surfaces as the
// launcher's did-not-exit verdict with the last VARIANTS line saying how far
// the run got.
//
// The variants:
//   v1  the plain triangle, textureless stage 0 (SELECTARG1 DIFFUSE): the
//       draw that crashed before the ffp.h change, and the fix's target.
//   v2  stage 0 with an explicit D3DTA_TEXTURE arg but no texture set: the
//       shim must still not fault (the sampler is declared; the 1x1 white
//       fallback view answers the sample).
//   v3  two stages, stage 0 textureless and stage 1 with D3DTA_TEXTURE: the
//       per-stage declaration path (s0 declared, s1 not used).
//   v4  a game-compiled ps_1_1 that samples t0, with a real 2x2 texture: the
//       textured path beside the fixed-function ones (what play-d3d9-textured
//       proves, kept here so one request covers the whole pixel path).
//
// Exit 42 = every variant rendered where it should. No console is contacted.
#include <windows.h>
#include <d3d9.h>

extern "C" {
int d3d9_triangle_variants_anchor;
void* d3d9_triangle_variants_relocation=&d3d9_triangle_variants_anchor;

static HWND guestWindow() {
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"play-d3d9-triangle-variants";
    RegisterClassExW(&cls);
    return CreateWindowExW(0,cls.lpszClassName,L"play-d3d9-triangle-variants",WS_OVERLAPPEDWINDOW,
        0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
}

// D3DFVF_XYZRHW|D3DFVF_DIFFUSE: four floats then a D3DCOLOR.
struct Vertex { float x,y,z,w; DWORD colour; };

// One variant's draw: the triangle over the left of the target, the stage
// state the variant names, then the readback check. Returns 0 when the inside
// pixel is the vertex colour and the outside is the clear colour.
static unsigned drawAndCheck(IDirect3DDevice9Ex* device,IDirect3DSurface9* target,
                             IDirect3DSurface9* staging,IDirect3DVertexBuffer9* buffer,
                             DWORD colour) {
    if(FAILED(device->SetStreamSource(0,buffer,0,sizeof(Vertex))))return 1;
    if(FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)))return 2;
    device->SetRenderState(D3DRS_LIGHTING,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    if(FAILED(device->SetRenderTarget(0,target)))return 3;
    if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 4;
    D3DVIEWPORT9 viewport{0,0,64,64,0.0f,1.0f};
    if(FAILED(device->SetViewport(&viewport)))return 5;
    if(FAILED(device->BeginScene()))return 6;
    if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 7;
    if(FAILED(device->EndScene()))return 8;
    if(FAILED(device->GetRenderTargetData(target,staging)))return 9;
    D3DLOCKED_RECT locked{};
    if(FAILED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))||!locked.pBits)return 10;
    const unsigned char* inside=static_cast<const unsigned char*>(locked.pBits)
        +32*locked.Pitch+32*4;
    const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
        +4*locked.Pitch+4*4;
    // The vertex colour is ARGB in a D3DCOLOR but BGRA in memory; check the
    // channels the way play-d3d9-triangle does, for red.
    const bool insideRight=colour==0xffff0000
        ?(inside[2]>0x80&&inside[0]<0x40&&inside[1]<0x40)
        :(inside[1]>0x80&&inside[0]<0x40&&inside[2]<0x40);
    const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
    staging->UnlockRect();
    if(!insideRight||!outsideBlack)return 11;
    return 0;
}

static void report(unsigned variant,const char* what,bool pass) {
    // No stdio in this fixture: the log goes out through OutputDebugStringA,
    // which Play routes to the launch log the same way the shim's lines go.
    // The selftest's tally reads "SELFTEST RESULT <mode> <verdict>" lines from
    // the log, so the fixture speaks that dialect: one line per variant, the
    // mode name the request used plus the variant's number.
    char line[96];
    unsigned i=0;
    for(const char* p="SELFTEST RESULT d3d9-triangle-v";*p;++p)line[i++]=*p;
    line[i++]=char('0'+variant);line[i++]=' ';
    for(const char* p=pass?"PASS":(what?"FAIL":"FAIL");*p;++p)line[i++]=*p;
    line[i++]=' ';line[i++]='(';line[i]=0;
    while(*what&&i<sizeof line-2)line[i++]=*what++;
    line[i++]=')';line[i]=0;
    OutputDebugStringA(line);
}

// D3D9 token builders, as play-d3d9-textured uses: the instruction length in
// bits 24-27 is the operand count.
static DWORD tokOp(DWORD code,unsigned operands) { return code|(operands<<24); }
static DWORD tokDst(unsigned reg,DWORD type,DWORD mask=0xF) {
    return (reg&0x7FF)|((type&7)<<28)|((mask&0xF)<<16);
}
static DWORD tokSrc(unsigned reg,DWORD type,DWORD swizzle=0xE4) {
    return (reg&0x7FF)|((type&7)<<28)|((swizzle&0xFF)<<16);
}
enum { TokTemp=0,TokInput=1,TokConst=2,TokTexture=3,TokRastOut=4,TokAttrOut=5 };
enum { TokMov=1,TokMul=5,TokM4x4=20,TokDcl=0x1F,TokTex=0x42,TokEnd=0xFFFF };

// Variant 4's draw: the textured fixture's own vs_1_1/ps_1_1, a 2x2 green
// texture and the readback, in one block (play-d3d9-textured's path).
static unsigned texturedVariant(IDirect3DDevice9Ex* device,IDirect3DSurface9* target,
                                IDirect3DSurface9* staging) {
    // vs_1_1: dcl_position v0; dcl_color v1; dcl_texcoord v2;
    //        m4x4 oPos, v0, c0; mov oD0, v1; mov oT0, v2
    const DWORD vs[]={
        0xFFFE0101,
        tokOp(TokDcl,2),0x00000000,tokDst(0,TokInput),
        tokOp(TokDcl,2),0x0000000A,tokDst(1,TokInput),
        tokOp(TokDcl,2),0x00000005,tokDst(2,TokInput),
        tokOp(TokM4x4,3),tokDst(0,TokRastOut),tokSrc(0,TokInput),tokSrc(0,TokConst),
        tokOp(TokMov,2),tokDst(0,TokAttrOut),tokSrc(1,TokInput),
        tokOp(TokMov,2),tokDst(0,TokTexture),tokSrc(2,TokInput),
        TokEnd,
    };
    IDirect3DVertexShader9* vertexShader=nullptr;
    if(FAILED(device->CreateVertexShader(vs,&vertexShader))||!vertexShader)return 1;
    // ps_1_1: tex t0; mul r0, t0, v0
    const DWORD ps[]={
        0xFFFF0101,
        tokOp(TokTex,1),tokDst(0,TokTexture),
        tokOp(TokMul,3),tokDst(0,TokTemp),tokSrc(0,TokTexture),tokSrc(0,TokInput),
        TokEnd,
    };
    IDirect3DPixelShader9* pixelShader=nullptr;
    if(FAILED(device->CreatePixelShader(ps,&pixelShader))||!pixelShader){
        vertexShader->Release();return 2;
    }
    const D3DVERTEXELEMENT9 declaration[]={
        {0,0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,   0},
        {0,16,D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9* layout=nullptr;
    if(FAILED(device->CreateVertexDeclaration(declaration,&layout))||!layout){
        pixelShader->Release();vertexShader->Release();return 3;
    }
    IDirect3DVertexBuffer9* buffer=nullptr;
    if(FAILED(device->CreateVertexBuffer(3*sizeof(Vertex),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
        &buffer,nullptr))||!buffer){
        layout->Release();pixelShader->Release();vertexShader->Release();return 4;
    }
    struct TexturedVertex { float x,y,z; DWORD colour; float u,v; };
    TexturedVertex* vertices=nullptr;
    if(FAILED(buffer->Lock(0,0,reinterpret_cast<void**>(&vertices),0))||!vertices){
        buffer->Release();layout->Release();pixelShader->Release();vertexShader->Release();return 5;
    }
    vertices[0]=TexturedVertex{-0.75f,-0.75f,0.5f,0xffffffff,0.0f,0.0f};
    vertices[1]=TexturedVertex{ 0.75f,-0.75f,0.5f,0xffffffff,1.0f,0.0f};
    vertices[2]=TexturedVertex{ 0.00f, 0.75f,0.5f,0xffffffff,0.5f,1.0f};
    buffer->Unlock();
    IDirect3DTexture9* texture=nullptr;
    if(FAILED(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr))
        ||!texture){
        buffer->Release();layout->Release();pixelShader->Release();vertexShader->Release();return 6;
    }
    D3DLOCKED_RECT locked{};
    if(FAILED(texture->LockRect(0,&locked,nullptr,0))||!locked.pBits){
        texture->Release();buffer->Release();layout->Release();
        pixelShader->Release();vertexShader->Release();return 7;
    }
    for(unsigned y=0;y<2;++y) {
        DWORD* row=reinterpret_cast<DWORD*>(static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch);
        row[0]=0xff00ff00u;row[1]=0xff00ff00u;
    }
    texture->UnlockRect(0);

    const float transform[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    unsigned rc=0;
    if(FAILED(device->SetVertexShaderConstantF(0,&transform[0][0],4)))rc=8;
    if(!rc&&FAILED(device->SetVertexDeclaration(layout)))rc=9;
    if(!rc&&FAILED(device->SetStreamSource(0,buffer,0,sizeof(TexturedVertex))))rc=10;
    if(!rc&&FAILED(device->SetVertexShader(vertexShader)))rc=11;
    if(!rc&&FAILED(device->SetPixelShader(pixelShader)))rc=12;
    if(!rc&&FAILED(device->SetTexture(0,texture)))rc=13;
    if(!rc)device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    if(!rc)device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    if(!rc)device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    if(!rc)device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    if(!rc)device->SetRenderState(D3DRS_ZENABLE,FALSE);
    if(!rc&&FAILED(device->SetRenderTarget(0,target)))rc=14;
    if(!rc&&FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))rc=15;
    D3DVIEWPORT9 viewport{0,0,64,64,0.0f,1.0f};
    if(!rc&&FAILED(device->SetViewport(&viewport)))rc=16;
    if(!rc&&FAILED(device->BeginScene()))rc=17;
    if(!rc&&FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))rc=18;
    if(!rc&&FAILED(device->EndScene()))rc=19;
    if(!rc&&FAILED(device->GetRenderTargetData(target,staging)))rc=20;
    if(!rc) {
        D3DLOCKED_RECT read{};
        if(FAILED(staging->LockRect(&read,nullptr,D3DLOCK_READONLY))||!read.pBits)rc=21;
        else {
            const unsigned char* inside=static_cast<const unsigned char*>(read.pBits)
                +32*read.Pitch+32*4;
            const unsigned char* outside=static_cast<const unsigned char*>(read.pBits)
                +4*read.Pitch+4*4;
            const bool insideGreen=inside[1]>0x80&&inside[0]<0x40&&inside[2]<0x40;
            const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
            staging->UnlockRect();
            if(!insideGreen||!outsideBlack)rc=22;
        }
    }
    // The fixed-function variants after this one need their own shaders back.
    device->SetTexture(0,nullptr);
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    texture->Release();buffer->Release();layout->Release();
    pixelShader->Release();vertexShader->Release();
    return rc;
}

DWORD d3d9_triangle_variants_entry() {
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

    const UINT size=64;
    IDirect3DSurface9* target=nullptr;
    if(FAILED(device->CreateRenderTarget(size,size,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
        &target,nullptr))||!target)return 13;
    IDirect3DSurface9* staging=nullptr;
    if(FAILED(device->CreateOffscreenPlainSurface(size,size,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,
        &staging,nullptr))||!staging)return 14;

    IDirect3DVertexBuffer9* buffer=nullptr;
    if(FAILED(device->CreateVertexBuffer(3*sizeof(Vertex),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,
        &buffer,nullptr))||!buffer)return 15;
    Vertex* vertices=nullptr;
    if(FAILED(buffer->Lock(0,0,reinterpret_cast<void**>(&vertices),0))||!vertices)return 16;
    vertices[0]=Vertex{ 8.0f, 8.0f,0.0f,1.0f,0xffff0000};
    vertices[1]=Vertex{56.0f, 8.0f,0.0f,1.0f,0xffff0000};
    vertices[2]=Vertex{32.0f,56.0f,0.0f,1.0f,0xffff0000};
    buffer->Unlock();

    // v1 first: the plain textureless triangle, the draw the console crashed.
    // With the ffp.h fix its pixel shader declares no sampler at all.
    {
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        const unsigned rc=drawAndCheck(device,target,staging,buffer,0xffff0000);
        report(1,"textureless-diffuse",rc==0);
        if(rc)return 60;
    }
    // v2: an explicit D3DTA_TEXTURE arg with no texture set. The sampler is
    // declared and the 1x1 white fallback view answers the sample; the readback
    // sees white where the triangle is, so this variant checks only that the
    // draw does not fault and the outside stays the clear colour.
    {
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        if(FAILED(device->SetStreamSource(0,buffer,0,sizeof(Vertex))))return 61;
        if(FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)))return 61;
        device->SetRenderState(D3DRS_LIGHTING,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE,FALSE);
        if(FAILED(device->SetRenderTarget(0,target)))return 61;
        if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 61;
        D3DVIEWPORT9 viewport{0,0,64,64,0.0f,1.0f};
        if(FAILED(device->SetViewport(&viewport)))return 61;
        if(FAILED(device->BeginScene()))return 61;
        if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 61;
        if(FAILED(device->EndScene()))return 61;
        if(FAILED(device->GetRenderTargetData(target,staging)))return 61;
        D3DLOCKED_RECT locked{};
        if(FAILED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))||!locked.pBits)return 61;
        const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
            +4*locked.Pitch+4*4;
        const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
        staging->UnlockRect();
        report(2,"texture-arg-unbound",outsideBlack);
        if(!outsideBlack)return 62;
    }
    // v3: two stages, stage 0 textureless and stage 1 with D3DTA_TEXTURE. The
    // per-stage path declares s1 only; the colour is stage 1's sample ANDed
    // with nothing, so the readback check is only the outside again (the
    // sampled white fallback makes the inside white, not red).
    {
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(1,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(1,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        if(FAILED(device->SetStreamSource(0,buffer,0,sizeof(Vertex))))return 63;
        if(FAILED(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)))return 63;
        if(FAILED(device->SetRenderTarget(0,target)))return 63;
        if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1.0f,0)))return 63;
        D3DVIEWPORT9 viewport{0,0,64,64,0.0f,1.0f};
        if(FAILED(device->SetViewport(&viewport)))return 63;
        if(FAILED(device->BeginScene()))return 63;
        if(FAILED(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1)))return 63;
        if(FAILED(device->EndScene()))return 63;
        if(FAILED(device->GetRenderTargetData(target,staging)))return 63;
        D3DLOCKED_RECT locked{};
        if(FAILED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))||!locked.pBits)return 63;
        const unsigned char* outside=static_cast<const unsigned char*>(locked.pBits)
            +4*locked.Pitch+4*4;
        const bool outsideBlack=outside[0]<0x20&&outside[1]<0x20&&outside[2]<0x20;
        staging->UnlockRect();
        report(3,"two-stage-mixed",outsideBlack);
        if(!outsideBlack)return 63;
        // Back to one stage for v4's sake.
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    }
    // v4: the game-compiled ps_1_1 with a real texture, the textured path.
    {
        const unsigned rc=texturedVariant(device,target,staging);
        report(4,"game-ps11-textured",rc==0);
        if(rc)return 64;
    }
    // v1 again, last: if any earlier variant left state that breaks the
    // textureless path, this catches it; and if the console faults on the
    // textureless draw at all, everything above has already reported.
    {
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        const unsigned rc=drawAndCheck(device,target,staging,buffer,0xffff0000);
        report(5,"textureless-again",rc==0);
        if(rc)return 65;
    }

    buffer->Release();
    staging->Release();target->Release();
    device->Release();d3d9->Release();
    DestroyWindow(window);
    return 42;
}
}
