// SPDX-License-Identifier: GPL-3.0-or-later
// Play's D3D9 shim: d3d9.dll on the console's D3D11.
//
// Built by build.sh as a CRT-free mingw PE DLL and mapped by Play's loader, so
// its d3d11.dll/dxgi.dll imports bind to Play's routes exactly as the D3D10
// shim's do (see ../d3dshim/base.h and research/D3D9-PLAN.md).
//
// The device draws. Shader model 1-3 bytecode is translated to DXBC by
// d3d9translator.dll (src/tools/build-d3d9-translator.sh, built from pinned
// DXUP source) and handed to D3D11; a draw with no shaders bound synthesises
// fixed-function vs_3_0/ps_3_0 from the transform, material and texture-stage
// state (ffp.h) and runs those through the same translator. Buffers, textures,
// vertex declarations, render states, samplers and the constant registers a
// fixed-function draw needs are real; every method of IDirect3DDevice9Ex and
// IDirect3D9Ex is present, because the guest's calls land on fixed vtable slots.
//
// Deliberate gaps, reported through the launch log rather than faked:
//   * Fixed-function lighting is not synthesised: D3DRS_LIGHTING is reported
//     once and geometry is drawn unlit (the decision record scopes it out).
//   * Triangle fans have no D3D11 topology; they are reported and not drawn.
//   * ID3DXEffect, ID3DXFont and the D3DX helpers live in d3dx9_*.dll, which
//     Play does not ship; a game that ships its own copy gets its own copy.
//   * Exclusive fullscreen is refused by the CoreWindow adapter, as for D3D10.
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../d3dshim/base.h"
#include "ffp.h"

