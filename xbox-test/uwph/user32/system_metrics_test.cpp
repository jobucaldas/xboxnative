#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto metrics=reinterpret_cast<decltype(&GetSystemMetrics)>(GetProcAddress(module,"GetSystemMetrics")); CHECK(metrics);
 auto adjust=reinterpret_cast<decltype(&AdjustWindowRectEx)>(GetProcAddress(module,"AdjustWindowRectEx")); CHECK(adjust);
 unsigned count=0;
 // Wine guarantees these fixed border dimensions independent of theme.
 for(int index : {SM_CXBORDER,SM_CYBORDER,SM_CXDLGFRAME,SM_CYDLGFRAME,SM_CXEDGE,SM_CYEDGE}) {
  SetLastError(0xdead); int expected=GetSystemMetrics(index); CHECK(GetLastError()==0xdead);
  SetLastError(0xdead); CHECK(metrics(index)==expected); CHECK(GetLastError()==0xdead); ++count;
 }
 for(int index : {-1,0x7fffffff}) {
  SetLastError(0xdead); CHECK(GetSystemMetrics(index)==0); CHECK(GetLastError()==0xdead);
  SetLastError(0xdead); CHECK(metrics(index)==0); CHECK(GetLastError()==0xdead); ++count;
 }
 struct Metric {int index,value;};
 for(const Metric test : {Metric{SM_CXFRAME,4},Metric{SM_CYFRAME,4},Metric{SM_CYCAPTION,19},Metric{SM_CYSMCAPTION,17},Metric{SM_CYMENU,20}}) {
  SetLastError(0xdead); CHECK(metrics(test.index)==test.value); CHECK(GetLastError()==0xdead); ++count;
 }
 auto resize=reinterpret_cast<void(WINAPI*)(LONG,LONG)>(GetProcAddress(module,"uwph_u32c_configure")); CHECK(resize);
 resize(1280,720);
 for(int index : {SM_CXSCREEN,SM_CXVIRTUALSCREEN}) {CHECK(metrics(index)==1280); ++count;}
 for(int index : {SM_CYSCREEN,SM_CYVIRTUALSCREEN}) {CHECK(metrics(index)==720); ++count;}
 for(int index : {SM_CMONITORS,SM_SAMEDISPLAYFORMAT}) {CHECK(metrics(index)==1); ++count;}
 for(int index : {SM_XVIRTUALSCREEN,SM_YVIRTUALSCREEN}) {CHECK(metrics(index)==0); ++count;}
 // Dynamic host theme values need not equal the record backend's classic
 // values. Both must agree with their respective AdjustWindowRectEx geometry.
 for(int backend=0;backend<2;++backend) {
  auto m=backend?metrics:&GetSystemMetrics;
  auto a=backend?adjust:&AdjustWindowRectEx;
  for(DWORD style : {DWORD(WS_BORDER),DWORD(WS_DLGFRAME),DWORD(WS_CAPTION),DWORD(WS_CAPTION|WS_THICKFRAME)})
  for(DWORD ex : {DWORD(0),DWORD(WS_EX_TOOLWINDOW),DWORD(WS_EX_CLIENTEDGE)})
  for(BOOL menu : {FALSE,TRUE}) {
   RECT r{0,0,100,100}; CHECK(a(&r,style,menu,ex));
   int frame=(style&WS_THICKFRAME)?m(SM_CXFRAME):((style&WS_DLGFRAME)?m(SM_CXDLGFRAME):m(SM_CXBORDER));
   if(ex&WS_EX_CLIENTEDGE) frame+=m(SM_CXEDGE);
   int caption=((style&WS_CAPTION)==WS_CAPTION)?m((ex&WS_EX_TOOLWINDOW)?SM_CYSMCAPTION:SM_CYCAPTION):0;
   CHECK(r.left==-frame && r.right==100+frame && r.bottom==100+frame);
   CHECK(r.top==-frame-caption-(menu?m(SM_CYMENU):0)); ++count;
  }
 }
 printf("PASS: %u system metric/Wine geometry checks (off-device)\n",count);
}
