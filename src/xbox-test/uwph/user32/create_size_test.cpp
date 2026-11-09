// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device: CreateWindowEx delivers WM_SIZE then WM_MOVE after WM_CREATE,
// as Windows and Wine do. Halo Wars: DE only learns its window size from that
// first WM_SIZE and later divides by it. Pass the record DLL path to test it;
// with no argument the host user32 is the parity reference.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int order[8],n;static int activateApp,activate,setFocus;static WPARAM sizeKind;static LPARAM sizeParam,moveParam;
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM p){
 if(m==WM_NCCREATE)return TRUE;
 if(m==WM_ACTIVATEAPP&&w)++activateApp;
 if(m==WM_ACTIVATE&&LOWORD(w)==WA_ACTIVE)++activate;
 if(m==WM_SETFOCUS)++setFocus;
 if(m==WM_CREATE&&n<8)order[n++]=WM_CREATE;
 if(m==WM_SIZE&&n<8){order[n++]=WM_SIZE;sizeKind=w;sizeParam=p;}
 if(m==WM_MOVE&&n<8){order[n++]=WM_MOVE;moveParam=p;}
 return m==WM_CREATE?0:DefWindowProcW(h,m,w,p);
}
int main(int argc,char** argv){
 HMODULE dll=argc>1?LoadLibraryA(argv[1]):GetModuleHandleA("user32.dll");CHECK(dll);
#define API(name) auto f##name=reinterpret_cast<decltype(&name)>(GetProcAddress(dll,#name));CHECK(f##name)
 API(RegisterClassW);API(CreateWindowExW);API(GetClientRect);API(DestroyWindow);API(ShowWindow);
 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"create-size";cls.hInstance=GetModuleHandleW(nullptr);
 CHECK(fRegisterClassW(&cls));
 // Frameless: client == window rect, origin == window origin.
 HWND pop=fCreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP,23,47,320,240,nullptr,nullptr,cls.hInstance,nullptr);CHECK(pop);
 CHECK(n==3&&order[0]==WM_CREATE&&order[1]==WM_SIZE&&order[2]==WM_MOVE);
 CHECK(sizeKind==SIZE_RESTORED&&sizeParam==MAKELPARAM(320,240)&&moveParam==MAKELPARAM(23,47));
 CHECK(fDestroyWindow(pop));
 // Framed and not visible: no size messages until the first show, then WM_SIZE
 // reports the client area. Same in Wine.
 n=0;sizeParam=0;
 HWND win=fCreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,10,20,800,600,nullptr,nullptr,cls.hInstance,nullptr);CHECK(win);
 CHECK(n==1&&!sizeParam);
 CHECK(fShowWindow(win,SW_SHOWNORMAL)==FALSE);
 RECT c{};CHECK(fGetClientRect(win,&c));
 CHECK(n==3&&order[1]==WM_SIZE&&order[2]==WM_MOVE&&sizeParam==MAKELPARAM(c.right,c.bottom)&&c.right>0&&c.right<800&&c.bottom<600);
 CHECK(fDestroyWindow(win));
 // Visible framed: delivered during CreateWindowEx.
 n=0;
 win=fCreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW|WS_VISIBLE,10,20,800,600,nullptr,nullptr,cls.hInstance,nullptr);CHECK(win);
 CHECK(fGetClientRect(win,&c));CHECK(sizeParam==MAKELPARAM(c.right,c.bottom)&&c.right>0);
 CHECK(fDestroyWindow(win));
 // The record user32 activates the first shown top-level window (Windows does;
 // a Wine session without a window manager may not, so only the record DLL is checked).
 if(argc>1){
  API(GetForegroundWindow);API(GetActiveWindow);API(GetFocus);
  activateApp=activate=setFocus=0;
  win=fCreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW|WS_VISIBLE,10,20,800,600,nullptr,nullptr,cls.hInstance,nullptr);CHECK(win);
  CHECK(fGetForegroundWindow()==win&&fGetActiveWindow()==win&&fGetFocus()==win);
  CHECK(activateApp==1&&activate==1&&setFocus==1);
  HWND second=fCreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);CHECK(second);
  CHECK(fGetForegroundWindow()==win&&activate==1);   // the first stays foreground
  CHECK(fDestroyWindow(second));CHECK(fDestroyWindow(win));
 }
 printf("create-size checks=%u failures=0\n",checks);
 return 0;
}