namespace {
// ------------------------------------------------------------------ formats
// D3D9 formats that map one-to-one onto DXGI. D3D9's numeric values are not
// DXGI's, so this is a switch rather than a cast; anything absent from the
// table is a format the console's D3D11 cannot sample or render as.
DXGI_FORMAT dxgiFormat(D3DFORMAT format) {
    switch(format) {
    case D3DFMT_A8R8G8B8:return DXGI_FORMAT_B8G8R8A8_UNORM;
    // D3D9's X formats ignore alpha, so the alpha channel is the right target;
    // the X variants (B8G8R8X8_UNORM) are not valid flip-model swap-chain
    // formats, and the CoreWindow adapter only creates flip-model chains.
    case D3DFMT_X8R8G8B8:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_A8B8G8R8:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_X8B8G8R8:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case D3DFMT_R5G6B5:return DXGI_FORMAT_B5G6R5_UNORM;
    case D3DFMT_A1R5G5B5:return DXGI_FORMAT_B5G5R5A1_UNORM;
    case D3DFMT_X1R5G5B5:return DXGI_FORMAT_B5G5R5A1_UNORM;
    case D3DFMT_A4R4G4B4:return DXGI_FORMAT_B4G4R4A4_UNORM;
    case D3DFMT_A2B10G10R10:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DFMT_A2R10G10B10:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DFMT_G16R16:return DXGI_FORMAT_R16G16_UNORM;
    case D3DFMT_A16B16G16R16:return DXGI_FORMAT_R16G16B16A16_UNORM;
    case D3DFMT_A8:return DXGI_FORMAT_A8_UNORM;
    case D3DFMT_L8:return DXGI_FORMAT_R8_UNORM;
    case D3DFMT_A8L8:return DXGI_FORMAT_R8G8_UNORM;
    case D3DFMT_L16:return DXGI_FORMAT_R16_UNORM;
    case D3DFMT_D16_LOCKABLE:return DXGI_FORMAT_D16_UNORM;
    case D3DFMT_D16:return DXGI_FORMAT_D16_UNORM;
    case D3DFMT_D32:return DXGI_FORMAT_D32_FLOAT;
    case D3DFMT_D32F_LOCKABLE:return DXGI_FORMAT_D32_FLOAT;
    case D3DFMT_D24S8:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24X8:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24X4S4:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_D24FS8:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case D3DFMT_DXT1:return DXGI_FORMAT_BC1_UNORM;
    case D3DFMT_DXT2:return DXGI_FORMAT_BC2_UNORM;
    case D3DFMT_DXT3:return DXGI_FORMAT_BC2_UNORM;
    case D3DFMT_DXT4:return DXGI_FORMAT_BC3_UNORM;
    case D3DFMT_DXT5:return DXGI_FORMAT_BC3_UNORM;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
bool depthFormat(D3DFORMAT format) {
    switch(format) {
    case D3DFMT_D16_LOCKABLE:case D3DFMT_D16:case D3DFMT_D32:case D3DFMT_D32F_LOCKABLE:
    case D3DFMT_D24S8:case D3DFMT_D24X8:case D3DFMT_D24X4S4:case D3DFMT_D24FS8:
        return true;
    default:return false;
    }
}
// D3DPOOL decides the D3D11 usage: DEFAULT is GPU-only, SYSTEMMEM and SCRATCH
// are CPU-visible, MANAGED has no D3D11 equivalent and is served as a dynamic
// CPU-writable resource (D3D11 keeps a copy, which is the observable behaviour).
D3D11_USAGE usageFor(D3DPOOL pool,DWORD d3d9Usage) {
    if(d3d9Usage&D3DUSAGE_DYNAMIC)return D3D11_USAGE_DYNAMIC;
    switch(pool) {
    case D3DPOOL_SYSTEMMEM:case D3DPOOL_SCRATCH:return D3D11_USAGE_STAGING;
    case D3DPOOL_MANAGED:return D3D11_USAGE_DYNAMIC;
    default:return D3D11_USAGE_DEFAULT;
    }
}
UINT cpuAccessFor(D3DPOOL pool,DWORD d3d9Usage) {
    if(d3d9Usage&D3DUSAGE_DYNAMIC)return D3D11_CPU_ACCESS_WRITE;
    switch(pool) {
    case D3DPOOL_SYSTEMMEM:case D3DPOOL_SCRATCH:return D3D11_CPU_ACCESS_READ|D3D11_CPU_ACCESS_WRITE;
    case D3DPOOL_MANAGED:return D3D11_CPU_ACCESS_WRITE;
    default:return 0;
    }
}
// D3D9 lock flags -> D3D11 map mode. A staging resource (SYSTEMMEM, SCRATCH) is
// the guest's own memory, so a plain lock is read-write there and only
// D3DLOCK_READONLY takes the write away. A dynamic resource accepts only
// WRITE_DISCARD or WRITE_NO_OVERWRITE, never plain WRITE: a lock that does not
// ask to discard must keep what earlier locks wrote, which is WRITE_NO_OVERWRITE
// (what Wine's d3d9 does for the same case).
D3D11_MAP mapMode(bool staging,DWORD flags) {
    if(staging)return flags&D3DLOCK_READONLY?D3D11_MAP_READ:D3D11_MAP_READ_WRITE;
    return flags&D3DLOCK_DISCARD?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_WRITE_NO_OVERWRITE;
}
bool stagingPool(D3DPOOL pool) { return pool==D3DPOOL_SYSTEMMEM||pool==D3DPOOL_SCRATCH; }
// D3D9's Levels=0 means the full chain: as many levels as halving both sides
// reaches 1x1 in.
UINT mipChain(UINT width,UINT height) {
    UINT levels=1;
    while(width>1||height>1){width=width>1?width>>1:1;height=height>1?height>>1:1;++levels;}
    return levels;
}
// D3D9 render states and sampler states are not D3D11 pipeline state, so they
// are stored and read back; the translation that would consume them is the
// unfinished half of the shim.
constexpr unsigned renderStateCount=256;
constexpr unsigned textureStageCount=16;
constexpr unsigned streamCount=16;
constexpr unsigned samplerStateCount=16;
constexpr unsigned maxVertexConst=256;
constexpr unsigned maxPixelConst=224;

void colorF(float out[4],D3DCOLOR color) {
    out[0]=float((color>>16)&0xff)/255.0f;
    out[1]=float((color>>8)&0xff)/255.0f;
    out[2]=float(color&0xff)/255.0f;
    out[3]=float((color>>24)&0xff)/255.0f;
}
void copyText(char* out,unsigned size,const char* text) {
    unsigned i=0;
    for(;i+1<size&&text[i];++i)out[i]=text[i];
    out[i]=0;
}
// The capability table is the same whether a guest asks the factory or a
// device, so both build it here.
void fillCaps(D3DCAPS9* caps,D3DDEVTYPE type,UINT adapter) {
    *caps=D3DCAPS9{};
    caps->DeviceType=type;
    caps->AdapterOrdinal=adapter;
    caps->Caps=D3DCAPS_READ_SCANLINE;
    caps->PresentationIntervals=D3DPRESENT_INTERVAL_IMMEDIATE|D3DPRESENT_INTERVAL_ONE;
    caps->DevCaps=D3DDEVCAPS_DRAWPRIMTLVERTEX|D3DDEVCAPS_HWTRANSFORMANDLIGHT;
    caps->MaxTextureWidth=caps->MaxTextureHeight=16384;
    caps->MaxVolumeExtent=2048;
    caps->MaxTextureRepeat=8192;
    caps->MaxTextureAspectRatio=8192;
    caps->MaxAnisotropy=16;
    caps->MaxVertexW=1e10f;
    caps->MaxTextureBlendStages=8;
    caps->MaxSimultaneousTextures=8;
    caps->MaxStreams=streamCount;
    caps->MaxStreamStride=255;
    caps->MaxPrimitiveCount=0x555555;
    caps->MaxVertexIndex=0xffffff;
    caps->MaxVertexShaderConst=maxVertexConst;
    caps->MaxUserClipPlanes=6;
    caps->MaxVertexBlendMatrices=4;
    caps->NumSimultaneousRTs=4;
    caps->MaxVertexShader30InstructionSlots=32768;
    caps->MaxPixelShader30InstructionSlots=32768;
    // The D3D11 device behind this shim is shader model 4+, which covers D3D9's
    // 3.0. Reporting it is the hardware truth; the shim's own translation of
    // D3D9 bytecode is what is missing, and Create*Shader says so.
    caps->VertexShaderVersion=D3DVS_VERSION(3,0);
    caps->PixelShaderVersion=D3DPS_VERSION(3,0);
    caps->PixelShader1xMaxValue=8.0f;
    caps->MaxPointSize=256.0f;
}
}

namespace {
// ------------------------------------------------------------------- shaders
// D3D9 bytecode becomes SM5 DXBC through Play's own translator
// (src/tools/build-d3d9-translator.sh), shipped in the runtime directory beside
// this shim. It is loaded on first use and never at load time: a shim without
// it still serves the fixed-function path, which synthesises its shaders from
// the same bytecode format.
struct Translator {
    bool tried=false;
    using Function=int(*)(const unsigned*,unsigned*,unsigned,unsigned*);
    Function translate=nullptr;
    Function get() {
        if(!tried) {
            tried=true;
            // No search flags: Play's loader serves the runtime directory for a
            // name it ships, and a copy in the game's own folder still wins,
            // exactly as for the Wine donors.
            HMODULE module=LoadLibraryExW(L"d3d9translator.dll",nullptr,0);
            if(module)
                translate=reinterpret_cast<Function>(GetProcAddress(module,"d3d9Translate"));
            if(!translate)
                d3dshim::noteOnce("D3D9 shim: d3d9translator.dll is unavailable; shader bytecode cannot be translated");
        }
        return translate;
    }
};
Translator translator;

// The length of a D3D9 shader blob in 32-bit words. The API never passes one:
// the version token opens the blob and the END token closes it.
unsigned bytecodeWords(const DWORD* code) {
    if(!code)return 0;
    unsigned i=1;
    for(unsigned guard=0;guard<262144;++guard) {
        const DWORD token=code[i];
        const DWORD opcode=token&0xFFFF;
        if(opcode==0xFFFF)return i+1;                             // D3DSIO_END
        if(opcode==0xFFFE){i+=1+((token>>16)&0x7FFF);continue;}    // D3DSIO_COMMENT
        i+=1+((token>>24)&0xF);                                   // D3DSI_INSTLENGTH
    }
    return 0;
}

// ------------------------------------------------------- vertex declarations
// One declared vertex input, already resolved to the stream and offset the
// D3D11 input layout needs.
struct Element {
    unsigned stream,offset;
    D3DDECLTYPE type;
    D3DDECLUSAGE usage;
    unsigned index;
};

DXGI_FORMAT inputFormat(D3DDECLTYPE type) {
    switch(type) {
    case D3DDECLTYPE_FLOAT1:return DXGI_FORMAT_R32_FLOAT;
    case D3DDECLTYPE_FLOAT2:return DXGI_FORMAT_R32G32_FLOAT;
    case D3DDECLTYPE_FLOAT3:return DXGI_FORMAT_R32G32B32_FLOAT;
    case D3DDECLTYPE_FLOAT4:return DXGI_FORMAT_R32G32B32A32_FLOAT;
    // D3DCOLOR is 0xAARRGGBB in a DWORD, so little-endian memory reads B,G,R,A:
    // the BGRA format is the one that returns the channels in RGBA order.
    case D3DDECLTYPE_D3DCOLOR:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4:return DXGI_FORMAT_R8G8B8A8_UINT;
    case D3DDECLTYPE_UBYTE4N:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2:return DXGI_FORMAT_R16G16_SINT;
    case D3DDECLTYPE_SHORT4:return DXGI_FORMAT_R16G16B16A16_SINT;
    case D3DDECLTYPE_SHORT2N:return DXGI_FORMAT_R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N:return DXGI_FORMAT_R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N:return DXGI_FORMAT_R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N:return DXGI_FORMAT_R16G16B16A16_UNORM;
    case D3DDECLTYPE_UDEC3:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DDECLTYPE_DEC3N:return DXGI_FORMAT_R10G10B10A2_UNORM;
    case D3DDECLTYPE_FLOAT16_2:return DXGI_FORMAT_R16G16_FLOAT;
    case D3DDECLTYPE_FLOAT16_4:return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
unsigned inputSize(D3DDECLTYPE type) {
    switch(type) {
    case D3DDECLTYPE_FLOAT1:case D3DDECLTYPE_D3DCOLOR:case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4N:case D3DDECLTYPE_SHORT2:case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_USHORT2N:case D3DDECLTYPE_UDEC3:case D3DDECLTYPE_DEC3N:
    case D3DDECLTYPE_FLOAT16_2:return 4;
    case D3DDECLTYPE_FLOAT2:case D3DDECLTYPE_SHORT4:case D3DDECLTYPE_SHORT4N:
    case D3DDECLTYPE_USHORT4N:case D3DDECLTYPE_FLOAT16_4:return 8;
    case D3DDECLTYPE_FLOAT3:return 12;
    case D3DDECLTYPE_FLOAT4:return 16;
    default:return 0;
    }
}
// The names the translator emits for a vertex input, which is what the D3D11
// input layout has to match (see the donor's convert::declUsage).
const char* semanticName(D3DDECLUSAGE usage) {
    switch(usage) {
    case D3DDECLUSAGE_POSITION:return "POSITION";
    case D3DDECLUSAGE_BLENDWEIGHT:return "BLENDWEIGHT";
    case D3DDECLUSAGE_BLENDINDICES:return "BLENDINDICES";
    case D3DDECLUSAGE_NORMAL:return "NORMAL";
    case D3DDECLUSAGE_PSIZE:return "PSIZE";
    case D3DDECLUSAGE_TEXCOORD:return "TEXCOORD";
    case D3DDECLUSAGE_TANGENT:return "TANGENT";
    case D3DDECLUSAGE_BINORMAL:return "BINORMAL";
    case D3DDECLUSAGE_TESSFACTOR:return "TESSFACTOR";
    case D3DDECLUSAGE_POSITIONT:return "POSITIONT";
    case D3DDECLUSAGE_COLOR:return "COLOR";
    case D3DDECLUSAGE_FOG:return "FOG";
    case D3DDECLUSAGE_DEPTH:return "DEPTH";
    case D3DDECLUSAGE_SAMPLE:return "SAMPLE";
    default:return "UNKNOWN";
    }
}
// An FVF is the same declaration in a packed form; expanding it here means the
// rest of the shim only ever sees elements.
unsigned decodeFVF(DWORD fvf,Element* out,unsigned capacity) {
    unsigned count=0,offset=0;
    auto add=[&](D3DDECLTYPE type,D3DDECLUSAGE usage,unsigned index) {
        if(count>=capacity||!inputSize(type))return;
        out[count].stream=0;out[count].offset=offset;out[count].type=type;
        out[count].usage=usage;out[count].index=index;
        offset+=inputSize(type);
        ++count;
    };
    const DWORD position=fvf&D3DFVF_POSITION_MASK;
    unsigned weights=0;
    switch(position) {
    case D3DFVF_XYZ:add(D3DDECLTYPE_FLOAT3,D3DDECLUSAGE_POSITION,0);break;
    case D3DFVF_XYZRHW:add(D3DDECLTYPE_FLOAT4,D3DDECLUSAGE_POSITIONT,0);break;
    case D3DFVF_XYZW:add(D3DDECLTYPE_FLOAT4,D3DDECLUSAGE_POSITION,0);break;
    case D3DFVF_XYZB1:case D3DFVF_XYZB2:case D3DFVF_XYZB3:
    case D3DFVF_XYZB4:case D3DFVF_XYZB5:
        add(D3DDECLTYPE_FLOAT3,D3DDECLUSAGE_POSITION,0);
        weights=unsigned((position-D3DFVF_XYZB1)/2)+1;
        for(unsigned i=0;i<weights;++i)add(D3DDECLTYPE_FLOAT1,D3DDECLUSAGE_BLENDWEIGHT,i);
        break;
    default:break;
    }
    if(fvf&D3DFVF_NORMAL)add(D3DDECLTYPE_FLOAT3,D3DDECLUSAGE_NORMAL,0);
    if(fvf&D3DFVF_PSIZE)add(D3DDECLTYPE_FLOAT1,D3DDECLUSAGE_PSIZE,0);
    if(fvf&D3DFVF_DIFFUSE)add(D3DDECLTYPE_D3DCOLOR,D3DDECLUSAGE_COLOR,0);
    if(fvf&D3DFVF_SPECULAR)add(D3DDECLTYPE_D3DCOLOR,D3DDECLUSAGE_COLOR,1);
    const unsigned texcoords=(fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;
    for(unsigned i=0;i<texcoords&&i<8;++i) {
        // D3DFVF_TEXCOORDSIZE1(i) is a mask, not a shift count: the two-bit
        // field for texcoord i sits at bit 16+2i. Masking the shifted value
        // gives 0..3, where 3 means one float and 0..2 mean two to four.
        const unsigned format=(fvf>>(16+2*i))&3;
        const D3DDECLTYPE type=format==D3DFVF_TEXTUREFORMAT1?D3DDECLTYPE_FLOAT1
            :format==D3DFVF_TEXTUREFORMAT2?D3DDECLTYPE_FLOAT2
            :format==D3DFVF_TEXTUREFORMAT3?D3DDECLTYPE_FLOAT3:D3DDECLTYPE_FLOAT4;
        add(type,D3DDECLUSAGE_TEXCOORD,i);
    }
    return count;
}

// ------------------------------------------------------- DXBC input signature
// D3D11 refuses an input layout that names an element the vertex shader does not
// declare, and D3D9 declarations routinely carry elements a shader ignores, so
// the layout is built from the translated shader's own signature rather than
// from the declaration alone.
struct SignatureElement {
    char name[32];
    unsigned index;
};
unsigned read32(const unsigned char* at) {
    return unsigned(at[0])|unsigned(at[1])<<8|unsigned(at[2])<<16|unsigned(at[3])<<24;
}
// Fills `out` with the ISGN elements of a DXBC blob; returns the count, or 0
// when the blob has no readable signature.
unsigned readInputSignature(const void* dxbc,unsigned bytes,SignatureElement* out,unsigned capacity) {
    const unsigned char* base=static_cast<const unsigned char*>(dxbc);
    if(!base||bytes<32)return 0;
    const unsigned chunks=read32(base+28);
    for(unsigned c=0;c<chunks;++c) {
        if(32+(c+1)*4>bytes)return 0;
        const unsigned offset=read32(base+32+c*4);
        if(offset+8>bytes)return 0;
        const unsigned char* head=base+offset;
        if(head[0]!='I'||head[1]!='S'||head[2]!='G'||head[3]!='N')continue;
        const unsigned char* payload=head+8;
        if(offset+8+8>bytes)return 0;
        const unsigned count=read32(payload);
        if(count>capacity)return 0;
        if(offset+8+8+count*24>bytes)return 0;
        for(unsigned i=0;i<count;++i) {
            const unsigned char* element=payload+8+i*24;
            const unsigned nameOffset=read32(element);
            const unsigned stringAt=8+nameOffset;
            if(offset+stringAt>=bytes)return 0;
            const char* name=reinterpret_cast<const char*>(head+stringAt);
            unsigned n=0;
            while(n+1<sizeof out[i].name&&name[n]&&offset+stringAt+n<bytes){out[i].name[n]=name[n];++n;}
            out[i].name[n]=0;
            out[i].index=read32(element+4);
        }
        return count;
    }
    return 0;
}
}

namespace {
// ------------------------------------------------------------------ surfaces
// A D3D9 surface is a 2D texture plus, when it can be rendered to, the views a
// D3D11 context needs. The back buffer comes from the swap chain, so the same
// wrapper serves both.
struct Surface : d3dshim::Base, IDirect3DSurface9 {
    Surface() { self=static_cast<IDirect3DSurface9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DSurface9),&__uuidof(IDirect3DResource9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    // The D3D11 texture is `inner`, so ~Base releases the one reference the
    // surface owns; `texture` is only an alias for the typed calls.
    ID3D11Texture2D* texture=nullptr;
    ID3D11RenderTargetView* rtv=nullptr;     // when the surface is a render target
    ID3D11DepthStencilView* dsv=nullptr;     // when the surface is depth-stencil
    D3DSURFACE_DESC desc{};
    // The mip level this surface is, for the texture's own levels; the back
    // buffer and standalone surfaces are level 0.
    UINT level=0;
    bool cpuVisible=false;                   // SYSTEMMEM, SCRATCH or DYNAMIC
    bool locked=false;
    // D3D9's surfaces and swap chains hold their device weakly: the device owns
    // them, not the other way round. Base::owner would be a strong reference, so
    // the device's own implicit back buffer would keep the device alive forever.
    // The device's guest-visible interface, not a Device*, because Device is
    // defined below this wrapper.
    IDirect3DDevice9* device=nullptr;
    ~Surface() {
        if(locked&&context)context->Unmap(texture,level);
        if(rtv)rtv->Release();
        if(dsv)dsv->Release();
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,const void* data,DWORD size,DWORD) override {
        return inner?inner->SetPrivateData(id,size,data):D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,void* data,DWORD* size) override {
        UINT bytes=size?*size:0;
        HRESULT hr=inner?inner->GetPrivateData(id,&bytes,data):D3DERR_INVALIDCALL;
        if(size)*size=bytes;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID id) override {
        return inner?inner->SetPrivateData(id,0,nullptr):D3DERR_INVALIDCALL;
    }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID,void** container) override {
        if(container)*container=nullptr;
        return D3DERR_INVALIDCALL;   // surfaces made here have no parent texture
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=desc;
        return D3D_OK;
    }
    // The back buffer and render targets live in D3DPOOL_DEFAULT, which D3D9
    // does not allow to be locked; the same call fails on Windows. A SYSTEMMEM,
    // SCRATCH or dynamic surface is CPU-visible and does map.
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* out,const RECT*,DWORD flags) override {
        if(!out)return D3DERR_INVALIDCALL;
        out->pBits=nullptr;out->Pitch=0;
        if(!cpuVisible||!context||!texture) {
            d3dshim::noteOnce("D3D9 shim: LockRect on a default-pool surface refused, as on Windows");
            return D3DERR_INVALIDCALL;
        }
        if(locked)return D3DERR_INVALIDCALL;   // D3D9 allows one lock at a time
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr=context->Map(texture,level,mapMode(stagingPool(desc.Pool),flags),0,&mapped);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        locked=true;
        out->pBits=mapped.pData;
        out->Pitch=INT(mapped.RowPitch);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect() override {
        if(!locked)return D3DERR_INVALIDCALL;
        context->Unmap(texture,level);
        locked=false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDC(HDC* dc) override { if(dc)*dc=nullptr;return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return D3DERR_INVALIDCALL; }
};
}

namespace {
// ------------------------------------------------------------- vertex buffers
// D3D9 buffers and textures all answer IDirect3DResource9's bookkeeping the
// same way, so the shared part is one base. It holds the device weakly, as
// Surface does: the device owns its resources, not the other way round.
template<class I> struct Resource : d3dshim::Base, I {
    IDirect3DDevice9* device=nullptr;
    ID3D11Device* device11=nullptr;   // borrowed from the device that made this
    bool cpuVisible=false;
    bool locked=false;
    IDirect3DDevice9* deviceOf() {
        if(device)device->AddRef();
        return device;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=deviceOf();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,const void* data,DWORD size,DWORD) override {
        return inner?inner->SetPrivateData(id,size,data):D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,void* data,DWORD* size) override {
        UINT bytes=size?*size:0;
        HRESULT hr=inner?inner->GetPrivateData(id,&bytes,data):D3DERR_INVALIDCALL;
        if(size)*size=bytes;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID id) override {
        return inner?inner->SetPrivateData(id,0,nullptr):D3DERR_INVALIDCALL;
    }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
};

struct VertexBuffer : Resource<IDirect3DVertexBuffer9> {
    VertexBuffer() { self=static_cast<IDirect3DVertexBuffer9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DVertexBuffer9),&__uuidof(IDirect3DResource9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    ID3D11Buffer* buffer=nullptr;   // also `inner`
    D3DVERTEXBUFFER_DESC desc{};
    ~VertexBuffer() {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** data,DWORD flags) override {
        if(!data)return D3DERR_INVALIDCALL;
        *data=nullptr;
        if(!buffer||!context)return D3DERR_INVALIDCALL;
        if(locked)return D3DERR_INVALIDCALL;
        if(!cpuVisible) {
            d3dshim::noteOnce("D3D9 shim: Lock on a default-pool vertex buffer refused, as on Windows");
            return D3DERR_INVALIDCALL;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // A dynamic D3D11 buffer only accepts WRITE_DISCARD or
        // WRITE_NO_OVERWRITE, never plain WRITE; the D3D9 flags pick between
        // them, so a lock without D3DLOCK_DISCARD keeps what earlier locks of
        // the same buffer wrote (a streaming lock at a non-zero offset).
        // Every buffer here is dynamic, SYSTEMMEM included (CreateVertexBuffer
        // serves it as dynamic storage), so no lock is a staging read.
        HRESULT hr=context->Map(buffer,0,mapMode(false,flags),0,&mapped);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        locked=true;
        // D3D9's offset is in bytes from the start of the buffer; D3D11's Map
        // always returns the start, so the two are added here.
        *data=static_cast<unsigned char*>(mapped.pData)+offset;
        (void)size;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override {
        if(!locked)return D3DERR_INVALIDCALL;
        context->Unmap(buffer,0);
        locked=false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=desc;
        return D3D_OK;
    }
};

struct IndexBuffer : Resource<IDirect3DIndexBuffer9> {
    IndexBuffer() { self=static_cast<IDirect3DIndexBuffer9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DIndexBuffer9),&__uuidof(IDirect3DResource9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    ID3D11Buffer* buffer=nullptr;   // also `inner`
    D3DINDEXBUFFER_DESC desc{};
    ~IndexBuffer() {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** data,DWORD flags) override {
        if(!data)return D3DERR_INVALIDCALL;
        *data=nullptr;
        if(!buffer||!context)return D3DERR_INVALIDCALL;
        if(locked)return D3DERR_INVALIDCALL;
        if(!cpuVisible) {
            d3dshim::noteOnce("D3D9 shim: Lock on a default-pool index buffer refused, as on Windows");
            return D3DERR_INVALIDCALL;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // Same flag translation as VertexBuffer::Lock: the buffer is dynamic
        // whatever its D3D9 pool, so only D3DLOCK_DISCARD discards.
        HRESULT hr=context->Map(buffer,0,mapMode(false,flags),0,&mapped);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        locked=true;
        *data=static_cast<unsigned char*>(mapped.pData)+offset;
        (void)size;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override {
        if(!locked)return D3DERR_INVALIDCALL;
        context->Unmap(buffer,0);
        locked=false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=desc;
        return D3D_OK;
    }
};
}

namespace {
// ---------------------------------------------------------------- textures
// A D3D9 texture is a D3D11 texture plus the surface wrappers D3D9 hands out
// for its levels. Each level surface owns its own reference on the D3D11
// texture through `inner`, so a guest may hold one past the texture's release,
// as on Windows.
struct Texture : Resource<IDirect3DTexture9> {
    Texture() { self=static_cast<IDirect3DTexture9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DTexture9),&__uuidof(IDirect3DBaseTexture9),
                    &__uuidof(IDirect3DResource9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    ID3D11Texture2D* texture=nullptr;   // also `inner`
    ID3D11ShaderResourceView* view=nullptr;
    UINT levels=1;
    D3DFORMAT format=D3DFMT_UNKNOWN;
    DWORD usage=0;
    D3DPOOL pool=D3DPOOL_DEFAULT;
    UINT width=0,height=0;
    DWORD lod=0;
    D3DTEXTUREFILTERTYPE autoGen=D3DTEXF_LINEAR;
    ~Texture() {
        if(view)view->Release();
    }
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD value) override { DWORD old=lod;lod=value;return old; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE type) override {
        autoGen=type;return D3D_OK;
    }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return autoGen; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level,D3DSURFACE_DESC* out) override {
        if(!out||level>=levels)return D3DERR_INVALIDCALL;
        *out=D3DSURFACE_DESC{};
        out->Format=format;
        out->Type=D3DRTYPE_SURFACE;
        out->Usage=usage;
        out->Pool=pool;
        out->MultiSampleType=D3DMULTISAMPLE_NONE;
        out->Width=width>>level?width>>level:1;
        out->Height=height>>level?height>>level:1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!texture||level>=levels)return D3DERR_INVALIDCALL;
        auto surface=d3dshim::create<Surface>();
        if(!surface)return E_OUTOFMEMORY;
        // The level surface shares the texture object and takes its own
        // reference, so it outlives the texture wrapper exactly as D3D9 allows.
        texture->AddRef();
        surface->texture=texture;
        surface->inner=texture;
        surface->device=device;
        surface->context=context;
        surface->cpuVisible=cpuVisible;
        surface->level=level;
        GetLevelDesc(level,&surface->desc);
        // A level of a texture that can be rendered to answers a render target
        // view, which is what SetRenderTarget needs from it.
        if(usage&D3DUSAGE_RENDERTARGET) {
            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format=dxgiFormat(format);
            view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipSlice=level;
            if(FAILED(device11->CreateRenderTargetView(texture,&view,&surface->rtv))) {
                d3dshim::destroy(surface);
                return D3DERR_INVALIDCALL;
            }
        }
        *out=surface;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* out,const RECT*,DWORD flags) override {
        if(!out)return D3DERR_INVALIDCALL;
        out->pBits=nullptr;out->Pitch=0;
        if(!texture||!context||level>=levels)return D3DERR_INVALIDCALL;
        if(!cpuVisible) {
            d3dshim::noteOnce("D3D9 shim: LockRect on a default-pool texture refused, as on Windows");
            return D3DERR_INVALIDCALL;
        }
        if(locked)return D3DERR_INVALIDCALL;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr=context->Map(texture,level,mapMode(stagingPool(pool),flags),0,&mapped);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        locked=true;
        lockedLevel=level;
        out->pBits=mapped.pData;
        out->Pitch=INT(mapped.RowPitch);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override {
        if(!locked||level!=lockedLevel)return D3DERR_INVALIDCALL;
        context->Unmap(texture,level);
        locked=false;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) override { return D3D_OK; }
    UINT lockedLevel=0;
};
}

namespace {
// ------------------------------------------------------- vertex declarations
// A D3D9 declaration is kept as the elements it named; the D3D11 input layout
// is built at draw time, because it has to match the vertex shader that is
// bound then (see the draw path in Device).
struct VertexDeclaration : d3dshim::Base, IDirect3DVertexDeclaration9 {
    VertexDeclaration() { self=static_cast<IDirect3DVertexDeclaration9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DVertexDeclaration9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    IDirect3DDevice9* device=nullptr;   // weak
    Element elements[32]{};
    unsigned count=0;
    // The declaration as the guest wrote it, for GetDeclaration.
    D3DVERTEXELEMENT9 original[33]{};
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* countOut) override {
        // D3D9 counts elements, terminator included, not bytes.
        const UINT elements=count+1;
        if(!out) {   // D3D9 lets a caller ask for the size only
            if(countOut)*countOut=elements;
            return D3D_OK;
        }
        // The caller's capacity is read before it is overwritten, so a buffer
        // that is too small is refused instead of overrun.
        if(!countOut||*countOut<elements)return D3DERR_INVALIDCALL;
        memcpy(out,original,elements*sizeof(D3DVERTEXELEMENT9));
        *countOut=elements;
        return D3D_OK;
    }
};

// -------------------------------------------------------------- shaders
// A D3D9 shader is its bytecode plus the DXBC the translator made of it. The
// bytecode is kept because GetFunction returns it, and because the vertex
// shader's input signature is what the input layout must match.
struct VertexShader : d3dshim::Base, IDirect3DVertexShader9 {
    VertexShader() { self=static_cast<IDirect3DVertexShader9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DVertexShader9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    IDirect3DDevice9* device=nullptr;   // weak
    ID3D11VertexShader* shader=nullptr; // also `inner`
    DWORD* bytecode=nullptr;            // the D3D9 source, for GetFunction
    unsigned bytecodeBytes=0;
    void* blob=nullptr;                 // the DXBC, for CreateInputLayout
    unsigned blobBytes=0;
    SignatureElement signature[16]{};
    unsigned signatureCount=0;
    // `shader` is `inner`, so ~Base releases the D3D11 object's one reference;
    // releasing it here too would free it twice.
    ~VertexShader() {
        d3dshim::release(bytecode);
        d3dshim::release(blob);
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFunction(void* data,UINT* size) override {
        if(size)*size=bytecodeBytes;
        if(!data)return D3D_OK;
        if(!size||*size<bytecodeBytes)return D3DERR_INVALIDCALL;
        memcpy(data,bytecode,bytecodeBytes);
        return D3D_OK;
    }
};

struct PixelShader : d3dshim::Base, IDirect3DPixelShader9 {
    PixelShader() { self=static_cast<IDirect3DPixelShader9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DPixelShader9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    IDirect3DDevice9* device=nullptr;   // weak
    ID3D11PixelShader* shader=nullptr;  // also `inner`
    DWORD* bytecode=nullptr;
    unsigned bytecodeBytes=0;
    // `shader` is `inner`, so ~Base releases it.
    ~PixelShader() {
        d3dshim::release(bytecode);
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFunction(void* data,UINT* size) override {
        if(size)*size=bytecodeBytes;
        if(!data)return D3D_OK;
        if(!size||*size<bytecodeBytes)return D3DERR_INVALIDCALL;
        memcpy(data,bytecode,bytecodeBytes);
        return D3D_OK;
    }
};
}

namespace {
// ---------------------------------------------------------------- swap chain
struct SwapChain : d3dshim::Base, IDirect3DSwapChain9 {
    SwapChain() { self=static_cast<IDirect3DSwapChain9*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DSwapChain9)); }
    D3DSHIM_REF
    D3DSHIM_QI
    IDXGISwapChain* chain=nullptr;
    Surface* backBuffer=nullptr;          // the wrapper handed to the guest
    D3DPRESENT_PARAMETERS present{};
    IDirect3DDevice9* device=nullptr;     // weak, as for Surface
    ~SwapChain() {
        if(backBuffer)backBuffer->Release();
        if(chain)chain->Release();
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT*,const RECT*,HWND,const RGNDATA*,DWORD) override {
        if(!chain)return D3DERR_INVALIDCALL;
        UINT interval=present.PresentationInterval==D3DPRESENT_INTERVAL_IMMEDIATE?0:1;
        HRESULT hr=chain->Present(interval,0);
        if(hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET)return D3DERR_DEVICELOST;
        return SUCCEEDED(hr)?D3D_OK:D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9*) override {
        return D3DERR_INVALIDCALL;   // the console has no front buffer to read back
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index,D3DBACKBUFFER_TYPE,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!backBuffer||index!=0)return D3DERR_INVALIDCALL;
        backBuffer->AddRef();
        *out=backBuffer;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS* status) override {
        if(!status)return D3DERR_INVALIDCALL;
        status->InVBlank=FALSE;status->ScanLine=0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* mode) override {
        if(!mode)return D3DERR_INVALIDCALL;
        mode->Width=present.BackBufferWidth;mode->Height=present.BackBufferHeight;
        mode->RefreshRate=0;mode->Format=present.BackBufferFormat;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=device;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=present;
        return D3D_OK;
    }
};
}

namespace {
// --------------------------------------------------------------------- device
struct Device : d3dshim::Base, IDirect3DDevice9Ex {
    ~Device() {
        // The context holds references on what was bound; drop ours first so
        // nothing outlives the device (Wine's present path never finishes
        // process exit while a D3D object outlives its device).
        if(ID3D11DeviceContext* immediate=context())immediate->ClearState();
        renderTarget.clear();depthStencil.clear();
        for(unsigned i=0;i<textureStageCount;++i)if(textures[i]){textures[i]->Release();textures[i]=nullptr;}
        for(unsigned i=0;i<streamCount;++i)if(streams[i]){streams[i]->Release();streams[i]=nullptr;}
        if(indices)indices->Release();
        if(declaration)declaration->Release();
        if(vertexShader)vertexShader->Release();
        if(pixelShader)pixelShader->Release();
        if(swapChain)swapChain->Release();
        if(parent)parent->Release();
        if(inputLayout)inputLayout->Release();
        if(vertexConstants)vertexConstants->Release();
        if(pixelConstants)pixelConstants->Release();
        if(blendState)blendState->Release();
        if(depthState)depthState->Release();
        if(rasterState)rasterState->Release();
        for(unsigned i=0;i<textureStageCount;++i)if(samplerState[i])samplerState[i]->Release();
        if(defaultView)defaultView->Release();
        if(ffpVertex)ffpVertex->Release();
        if(ffpPixel)ffpPixel->Release();
        if(ffpLayout)ffpLayout->Release();
        host.destroy();
    }
    D3DSHIM_REF
    D3DSHIM_QI
    d3dshim::Host host;
    SwapChain* swapChain=nullptr;
    d3dshim::Held<1> renderTarget;      // what Clear() clears
    d3dshim::Held<1> depthStencil;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DPRESENT_PARAMETERS present{};
    DWORD renderStates[renderStateCount]{};
    DWORD textureStageStates[textureStageCount][32]{};
    DWORD samplerStates[samplerStateCount][16]{};
    IDirect3DBaseTexture9* textures[textureStageCount]{};
    IDirect3DVertexBuffer9* streams[streamCount]{};
    UINT streamOffsets[streamCount]{};
    UINT streamStrides[streamCount]{};
    IDirect3DIndexBuffer9* indices=nullptr;
    IDirect3DVertexDeclaration9* declaration=nullptr;
    DWORD fvf=0;
    IDirect3DVertexShader9* vertexShader=nullptr;
    IDirect3DPixelShader9* pixelShader=nullptr;
    float vertexConstF[maxVertexConst][4]{};
    float pixelConstF[maxPixelConst][4]{};
    D3DVIEWPORT9 viewport{0,0,0,0,0.0f,1.0f};
    RECT scissor{};
    IDirect3D9Ex* parent=nullptr;       // the factory that made this device
    D3DDEVTYPE deviceType=D3DDEVTYPE_HAL;
    // ------------------------------------------------------- pipeline objects
    // D3D11 keeps its pipeline state in objects, D3D9 in loose state values.
    // The shim caches the objects it derives from the loose values and rebuilds
    // one only when a value it was built from changes.
    D3DMATRIX world{},view{},projection{};
    D3DMATERIAL9 material{};
    ID3D11InputLayout* inputLayout=nullptr;
    ID3D11Buffer* vertexConstants=nullptr;
    ID3D11Buffer* pixelConstants=nullptr;
    ID3D11BlendState* blendState=nullptr;
    ID3D11DepthStencilState* depthState=nullptr;
    ID3D11RasterizerState* rasterState=nullptr;
    ID3D11SamplerState* samplerState[textureStageCount]{};
    ID3D11ShaderResourceView* defaultView=nullptr;   // the 1x1 white stand-in
    // Fixed-function shaders are rebuilt when the declaration or the stage
    // state they were generated from changes; `ffpSignature` is the state they
    // were built for.
    ID3D11VertexShader* ffpVertex=nullptr;
    ID3D11PixelShader* ffpPixel=nullptr;
    unsigned ffpVertexSignature=0,ffpPixelSignature=0;
    ID3D11InputLayout* ffpLayout=nullptr;
    unsigned ffpLayoutSignature=0;
    unsigned shaderLayoutSignature=0;
    unsigned blendSignature=0,depthSignature=0,rasterSignature=0;
    unsigned samplerSignature[textureStageCount]{};
    unsigned constantsSignature=0;
    unsigned resourceSignature=0;
    bool reportedLighting=false;

    Device() {
        self=static_cast<IDirect3DDevice9Ex*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3DDevice9),&__uuidof(IDirect3DDevice9Ex));
        // D3D9's documented default state, which a game may rely on without
        // ever setting it: z-test on, z-write on, cull counter-clockwise faces,
        // no blending, all colour channels written.
        renderStates[D3DRS_ZENABLE]=D3DZB_TRUE;
        renderStates[D3DRS_ZWRITEENABLE]=TRUE;
        renderStates[D3DRS_ZFUNC]=D3DCMP_LESSEQUAL;
        renderStates[D3DRS_CULLMODE]=D3DCULL_CCW;
        renderStates[D3DRS_FILLMODE]=D3DFILL_SOLID;
        renderStates[D3DRS_ALPHABLENDENABLE]=FALSE;
        renderStates[D3DRS_SRCBLEND]=D3DBLEND_ONE;
        renderStates[D3DRS_DESTBLEND]=D3DBLEND_ZERO;
        renderStates[D3DRS_BLENDOP]=D3DBLENDOP_ADD;
        renderStates[D3DRS_COLORWRITEENABLE]=0xF;
        renderStates[D3DRS_TEXTUREFACTOR]=0xFFFFFFFF;
        renderStates[D3DRS_LIGHTING]=TRUE;
        renderStates[D3DRS_SCISSORTESTENABLE]=FALSE;
        renderStates[D3DRS_ALPHATESTENABLE]=FALSE;
        renderStates[D3DRS_SEPARATEALPHABLENDENABLE]=FALSE;
        renderStates[D3DRS_SRCBLENDALPHA]=D3DBLEND_ONE;
        renderStates[D3DRS_DESTBLENDALPHA]=D3DBLEND_ZERO;
        renderStates[D3DRS_BLENDOPALPHA]=D3DBLENDOP_ADD;
        // Identity transforms, so a game that draws without setting them gets
        // its vertices where it put them rather than at the origin.
        world._11=world._22=world._33=world._44=1.0f;
        view._11=view._22=view._33=view._44=1.0f;
        projection._11=projection._22=projection._33=projection._44=1.0f;
        // D3D9's default texture stage: stage 0 modulates the texture with the
        // diffuse colour; every later stage is disabled.
        for(unsigned i=0;i<textureStageCount;++i) {
            textureStageStates[i][D3DTSS_COLOROP]=i?D3DTOP_DISABLE:D3DTOP_MODULATE;
            textureStageStates[i][D3DTSS_COLORARG1]=D3DTA_TEXTURE;
            textureStageStates[i][D3DTSS_COLORARG2]=D3DTA_CURRENT;
            textureStageStates[i][D3DTSS_ALPHAOP]=i?D3DTOP_DISABLE:D3DTOP_SELECTARG1;
            textureStageStates[i][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;
            textureStageStates[i][D3DTSS_ALPHAARG2]=D3DTA_CURRENT;
            // D3D9 defaults every sampler to point sampling with wrap.
            samplerStates[i][D3DSAMP_MINFILTER]=D3DTEXF_POINT;
            samplerStates[i][D3DSAMP_MAGFILTER]=D3DTEXF_POINT;
            samplerStates[i][D3DSAMP_MIPFILTER]=D3DTEXF_NONE;
            samplerStates[i][D3DSAMP_ADDRESSU]=D3DTADDRESS_WRAP;
            samplerStates[i][D3DSAMP_ADDRESSV]=D3DTADDRESS_WRAP;
            samplerStates[i][D3DSAMP_ADDRESSW]=D3DTADDRESS_WRAP;
        }
        // The pixel shader's own constants: the complement source and the
        // texture factor, which the shim overwrites from the render state.
        pixelConstF[ffp::PcOne][0]=pixelConstF[ffp::PcOne][1]=1.0f;
        pixelConstF[ffp::PcOne][2]=pixelConstF[ffp::PcOne][3]=1.0f;
        for(unsigned i=0;i<4;++i)pixelConstF[ffp::PcTFactor][i]=1.0f;
    }

    ID3D11Device* device() const { return host.device; }
    ID3D11DeviceContext* context() const { return host.context; }

    // ------------------------------------------------------------ device info
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 512u*1024u*1024u; }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=parent;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps) override {
        if(!caps)return D3DERR_INVALIDCALL;
        fillCaps(caps,deviceType,creation.AdapterOrdinal);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT index,D3DDISPLAYMODE* mode) override {
        if(!mode||index!=0)return D3DERR_INVALIDCALL;
        mode->Width=present.BackBufferWidth;mode->Height=present.BackBufferHeight;
        mode->RefreshRate=0;mode->Format=present.BackBufferFormat;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=creation;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT,UINT,IDirect3DSurface9*) override { return D3DERR_INVALIDCALL; }
    void STDMETHODCALLTYPE SetCursorPosition(int,int,DWORD) override {}
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL show) override { return show; }

    // ------------------------------------------------------------ swap chains
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: additional swap chains are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index,IDirect3DSwapChain9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(index!=0||!swapChain)return D3DERR_INVALIDCALL;
        swapChain->AddRef();
        *out=swapChain;
        return D3D_OK;
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 1; }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* parameters) override {
        if(!parameters)return D3DERR_INVALIDCALL;
        return rebuildSwapChain(parameters);
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND override,const RGNDATA* dirty) override {
        if(!swapChain)return D3DERR_INVALIDCALL;
        return swapChain->Present(src,dst,override,dirty,0);
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT chainIndex,UINT backIndex,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(chainIndex!=0||!swapChain)return D3DERR_INVALIDCALL;
        return swapChain->GetBackBuffer(backIndex,type,out);
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT chainIndex,D3DRASTER_STATUS* status) override {
        if(chainIndex!=0||!swapChain)return D3DERR_INVALIDCALL;
        return swapChain->GetRasterStatus(status);
    }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetGammaRamp(UINT,DWORD,const D3DGAMMARAMP*) override {}
    void STDMETHODCALLTYPE GetGammaRamp(UINT,D3DGAMMARAMP* ramp) override {
        if(ramp)for(unsigned i=0;i<256;++i){ramp->red[i]=WORD(i*257);ramp->green[i]=WORD(i*257);ramp->blue[i]=WORD(i*257);}
    }

    // -------------------------------------------------------------- resources
    HRESULT createTexture2D(UINT width,UINT height,UINT levels,DWORD usage,D3DFORMAT format,
                            D3DPOOL pool,ID3D11Texture2D** out) {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        DXGI_FORMAT dxgi=dxgiFormat(format);
        if(dxgi==DXGI_FORMAT_UNKNOWN) {
            d3dshim::noteOnce("D3D9 shim: texture format has no DXGI equivalent");
            return D3DERR_INVALIDCALL;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;
        // D3D9's Levels=0 asks for the full chain down to 1x1.
        desc.MipLevels=levels?levels:mipChain(width,height);
        desc.ArraySize=1;
        desc.Format=dxgi;desc.SampleDesc.Count=1;
        desc.Usage=usageFor(pool,usage);
        desc.BindFlags=0;
        if(usage&D3DUSAGE_RENDERTARGET)desc.BindFlags|=D3D11_BIND_RENDER_TARGET;
        if(usage&D3DUSAGE_DEPTHSTENCIL)desc.BindFlags|=D3D11_BIND_DEPTH_STENCIL;
        if(!desc.BindFlags&&desc.Usage==D3D11_USAGE_DEFAULT)desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=cpuAccessFor(pool,usage);
        // D3D11 refuses a dynamic resource with no bind flag and a staging
        // resource with one, so the two pools get exactly what they allow.
        if(desc.Usage==D3D11_USAGE_DYNAMIC&&!desc.BindFlags)desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(desc.Usage==D3D11_USAGE_STAGING)desc.BindFlags=0;
        // A MANAGED texture is CPU-writable and sampled, which D3D11 spells
        // dynamic + shader resource; a mipmapped one cannot be dynamic, so it
        // falls back to default and refuses Lock, as the D3D9 runtime does for
        // a managed texture it cannot keep in system memory.
        if(pool==D3DPOOL_MANAGED&&desc.MipLevels>1&&desc.Usage==D3D11_USAGE_DYNAMIC) {
            desc.Usage=D3D11_USAGE_DEFAULT;
            desc.CPUAccessFlags=0;
            if(!desc.BindFlags)desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        }
        return device()->CreateTexture2D(&desc,nullptr,out);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width,UINT height,UINT levels,DWORD usage,
                                            D3DFORMAT format,D3DPOOL pool,
                                            IDirect3DTexture9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!width||!height)return D3DERR_INVALIDCALL;
        ID3D11Texture2D* texture=nullptr;
        HRESULT hr=createTexture2D(width,height,levels,usage,format,pool,&texture);
        if(FAILED(hr))return hr;
        auto wrapper=d3dshim::create<Texture>();
        if(!wrapper){texture->Release();return E_OUTOFMEMORY;}
        wrapper->texture=texture;
        wrapper->inner=texture;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->device11=device();
        wrapper->context=context();
        const UINT mips=levels?levels:mipChain(width,height);
        wrapper->cpuVisible=usageFor(pool,usage)!=D3D11_USAGE_DEFAULT;
        // A mipmapped MANAGED texture fell back to default above, so it is not
        // CPU-visible however the pool names it.
        if(pool==D3DPOOL_MANAGED&&mips>1)wrapper->cpuVisible=false;
        wrapper->format=format;
        wrapper->usage=usage;
        wrapper->pool=pool;
        wrapper->width=width;
        wrapper->height=height;
        wrapper->levels=mips;
        // A texture the guest binds to a sampler needs a view; a render target
        // or depth surface does not, and D3D11 refuses the combination on some
        // formats, so the view is only made when the texture is sampled.
        if(!(usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format=dxgiFormat(format);
            view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels=wrapper->levels;
            if(FAILED(device()->CreateShaderResourceView(texture,&view,&wrapper->view))) {
                d3dshim::destroy(wrapper);
                return D3DERR_INVALIDCALL;
            }
        }
        *out=wrapper;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,
                                                  IDirect3DVolumeTexture9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: volume textures are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,
                                                IDirect3DCubeTexture9** out,HANDLE*) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: cube textures are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length,DWORD usage,DWORD fvf,D3DPOOL pool,
                                                 IDirect3DVertexBuffer9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!length)return D3DERR_INVALIDCALL;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=length;
        desc.Usage=usageFor(pool,usage);
        // A vertex buffer the guest locks is bound as a shader resource only in
        // D3D11's byte-addressable model, which the draw path does not use; the
        // bind flag it needs is VERTEX_BUFFER.
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags=cpuAccessFor(pool,usage);
        // D3D11 refuses a staging buffer with a bind flag, so a SYSTEMMEM
        // buffer is served as dynamic CPU-writable storage instead: D3D9's
        // observable contract for it is Lock/Unlock, which that supports.
        if(desc.Usage==D3D11_USAGE_STAGING) {
            desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        }
        ID3D11Buffer* buffer=nullptr;
        HRESULT hr=device()->CreateBuffer(&desc,nullptr,&buffer);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        auto wrapper=d3dshim::create<VertexBuffer>();
        if(!wrapper){buffer->Release();return E_OUTOFMEMORY;}
        wrapper->buffer=buffer;
        wrapper->inner=buffer;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->device11=device();
        wrapper->context=context();
        wrapper->cpuVisible=desc.CPUAccessFlags!=0;
        wrapper->desc.Format=D3DFMT_VERTEXDATA;
        wrapper->desc.Type=D3DRTYPE_VERTEXBUFFER;
        wrapper->desc.Usage=usage;
        wrapper->desc.Pool=pool;
        wrapper->desc.Size=length;
        wrapper->desc.FVF=fvf;
        *out=wrapper;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length,DWORD usage,D3DFORMAT format,D3DPOOL pool,
                                                IDirect3DIndexBuffer9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!length)return D3DERR_INVALIDCALL;
        if(format!=D3DFMT_INDEX16&&format!=D3DFMT_INDEX32) {
            d3dshim::noteOnce("D3D9 shim: index buffers must be D3DFMT_INDEX16 or D3DFMT_INDEX32");
            return D3DERR_INVALIDCALL;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=length;
        desc.Usage=usageFor(pool,usage);
        desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags=cpuAccessFor(pool,usage);
        if(desc.Usage==D3D11_USAGE_STAGING) {
            desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        }
        ID3D11Buffer* buffer=nullptr;
        HRESULT hr=device()->CreateBuffer(&desc,nullptr,&buffer);
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        auto wrapper=d3dshim::create<IndexBuffer>();
        if(!wrapper){buffer->Release();return E_OUTOFMEMORY;}
        wrapper->buffer=buffer;
        wrapper->inner=buffer;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->device11=device();
        wrapper->context=context();
        wrapper->cpuVisible=desc.CPUAccessFlags!=0;
        wrapper->desc.Format=format;
        wrapper->desc.Type=D3DRTYPE_INDEXBUFFER;
        wrapper->desc.Usage=usage;
        wrapper->desc.Pool=pool;
        wrapper->desc.Size=length;
        *out=wrapper;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width,UINT height,D3DFORMAT format,
                                                 D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                 IDirect3DSurface9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        return makeSurface(width,height,format,samples,quality,true,false,out);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width,UINT height,D3DFORMAT format,
                                                        D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                        IDirect3DSurface9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        return makeSurface(width,height,format,samples,quality,false,true,out);
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*) override {
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9*,IDirect3DBaseTexture9*) override {
        return D3DERR_INVALIDCALL;
    }
    // A render target's pixels copied into a SYSTEMMEM surface, which is how a
    // game takes a screenshot and how the off-device fixtures read back what
    // they drew. D3D9 requires the same size and format on both surfaces, and
    // D3D11's CopyResource needs the same texture description, so this is the
    // copy D3D9 documents.
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* source,
                                                  IDirect3DSurface9* destination) override {
        if(!source||!destination)return D3DERR_INVALIDCALL;
        auto src=static_cast<Surface*>(source);
        auto dst=static_cast<Surface*>(destination);
        if(!src->texture||!dst->texture)return D3DERR_INVALIDCALL;
        if(!dst->cpuVisible) {
            d3dshim::noteOnce("D3D9 shim: GetRenderTargetData needs a SYSTEMMEM destination surface");
            return D3DERR_INVALIDCALL;
        }
        D3D11_TEXTURE2D_DESC from{},to{};
        src->texture->GetDesc(&from);
        dst->texture->GetDesc(&to);
        if(from.Width!=to.Width||from.Height!=to.Height||from.Format!=to.Format)return D3DERR_INVALIDCALL;
        context()->CopyResource(dst->texture,src->texture);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT,IDirect3DSurface9*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,
                                          D3DTEXTUREFILTERTYPE) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9*,const RECT*,D3DCOLOR) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,
                                                          IDirect3DSurface9** out,HANDLE*) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        return makeSurface(width,height,format,D3DMULTISAMPLE_NONE,0,false,false,out,pool);
    }

    // ------------------------------------------------------------ render state
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index,IDirect3DSurface9* surface) override {
        if(index!=0)return D3DERR_INVALIDCALL;
        if(surface) {
            auto wrapper=static_cast<Surface*>(surface);
            if(!wrapper->rtv)return D3DERR_INVALIDCALL;
            renderTarget.set(0,surface);
            context()->OMSetRenderTargets(1,&wrapper->rtv,nullptr);
        } else {
            renderTarget.set(0,nullptr);
            context()->OMSetRenderTargets(0,nullptr,nullptr);
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index,IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(index!=0)return D3DERR_INVALIDCALL;
        *out=static_cast<IDirect3DSurface9*>(renderTarget.objects[0]);
        if(*out)(*out)->AddRef();
        return *out?D3D_OK:D3DERR_INVALIDCALL;
    }
    ID3D11RenderTargetView* currentTarget() const {
        auto surface=static_cast<IDirect3DSurface9*>(renderTarget.objects[0]);
        return surface?static_cast<Surface*>(surface)->rtv:nullptr;
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* surface) override {
        ID3D11DepthStencilView* view=nullptr;
        if(surface) {
            view=static_cast<Surface*>(surface)->dsv;
            if(!view)return D3DERR_INVALIDCALL;
        }
        depthStencil.set(0,surface);
        ID3D11RenderTargetView* rt=currentTarget();
        context()->OMSetRenderTargets(rt?1:0,rt?&rt:nullptr,view);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=static_cast<IDirect3DSurface9*>(depthStencil.objects[0]);
        if(*out)(*out)->AddRef();
        return *out?D3D_OK:D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Clear(DWORD rectCount,const D3DRECT*,DWORD flags,D3DCOLOR color,float z,DWORD stencil) override {
        if(rectCount)d3dshim::noteOnce("D3D9 shim: Clear with rectangles clears the whole target");
        if(flags&D3DCLEAR_TARGET) {
            ID3D11RenderTargetView* view=currentTarget();
            if(!view&&swapChain&&swapChain->backBuffer)view=swapChain->backBuffer->rtv;
            if(view) {
                float rgba[4];colorF(rgba,color);
                context()->ClearRenderTargetView(view,rgba);
            }
        }
        if((flags&D3DCLEAR_ZBUFFER)||(flags&D3DCLEAR_STENCIL)) {
            auto surface=static_cast<IDirect3DSurface9*>(depthStencil.objects[0]);
            ID3D11DepthStencilView* view=surface?static_cast<Surface*>(surface)->dsv:nullptr;
            if(view) {
                UINT clearFlags=0;
                if(flags&D3DCLEAR_ZBUFFER)clearFlags|=D3D11_CLEAR_DEPTH;
                if(flags&D3DCLEAR_STENCIL)clearFlags|=D3D11_CLEAR_STENCIL;
                context()->ClearDepthStencilView(view,clearFlags,z,UINT8(stencil));
            }
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state,const D3DMATRIX* matrix) override {
        if(!matrix)return D3DERR_INVALIDCALL;
        switch(state) {
        case D3DTS_WORLD:world=*matrix;break;
        case D3DTS_VIEW:view=*matrix;break;
        case D3DTS_PROJECTION:projection=*matrix;break;
        case D3DTS_TEXTURE0:case D3DTS_TEXTURE1:case D3DTS_TEXTURE2:case D3DTS_TEXTURE3:
        case D3DTS_TEXTURE4:case D3DTS_TEXTURE5:case D3DTS_TEXTURE6:case D3DTS_TEXTURE7:
            // Texture-coordinate generation is not synthesised; the generated
            // shader passes the vertex's own coordinates through, which is what
            // D3DTTFF_DISABLE (the default) asks for.
            d3dshim::noteOnce("D3D9 shim: texture-coordinate generation transforms are ignored");
            break;
        default:break;
        }
        ffpVertexSignature=0;   // the generated shader's constants changed
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state,D3DMATRIX* matrix) override {
        if(!matrix)return D3DERR_INVALIDCALL;
        switch(state) {
        case D3DTS_WORLD:*matrix=world;break;
        case D3DTS_VIEW:*matrix=view;break;
        case D3DTS_PROJECTION:*matrix=projection;break;
        default:
            *matrix=D3DMATRIX{};
            matrix->_11=matrix->_22=matrix->_33=matrix->_44=1.0f;
            break;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state,const D3DMATRIX* matrix) override {
        if(!matrix)return D3DERR_INVALIDCALL;
        D3DMATRIX* target=state==D3DTS_WORLD?&world:state==D3DTS_VIEW?&view
                        :state==D3DTS_PROJECTION?&projection:nullptr;
        if(!target)return D3D_OK;
        *target=multiply(*target,*matrix);
        ffpVertexSignature=0;
        return D3D_OK;
    }
    // Row-vector convention, as D3D9 uses: result = a * b.
    static D3DMATRIX multiply(const D3DMATRIX& a,const D3DMATRIX& b) {
        D3DMATRIX out{};
        for(unsigned row=0;row<4;++row) {
            for(unsigned column=0;column<4;++column) {
                float sum=0.0f;
                for(unsigned k=0;k<4;++k)sum+=a.m[row][k]*b.m[k][column];
                out.m[row][column]=sum;
            }
        }
        return out;
    }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* value) override {
        if(!value)return D3DERR_INVALIDCALL;
        viewport=*value;
        D3D11_VIEWPORT out{};
        out.TopLeftX=float(value->X);out.TopLeftY=float(value->Y);
        out.Width=float(value->Width);out.Height=float(value->Height);
        out.MinDepth=value->MinZ;out.MaxDepth=value->MaxZ;
        context()->RSSetViewports(1,&out);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=viewport;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* value) override {
        if(!value)return D3DERR_INVALIDCALL;
        material=*value;
        ffpVertexSignature=0;   // the generated shader reads the material's diffuse
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=material;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD,const D3DLIGHT9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD,D3DLIGHT9* light) override {
        if(light)*light=D3DLIGHT9{};
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD,WINBOOL) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD,WINBOOL* enabled) override {
        if(enabled)*enabled=FALSE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD,const float*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD,float* plane) override {
        if(plane)for(unsigned i=0;i<4;++i)plane[i]=0.0f;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state,DWORD value) override {
        if(unsigned(state)>=renderStateCount)return D3DERR_INVALIDCALL;
        renderStates[state]=value;
        // The pipeline objects are rebuilt from these values at the next draw;
        // the signatures are what decides which of them actually changed.
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state,DWORD* value) override {
        if(!value||unsigned(state)>=renderStateCount)return D3DERR_INVALIDCALL;
        *value=renderStates[state];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: state blocks are not implemented");
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** out) override {
        if(out)*out=nullptr;
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* status) override {
        if(status) {
            status->ClipUnion=0;
            status->ClipIntersection=D3DCS_ALL;
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage,IDirect3DBaseTexture9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(stage>=textureStageCount)return D3DERR_INVALIDCALL;
        *out=textures[stage];
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* texture) override {
        if(stage>=textureStageCount)return D3DERR_INVALIDCALL;
        if(textures[stage]==texture)return D3D_OK;
        if(texture)texture->AddRef();
        if(textures[stage])textures[stage]->Release();
        textures[stage]=texture;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE type,DWORD* value) override {
        if(!value||stage>=textureStageCount||unsigned(type)>=32)return D3DERR_INVALIDCALL;
        *value=textureStageStates[stage][type];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE type,DWORD value) override {
        if(stage>=textureStageCount||unsigned(type)>=32)return D3DERR_INVALIDCALL;
        textureStageStates[stage][type]=value;
        ffpPixelSignature=0;   // the generated pixel shader's stages changed
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE type,DWORD* value) override {
        if(!value||sampler>=samplerStateCount||unsigned(type)>=16)return D3DERR_INVALIDCALL;
        *value=samplerStates[sampler][type];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE type,DWORD value) override {
        if(sampler>=samplerStateCount||unsigned(type)>=16)return D3DERR_INVALIDCALL;
        samplerStates[sampler][type]=value;
        samplerSignature[sampler]=0;   // the D3D11 sampler state is rebuilt
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override {
        if(passes)*passes=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT,const PALETTEENTRY*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT,PALETTEENTRY*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* number) override {
        if(number)*number=0;
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* rect) override {
        if(!rect)return D3DERR_INVALIDCALL;
        scissor=*rect;
        D3D11_RECT out{rect->left,rect->top,rect->right,rect->bottom};
        context()->RSSetScissorRects(1,&out);
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* rect) override {
        if(!rect)return D3DERR_INVALIDCALL;
        *rect=scissor;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL) override { return D3D_OK; }
    WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return D3D_OK; }
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0.0f; }

    // ----------------------------------------------------------------- drawing
    // ------------------------------------------------------------ translation
    HRESULT translateShader(const DWORD* code,unsigned words,bool vertex,void** out) {
        auto translate=translator.get();
        if(!translate) {
            d3dshim::noteOnce("D3D9 shim: shader creation refused, d3d9translator.dll is not available");
            return D3DERR_INVALIDCALL;
        }
        // The translator writes DXBC, which is larger than the D3D9 source for a
        // short shader; the buffer is sized from the source with headroom and
        // the translator refuses rather than truncating.
        const unsigned capacity=words*16+1024;
        unsigned* buffer=static_cast<unsigned*>(d3dshim::allocate(capacity*sizeof(unsigned)));
        if(!buffer)return E_OUTOFMEMORY;
        unsigned bytes=0;
        const int rc=translate(reinterpret_cast<const unsigned*>(code),
                               reinterpret_cast<unsigned*>(buffer),capacity,&bytes);
        if(rc) {
            d3dshim::release(buffer);
            d3dshim::Line line("D3D9 shim: the shader translator refused the bytecode rc=");
            line.dec(unsigned(rc));line.emit();
            return D3DERR_INVALIDCALL;
        }
        HRESULT hr;
        if(vertex) {
            hr=device()->CreateVertexShader(buffer,bytes,nullptr,
                reinterpret_cast<ID3D11VertexShader**>(out));
        } else {
            hr=device()->CreatePixelShader(buffer,bytes,nullptr,
                reinterpret_cast<ID3D11PixelShader**>(out));
        }
        d3dshim::release(buffer);
        if(FAILED(hr)) {
            d3dshim::Line line("D3D9 shim: D3D11 refused the translated shader hr=");
            line.hex((unsigned long)hr);line.emit();
            return D3DERR_INVALIDCALL;
        }
        return D3D_OK;
    }
    // The same translation, keeping the DXBC: a vertex shader's input layout is
    // built against its blob, so the blob outlives the CreateVertexShader call.
    HRESULT translateShaderBlob(const DWORD* code,unsigned words,bool vertex,void** shader,void** blob,
                                unsigned* blobBytes) {
        auto translate=translator.get();
        if(!translate) {
            d3dshim::noteOnce("D3D9 shim: shader creation refused, d3d9translator.dll is not available");
            return D3DERR_INVALIDCALL;
        }
        const unsigned capacity=words*16+1024;
        unsigned* buffer=static_cast<unsigned*>(d3dshim::allocate(capacity*sizeof(unsigned)));
        if(!buffer)return E_OUTOFMEMORY;
        unsigned bytes=0;
        const int rc=translate(reinterpret_cast<const unsigned*>(code),
                               reinterpret_cast<unsigned*>(buffer),capacity,&bytes);
        if(rc) {
            d3dshim::release(buffer);
            d3dshim::Line line("D3D9 shim: the shader translator refused the bytecode rc=");
            line.dec(unsigned(rc));line.emit();
            return D3DERR_INVALIDCALL;
        }
        HRESULT hr;
        if(vertex) {
            hr=device()->CreateVertexShader(buffer,bytes,nullptr,
                reinterpret_cast<ID3D11VertexShader**>(shader));
        } else {
            hr=device()->CreatePixelShader(buffer,bytes,nullptr,
                reinterpret_cast<ID3D11PixelShader**>(shader));
        }
        if(FAILED(hr)) {
            d3dshim::release(buffer);
            d3dshim::Line line("D3D9 shim: D3D11 refused the translated shader hr=");
            line.hex((unsigned long)hr);line.emit();
            return D3DERR_INVALIDCALL;
        }
        if(blob)*blob=buffer;
        else d3dshim::release(buffer);
        if(blobBytes)*blobBytes=bytes;
        return D3D_OK;
    }

    // ------------------------------------------------------- pipeline objects
    // A signature is a cheap fingerprint of the state an object was built from:
    // two runs that produce the same number produce the same object.
    static unsigned mix(unsigned hash,unsigned value) {
        hash^=value;hash*=16777619u;return hash;
    }
    unsigned blendSignatureOf() const {
        unsigned hash=2166136261u;
        hash=mix(hash,renderStates[D3DRS_ALPHABLENDENABLE]);
        hash=mix(hash,renderStates[D3DRS_SEPARATEALPHABLENDENABLE]);
        hash=mix(hash,renderStates[D3DRS_SRCBLEND]);
        hash=mix(hash,renderStates[D3DRS_DESTBLEND]);
        hash=mix(hash,renderStates[D3DRS_BLENDOP]);
        hash=mix(hash,renderStates[D3DRS_SRCBLENDALPHA]);
        hash=mix(hash,renderStates[D3DRS_DESTBLENDALPHA]);
        hash=mix(hash,renderStates[D3DRS_BLENDOPALPHA]);
        hash=mix(hash,renderStates[D3DRS_COLORWRITEENABLE]);
        return hash;
    }
    unsigned depthSignatureOf() const {
        unsigned hash=2166136261u;
        hash=mix(hash,renderStates[D3DRS_ZENABLE]);
        hash=mix(hash,renderStates[D3DRS_ZWRITEENABLE]);
        hash=mix(hash,renderStates[D3DRS_ZFUNC]);
        return hash;
    }
    unsigned rasterSignatureOf() const {
        unsigned hash=2166136261u;
        hash=mix(hash,renderStates[D3DRS_CULLMODE]);
        hash=mix(hash,renderStates[D3DRS_FILLMODE]);
        hash=mix(hash,renderStates[D3DRS_SCISSORTESTENABLE]);
        return hash;
    }
    unsigned samplerSignatureOf(unsigned stage) const {
        unsigned hash=2166136261u;
        for(unsigned i=0;i<16;++i)hash=mix(hash,samplerStates[stage][i]);
        return hash;
    }
    // D3D9's blend factors are DXGI's for the common cases but not for the
    // D3DBLEND_* aliases, so this is a switch rather than a cast.
    static D3D11_BLEND blendOf(DWORD value) {
        switch(value) {
        case D3DBLEND_ZERO:return D3D11_BLEND_ZERO;
        case D3DBLEND_ONE:return D3D11_BLEND_ONE;
        case D3DBLEND_SRCCOLOR:return D3D11_BLEND_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR:return D3D11_BLEND_INV_SRC_COLOR;
        case D3DBLEND_SRCALPHA:return D3D11_BLEND_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA:return D3D11_BLEND_INV_SRC_ALPHA;
        case D3DBLEND_DESTALPHA:return D3D11_BLEND_DEST_ALPHA;
        case D3DBLEND_INVDESTALPHA:return D3D11_BLEND_INV_DEST_ALPHA;
        case D3DBLEND_DESTCOLOR:return D3D11_BLEND_DEST_COLOR;
        case D3DBLEND_INVDESTCOLOR:return D3D11_BLEND_INV_DEST_COLOR;
        case D3DBLEND_SRCALPHASAT:return D3D11_BLEND_SRC_ALPHA_SAT;
        default:return D3D11_BLEND_ONE;
        }
    }
    static D3D11_BLEND_OP blendOpOf(DWORD value) {
        switch(value) {
        case D3DBLENDOP_SUBTRACT:return D3D11_BLEND_OP_SUBTRACT;
        case D3DBLENDOP_REVSUBTRACT:return D3D11_BLEND_OP_REV_SUBTRACT;
        case D3DBLENDOP_MIN:return D3D11_BLEND_OP_MIN;
        case D3DBLENDOP_MAX:return D3D11_BLEND_OP_MAX;
        default:return D3D11_BLEND_OP_ADD;
        }
    }
    static D3D11_COMPARISON_FUNC compareOf(DWORD value) {
        switch(value) {
        case D3DCMP_NEVER:return D3D11_COMPARISON_NEVER;
        case D3DCMP_LESS:return D3D11_COMPARISON_LESS;
        case D3DCMP_EQUAL:return D3D11_COMPARISON_EQUAL;
        case D3DCMP_LESSEQUAL:return D3D11_COMPARISON_LESS_EQUAL;
        case D3DCMP_GREATER:return D3D11_COMPARISON_GREATER;
        case D3DCMP_NOTEQUAL:return D3D11_COMPARISON_NOT_EQUAL;
        case D3DCMP_GREATEREQUAL:return D3D11_COMPARISON_GREATER_EQUAL;
        default:return D3D11_COMPARISON_ALWAYS;
        }
    }
    static D3D11_FILTER filterOf(DWORD min,DWORD mag,DWORD mip) {
        const bool pointMin=min!=D3DTEXF_LINEAR&&min!=D3DTEXF_ANISOTROPIC;
        const bool pointMag=mag!=D3DTEXF_LINEAR&&mag!=D3DTEXF_ANISOTROPIC;
        const bool pointMip=mip==D3DTEXF_NONE||mip==D3DTEXF_POINT;
        if(pointMin&&pointMag&&pointMip)return D3D11_FILTER_MIN_MAG_MIP_POINT;
        if(pointMin&&pointMag)return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        if(pointMin&&pointMip)return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
        if(pointMin)return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
        if(pointMag&&pointMip)return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        if(pointMag)return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        if(pointMip)return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    }
    static D3D11_TEXTURE_ADDRESS_MODE addressOf(DWORD value) {
        switch(value) {
        case D3DTADDRESS_MIRROR:return D3D11_TEXTURE_ADDRESS_MIRROR;
        case D3DTADDRESS_CLAMP:return D3D11_TEXTURE_ADDRESS_CLAMP;
        case D3DTADDRESS_BORDER:return D3D11_TEXTURE_ADDRESS_BORDER;
        case D3DTADDRESS_MIRRORONCE:return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        default:return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    }

    // Builds the pipeline state the current render states describe, replacing
    // only the objects whose signature changed. Returns false when an object
    // could not be made, which the caller reports as a failed draw.
    bool syncPipeline() {
        ID3D11Device* dev=device();
        ID3D11DeviceContext* ctx=context();
        const unsigned blend=blendSignatureOf();
        if(blend!=blendSignature) {
            D3D11_BLEND_DESC desc{};
            desc.RenderTarget[0].BlendEnable=renderStates[D3DRS_ALPHABLENDENABLE]?TRUE:FALSE;
            desc.RenderTarget[0].SrcBlend=blendOf(renderStates[D3DRS_SRCBLEND]);
            desc.RenderTarget[0].DestBlend=blendOf(renderStates[D3DRS_DESTBLEND]);
            desc.RenderTarget[0].BlendOp=blendOpOf(renderStates[D3DRS_BLENDOP]);
            if(renderStates[D3DRS_SEPARATEALPHABLENDENABLE]) {
                desc.RenderTarget[0].SrcBlendAlpha=blendOf(renderStates[D3DRS_SRCBLENDALPHA]);
                desc.RenderTarget[0].DestBlendAlpha=blendOf(renderStates[D3DRS_DESTBLENDALPHA]);
                desc.RenderTarget[0].BlendOpAlpha=blendOpOf(renderStates[D3DRS_BLENDOPALPHA]);
            } else {
                desc.RenderTarget[0].SrcBlendAlpha=desc.RenderTarget[0].SrcBlend;
                desc.RenderTarget[0].DestBlendAlpha=desc.RenderTarget[0].DestBlend;
                desc.RenderTarget[0].BlendOpAlpha=desc.RenderTarget[0].BlendOp;
            }
            desc.RenderTarget[0].RenderTargetWriteMask=BYTE(renderStates[D3DRS_COLORWRITEENABLE]&0xF);
            ID3D11BlendState* made=nullptr;
            if(FAILED(dev->CreateBlendState(&desc,&made)))return false;
            if(blendState)blendState->Release();
            blendState=made;
            blendSignature=blend;
        }
        const unsigned depth=depthSignatureOf();
        if(depth!=depthSignature) {
            D3D11_DEPTH_STENCIL_DESC desc{};
            desc.DepthEnable=renderStates[D3DRS_ZENABLE]!=D3DZB_FALSE;
            desc.DepthWriteMask=renderStates[D3DRS_ZWRITEENABLE]?D3D11_DEPTH_WRITE_MASK_ALL
                                                                 :D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc=compareOf(renderStates[D3DRS_ZFUNC]);
            desc.StencilEnable=FALSE;
            ID3D11DepthStencilState* made=nullptr;
            if(FAILED(dev->CreateDepthStencilState(&desc,&made)))return false;
            if(depthState)depthState->Release();
            depthState=made;
            depthSignature=depth;
        }
        const unsigned raster=rasterSignatureOf();
        if(raster!=rasterSignature) {
            D3D11_RASTERIZER_DESC desc{};
            desc.FillMode=renderStates[D3DRS_FILLMODE]==D3DFILL_WIREFRAME?D3D11_FILL_WIREFRAME
                                                                       :D3D11_FILL_SOLID;
            // D3DCULL_CCW culls the counter-clockwise-wound triangles, so it
            // keeps clockwise ones: D3D11's front face is clockwise here
            // (FrontCounterClockwise=FALSE), so that is CULL_BACK.
            desc.CullMode=renderStates[D3DRS_CULLMODE]==D3DCULL_CCW?D3D11_CULL_BACK
                         :renderStates[D3DRS_CULLMODE]==D3DCULL_CW?D3D11_CULL_FRONT
                         :D3D11_CULL_NONE;
            desc.FrontCounterClockwise=FALSE;
            desc.DepthClipEnable=TRUE;
            desc.ScissorEnable=renderStates[D3DRS_SCISSORTESTENABLE]?TRUE:FALSE;
            desc.MultisampleEnable=FALSE;
            desc.AntialiasedLineEnable=FALSE;
            ID3D11RasterizerState* made=nullptr;
            if(FAILED(dev->CreateRasterizerState(&desc,&made)))return false;
            if(rasterState)rasterState->Release();
            rasterState=made;
            rasterSignature=raster;
        }
        ctx->OMSetBlendState(blendState,nullptr,0xffffffff);
        ctx->OMSetDepthStencilState(depthState,0);
        ctx->RSSetState(rasterState);
        return true;
    }
    // The sampler and texture views for the stages that are in use.
    void syncTextures() {
        ID3D11Device* dev=device();
        ID3D11DeviceContext* ctx=context();
        ID3D11ShaderResourceView* views[textureStageCount]{};
        ID3D11SamplerState* samplers[textureStageCount]{};
        for(unsigned i=0;i<textureStageCount;++i) {
            const unsigned signature=samplerSignatureOf(i);
            if(signature!=samplerSignature[i]) {
                D3D11_SAMPLER_DESC desc{};
                desc.Filter=filterOf(samplerStates[i][D3DSAMP_MINFILTER],
                                     samplerStates[i][D3DSAMP_MAGFILTER],
                                     samplerStates[i][D3DSAMP_MIPFILTER]);
                desc.AddressU=addressOf(samplerStates[i][D3DSAMP_ADDRESSU]);
                desc.AddressV=addressOf(samplerStates[i][D3DSAMP_ADDRESSV]);
                desc.AddressW=addressOf(samplerStates[i][D3DSAMP_ADDRESSW]);
                desc.MaxAnisotropy=1;
                desc.ComparisonFunc=D3D11_COMPARISON_NEVER;
                desc.MinLOD=0.0f;
                desc.MaxLOD=D3D11_FLOAT32_MAX;
                ID3D11SamplerState* made=nullptr;
                if(SUCCEEDED(dev->CreateSamplerState(&desc,&made))) {
                    if(samplerState[i])samplerState[i]->Release();
                    samplerState[i]=made;
                    samplerSignature[i]=signature;
                }
            }
            samplers[i]=samplerState[i];
            // D3D9 samples a stage with no texture as white, so an untextured
            // draw keeps its diffuse colour under the default modulate op;
            // D3D11 would sample a null view as black.
            views[i]=textures[i]?static_cast<Texture*>(textures[i])->view:defaultView;
        }
        // D3D9 binds textures per stage and the translator emits one sampler and
        // one texture binding per stage in the same order, so the D3D11 slots
        // line up with the D3D9 stage indices.
        ctx->PSSetShaderResources(0,textureStageCount,views);
        ctx->PSSetSamplers(0,textureStageCount,samplers);
    }
    // The constant buffer the translated shaders read. The translator lays
    // D3D9's c0..cN out one float4 per register, which is the same layout the
    // guest filled through SetVertexShaderConstantF, so this is a copy.
    bool syncConstants() {
        ID3D11Device* dev=device();
        ID3D11DeviceContext* ctx=context();
        // The fixed-function vertex shader reads world x view x projection from
        // c0..c3 and the material's diffuse from c4. A game's own shader reads
        // whatever it set, so these are written only into the registers the
        // generated shader uses - and only when it is the one bound.
        if(!vertexShader) {
            const D3DMATRIX wvp=multiply(multiply(world,view),projection);
            for(unsigned row=0;row<4;++row)
                for(unsigned column=0;column<4;++column)
                    vertexConstF[row][column]=wvp.m[row][column];
            vertexConstF[4][0]=material.Diffuse.r;
            vertexConstF[4][1]=material.Diffuse.g;
            vertexConstF[4][2]=material.Diffuse.b;
            vertexConstF[4][3]=material.Diffuse.a;
            // The viewport transform a POSITIONT vertex needs (see ffp.h): x and
            // y go from pixels to clip space, z and w pass through.
            const float width=viewport.Width?float(viewport.Width):1.0f;
            const float height=viewport.Height?float(viewport.Height):1.0f;
            vertexConstF[5][0]=2.0f/width;vertexConstF[5][1]=-2.0f/height;
            vertexConstF[5][2]=1.0f;vertexConstF[5][3]=1.0f;
            vertexConstF[6][0]=-1.0f;vertexConstF[6][1]=1.0f;
            vertexConstF[6][2]=0.0f;vertexConstF[6][3]=0.0f;
        }
        if(!vertexConstants) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth=maxVertexConst*16;
            desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            if(FAILED(dev->CreateBuffer(&desc,nullptr,&vertexConstants)))return false;
        }
        if(!pixelConstants) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth=maxPixelConst*16;
            desc.Usage=D3D11_USAGE_DYNAMIC;
            desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            if(FAILED(dev->CreateBuffer(&desc,nullptr,&pixelConstants)))return false;
        }
        // The guest's constants are copied on every draw: it may have changed
        // any register since the last one, and D3D11's dynamic buffer makes that
        // a single write rather than a tracking problem.
        // c1 is the generated pixel shader's texture factor, so only that
        // shader may have it written; a game's own pixel shader owns c1 and
        // reads the constant it set there.
        if(!pixelShader)colorF(pixelConstF[ffp::PcTFactor],renderStates[D3DRS_TEXTUREFACTOR]);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(ctx->Map(vertexConstants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        memcpy(mapped.pData,vertexConstF,maxVertexConst*16);
        ctx->Unmap(vertexConstants,0);
        if(FAILED(ctx->Map(pixelConstants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        memcpy(mapped.pData,pixelConstF,maxPixelConst*16);
        ctx->Unmap(pixelConstants,0);
        ctx->VSSetConstantBuffers(0,1,&vertexConstants);
        ctx->PSSetConstantBuffers(0,1,&pixelConstants);
        return true;
    }
    // The vertex inputs the declaration or FVF describes, with the register the
    // generated fixed-function shader reads each from. The position is moved to
    // the front because the generated shader always reads inputs[0] for it.
    unsigned collectElements(Element* elements,unsigned capacity) const {
        if(declaration) {
            auto wrapper=static_cast<const VertexDeclaration*>(declaration);
            const unsigned count=wrapper->count<capacity?wrapper->count:capacity;
            for(unsigned i=0;i<count;++i)elements[i]=wrapper->elements[i];
            return count;
        }
        return decodeFVF(fvf,elements,capacity);
    }
    // The D3D9 primitive type as D3D11 sees it; the topology is the same
    // enumeration for the types both have.
    static D3D11_PRIMITIVE_TOPOLOGY topologyOf(D3DPRIMITIVETYPE type,bool indexed) {
        switch(type) {
        case D3DPT_POINTLIST:return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        case D3DPT_LINELIST:return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case D3DPT_LINESTRIP:return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case D3DPT_TRIANGLELIST:return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case D3DPT_TRIANGLESTRIP:return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        default:
            // D3D11 has no triangle fan: the shim reports it rather than
            // drawing nothing silently.
            d3dshim::noteOnce("D3D9 shim: triangle fans have no D3D11 topology and are not drawn");
            return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }
    HRESULT drawCheck() {
        if(!currentTarget()) {
            d3dshim::noteOnce("D3D9 shim: draw without a render target");
            return D3DERR_INVALIDCALL;
        }
        if(!syncPipeline()||!syncConstants()) {
            d3dshim::noteOnce("D3D9 shim: could not build the D3D11 pipeline state for the draw");
            return D3DERR_INVALIDCALL;
        }
        syncTextures();
        return D3D_OK;
    }
    // The shaders and layout a draw uses. A game's own shaders win; otherwise
    // the fixed-function ones are generated from the declaration and the
    // texture-stage state, and rebuilt when that state changes.
    HRESULT bindShaders() {
        ID3D11DeviceContext* ctx=context();
        if(vertexShader&&pixelShader) {
            auto vs=static_cast<VertexShader*>(vertexShader);
            auto ps=static_cast<PixelShader*>(pixelShader);
            ctx->VSSetShader(vs->shader,nullptr,0);
            ctx->PSSetShader(ps->shader,nullptr,0);
            // A game's shader declares its own inputs; the layout still has to
            // match them, so it is rebuilt for the shader that is bound.
            Element elements[16]{};
            const unsigned count=collectElements(elements,16);
            D3D11_INPUT_ELEMENT_DESC descs[16]{};
            unsigned used=0;
            for(unsigned s=0;s<vs->signatureCount&&used<16;++s) {
                for(unsigned e=0;e<count;++e) {
                    const char* name=semanticName(elements[e].usage);
                    unsigned n=0;while(name[n]&&vs->signature[s].name[n]==name[n])++n;
                    if(name[n]||vs->signature[s].name[n])continue;
                    if(elements[e].index!=vs->signature[s].index)continue;
                    D3D11_INPUT_ELEMENT_DESC& desc=descs[used++];
                    desc.SemanticName=name;
                    desc.SemanticIndex=elements[e].index;
                    desc.Format=inputFormat(elements[e].type);
                    desc.InputSlot=elements[e].stream;
                    desc.AlignedByteOffset=elements[e].offset;
                    desc.InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;
                    break;
                }
            }
            if(!used) {
                d3dshim::noteOnce("D3D9 shim: the vertex declaration names no input the vertex shader declares");
                return D3DERR_INVALIDCALL;
            }
            const unsigned signature=layoutSignatureOf(descs,used);
            if(signature!=shaderLayoutSignature||!inputLayout) {
                ID3D11InputLayout* made=nullptr;
                const HRESULT hr=device()->CreateInputLayout(descs,used,vs->blob,vs->blobBytes,&made);
                if(FAILED(hr)) {
                    d3dshim::Line line("D3D9 shim: D3D11 refused the input layout for the draw hr=");
                    line.hex((unsigned long)hr);line.emit();
                    return D3DERR_INVALIDCALL;
                }
                if(inputLayout)inputLayout->Release();
                inputLayout=made;
                shaderLayoutSignature=signature;
            }
            ctx->IASetInputLayout(inputLayout);
            return D3D_OK;
        }
        if(vertexShader||pixelShader) {
            d3dshim::noteOnce("D3D9 shim: a draw needs both a vertex and a pixel shader; fixed-function is used only when neither is set");
            return D3DERR_INVALIDCALL;
        }
        // Fixed function. Lighting is out of scope for this milestone and is
        // reported once rather than approximated.
        if(renderStates[D3DRS_LIGHTING]&&!reportedLighting) {
            reportedLighting=true;
            d3dshim::noteOnce("D3D9 shim: fixed-function lighting is not implemented; geometry is drawn unlit");
        }
        Element elements[16]{};
        const unsigned count=collectElements(elements,16);
        if(!count) {
            d3dshim::noteOnce("D3D9 shim: a fixed-function draw needs a vertex declaration or an FVF");
            return D3DERR_INVALIDCALL;
        }
        // The generated vertex shader reads its inputs from registers in the
        // order the declaration lists them, with the position first.
        ffp::VertexOptions vertex{};
        vertex.inputCount=0;
        unsigned position=count;
        for(unsigned i=0;i<count;++i) {
            if(elements[i].usage==D3DDECLUSAGE_POSITION||elements[i].usage==D3DDECLUSAGE_POSITIONT) {
                position=i;break;
            }
        }
        if(position==count) {
            d3dshim::noteOnce("D3D9 shim: the vertex declaration has no position element");
            return D3DERR_INVALIDCALL;
        }
        auto addInput=[&](const Element& element,unsigned reg) {
            if(vertex.inputCount>=16)return;
            ffp::Input& input=vertex.inputs[vertex.inputCount++];
            input.usage=element.usage;
            input.index=element.index;
            input.reg=reg;
        };
        // The generated shader reads its inputs from v0, v1, ... in the order
        // they are declared here; the position is always v0.
        addInput(elements[position],0);
        vertex.positionTransformed=elements[position].usage==D3DDECLUSAGE_POSITIONT;
        vertex.colourReg=-1;
        vertex.texcoordCount=0;
        for(unsigned i=0;i<count;++i) {
            if(i==position)continue;
            if(elements[i].usage==D3DDECLUSAGE_COLOR&&elements[i].index==0&&vertex.colourReg<0) {
                vertex.colourReg=int(vertex.inputCount);
                addInput(elements[i],unsigned(vertex.colourReg));
            } else if(elements[i].usage==D3DDECLUSAGE_TEXCOORD&&vertex.texcoordCount<8) {
                const unsigned reg=vertex.inputCount;
                addInput(elements[i],reg);
                vertex.texcoordReg[vertex.texcoordCount++]=int(reg);
            }
        }
        const unsigned vertexSignature=vertexShaderSignature(vertex);
        if(vertexSignature!=ffpVertexSignature||!ffpVertex) {
            // On the heap, not the stack: a 5 KB frame is more than the
            // compiler's stack-probe threshold, and the probe (___chkstk_ms)
            // lives in mingw's CRT, which this DLL deliberately does not link.
            DWORD* tokens=static_cast<DWORD*>(d3dshim::allocate(512*sizeof(DWORD)));
            if(!tokens)return E_OUTOFMEMORY;
            const unsigned words=ffp::writeVertexShader(vertex,tokens,512);
            ID3D11VertexShader* made=nullptr;
            const HRESULT hr=words?translateShader(tokens,words,true,reinterpret_cast<void**>(&made))
                                  :D3DERR_INVALIDCALL;
            d3dshim::release(tokens);
            if(FAILED(hr))return hr;
            if(ffpVertex)ffpVertex->Release();
            ffpVertex=made;
            ffpVertexSignature=vertexSignature;
        }
        ffp::PixelOptions pixel{};
        pixel.stageCount=0;
        pixel.texcoordCount=0;
        for(unsigned i=0;i<8;++i) {
            if(i<vertex.texcoordCount)pixel.texcoordReg[pixel.texcoordCount++]=vertex.texcoordReg[i];
        }
        for(unsigned i=0;i<8;++i) {
            ffp::Stage& stage=pixel.stages[pixel.stageCount];
            stage.colorOp=textureStageStates[i][D3DTSS_COLOROP];
            stage.colorArg1=textureStageStates[i][D3DTSS_COLORARG1];
            stage.colorArg2=textureStageStates[i][D3DTSS_COLORARG2];
            stage.alphaOp=textureStageStates[i][D3DTSS_ALPHAOP];
            stage.alphaArg1=textureStageStates[i][D3DTSS_ALPHAARG1];
            stage.alphaArg2=textureStageStates[i][D3DTSS_ALPHAARG2];
            if(stage.colorOp==ffp::TopDisable)break;
            ++pixel.stageCount;
        }
        const unsigned pixelSignature=pixelShaderSignature(pixel);
        if(pixelSignature!=ffpPixelSignature||!ffpPixel) {
            DWORD* tokens=static_cast<DWORD*>(d3dshim::allocate(1024*sizeof(DWORD)));
            if(!tokens)return E_OUTOFMEMORY;
            const unsigned words=ffp::writePixelShader(pixel,tokens,1024);
            ID3D11PixelShader* made=nullptr;
            const HRESULT hr=words?translateShader(tokens,words,false,reinterpret_cast<void**>(&made))
                                  :D3DERR_INVALIDCALL;
            d3dshim::release(tokens);
            if(FAILED(hr))return hr;
            if(ffpPixel)ffpPixel->Release();
            ffpPixel=made;
            ffpPixelSignature=pixelSignature;
        }
        ctx->VSSetShader(ffpVertex,nullptr,0);
        ctx->PSSetShader(ffpPixel,nullptr,0);
        // The layout names the generated shader's own inputs: POSITION/COLOR/
        // TEXCOORD as the declaration has them, at the register the generated
        // shader reads.
        D3D11_INPUT_ELEMENT_DESC descs[16]{};
        unsigned used=0;
        for(unsigned i=0;i<vertex.inputCount&&used<16;++i) {
            // The generated shader's inputs are named after their D3D9 usage,
            // which the translator maps back to the same semantics.
            const Element* source=nullptr;
            for(unsigned e=0;e<count;++e) {
                if(elements[e].usage==vertex.inputs[i].usage&&elements[e].index==vertex.inputs[i].index) {
                    source=&elements[e];break;
                }
            }
            if(!source)continue;
            D3D11_INPUT_ELEMENT_DESC& desc=descs[used++];
            desc.SemanticName=semanticName(source->usage);
            desc.SemanticIndex=source->index;
            desc.Format=inputFormat(source->type);
            desc.InputSlot=source->stream;
            desc.AlignedByteOffset=source->offset;
            desc.InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;
        }
        const unsigned signature=layoutSignatureOf(descs,used);
        if(signature!=ffpLayoutSignature||!ffpLayout) {
            // The layout is built against the same DXBC the vertex shader was
            // made from, so the two always agree.
            DWORD tokens[512];
            const unsigned words=ffp::writeVertexShader(vertex,tokens,512);
            void* dxbc=nullptr;unsigned bytes=0;
            ID3D11VertexShader* shader=nullptr;
            if(!words||FAILED(translateShaderBlob(tokens,words,true,
                    reinterpret_cast<void**>(&shader),&dxbc,&bytes)))return D3DERR_INVALIDCALL;
            ID3D11InputLayout* made=nullptr;
            const HRESULT hr=device()->CreateInputLayout(descs,used,dxbc,bytes,&made);
            shader->Release();
            d3dshim::release(dxbc);
            if(FAILED(hr)) {
                d3dshim::Line line("D3D9 shim: D3D11 refused the fixed-function input layout hr=");
                line.hex((unsigned long)hr);line.emit();
                return D3DERR_INVALIDCALL;
            }
            if(ffpLayout)ffpLayout->Release();
            ffpLayout=made;
            ffpLayoutSignature=signature;
        }
        ctx->IASetInputLayout(ffpLayout);
        return D3D_OK;
    }
    static unsigned vertexShaderSignature(const ffp::VertexOptions& options) {
        unsigned hash=2166136261u;
        for(unsigned i=0;i<options.inputCount;++i) {
            hash=mix(hash,unsigned(options.inputs[i].usage));
            hash=mix(hash,options.inputs[i].index);
            hash=mix(hash,options.inputs[i].reg);
        }
        hash=mix(hash,options.positionTransformed?1u:0u);
        hash=mix(hash,unsigned(options.colourReg+1));
        for(unsigned i=0;i<options.texcoordCount;++i)hash=mix(hash,unsigned(options.texcoordReg[i]+1));
        return hash;
    }
    static unsigned pixelShaderSignature(const ffp::PixelOptions& options) {
        unsigned hash=2166136261u;
        for(unsigned i=0;i<options.texcoordCount;++i)hash=mix(hash,unsigned(options.texcoordReg[i]+1));
        for(unsigned i=0;i<options.stageCount;++i) {
            hash=mix(hash,options.stages[i].colorOp);
            hash=mix(hash,options.stages[i].colorArg1);
            hash=mix(hash,options.stages[i].colorArg2);
            hash=mix(hash,options.stages[i].alphaOp);
            hash=mix(hash,options.stages[i].alphaArg1);
            hash=mix(hash,options.stages[i].alphaArg2);
        }
        return hash;
    }
    static unsigned layoutSignatureOf(const D3D11_INPUT_ELEMENT_DESC* descs,unsigned count) {
        unsigned hash=2166136261u;
        for(unsigned i=0;i<count;++i) {
            const char* name=descs[i].SemanticName;unsigned n=0;
            while(name[n])hash=mix(hash,unsigned(name[n++]));
            hash=mix(hash,descs[i].SemanticIndex);
            hash=mix(hash,unsigned(descs[i].Format));
            hash=mix(hash,descs[i].InputSlot);
            hash=mix(hash,descs[i].AlignedByteOffset);
        }
        return hash;
    }
    // D3D9's draw calls take a primitive count and D3D11's take a vertex or
    // index count, so the two differ by the vertices per primitive.
    static UINT verticesFor(D3DPRIMITIVETYPE type,UINT primitives) {
        switch(type) {
        case D3DPT_POINTLIST:return primitives;
        case D3DPT_LINELIST:return primitives*2;
        case D3DPT_LINESTRIP:return primitives+1;
        case D3DPT_TRIANGLELIST:return primitives*3;
        case D3DPT_TRIANGLESTRIP:
        case D3DPT_TRIANGLEFAN:return primitives+2;
        default:return 0;
        }
    }
    // The stream sources, the index buffer and the topology, then the call.
    HRESULT issue(D3DPRIMITIVETYPE type,bool indexed,UINT indexCount,UINT startIndex,INT baseVertex,
                  UINT startVertex) {
        ID3D11DeviceContext* ctx=context();
        ID3D11Buffer* buffers[streamCount]{};
        UINT offsets[streamCount]{},strides[streamCount]{};
        unsigned highest=0;
        for(unsigned i=0;i<streamCount;++i) {
            if(streams[i]) {
                buffers[i]=static_cast<VertexBuffer*>(streams[i])->buffer;
                offsets[i]=streamOffsets[i];
                strides[i]=streamStrides[i];
                highest=i+1;
            }
        }
        if(!highest) {
            d3dshim::noteOnce("D3D9 shim: draw without a stream source");
            return D3DERR_INVALIDCALL;
        }
        ctx->IASetVertexBuffers(0,highest,buffers,strides,offsets);
        if(indexed) {
            if(!indices) {
                d3dshim::noteOnce("D3D9 shim: indexed draw without an index buffer");
                return D3DERR_INVALIDCALL;
            }
            auto wrapper=static_cast<IndexBuffer*>(indices);
            ctx->IASetIndexBuffer(wrapper->buffer,
                wrapper->desc.Format==D3DFMT_INDEX32?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R16_UINT,0);
        } else {
            ctx->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
        }
        ctx->IASetPrimitiveTopology(topologyOf(type,indexed));
        if(indexed) {
            // D3D9's BaseVertexIndex is folded into the vertex offset, which is
            // what D3D11's DrawIndexed takes as its own BaseVertexLocation.
            ctx->DrawIndexed(indexCount,startIndex,baseVertex);
        } else {
            ctx->Draw(indexCount,startVertex);
        }
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type,UINT startVertex,UINT count) override {
        HRESULT hr=drawCheck();
        if(FAILED(hr))return hr;
        hr=bindShaders();
        if(FAILED(hr))return hr;
        return issue(type,false,verticesFor(type,count),0,0,startVertex);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type,INT baseVertex,UINT,UINT,
                                                   UINT startIndex,UINT count) override {
        HRESULT hr=drawCheck();
        if(FAILED(hr))return hr;
        hr=bindShaders();
        if(FAILED(hr))return hr;
        return issue(type,true,verticesFor(type,count),startIndex,baseVertex,0);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,
                                              UINT stride) override {
        HRESULT hr=drawCheck();
        if(FAILED(hr))return hr;
        hr=bindShaders();
        if(FAILED(hr))return hr;
        // D3D9's count is primitives; the array holds the vertices they name.
        return uploadAndDraw(type,false,verticesFor(type,count),0,0,data,stride,nullptr,D3DFMT_UNKNOWN);
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT minIndex,UINT count,
                                                     UINT indexCount,const void* indexData,
                                                     D3DFORMAT indexFormat,const void* vertexData,
                                                     UINT stride) override {
        HRESULT hr=drawCheck();
        if(FAILED(hr))return hr;
        hr=bindShaders();
        if(FAILED(hr))return hr;
        // D3D9's minIndex is the lowest vertex the indices name, not an offset
        // into the array: the whole array is copied and the indices index it as
        // the guest wrote them.
        return uploadAndDraw(type,true,count,minIndex,verticesFor(type,indexCount),vertexData,stride,
                             indexData,indexFormat);
    }
    // The UP variants keep their data in guest memory. D3D11 has no equivalent,
    // so the data is copied into a dynamic buffer for the draw, which is what
    // Wine's d3d9 does too. `vertexCount` is the number of vertices the array
    // holds; `minIndex` is the vertex the guest's index values count from.
    HRESULT uploadAndDraw(D3DPRIMITIVETYPE type,bool indexed,UINT vertexCount,UINT minIndex,
                          UINT indexCount,const void* vertexData,UINT stride,
                          const void* indexData,D3DFORMAT indexFormat) {
        if(!vertexData||!stride)return D3DERR_INVALIDCALL;
        ID3D11Device* dev=device();
        ID3D11DeviceContext* ctx=context();
        const unsigned vertexBytes=vertexCount*stride;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=vertexBytes;
        desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* buffer=nullptr;
        if(FAILED(dev->CreateBuffer(&desc,nullptr,&buffer)))return D3DERR_INVALIDCALL;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(ctx->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))){buffer->Release();return D3DERR_INVALIDCALL;}
        memcpy(mapped.pData,vertexData,vertexBytes);
        ctx->Unmap(buffer,0);
        // D3D11 requires the offset array to be non-null; the UP data starts at
        // vertex 0 of the buffer just filled.
        const UINT offset=0;
        ctx->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        ID3D11Buffer* indexBuffer=nullptr;
        if(indexed) {
            if(!indexData) {
                buffer->Release();
                return D3DERR_INVALIDCALL;
            }
            const unsigned width=indexFormat==D3DFMT_INDEX32?4:2;
            D3D11_BUFFER_DESC indexDesc{};
            indexDesc.ByteWidth=indexCount*width;
            indexDesc.Usage=D3D11_USAGE_DYNAMIC;
            indexDesc.BindFlags=D3D11_BIND_INDEX_BUFFER;
            indexDesc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            if(FAILED(dev->CreateBuffer(&indexDesc,nullptr,&indexBuffer))) {
                buffer->Release();
                return D3DERR_INVALIDCALL;
            }
            if(FAILED(ctx->Map(indexBuffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
                indexBuffer->Release();
                buffer->Release();
                return D3DERR_INVALIDCALL;
            }
            memcpy(mapped.pData,indexData,indexCount*width);
            ctx->Unmap(indexBuffer,0);
            ctx->IASetIndexBuffer(indexBuffer,
                indexFormat==D3DFMT_INDEX32?DXGI_FORMAT_R32_UINT:DXGI_FORMAT_R16_UINT,0);
        }
        ctx->IASetPrimitiveTopology(topologyOf(type,indexed));
        // D3D9's index values count from MinVertexIndex, which D3D11 spells
        // BaseVertexLocation - the same fold the non-UP path makes for
        // BaseVertexIndex.
        if(indexed)ctx->DrawIndexed(indexCount,0,minIndex);
        else ctx->Draw(vertexCount,0);
        if(indexBuffer)indexBuffer->Release();
        buffer->Release();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT,UINT,UINT,IDirect3DVertexBuffer9*,
                                              IDirect3DVertexDeclaration9*,DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* elements,
                                                      IDirect3DVertexDeclaration9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!elements)return D3DERR_INVALIDCALL;
        auto wrapper=d3dshim::create<VertexDeclaration>();
        if(!wrapper)return E_OUTOFMEMORY;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        for(unsigned i=0;i<32;++i) {
            const D3DVERTEXELEMENT9& e=elements[i];
            wrapper->original[i]=e;
            if(e.Stream==0xFF) {   // D3DDECLUSAGE_END: the terminator
                wrapper->original[i+1]=D3DVERTEXELEMENT9{};
                break;
            }
            Element& slot=wrapper->elements[wrapper->count];
            slot.stream=e.Stream;
            slot.offset=e.Offset;
            slot.type=D3DDECLTYPE(e.Type);
            slot.usage=D3DDECLUSAGE(e.Usage);
            slot.index=e.UsageIndex;
            if(!inputSize(slot.type)) {
                d3dshim::destroy(wrapper);
                d3dshim::noteOnce("D3D9 shim: vertex declaration uses a type D3D11 has no input format for");
                return D3DERR_INVALIDCALL;
            }
            ++wrapper->count;
        }
        *out=wrapper;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* value) override {
        if(value)value->AddRef();
        if(declaration)declaration->Release();
        declaration=value;
        ffpVertexSignature=0;   // the generated shader's inputs changed
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=declaration;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD value) override {
        fvf=value;
        // Setting an FVF replaces the declaration, as D3D9 documents.
        if(declaration){declaration->Release();declaration=nullptr;}
        ffpVertexSignature=0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* value) override {
        if(!value)return D3DERR_INVALIDCALL;
        *value=fvf;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!code)return D3DERR_INVALIDCALL;
        const unsigned words=bytecodeWords(code);
        if(!words)return D3DERR_INVALIDCALL;
        auto wrapper=d3dshim::create<VertexShader>();
        if(!wrapper)return E_OUTOFMEMORY;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->context=context();
        wrapper->bytecode=static_cast<DWORD*>(d3dshim::allocate(words*sizeof(DWORD)));
        if(!wrapper->bytecode){d3dshim::destroy(wrapper);return E_OUTOFMEMORY;}
        memcpy(wrapper->bytecode,code,words*sizeof(DWORD));
        wrapper->bytecodeBytes=words*sizeof(DWORD);
        HRESULT hr=translateShaderBlob(code,words,true,
            reinterpret_cast<void**>(&wrapper->shader),&wrapper->blob,&wrapper->blobBytes);
        if(FAILED(hr)){d3dshim::destroy(wrapper);return hr;}
        wrapper->inner=wrapper->shader;
        wrapper->signatureCount=readInputSignature(wrapper->blob,wrapper->blobBytes,
            wrapper->signature,16);
        *out=wrapper;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* shader) override {
        if(shader)shader->AddRef();
        if(vertexShader)vertexShader->Release();
        vertexShader=shader;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=vertexShader;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start,const float* data,UINT count) override {
        if(!data||start+count>maxVertexConst)return D3DERR_INVALIDCALL;
        memcpy(vertexConstF[start],data,count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start,float* data,UINT count) override {
        if(!data||start+count>maxVertexConst)return D3DERR_INVALIDCALL;
        memcpy(data,vertexConstF[start],count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT,const int*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT,int*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT,const WINBOOL*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT,WINBOOL*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride) override {
        if(stream>=streamCount)return D3DERR_INVALIDCALL;
        if(buffer)buffer->AddRef();
        if(streams[stream])streams[stream]->Release();
        streams[stream]=buffer;
        streamOffsets[stream]=offset;streamStrides[stream]=stride;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream,IDirect3DVertexBuffer9** buffer,UINT* offset,UINT* stride) override {
        if(stream>=streamCount)return D3DERR_INVALIDCALL;
        if(buffer){*buffer=streams[stream];if(*buffer)(*buffer)->AddRef();}
        if(offset)*offset=streamOffsets[stream];
        if(stride)*stride=streamStrides[stream];
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT,UINT* divider) override {
        if(divider)*divider=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* buffer) override {
        if(buffer)buffer->AddRef();
        if(indices)indices->Release();
        indices=buffer;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=indices;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(!code)return D3DERR_INVALIDCALL;
        const unsigned words=bytecodeWords(code);
        if(!words)return D3DERR_INVALIDCALL;
        auto wrapper=d3dshim::create<PixelShader>();
        if(!wrapper)return E_OUTOFMEMORY;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->context=context();
        wrapper->bytecode=static_cast<DWORD*>(d3dshim::allocate(words*sizeof(DWORD)));
        if(!wrapper->bytecode){d3dshim::destroy(wrapper);return E_OUTOFMEMORY;}
        memcpy(wrapper->bytecode,code,words*sizeof(DWORD));
        wrapper->bytecodeBytes=words*sizeof(DWORD);
        HRESULT hr=translateShader(code,words,false,reinterpret_cast<void**>(&wrapper->shader));
        if(FAILED(hr)){d3dshim::destroy(wrapper);return hr;}
        wrapper->inner=wrapper->shader;
        *out=wrapper;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader) override {
        if(shader)shader->AddRef();
        if(pixelShader)pixelShader->Release();
        pixelShader=shader;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out) override {
        if(!out)return D3DERR_INVALIDCALL;
        *out=pixelShader;
        if(*out)(*out)->AddRef();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start,const float* data,UINT count) override {
        if(!data||start+count>maxPixelConst)return D3DERR_INVALIDCALL;
        memcpy(pixelConstF[start],data,count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT start,float* data,UINT count) override {
        if(!data||start+count>maxPixelConst)return D3DERR_INVALIDCALL;
        memcpy(data,pixelConstF[start],count*sizeof(float[4]));
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT,const int*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT,int*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT,const WINBOOL*,UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT,WINBOOL*,UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT,const float*,const D3DRECTPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT,const float*,const D3DTRIPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE,IDirect3DQuery9** out) override {
        if(out)*out=nullptr;
        d3dshim::noteOnce("D3D9 shim: queries are not implemented");
        return D3DERR_INVALIDCALL;
    }

    // ------------------------------------------------------------ IDirect3D9Ex
    HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT,UINT,float*,float*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9*,IDirect3DSurface9*,IDirect3DVertexBuffer9*,
                                           UINT,IDirect3DVertexBuffer9*,D3DCOMPOSERECTSOP,INT,INT) override {
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE PresentEx(const RECT* src,const RECT* dst,HWND override,const RGNDATA* dirty,DWORD) override {
        return Present(src,dst,override,dirty);
    }
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* priority) override {
        if(priority)*priority=0;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9**,UINT32) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override {
        if(latency)*latency=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT width,UINT height,D3DFORMAT format,
                                                   D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                   IDirect3DSurface9** out,HANDLE*,DWORD) override {
        return CreateRenderTarget(width,height,format,samples,quality,FALSE,out,nullptr);
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT width,UINT height,D3DFORMAT format,D3DPOOL pool,
                                                            IDirect3DSurface9** out,HANDLE*,DWORD) override {
        return CreateOffscreenPlainSurface(width,height,format,pool,out,nullptr);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT width,UINT height,D3DFORMAT format,
                                                          D3DMULTISAMPLE_TYPE samples,DWORD quality,WINBOOL,
                                                          IDirect3DSurface9** out,HANDLE*,DWORD) override {
        return CreateDepthStencilSurface(width,height,format,samples,quality,FALSE,out,nullptr);
    }
    HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS* parameters,D3DDISPLAYMODEEX*) override {
        if(!parameters)return D3DERR_INVALIDCALL;
        return rebuildSwapChain(parameters);
    }
    HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT index,D3DDISPLAYMODEEX* mode,D3DDISPLAYROTATION* rotation) override {
        if(!mode||index!=0)return D3DERR_INVALIDCALL;
        mode->Size=sizeof(*mode);
        mode->Width=present.BackBufferWidth;mode->Height=present.BackBufferHeight;
        mode->RefreshRate=0;mode->Format=present.BackBufferFormat;
        mode->ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
        if(rotation)*rotation=D3DDISPLAYROTATION_IDENTITY;
        return D3D_OK;
    }

    // ---------------------------------------------------------------- helpers
    HRESULT makeSurface(UINT width,UINT height,D3DFORMAT format,D3DMULTISAMPLE_TYPE samples,DWORD quality,
                        bool renderTarget,bool depthStencil,IDirect3DSurface9** out,
                        D3DPOOL pool=D3DPOOL_DEFAULT) {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        DWORD usage=renderTarget?D3DUSAGE_RENDERTARGET:(depthStencil?D3DUSAGE_DEPTHSTENCIL:0);
        ID3D11Texture2D* texture=nullptr;
        HRESULT hr=createTexture2D(width,height,1,usage,format,pool,&texture);
        if(FAILED(hr))return hr;
        auto surface=d3dshim::create<Surface>();
        if(!surface){texture->Release();return E_OUTOFMEMORY;}
        surface->texture=texture;
        surface->inner=texture;
        surface->device=static_cast<IDirect3DDevice9*>(self);
        surface->context=context();
        surface->cpuVisible=usageFor(pool,usage)!=D3D11_USAGE_DEFAULT;
        surface->desc.Format=format;
        surface->desc.Type=D3DRTYPE_SURFACE;
        surface->desc.Usage=usage;
        surface->desc.Pool=pool;
        surface->desc.MultiSampleType=samples;
        surface->desc.MultiSampleQuality=quality;
        surface->desc.Width=width;
        surface->desc.Height=height;
        if(renderTarget) {
            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format=dxgiFormat(format);
            view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            hr=device()->CreateRenderTargetView(texture,&view,&surface->rtv);
            if(FAILED(hr)){d3dshim::destroy(surface);return hr;}
        }
        if(depthStencil) {
            D3D11_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format=dxgiFormat(format);
            view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            hr=device()->CreateDepthStencilView(texture,&view,&surface->dsv);
            if(FAILED(hr)){d3dshim::destroy(surface);return hr;}
        }
        *out=surface;
        return D3D_OK;
    }
    // The swap chain is rebuilt on Reset: D3D9 requires the guest to release its
    // default-pool resources first, and the CoreWindow adapter has no
    // ResizeBuffers path of its own, so a fresh chain is the honest
    // implementation.
    HRESULT rebuildSwapChain(D3DPRESENT_PARAMETERS* parameters) {
        if(swapChain){swapChain->Release();swapChain=nullptr;}
        renderTarget.clear();depthStencil.clear();
        HRESULT hr=createSwapChain(parameters);
        if(FAILED(hr))return hr;
        present=*parameters;
        return D3D_OK;
    }
    HRESULT createSwapChain(D3DPRESENT_PARAMETERS* parameters) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width=parameters->BackBufferWidth;
        desc.BufferDesc.Height=parameters->BackBufferHeight;
        desc.BufferDesc.Format=dxgiFormat(parameters->BackBufferFormat);
        desc.BufferDesc.RefreshRate.Numerator=0;
        desc.BufferDesc.RefreshRate.Denominator=1;
        desc.SampleDesc.Count=parameters->MultiSampleType?UINT(parameters->MultiSampleType):1;
        desc.SampleDesc.Quality=parameters->MultiSampleQuality;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=parameters->BackBufferCount?parameters->BackBufferCount:1;
        desc.OutputWindow=parameters->hDeviceWindow?parameters->hDeviceWindow:creation.hFocusWindow;
        desc.Windowed=TRUE;   // the CoreWindow adapter has no exclusive mode
        desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        if(desc.BufferDesc.Format==DXGI_FORMAT_UNKNOWN) {
            d3dshim::noteOnce("D3D9 shim: back buffer format has no DXGI equivalent");
            return D3DERR_INVALIDCALL;
        }
        // The device's own adapter factory, exactly as the D3D10 shim does it:
        // Play redirects this factory's CreateSwapChain for a guest window to
        // the validated CoreWindow adapter.
        IDXGIDevice* dxgi=nullptr;
        IDXGIAdapter* adapter=nullptr;
        IDXGIFactory* local=nullptr;
        HRESULT hr=device()->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgi));
        if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&adapter);
        if(dxgi)dxgi->Release();
        if(SUCCEEDED(hr))hr=adapter->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(&local));
        if(adapter)adapter->Release();
        if(FAILED(hr))return D3DERR_INVALIDCALL;
        IDXGISwapChain* chain=nullptr;
        hr=d3dshim::createSwapChain(local,device(),&desc,&chain);
        local->Release();
        if(FAILED(hr)) {
            d3dshim::Line line("D3D9 shim: CreateSwapChain failed hr=");
            line.hex((unsigned long)hr);line.emit();
            return D3DERR_INVALIDCALL;
        }
        auto wrapper=d3dshim::create<SwapChain>();
        if(!wrapper){chain->Release();return E_OUTOFMEMORY;}
        wrapper->chain=chain;
        wrapper->device=static_cast<IDirect3DDevice9*>(self);
        wrapper->context=context();
        wrapper->present=*parameters;
        // The back buffer is the device's first render target, as on Windows.
        ID3D11Texture2D* texture=nullptr;
        hr=chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
        if(SUCCEEDED(hr)&&texture) {
            auto surface=d3dshim::create<Surface>();
            if(surface) {
                surface->texture=texture;
                surface->inner=texture;
                surface->device=static_cast<IDirect3DDevice9*>(self);
                surface->context=context();
                surface->desc.Format=parameters->BackBufferFormat;
                surface->desc.Type=D3DRTYPE_SURFACE;
                surface->desc.Usage=D3DUSAGE_RENDERTARGET;
                surface->desc.Pool=D3DPOOL_DEFAULT;
                surface->desc.MultiSampleType=parameters->MultiSampleType;
                surface->desc.MultiSampleQuality=parameters->MultiSampleQuality;
                D3D11_TEXTURE2D_DESC got{};
                texture->GetDesc(&got);
                surface->desc.Width=got.Width;
                surface->desc.Height=got.Height;
                D3D11_RENDER_TARGET_VIEW_DESC view{};
                view.Format=got.Format;
                view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
                if(SUCCEEDED(device()->CreateRenderTargetView(texture,&view,&surface->rtv))) {
                    wrapper->backBuffer=surface;
                    renderTarget.set(0,surface);
                    context()->OMSetRenderTargets(1,&surface->rtv,nullptr);
                } else {
                    d3dshim::destroy(surface);
                }
            } else {
                texture->Release();
            }
        }
        swapChain=wrapper;
        // EnableAutoDepthStencil makes the device create and bind a depth
        // surface of its own, as D3D9 documents. It is not a guest-held object,
        // so the device owns the one reference.
        if(parameters->EnableAutoDepthStencil) {
            IDirect3DSurface9* autoDepth=nullptr;
            if(SUCCEEDED(makeSurface(desc.BufferDesc.Width,desc.BufferDesc.Height,
                    parameters->AutoDepthStencilFormat,D3DMULTISAMPLE_NONE,0,false,true,&autoDepth))) {
                depthStencil.set(0,autoDepth);
                autoDepth->Release();   // the Held slot owns it now
                ID3D11RenderTargetView* rt=currentTarget();
                context()->OMSetRenderTargets(rt?1:0,rt?&rt:nullptr,static_cast<Surface*>(autoDepth)->dsv);
            }
        }
        return D3D_OK;
    }
};
}

