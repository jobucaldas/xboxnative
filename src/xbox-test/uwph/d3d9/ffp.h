// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Fixed-function shader synthesis for Play's D3D9 shim.
//
// D3D9 has no fixed-function hardware: the runtime builds a vertex and a pixel
// shader from the transform, material and texture-stage state and runs them.
// The shim does the same, emitting shader model 3.0 tokens that the device
// hands to the same D3D9->DXBC translator a game's own shader goes through
// (src/tools/build-d3d9-translator.sh), so there is one shader path in the shim
// rather than two.
//
// The constant layout is the shim's own, because the shim also fills the
// buffer; a game's own constants are never read by these shaders:
//   vertex   c0..c3  world x view x projection, one row per register
//            c4      material diffuse
//            c5      viewport fixup (2/W, -2/H, 1, 1), for POSITIONT
//            c6      viewport offset (-1, 1, 0, 0), for POSITIONT
//   pixel    c0      (1,1,1,1), for the complement modifier
//            c1      D3DRS_TEXTUREFACTOR
//
// Lighting is not synthesised: the shim reports D3DRS_LIGHTING once and renders
// unlit, which is what the milestone's decision record scopes out.
namespace ffp {

// One vertex input as the declaration or FVF describes it, with the register
// the generated shader reads it from. The caller builds the D3D11 input layout
// from the same list, so the two always agree.
struct Input {
    D3DDECLUSAGE usage;
    unsigned index;
    unsigned reg;
};

struct VertexOptions {
    Input inputs[16];
    unsigned inputCount;        // inputs[0] is the POSITION/POSITIONT input
    bool positionTransformed;   // D3DDECLUSAGE_POSITIONT: already in screen space
    int colourReg;              // the COLOR input's register, or -1
    int texcoordReg[8];         // the TEXCOORD input registers, or -1
    unsigned texcoordCount;
};

struct Stage {
    DWORD colorOp,colorArg1,colorArg2;
    DWORD alphaOp,alphaArg1,alphaArg2;
};

struct PixelOptions {
    Stage stages[8];
    unsigned stageCount;
    int texcoordReg[8];         // the TEXCOORD input registers, or -1
    unsigned texcoordCount;
};

// D3D9 token builders. The instruction length in bits 24-27 is the operand
// count: without it the translator reads the next token as an operand.
struct Bytecode {
    DWORD* out;
    unsigned capacity;
    unsigned count=0;
    bool overflow=false;
    Bytecode(DWORD* buffer,unsigned words) : out(buffer),capacity(words) {}
    void push(DWORD token) {
        if(count>=capacity){overflow=true;return;}
        out[count++]=token;
    }
    void op(DWORD opcode,unsigned operands) { push(opcode|(operands<<24)); }
    static DWORD dst(unsigned reg,DWORD type,DWORD mask=0xF) {
        return (reg&0x7FF)|((type&7)<<28)|(((type>>3)&3)<<11)|((mask&0xF)<<16);
    }
    static DWORD src(unsigned reg,DWORD type,DWORD swizzle=0xE4) {
        return (reg&0x7FF)|((type&7)<<28)|(((type>>3)&3)<<11)|((swizzle&0xFF)<<16);
    }
    static DWORD usage(D3DDECLUSAGE use,unsigned index) { return DWORD(use)|(index<<16); }
};

// D3D9 register types (d3d9types.h D3DSHADER_PARAM_REGISTER_TYPE).
enum { RegTemp=0,RegInput=1,RegConst=2,RegOutput=6,RegSampler=10 };
// D3D9 opcodes used here.
enum {
    OpMov=1,OpAdd=2,OpSub=3,OpMul=5,OpDp4=9,OpDcl=31,OpTex=66,OpEnd=0xFFFF
};
// The shader version tokens: vs_3_0 and ps_3_0.
enum { Vs30=0xFFFE0300u,Ps30=0xFFFF0300u };
// D3DSTT_2D, the texture type a sampler declaration carries.
enum { Texture2D=2u<<27 };

inline void dcl(Bytecode& b,D3DDECLUSAGE use,unsigned index,unsigned reg,DWORD type) {
    b.op(OpDcl,2);
    b.push(Bytecode::usage(use,index));
    b.push(Bytecode::dst(reg,type));
}

// Writes a vs_3_0 that transforms the position and passes the colour and
// texture coordinates through. Returns the token count, or 0 when the buffer
// is too small (a truncated shader is never returned).
inline unsigned writeVertexShader(const VertexOptions& options,DWORD* out,unsigned capacity) {
    Bytecode b(out,capacity);
    b.push(Vs30);
    for(unsigned i=0;i<options.inputCount;++i)
        dcl(b,options.inputs[i].usage,options.inputs[i].index,options.inputs[i].reg,RegInput);
    dcl(b,D3DDECLUSAGE_POSITION,0,0,RegOutput);
    dcl(b,D3DDECLUSAGE_COLOR,0,1,RegOutput);
    for(unsigned i=0;i<options.texcoordCount;++i)
        dcl(b,D3DDECLUSAGE_TEXCOORD,i,2+i,RegOutput);
    // inputs[0] is the position the caller put first.
    const unsigned positionReg=options.inputCount?options.inputs[0].reg:0;
    if(options.positionTransformed) {
        // D3D9's POSITIONT is already in screen space, but D3D11's SV_Position
        // is clip space: the viewport transform D3D9 would have applied is done
        // here, from c5/c6, or every vertex lands outside the guard band.
        // Outputs are write-only in SM3: the fixup goes through a temp
        // register, never reading oPos back as a source.
        b.op(OpMul,3);b.push(Bytecode::dst(0,RegTemp,1));
        b.push(Bytecode::src(positionReg,RegInput,0x00));b.push(Bytecode::src(5,RegConst,0x00));
        b.op(OpAdd,3);b.push(Bytecode::dst(0,RegOutput,1));
        b.push(Bytecode::src(0,RegTemp,0x00));b.push(Bytecode::src(6,RegConst,0x00));
        b.op(OpMul,3);b.push(Bytecode::dst(0,RegTemp,2));
        b.push(Bytecode::src(positionReg,RegInput,0x55));b.push(Bytecode::src(5,RegConst,0x55));
        b.op(OpAdd,3);b.push(Bytecode::dst(0,RegOutput,2));
        b.push(Bytecode::src(0,RegTemp,0x55));b.push(Bytecode::src(6,RegConst,0x55));
        b.op(OpMov,2);b.push(Bytecode::dst(0,RegOutput,0xC));
        b.push(Bytecode::src(positionReg,RegInput,0xEE));
    } else {
        // Four masked dp4 rows rather than m4x4: the donor's matrix opcodes
        // write the whole destination on every column, so only the last row
        // would survive.
        for(unsigned row=0;row<4;++row) {
            b.op(OpDp4,3);
            b.push(Bytecode::dst(0,RegOutput,1u<<row));
            b.push(Bytecode::src(positionReg,RegInput));
            b.push(Bytecode::src(row,RegConst));
        }
    }
    b.op(OpMov,2);b.push(Bytecode::dst(1,RegOutput));
    // With lighting off and no vertex colour the material diffuse stands in,
    // which is what D3D9's own fixed-function shader does.
    b.push(options.colourReg>=0?Bytecode::src(unsigned(options.colourReg),RegInput)
                               :Bytecode::src(4,RegConst));
    for(unsigned i=0;i<options.texcoordCount;++i) {
        b.op(OpMov,2);b.push(Bytecode::dst(2+i,RegOutput));
        b.push(Bytecode::src(unsigned(options.texcoordReg[i]),RegInput));
    }
    b.push(OpEnd);
    return b.overflow?0:b.count;
}

// D3DTA_* argument selectors.
enum {
    TaDiffuse=0,TaCurrent=1,TaTexture=2,TaTFactor=3,TaSpecular=4,TaTemp=5,TaConstant=6,
    TaSelectMask=0xF,TaComplement=0x10,TaAlphaReplicate=0x20
};
// D3DTOP_* texture-stage operations.
enum {
    TopDisable=1,TopSelectArg1=2,TopSelectArg2=3,TopModulate=4,TopModulate2x=5,
    TopModulate4x=6,TopAdd=7,TopSubtract=10
};

// Scratch registers the generated pixel shader uses.
enum { PsTexture=0,PsCurrent=1,PsArg1=2,PsArg2=3 };
// Pixel constants the shim fills before every draw.
enum { PcOne=0,PcTFactor=1 };
// The pixel shader's COLOR0 input register.
enum { PsColourReg=8 };

// Emits the instruction that loads one D3DTA argument into `reg`, applying the
// complement and alpha-replicate modifiers.
inline void argument(Bytecode& b,DWORD selector,unsigned reg,unsigned stage) {
    const DWORD base=selector&TaSelectMask;
    switch(base) {
    case TaTexture:
        b.op(OpMov,2);b.push(Bytecode::dst(reg,RegTemp));
        b.push(Bytecode::src(PsTexture,RegTemp));
        break;
    case TaCurrent:
        // Stage 0's "current" is the diffuse colour, as D3D9 defines it.
        b.op(OpMov,2);b.push(Bytecode::dst(reg,RegTemp));
        b.push(stage?Bytecode::src(PsCurrent,RegTemp)
                    :Bytecode::src(PsColourReg,RegInput));
        break;
    case TaTFactor:
        b.op(OpMov,2);b.push(Bytecode::dst(reg,RegTemp));
        b.push(Bytecode::src(PcTFactor,RegConst));
        break;
    case TaSpecular:
    case TaDiffuse:
    default:
        b.op(OpMov,2);b.push(Bytecode::dst(reg,RegTemp));
        b.push(Bytecode::src(PsColourReg,RegInput));
        break;
    }
    if(selector&TaComplement) {
        // 1-x as add with a negated second source, for the same reason as
        // TopSubtract: the translator's sub path negates both sources.
        b.op(OpAdd,3);b.push(Bytecode::dst(reg,RegTemp));
        b.push(Bytecode::src(PcOne,RegConst));
        b.push(Bytecode::src(reg,RegTemp)|0x01000000u);
    }
    if(selector&TaAlphaReplicate) {
        // xxxx: every channel takes the alpha.
        b.op(OpMov,2);b.push(Bytecode::dst(reg,RegTemp));
        b.push(Bytecode::src(reg,RegTemp,0xFF));
    }
}

// Emits one stage's operation into `mask` of the current register.
inline void operation(Bytecode& b,DWORD op,DWORD arg1,DWORD arg2,unsigned stage,DWORD mask) {
    argument(b,arg1,PsArg1,stage);
    if(op!=TopSelectArg1)argument(b,arg2,PsArg2,stage);
    switch(op) {
    case TopSelectArg1:
        b.op(OpMov,2);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsArg1,RegTemp));
        break;
    case TopSelectArg2:
        b.op(OpMov,2);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsArg2,RegTemp));
        break;
    case TopAdd:
        b.op(OpAdd,3);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsArg1,RegTemp));b.push(Bytecode::src(PsArg2,RegTemp));
        break;
    case TopSubtract:
        // D3D9's sub is add with a negated second source (D3DSPSM_NEG in
        // bits 24-27). Emitting the add directly keeps the translator from
        // touching its own sub path, which negates both sources (the
        // console rendered every OpSub black).
        b.op(OpAdd,3);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsArg1,RegTemp));
        b.push(Bytecode::src(PsArg2,RegTemp)|0x01000000u);
        break;
    case TopModulate2x:
    case TopModulate4x:
        b.op(OpMul,3);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsArg1,RegTemp));b.push(Bytecode::src(PsArg2,RegTemp));
        b.op(OpAdd,3);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsCurrent,RegTemp));b.push(Bytecode::src(PsCurrent,RegTemp));
        if(op==TopModulate4x) {
            b.op(OpAdd,3);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
            b.push(Bytecode::src(PsCurrent,RegTemp));b.push(Bytecode::src(PsCurrent,RegTemp));
        }
        break;
    case TopModulate:
    default:
        b.op(OpMul,3);b.push(Bytecode::dst(PsCurrent,RegTemp,mask));
        b.push(Bytecode::src(PsArg1,RegTemp));b.push(Bytecode::src(PsArg2,RegTemp));
        break;
    }
}

