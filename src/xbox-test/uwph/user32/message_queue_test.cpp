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
static DWORD pumpThread;
static DWORD WINAPI latePost(void* delay){Sleep((DWORD)(UINT_PTR)delay);return postThread(pumpThread,WM_APP+20,5,6)?0:1;}
// GetMessage/MsgWait/TranslateMessage and the dialog/key-name entry points.
static void pumpChecks(HMODULE dll){
#define LOADP(name) auto f##name=reinterpret_cast<decltype(&name)>(GetProcAddress(dll,#name));CHECK(f##name!=nullptr);if(!f##name)return
 LOADP(GetMessageW);LOADP(GetMessageA);LOADP(MsgWaitForMultipleObjects);LOADP(MsgWaitForMultipleObjectsEx);
 LOADP(TranslateMessage);LOADP(DispatchMessageA);LOADP(GetMessageExtraInfo);LOADP(GetKeyNameTextW);
 LOADP(DialogBoxParamW);LOADP(EndDialog);LOADP(DragDetect);LOADP(PostQuitMessage);LOADP(RegisterClassW);
 LOADP(CreateWindowExW);LOADP(DestroyWindow);LOADP(UnregisterClassW);
 using KeyEvent=void(WINAPI*)(int,BOOL);auto key=(KeyEvent)GetProcAddress(dll,"uwph_u32c_key_event");CHECK(key!=nullptr);if(!key)return;
 pumpThread=GetCurrentThreadId();MSG m{};
 // GetMessage: posted message, WM_QUIT -> 0, invalid filter -> -1, blocking wake.
 CHECK(postThread(pumpThread,WM_APP+10,1,2));
 CHECK(fGetMessageW(&m,nullptr,0,0)>0&&m.message==WM_APP+10&&m.wParam==1&&m.lParam==2);
 fPostQuitMessage(7);CHECK(fGetMessageA(&m,nullptr,0,0)==0&&m.message==WM_QUIT&&m.wParam==7);
 SetLastError(0);CHECK(fGetMessageW(&m,(HWND)123,0,0)==-1&&GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
 HANDLE late=CreateThread(nullptr,0,latePost,(void*)(UINT_PTR)100,0,nullptr);CHECK(late);
 DWORD start=GetTickCount();
 CHECK(fGetMessageW(&m,nullptr,0,0)>0&&m.message==WM_APP+20&&m.wParam==5&&GetTickCount()-start>=50);
 CHECK(WaitForSingleObject(late,5000)==WAIT_OBJECT_0);CloseHandle(late);
 // MsgWait: timeout, handle, newly posted message, consumed wake bit, flags.
 CHECK(fMsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT)==WAIT_TIMEOUT);
 HANDLE signaled=CreateEventW(nullptr,TRUE,TRUE,nullptr);
 CHECK(fMsgWaitForMultipleObjects(1,&signaled,FALSE,0,QS_ALLINPUT)==WAIT_OBJECT_0);
 ResetEvent(signaled);
 late=CreateThread(nullptr,0,latePost,(void*)(UINT_PTR)50,0,nullptr);
 CHECK(fMsgWaitForMultipleObjects(1,&signaled,FALSE,5000,QS_POSTMESSAGE)==WAIT_OBJECT_0+1);
 CHECK(WaitForSingleObject(late,5000)==WAIT_OBJECT_0);CloseHandle(late);
 CHECK(!peek(&m,nullptr,WM_APP+30,WM_APP+31,PM_REMOVE)); // checks the queue, leaves WM_APP+20
 CHECK(fMsgWaitForMultipleObjects(1,&signaled,FALSE,20,QS_POSTMESSAGE)==WAIT_TIMEOUT);
 CHECK(fMsgWaitForMultipleObjectsEx(1,&signaled,20,QS_POSTMESSAGE,MWMO_INPUTAVAILABLE)==WAIT_OBJECT_0+1);
 CHECK(fMsgWaitForMultipleObjects(1,&signaled,FALSE,20,QS_TIMER)==WAIT_TIMEOUT); // no timer source
 CHECK(peek(&m,nullptr,0,0,PM_REMOVE)&&m.message==WM_APP+20);
 SetLastError(0);CHECK(fMsgWaitForMultipleObjects(1,&signaled,TRUE,0,QS_ALLINPUT)==WAIT_FAILED&&GetLastError()==ERROR_NOT_SUPPORTED);
 HANDLE many[MAXIMUM_WAIT_OBJECTS];for(auto& h:many)h=signaled;
 SetLastError(0);CHECK(fMsgWaitForMultipleObjects(MAXIMUM_WAIT_OBJECTS,many,FALSE,0,0)==WAIT_FAILED&&GetLastError()==ERROR_INVALID_PARAMETER);
 CloseHandle(signaled);
 CHECK(fGetMessageExtraInfo()==0);
 // TranslateMessage posts characters from the current key state.
 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"pump-test";cls.hInstance=GetModuleHandleW(nullptr);
 CHECK(fRegisterClassW(&cls)!=0);
 HWND h=fCreateWindowExW(0,cls.lpszClassName,L"",0,0,0,10,10,nullptr,nullptr,cls.hInstance,nullptr);CHECK(h);
 MSG down{h,WM_KEYDOWN,'A',0x001E0001,0,{0,0}};
 CHECK(fTranslateMessage(&down)&&peek(&m,nullptr,0,0,PM_REMOVE)&&m.hwnd==h&&m.message==WM_CHAR&&m.wParam=='a'&&m.lParam==0x001E0001);
 key(VK_SHIFT,TRUE);CHECK(fTranslateMessage(&down)&&peek(&m,nullptr,0,0,PM_REMOVE)&&m.wParam=='A');key(VK_SHIFT,FALSE);
 MSG sys=down;sys.message=WM_SYSKEYDOWN;
 CHECK(fTranslateMessage(&sys)&&peek(&m,nullptr,0,0,PM_REMOVE)&&m.message==WM_SYSCHAR&&m.wParam=='a');
 MSG up=down;up.message=WM_KEYUP;CHECK(fTranslateMessage(&up)&&!peek(&m,nullptr,0,0,PM_REMOVE));
 MSG f1{h,WM_KEYDOWN,VK_F1,0x003B0001,0,{0,0}};CHECK(fTranslateMessage(&f1)&&!peek(&m,nullptr,0,0,PM_REMOVE));
 MSG other{h,WM_APP,0,0,0,{0,0}};CHECK(!fTranslateMessage(&other)&&!fTranslateMessage(nullptr));
 MSG app{h,WM_APP+1,3,0,0,{0,0}};unsigned before=calls;CHECK(fDispatchMessageA(&app)==103&&calls==before+1);
 // US-layout key names, extended/don't-care bits, truncation and unknown keys.
 wchar_t name[32];
 auto keyText=[&](LONG lp,const wchar_t* expected){int n=fGetKeyNameTextW(lp,name,32);return n==(int)wcslen(expected)&&!wcscmp(name,expected);};
 CHECK(keyText(0x001E0000,L"A"));CHECK(keyText(0x00010000,L"Esc"));CHECK(keyText(0x00020000,L"1"));
 CHECK(keyText(0x00360000,L"Right Shift"));CHECK(keyText(0x02360000,L"Shift"));
 CHECK(keyText(0x01480000,L"Up"));CHECK(keyText(0x00480000,L"Num 8"));CHECK(keyText(0x011C0000,L"Num Enter"));
 CHECK(keyText(0x011D0000,L"Right Ctrl"));CHECK(keyText(0x00390000,L"Space"));CHECK(keyText(0x00570000,L"F11"));
 CHECK(fGetKeyNameTextW(0x000E0000,name,3)==2&&!wcscmp(name,L"Ba"));
 CHECK(fGetKeyNameTextW(0x007F0000,name,32)==0&&name[0]==0);CHECK(fGetKeyNameTextW(0x001E0000,name,0)==0);
 // No dialog manager or pointer capture.
 SetLastError(0);CHECK(fDialogBoxParamW(nullptr,MAKEINTRESOURCEW(101),nullptr,nullptr,0)==-1&&GetLastError()==ERROR_NOT_SUPPORTED);
 SetLastError(0);CHECK(fDialogBoxParamW(nullptr,MAKEINTRESOURCEW(101),(HWND)123,nullptr,0)==0&&GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
 CHECK(!fEndDialog(h,1));CHECK(!fDragDetect(h,POINT{1,1}));
 // Message-only windows (HWND_MESSAGE): created parentless, receive posted
 // messages, and are not enumerated as top-level windows.
 LOADP(EnumWindows);LOADP(GetParent);LOADP(PostMessageW);
 HWND messageOnly=fCreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);CHECK(messageOnly);
 CHECK(fGetParent(messageOnly)==nullptr);
 struct Seen{HWND target;bool found;} seen{messageOnly,false},top{h,false};
 auto enumerate=[](HWND w,LPARAM p)->BOOL{auto s=(Seen*)p;if(w==s->target)s->found=true;return TRUE;};
 CHECK(fEnumWindows(enumerate,(LPARAM)&seen)&&!seen.found);CHECK(fEnumWindows(enumerate,(LPARAM)&top)&&top.found);
 CHECK(fPostMessageW(messageOnly,WM_APP+1,4,0));before=calls;
 CHECK(fGetMessageW(&m,nullptr,0,0)>0&&m.hwnd==messageOnly&&fDispatchMessageA(&m)==104&&calls==before+1);
 LOADP(GetWindowThreadProcessId);DWORD pid=0;
 CHECK(fGetWindowThreadProcessId(h,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId());
 CHECK(fGetWindowThreadProcessId(messageOnly,nullptr)==GetCurrentThreadId());
 pid=7;SetLastError(0);CHECK(fGetWindowThreadProcessId((HWND)123,&pid)==0&&pid==7&&GetLastError()==ERROR_INVALID_WINDOW_HANDLE);
 CHECK(fDestroyWindow(messageOnly));
 CHECK(fDestroyWindow(h));CHECK(fUnregisterClassW(cls.lpszClassName,cls.hInstance));
 // Wine user32 DPI internals (shcore donor delay imports): one 96-DPI display.
 using MonitorDpi=BOOL(WINAPI*)(HMONITOR,UINT,UINT*,UINT*);using SetAware=BOOL(WINAPI*)(int);using GetAware=BOOL(WINAPI*)(HANDLE,int*);
 auto dpi=(MonitorDpi)GetProcAddress(dll,"GetDpiForMonitorInternal");auto setAware=(SetAware)GetProcAddress(dll,"SetProcessDpiAwarenessInternal");
 auto getAware=(GetAware)GetProcAddress(dll,"GetProcessDpiAwarenessInternal");CHECK(dpi&&setAware&&getAware);if(!dpi||!setAware||!getAware)return;
 UINT x=0,y=0;CHECK(dpi(nullptr,0,&x,&y)&&x==96&&y==96);
 SetLastError(0);CHECK(!dpi(nullptr,3,&x,&y)&&GetLastError()==ERROR_BAD_ARGUMENTS);
 SetLastError(0);CHECK(!dpi(nullptr,0,nullptr,&y)&&GetLastError()==ERROR_INVALID_ADDRESS);
 int aware=-1;CHECK(getAware(nullptr,&aware)&&aware==0);
 SetLastError(0);CHECK(!setAware(3)&&GetLastError()==ERROR_INVALID_PARAMETER);
 CHECK(setAware(2));CHECK(getAware(GetCurrentProcess(),&aware)&&aware==2);
 SetLastError(0);CHECK(!setAware(1)&&GetLastError()==ERROR_ACCESS_DENIED);
 CHECK(getAware((HANDLE)(LONG_PTR)1234,&aware)&&aware==0); // other process: unaware
 // Raw input registrations read back as Wine's NtUserGetRegisteredRawInputDevices.
 LOADP(RegisterRawInputDevices);LOADP(GetRegisteredRawInputDevices);
 RAWINPUTDEVICE reg[2]={{1,6,0,nullptr},{1,5,0,nullptr}},out[2]{};UINT n=0;
 CHECK(fGetRegisteredRawInputDevices(nullptr,&n,sizeof(RAWINPUTDEVICE))==0&&n==0);
 CHECK(fRegisterRawInputDevices(reg,2,sizeof(RAWINPUTDEVICE)));
 n=0;CHECK(fGetRegisteredRawInputDevices(nullptr,&n,sizeof(RAWINPUTDEVICE))==0&&n==2);
 n=1;SetLastError(0);CHECK(fGetRegisteredRawInputDevices(out,&n,sizeof(RAWINPUTDEVICE))==~0u&&GetLastError()==ERROR_INSUFFICIENT_BUFFER&&n==2);
 n=2;CHECK(fGetRegisteredRawInputDevices(out,&n,sizeof(RAWINPUTDEVICE))==2&&out[0].usUsage==6&&out[1].usUsage==5);
 n=0;SetLastError(0);CHECK(fGetRegisteredRawInputDevices(out,&n,sizeof(RAWINPUTDEVICE))==~0u&&GetLastError()==ERROR_INVALID_PARAMETER);
 SetLastError(0);CHECK(fGetRegisteredRawInputDevices(nullptr,&n,1)==~0u&&GetLastError()==ERROR_INVALID_PARAMETER);
 reg[0].dwFlags=reg[1].dwFlags=RIDEV_REMOVE;CHECK(fRegisterRawInputDevices(reg,2,sizeof(RAWINPUTDEVICE)));
 n=0;CHECK(fGetRegisteredRawInputDevices(nullptr,&n,sizeof(RAWINPUTDEVICE))==0&&n==0);
}
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
 pumpChecks(dll);
 printf("OFF-DEVICE guest message queue checks=%u failures=%u; no console/frame claim\n",checks,failures);
 return failures?1:0;
}