namespace {
// -------------------------------------------------------------------- factory
struct Factory : d3dshim::Base, IDirect3D9Ex {
    Factory() { self=static_cast<IDirect3D9Ex*>(this);
        D3DSHIM_IDS(&__uuidof(IDirect3D9),&__uuidof(IDirect3D9Ex)); }
    ~Factory() {
        if(factory)factory->Release();
    }
    D3DSHIM_REF
    D3DSHIM_QI
    IDXGIFactory* factory=nullptr;
    HRESULT init() {
        if(factory)return S_OK;
        return d3dshim::factory(&factory);
    }
    // The console exposes one adapter; the CoreWindow adapter backs it.
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter,DWORD,D3DADAPTER_IDENTIFIER9* identifier) override {
        if(!identifier||adapter!=0)return D3DERR_INVALIDCALL;
        *identifier=D3DADAPTER_IDENTIFIER9{};
        copyText(identifier->Driver,sizeof identifier->Driver,"Play D3D9 shim");
        copyText(identifier->Description,sizeof identifier->Description,"Play D3D9 shim");
        copyText(identifier->DeviceName,sizeof identifier->DeviceName,"Console CoreWindow adapter");
        identifier->DriverVersion.QuadPart=0x00010000;
        identifier->VendorId=0x1414;   // Microsoft
        identifier->DeviceId=0x0001;
        identifier->WHQLLevel=1;
        return D3D_OK;
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter,D3DFORMAT format) override {
        if(adapter!=0)return 0;
        return dxgiFormat(format)==DXGI_FORMAT_UNKNOWN?0:1;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter,D3DFORMAT format,UINT index,D3DDISPLAYMODE* mode) override {
        if(adapter!=0||index!=0||!mode)return D3DERR_INVALIDCALL;
        if(dxgiFormat(format)==DXGI_FORMAT_UNKNOWN)return D3DERR_INVALIDCALL;
        mode->Width=1920;mode->Height=1080;mode->RefreshRate=60;mode->Format=format;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter,D3DDISPLAYMODE* mode) override {
        if(adapter!=0||!mode)return D3DERR_INVALIDCALL;
        mode->Width=1920;mode->Height=1080;mode->RefreshRate=60;mode->Format=D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter,D3DDEVTYPE type,D3DFORMAT displayFormat,
                                              D3DFORMAT backBufferFormat,WINBOOL) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(dxgiFormat(displayFormat)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        if(dxgiFormat(backBufferFormat)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter,D3DDEVTYPE type,D3DFORMAT,DWORD usage,
                                                D3DRESOURCETYPE,D3DFORMAT format) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        // A render target is a texture the shim can make a render-target view
        // for, which is any format with a DXGI equivalent (CreateTexture builds
        // the view on demand, see Texture::GetSurfaceLevel).
        return dxgiFormat(format)==DXGI_FORMAT_UNKNOWN?D3DERR_NOTAVAILABLE:D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter,D3DDEVTYPE type,D3DFORMAT format,WINBOOL,
                                                         D3DMULTISAMPLE_TYPE samples,DWORD* quality) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(dxgiFormat(format)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        // The CoreWindow adapter has no resolve layer, so only single-sample is
        // offered; the swap chain refuses anything else too.
        if(samples!=D3DMULTISAMPLE_NONE)return D3DERR_NOTAVAILABLE;
        if(quality)*quality=1;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter,D3DDEVTYPE type,D3DFORMAT,D3DFORMAT,
                                                     D3DFORMAT dsFormat) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        return depthFormat(dsFormat)?D3D_OK:D3DERR_NOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter,D3DDEVTYPE type,D3DFORMAT src,
                                                          D3DFORMAT dst) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL)return D3DERR_INVALIDCALL;
        if(dxgiFormat(src)==DXGI_FORMAT_UNKNOWN||dxgiFormat(dst)==DXGI_FORMAT_UNKNOWN)return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter,D3DDEVTYPE type,D3DCAPS9* caps) override {
        if(adapter!=0||type!=D3DDEVTYPE_HAL||!caps)return D3DERR_INVALIDCALL;
        fillCaps(caps,type,adapter);
        return D3D_OK;
    }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return nullptr; }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
                                           D3DPRESENT_PARAMETERS* parameters,IDirect3DDevice9** out) override {
        return createDevice(adapter,type,focus,flags,parameters,reinterpret_cast<void**>(out));
    }

    // ------------------------------------------------------------ IDirect3D9Ex
    UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT adapter,const D3DDISPLAYMODEFILTER* filter) override {
        return GetAdapterModeCount(adapter,filter?filter->Format:D3DFMT_X8R8G8B8);
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT adapter,const D3DDISPLAYMODEFILTER* filter,UINT index,
                                                 D3DDISPLAYMODEEX* mode) override {
        if(!mode)return D3DERR_INVALIDCALL;
        D3DDISPLAYMODE plain{};
        HRESULT hr=EnumAdapterModes(adapter,filter?filter->Format:D3DFMT_X8R8G8B8,index,&plain);
        if(FAILED(hr))return hr;
        mode->Size=sizeof(*mode);mode->Width=plain.Width;mode->Height=plain.Height;
        mode->RefreshRate=plain.RefreshRate;mode->Format=plain.Format;
        mode->ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT adapter,D3DDISPLAYMODEEX* mode,
                                                      D3DDISPLAYROTATION* rotation) override {
        if(!mode)return D3DERR_INVALIDCALL;
        D3DDISPLAYMODE plain{};
        HRESULT hr=GetAdapterDisplayMode(adapter,&plain);
        if(FAILED(hr))return hr;
        mode->Size=sizeof(*mode);mode->Width=plain.Width;mode->Height=plain.Height;
        mode->RefreshRate=plain.RefreshRate;mode->Format=plain.Format;
        mode->ScanLineOrdering=D3DSCANLINEORDERING_PROGRESSIVE;
        if(rotation)*rotation=D3DDISPLAYROTATION_IDENTITY;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
                                             D3DPRESENT_PARAMETERS* parameters,D3DDISPLAYMODEEX*,
                                             IDirect3DDevice9Ex** out) override {
        return createDevice(adapter,type,focus,flags,parameters,reinterpret_cast<void**>(out));
    }
    HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT adapter,LUID* luid) override {
        if(!luid||adapter!=0)return D3DERR_INVALIDCALL;
        if(FAILED(init()))return D3DERR_NOTAVAILABLE;
        IDXGIAdapter* dxgiAdapter=nullptr;
        if(FAILED(factory->EnumAdapters(adapter,&dxgiAdapter))||!dxgiAdapter)return D3DERR_NOTAVAILABLE;
        DXGI_ADAPTER_DESC desc{};
        HRESULT hr=dxgiAdapter->GetDesc(&desc);
        dxgiAdapter->Release();
        if(FAILED(hr))return D3DERR_NOTAVAILABLE;
        *luid=desc.AdapterLuid;
        return D3D_OK;
    }

    HRESULT createDevice(UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
                         D3DPRESENT_PARAMETERS* parameters,void** out) {
        if(!out)return D3DERR_INVALIDCALL;
        *out=nullptr;
        if(adapter!=0||!parameters)return D3DERR_INVALIDCALL;
        if(type!=D3DDEVTYPE_HAL) {
            d3dshim::noteOnce("D3D9 shim: only D3DDEVTYPE_HAL is available; REF and NULLREF are refused");
            return D3DERR_INVALIDCALL;
        }
        if(FAILED(init()))return D3DERR_NOTAVAILABLE;
        auto device=d3dshim::create<Device>();
        if(!device)return E_OUTOFMEMORY;
        static const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,D3D_FEATURE_LEVEL_9_3};
        HRESULT hr=device->host.create(nullptr,D3D_DRIVER_TYPE_HARDWARE,levels,4,0);
        if(FAILED(hr)){d3dshim::destroy(device);return D3DERR_NOTAVAILABLE;}
        // The 1x1 white view an unbound texture stage samples (syncTextures).
        D3D11_TEXTURE2D_DESC white{};
        white.Width=white.Height=1;white.MipLevels=white.ArraySize=1;
        white.Format=DXGI_FORMAT_B8G8R8A8_UNORM;white.SampleDesc.Count=1;
        white.Usage=D3D11_USAGE_DEFAULT;white.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const UINT32 opaque=0xFFFFFFFF;
        D3D11_SUBRESOURCE_DATA pixel{};
        pixel.pSysMem=&opaque;pixel.SysMemPitch=sizeof opaque;
        ID3D11Texture2D* whiteTexture=nullptr;
        if(SUCCEEDED(device->host.device->CreateTexture2D(&white,&pixel,&whiteTexture))) {
            device->host.device->CreateShaderResourceView(whiteTexture,nullptr,&device->defaultView);
            whiteTexture->Release();
        }
        if(!device->defaultView) {
            d3dshim::noteOnce("D3D9 shim: could not create the white texture an unbound stage samples");
            d3dshim::destroy(device);
            return D3DERR_NOTAVAILABLE;
        }
        device->creation.AdapterOrdinal=adapter;
        device->creation.DeviceType=type;
        device->creation.hFocusWindow=focus;
        device->creation.BehaviorFlags=flags;
        device->deviceType=type;
        device->parent=this;
        AddRef();   // the device holds the factory it came from
        hr=device->createSwapChain(parameters);
        if(FAILED(hr)) {
            d3dshim::destroy(device);   // releases the factory reference above
            return hr;
        }
        device->present=*parameters;
        *out=device;
        return D3D_OK;
    }
};
}

