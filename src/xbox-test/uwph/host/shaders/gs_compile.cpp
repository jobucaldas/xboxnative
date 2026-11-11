// Off-device only: mingw g++ gs_compile.cpp -ld3dcompiler -o gs-compile.exe;
// run under Wine from this directory. No compiler is shipped in the package.
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
int main() {
 ID3DBlob *code=nullptr,*errors=nullptr;
 HRESULT hr=D3DCompileFromFile(L"gs_point.hlsl",nullptr,nullptr,"gs_main",
   "gs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
 if(errors) { fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr); errors->Release(); }
 if(FAILED(hr)) return 1;
 FILE *f=fopen("triangle_gs.dxbc","wb"); if(!f) return 2;
 fwrite(code->GetBufferPointer(),1,code->GetBufferSize(),f); fclose(f); code->Release();
 return 0;
}
