// Read-only owner integration. Events are injected off-device on a UI-like thread.
#include "feed.h"
#include <thread>
#include <cstdio>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static LRESULT CALLBACK proc(HWND,UINT message,WPARAM,LPARAM){return message==WM_NCCREATE?TRUE:0;}
int main(int argc,char** argv){
 CHECK(argc==2);HMODULE records=LoadLibraryA(argv[1]);CHECK(records);
#define LOAD(name) auto f##name=reinterpret_cast<decltype(&name)>(GetProcAddress(records,#name));CHECK(f##name)
 LOAD(RegisterClassW);LOAD(CreateWindowExW);LOAD(SetForegroundWindow);LOAD(PeekMessageW);
 LOAD(GetAsyncKeyState);LOAD(GetCursorPos);LOAD(DestroyWindow);LOAD(UnregisterClassW);
 pointer_core::Feed feed;
#define FIELD(field,name) feed.field=reinterpret_cast<decltype(feed.field)>(GetProcAddress(records,name));CHECK(feed.field)
 FIELD(foreground,"GetForegroundWindow");FIELD(post,"PostMessageW");FIELD(key,"uwph_u32c_key_event");
 FIELD(client_rect,"GetClientRect");FIELD(to_screen,"ClientToScreen");FIELD(to_client,"ScreenToClient");
 FIELD(set_position,"SetCursorPos");FIELD(get_position,"GetCursorPos");
 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"gfxin-pointer";
 CHECK(fRegisterClassW(&cls));
 HWND window=fCreateWindowExW(0,cls.lpszClassName,L"pointer",WS_VISIBLE,100,200,640,480,nullptr,nullptr,cls.hInstance,nullptr);
 CHECK(window && fSetForegroundWindow(window));
 bool accepted=false;std::thread ui([&]{accepted=feed.update(320,240,640,480,MK_LBUTTON);});ui.join();CHECK(accepted);
 POINT cursor{};CHECK(fGetCursorPos(&cursor) && cursor.x==420 && cursor.y==440);
 CHECK(fGetAsyncKeyState(VK_LBUTTON)&0x8000);
 MSG message{};CHECK(fPeekMessageW(&message,nullptr,WM_LBUTTONDOWN,WM_LBUTTONDOWN,PM_REMOVE));
 CHECK(message.hwnd==window && message.wParam==MK_LBUTTON && message.lParam==MAKELPARAM(320,240));
 CHECK(fPeekMessageW(&message,nullptr,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_REMOVE));
 CHECK(message.hwnd==window && message.wParam==MK_LBUTTON && message.lParam==MAKELPARAM(320,240));
 std::thread release([&]{accepted=feed.update(0,0,640,480,0);});release.join();CHECK(accepted);
 CHECK(!(fGetAsyncKeyState(VK_LBUTTON)&0x8000));
 CHECK(fPeekMessageW(&message,nullptr,WM_LBUTTONUP,WM_LBUTTONUP,PM_REMOVE));
 CHECK(message.wParam==0 && message.lParam==0);
 CHECK(fDestroyWindow(window));CHECK(fUnregisterClassW(cls.lpszClassName,cls.hInstance));FreeLibrary(records);
 puts("pointer queue: actual owner registry, cross-thread buttons/movement and cursor coordinates PASS");
}
