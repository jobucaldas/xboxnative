// Off-device regression against the packaged record DLL, not host user32.
#include <windows.h>
#include <cstdio>
#include <cstring>
static unsigned checks=0,failures=0,calls=0,timerCalls=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL %d %s\n",__LINE__,#x);}}while(0)
static decltype(&PeekMessageW) peek;
static decltype(&PostThreadMessageW) postThread;
static HANDLE ready,go;
static DWORD WINAPI worker(void*){
 MSG m{};CHECK(!peek(&m,nullptr,0,0,PM_REMOVE));SetEvent(ready);
 WaitForSingleObject(go,5000);
 CHECK(peek(&m,nullptr,0,0,PM_REMOVE)&&m.message==WM_APP+9&&m.wParam==99);
 return 0;
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM){
 if(m==WM_NCCREATE)return TRUE;
 if(m==WM_APP+1){++calls;return w+100;}
 return 0;
}
static void CALLBACK timer(HWND,UINT m,UINT_PTR id,DWORD){if(m==WM_TIMER&&id==17)++timerCalls;}
int main(int argc,char** argv){
 HMODULE dll=LoadLibraryA(argc>1?argv[1]:"uwph-u32c.dll");CHECK(dll!=nullptr);if(!dll)return 2;
#define LOAD(name) auto f##name=reinterpret_cast<decltype(&name)>(GetProcAddress(dll,#name));CHECK(f##name!=nullptr)
 LOAD(PeekMessageW);LOAD(PeekMessageA);LOAD(PostMessageW);LOAD(PostThreadMessageW);LOAD(PostQuitMessage);LOAD(DispatchMessageW);
 LOAD(RegisterClassW);LOAD(CreateWindowExW);LOAD(DestroyWindow);LOAD(UnregisterClassW);
 peek=fPeekMessageW;postThread=fPostThreadMessageW;
 MSG m{},unchanged;memset(&m,0x5a,sizeof m);unchanged=m;
 CHECK(!peek(&m,nullptr,0,0,PM_REMOVE)&&!memcmp(&m,&unchanged,sizeof m));
 SetLastError(0);CHECK(!peek(nullptr,nullptr,0,0,0)&&GetLastError()==ERROR_INVALID_PARAMETER);
 CHECK(!fPostMessageW((HWND)123,WM_APP,0,0));
 CHECK(!postThread(0xdead,WM_APP,0,0));
 CHECK(fPostMessageW(nullptr,WM_APP+2,22,33));
 CHECK(peek(&m,(HWND)-1,0,0,PM_NOREMOVE)&&m.message==WM_APP+2&&m.wParam==22&&m.lParam==33);
 CHECK(peek(&m,nullptr,0,0,PM_NOREMOVE)&&m.message==WM_APP+2);
 CHECK(!peek(&m,nullptr,WM_APP+3,WM_APP+4,PM_REMOVE));
 CHECK(!peek(&m,nullptr,0,0,PM_REMOVE|(QS_INPUT<<16)));
 CHECK(fPeekMessageA(&m,nullptr,0,0,PM_REMOVE)&&m.message==WM_APP+2);
 CHECK(!peek(&m,nullptr,0,0,PM_REMOVE));
 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"queue-test";cls.hInstance=GetModuleHandleW(nullptr);
 CHECK(fRegisterClassW(&cls)!=0);
 HWND h=fCreateWindowExW(0,cls.lpszClassName,L"",0,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);CHECK(h);
 HWND child=fCreateWindowExW(0,cls.lpszClassName,L"",WS_CHILD,0,0,10,10,h,nullptr,cls.hInstance,nullptr);CHECK(child);
 CHECK(fPostMessageW(child,WM_APP+1,7,0));
 CHECK(!peek(&m,(HWND)-1,0,0,PM_REMOVE));
 CHECK(peek(&m,h,0,0,PM_REMOVE)&&m.hwnd==child);
 CHECK(fDispatchMessageW(&m)==107&&calls==1);
 m.hwnd=nullptr;CHECK(fDispatchMessageW(&m)==0&&calls==1);
 m.message=WM_TIMER;m.wParam=17;m.lParam=(LPARAM)timer;
 CHECK(fDispatchMessageW(&m)==0&&timerCalls==1);
 CHECK(!fPostMessageW(h,WM_SETTEXT,0,(LPARAM)L"unsafe pointer"));
 CHECK(!fPostMessageW(h,WM_CHAR,0x20ac,0)); // explicit codepage limit, never silently corrupt
 fPostQuitMessage(42);CHECK(peek(&m,nullptr,WM_APP,WM_APP,PM_NOREMOVE)&&m.message==WM_QUIT&&m.wParam==42);
 CHECK(peek(&m,nullptr,WM_APP,WM_APP,PM_REMOVE)&&m.message==WM_QUIT);
 CHECK(!peek(&m,nullptr,0,0,PM_REMOVE));
 ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);go=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 DWORD tid;HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,&tid);CHECK(thread);
 CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
 CHECK(postThread(tid,WM_APP+9,99,0));CHECK(!peek(&m,nullptr,0,0,PM_REMOVE));SetEvent(go);
 CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);CloseHandle(ready);CloseHandle(go);
 CHECK(fPostMessageW(child,WM_APP,0,0));CHECK(fDestroyWindow(child));
 CHECK(!peek(&m,nullptr,0,0,PM_REMOVE));CHECK(fDestroyWindow(h));CHECK(fUnregisterClassW(cls.lpszClassName,cls.hInstance));
 printf("OFF-DEVICE guest message queue checks=%u failures=%u; no console/frame claim\n",checks,failures);
 return failures?1:0;
}
