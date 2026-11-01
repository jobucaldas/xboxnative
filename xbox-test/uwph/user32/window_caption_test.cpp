#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(){
 HMODULE m=LoadLibraryW(L"uwph-u32c.dll");CHECK(m);
 auto adjust=reinterpret_cast<decltype(&AdjustWindowRectEx)>(GetProcAddress(m,"AdjustWindowRectEx"));CHECK(adjust);
 unsigned count=0;
 for(DWORD extra : {DWORD(0),DWORD(WS_VISIBLE),DWORD(WS_CHILD),DWORD(WS_POPUP)})
 for(DWORD border : {DWORD(0),DWORD(WS_BORDER),DWORD(WS_DLGFRAME),DWORD(WS_CAPTION)}) {
  RECT candidate{10,20,110,120},reference=candidate;
  SetLastError(0xdead);CHECK(adjust(&candidate,extra|border,FALSE,0));CHECK(GetLastError()==0xdead);
  CHECK(AdjustWindowRectEx(&reference,extra|border,FALSE,0));
  // Wine's theme metrics can vary; test the caption-only asymmetry rather
  // than claiming the backend's fixed classic dimensions match the host.
  LONG asymmetric=(20-candidate.top)-(10-candidate.left);
  LONG wine_asymmetric=(20-reference.top)-(10-reference.left);
  CHECK((asymmetric!=0)==(wine_asymmetric!=0));
  CHECK(asymmetric==(border==WS_CAPTION?19:0));
  CHECK(candidate.right-110==10-candidate.left);
  CHECK(candidate.bottom-120==10-candidate.left);
  ++count;
 }
 printf("PASS: %u caption/border style combinations; Wine asymmetry comparisons (off-device)\n",count);
}
