// Off-device only: compile the D3D10 fixture's SM4 shaders with the host's
// d3dcompiler under Wine. Writes d3d10_vs.dxbc, d3d10_ps.dxbc, d3d10_gs.dxbc.
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
static const char* source =
"struct VSIO { float4 position : SV_POSITION; float4 color : COLOR; };\n"
"VSIO vs_main(float2 position : POSITION, float4 color : COLOR) {\n"
"    VSIO v; v.position = float4(position, 0.0, 1.0); v.color = color; return v;\n"
"}\n"
"struct GSIO { float4 position : SV_POSITION; float4 color : COLOR; };\n"
"[maxvertexcount(3)]\n"
"void gs_main(point GSIO vin[1], inout TriangleStream<GSIO> stream) {\n"
"    GSIO o; o.color = vin[0].color;\n"
"    o.position = float4(vin[0].position.x,       vin[0].position.y + 0.4, 0.0, 1.0); stream.Append(o);\n"
"    o.position = float4(vin[0].position.x + 0.4, vin[0].position.y - 0.4, 0.0, 1.0); stream.Append(o);\n"
"    o.position = float4(vin[0].position.x - 0.4, vin[0].position.y - 0.4, 0.0, 1.0); stream.Append(o);\n"
"}\n"
"float4 ps_main(VSIO v) : SV_TARGET { return v.color; }\n";
int main() {
    ID3DBlob* code=nullptr; ID3DBlob* errors=nullptr;
    HRESULT hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"vs_main","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors){fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr))return 1;
    FILE* f=fopen("d3d10_vs.dxbc","wb");fwrite(code->GetBufferPointer(),1,code->GetBufferSize(),f);fclose(f);code->Release();
    hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"ps_main","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors){fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr))return 1;
    f=fopen("d3d10_ps.dxbc","wb");fwrite(code->GetBufferPointer(),1,code->GetBufferSize(),f);fclose(f);code->Release();
    hr=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"gs_main","gs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors){fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}
    if(FAILED(hr))return 1;
    f=fopen("d3d10_gs.dxbc","wb");fwrite(code->GetBufferPointer(),1,code->GetBufferSize(),f);fclose(f);code->Release();
    return 0;
}