// True when one of a stage's arguments names the stage's texture. The mask
// strips the complement and alpha-replicate modifiers, which do not change
// what the argument reads.
inline bool stageUsesTexture(const Stage& stage) {
    return (stage.colorArg1&TaSelectMask)==TaTexture || (stage.colorArg2&TaSelectMask)==TaTexture
        || (stage.alphaArg1&TaSelectMask)==TaTexture || (stage.alphaArg2&TaSelectMask)==TaTexture;
}

// Writes a ps_3_0 that samples the stages' textures and combines them. A stage
// whose arguments never name D3DTA_TEXTURE gets no sampler declaration and no
// tex instruction, which is what D3D9 does for a stage with no texture bound;
// declaring and sampling one anyway hands the console's d3d11on12 a resource
// nothing binds, and its shader translation derefs null. Returns the token
// count, or 0 when the buffer is too small.
inline unsigned writePixelShader(const PixelOptions& options,DWORD* out,unsigned capacity) {
    Bytecode b(out,capacity);
    b.push(Ps30);
    for(unsigned i=0;i<options.texcoordCount;++i)
        dcl(b,D3DDECLUSAGE_TEXCOORD,i,unsigned(options.texcoordReg[i]),RegInput);
    dcl(b,D3DDECLUSAGE_COLOR,0,PsColourReg,RegInput);
    dcl(b,D3DDECLUSAGE_COLOR,0,0,RegOutput);
    // Per-stage emission: a stage that never names its texture gets no sampler
    // declaration and no tex instruction. The dcl loop runs first because the
    // declarations must precede the instructions that use them.
    for(unsigned i=0;i<options.stageCount;++i) {
        if(!stageUsesTexture(options.stages[i]))continue;
        b.op(OpDcl,2);
        b.push(Texture2D);
        b.push(Bytecode::dst(i,RegSampler));
    }
    // Stage 0 starts from the diffuse colour, which is D3D9's "current" there.
    b.op(OpMov,2);b.push(Bytecode::dst(PsCurrent,RegTemp));
    b.push(Bytecode::src(PsColourReg,RegInput));
    for(unsigned i=0;i<options.stageCount;++i) {
        const Stage& stage=options.stages[i];
        if(stage.colorOp==TopDisable)break;
        if(stageUsesTexture(stage)) {
            b.op(OpTex,3);
            b.push(Bytecode::dst(PsTexture,RegTemp));
            b.push(i<options.texcoordCount?Bytecode::src(unsigned(options.texcoordReg[i]),RegInput)
                                          :Bytecode::src(PsTexture,RegTemp));
            b.push(Bytecode::src(i,RegSampler));
        }
        operation(b,stage.colorOp,stage.colorArg1,stage.colorArg2,i,0x7);
        if(stage.alphaOp!=TopDisable)
            operation(b,stage.alphaOp,stage.alphaArg1,stage.alphaArg2,i,0x8);
    }
    b.op(OpMov,2);b.push(Bytecode::dst(0,RegOutput));
    b.push(Bytecode::src(PsCurrent,RegTemp));
    b.push(OpEnd);
    return b.overflow?0:b.count;
}

}
