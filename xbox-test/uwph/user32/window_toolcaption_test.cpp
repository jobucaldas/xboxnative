#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto adjust=reinterpret_cast<decltype(&AdjustWindowRectEx)>(GetProcAddress(module,"AdjustWindowRectEx")); CHECK(adjust);
 NONCLIENTMETRICSW ncm{}; ncm.cbSize=sizeof(ncm);
 CHECK(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0));
 unsigned count=0;
 for(DWORD style : {DWORD(0),DWORD(WS_BORDER),DWORD(WS_DLGFRAME),DWORD(WS_CAPTION),DWORD(WS_CAPTION|WS_THICKFRAME)})
 for(DWORD ex : {DWORD(0),DWORD(WS_EX_STATICEDGE),DWORD(WS_EX_CLIENTEDGE),DWORD(WS_EX_DLGMODALFRAME)})
 for(BOOL menu : {FALSE,TRUE}) {
  RECT base{10,20,110,120},tool=base,reference=base,reference_tool=base;
  CHECK(adjust(&base,style,menu,ex));
  SetLastError(0xdead); CHECK(adjust(&tool,style,menu,ex|WS_EX_TOOLWINDOW)); CHECK(GetLastError()==0xdead);
  CHECK(AdjustWindowRectEx(&reference,style,menu,ex));
  CHECK(AdjustWindowRectEx(&reference_tool,style,menu,ex|WS_EX_TOOLWINDOW));
  bool caption=(style&WS_CAPTION)==WS_CAPTION;
  // Host theme metrics vary. Compare Wine with its own metrics and the
  // record backend with its documented fixed classic 19/17 pixel heights.
  CHECK(reference_tool.top-reference.top==(caption?ncm.iCaptionHeight-ncm.iSmCaptionHeight:0));
  CHECK(tool.top-base.top==(caption?2:0));
  CHECK(tool.left==base.left && tool.right==base.right && tool.bottom==base.bottom);
  ++count;
 }
 printf("PASS: %u tool-window style/menu combinations; Wine metrics checked (off-device)\n",count);
}
