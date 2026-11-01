#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto adjust=reinterpret_cast<decltype(&AdjustWindowRectEx)>(GetProcAddress(module,"AdjustWindowRectEx")); CHECK(adjust);
 unsigned count=0;
 for(DWORD style : {DWORD(0),DWORD(WS_BORDER),DWORD(WS_CAPTION),DWORD(WS_THICKFRAME),DWORD(WS_CAPTION|WS_THICKFRAME)})
 for(DWORD ex : {DWORD(0),DWORD(WS_EX_STATICEDGE),DWORD(WS_EX_CLIENTEDGE),DWORD(WS_EX_DLGMODALFRAME)})
 for(BOOL menu : {FALSE,TRUE}) {
  RECT base{10,20,110,120},iconic=base,reference=base,reference_iconic=base;
  CHECK(adjust(&base,style,menu,ex));
  SetLastError(0xdead); CHECK(adjust(&iconic,style|WS_MINIMIZE,menu,ex)); CHECK(GetLastError()==0xdead);
  CHECK(AdjustWindowRectEx(&reference,style,menu,ex));
  CHECK(AdjustWindowRectEx(&reference_iconic,style|WS_MINIMIZE,menu,ex));
  // WS_MINIMIZE does not change requested nonclient geometry in Wine.
  CHECK(iconic.left-base.left==reference_iconic.left-reference.left);
  CHECK(iconic.top-base.top==reference_iconic.top-reference.top);
  CHECK(iconic.right-base.right==reference_iconic.right-reference.right);
  CHECK(iconic.bottom-base.bottom==reference_iconic.bottom-reference.bottom);
  ++count;
 }
 printf("PASS: %u minimized style/menu combinations; Wine delta comparisons (off-device)\n",count);
}
