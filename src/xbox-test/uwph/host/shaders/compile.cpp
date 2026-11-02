// Off-device only: mingw g++ compile.cpp -ld3dcompiler -o compile.exe;
// run under Wine from this directory. No compiler is shipped in the package.
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
int main() {
 for (int i=0;i<2;++i) {
  ID3DBlob *code=nullptr,*errors=nullptr;
  HRESULT hr=D3DCompileFromFile(L"triangle.hlsl",nullptr,nullptr,i?"ps_main":"vs_main",
    i?"ps_5_0":"vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
  if(errors) { fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr); errors->Release(); }
  if(FAILED(hr)) return 1;
  FILE *f=fopen(i?"triangle_ps.dxbc":"triangle_vs.dxbc","wb"); if(!f) return 2;
  fwrite(code->GetBufferPointer(),1,code->GetBufferSize(),f); fclose(f); code->Release();
 }
}