extern "C" {
// ------------------------------------------------------------------- d3d9.dll
__declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    if(sdk!=D3D_SDK_VERSION) {
        d3dshim::noteOnce("D3D9 shim: Direct3DCreate9 refused an SDK version other than D3D_SDK_VERSION");
        return nullptr;
    }
    auto factory=d3dshim::create<Factory>();
    if(!factory)return nullptr;
    if(FAILED(factory->init())){d3dshim::destroy(factory);return nullptr;}
    return static_cast<IDirect3D9*>(static_cast<IDirect3D9Ex*>(factory));
}
__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex** out) {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(sdk!=D3D_SDK_VERSION)return D3DERR_INVALIDCALL;
    auto factory=d3dshim::create<Factory>();
    if(!factory)return E_OUTOFMEMORY;
    if(FAILED(factory->init())){d3dshim::destroy(factory);return D3DERR_NOTAVAILABLE;}
    *out=factory;
    return D3D_OK;
}
// The D3DPERF family is the PIX instrumentation API; a release runtime answers
// without recording, which is what Wine's does too.
__declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus() { return 0; }
__declspec(dllexport) WINBOOL WINAPI D3DPERF_QueryRepeatFrame() { return FALSE; }
__declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD) {}
__declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR,const wchar_t*) { return 0; }
__declspec(dllexport) int WINAPI D3DPERF_EndEvent() { return 0; }
__declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR,const wchar_t*) {}
__declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR,const wchar_t*) {}
__declspec(dllexport) void WINAPI DebugSetLevel(DWORD) {}
__declspec(dllexport) void WINAPI DebugSetMute() {}
__declspec(dllexport) void* WINAPI Direct3DShaderValidatorCreate9() { return nullptr; }
}
