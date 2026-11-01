#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto adjust=reinterpret_cast<decltype(&AdjustWindowRectEx)>(GetProcAddress(module,"AdjustWindowRectEx")); CHECK(adjust);
 unsigned count=0;
 for(DWORD style : {DWORD(0),DWORD(WS_BORDER),DWORD(WS_DLGFRAME),DWORD(WS_CAPTION),DWORD(WS_THICKFRAME),DWORD(WS_CAPTION|WS_THICKFRAME)})
 for(DWORD ex : {DWORD(0),DWORD(WS_EX_DLGMODALFRAME),DWORD(WS_EX_CLIENTEDGE),DWORD(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE)})
 for(BOOL menu : {FALSE,TRUE}) {
  RECT base{10,20,110,120},edge=base,reference=base,reference_edge=base;
  CHECK(adjust(&base,style,menu,ex));
  SetLastError(0xdead); CHECK(adjust(&edge,style,menu,ex|WS_EX_STATICEDGE)); CHECK(GetLastError()==0xdead);
  CHECK(AdjustWindowRectEx(&reference,style,menu,ex));
  CHECK(AdjustWindowRectEx(&reference_edge,style,menu,ex|WS_EX_STATICEDGE));
  // Compare only the static-edge delta; theme-dependent base metrics differ.
  CHECK(edge.left-base.left==reference_edge.left-reference.left);
  CHECK(edge.top-base.top==reference_edge.top-reference.top);
  CHECK(edge.right-base.right==reference_edge.right-reference.right);
  CHECK(edge.bottom-base.bottom==reference_edge.bottom-reference.bottom);
  ++count;
 }
 printf("PASS: %u static-edge style/menu combinations; Wine delta comparisons (off-device)\n",count);
}
