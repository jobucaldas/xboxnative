// Off-device integration: injected CoreWindow-like events, actual owner DLL
// window registry and cross-thread posted-message queue. No physical input.
#include "feed.h"
#include <thread>
#include <cstdio>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static LRESULT CALLBACK proc(HWND,UINT m,WPARAM,LPARAM){return m==WM_NCCREATE?TRUE:0;}
int main(int argc,char** argv) {
    CHECK(argc==2);
    HMODULE records=LoadLibraryA(argv[1]);CHECK(records);
#define LOAD(name) auto f##name=reinterpret_cast<decltype(&name)>(GetProcAddress(records,#name));CHECK(f##name)
    LOAD(RegisterClassW);LOAD(CreateWindowExW);LOAD(DestroyWindow);LOAD(UnregisterClassW);
    LOAD(SetForegroundWindow);LOAD(PeekMessageW);LOAD(GetAsyncKeyState);
    keyboard_core::Feed feed;
    feed.sink=reinterpret_cast<keyboard_core::KeyEvent>(GetProcAddress(records,"uwph_u32c_key_event"));
    feed.focus=reinterpret_cast<keyboard_core::TargetWindow>(GetProcAddress(records,"GetFocus"));
    feed.foreground=reinterpret_cast<keyboard_core::TargetWindow>(GetProcAddress(records,"GetForegroundWindow"));
    feed.post=reinterpret_cast<keyboard_core::PostMessage>(GetProcAddress(records,"PostMessageW"));
    CHECK(feed.sink && feed.focus && feed.foreground && feed.post);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"gfxin-key-events";
    CHECK(fRegisterClassW(&cls));
    HWND window=fCreateWindowExW(0,cls.lpszClassName,L"guest",WS_VISIBLE,0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
    CHECK(window && fSetForegroundWindow(window));
    // Feed from a different thread, like CoreWindow callbacks in the launcher.
    std::thread ui([&]{feed.key('W',0x11,false,true);});ui.join();
    CHECK(fGetAsyncKeyState('W') & 0x8000);
    MSG message{};
    CHECK(fPeekMessageW(&message,nullptr,0,0,PM_REMOVE));
    CHECK(message.hwnd==window && message.message==WM_KEYDOWN && message.wParam=='W' && message.lParam==0x00110001);
    std::thread repeat([&]{feed.key('W',0x11,false,true,3);feed.key('W',0x11,false,false);});repeat.join();
    CHECK(!(fGetAsyncKeyState('W') & 0x8000));
    CHECK(fPeekMessageW(&message,nullptr,0,0,PM_REMOVE) && message.message==WM_KEYDOWN && message.lParam==0x40110003);
    CHECK(fPeekMessageW(&message,nullptr,0,0,PM_REMOVE) && message.message==WM_KEYUP && DWORD(message.lParam)==0xc0110001);
    CHECK(!fPeekMessageW(&message,nullptr,0,0,PM_REMOVE));
    bool posted=false;
    std::thread text([&]{posted=feed.character('w',0x11,false,false,true,false,2);});text.join();
    CHECK(posted);
    CHECK(fPeekMessageW(&message,nullptr,0,0,PM_REMOVE));
    CHECK(message.hwnd==window && message.message==WM_CHAR && message.wParam=='w' && message.lParam==0x40110002);
    CHECK(!fPeekMessageW(&message,nullptr,0,0,PM_REMOVE));
    std::thread focusLoss([&]{feed.key(VK_SHIFT,0x36,false,true);feed.deactivate();});focusLoss.join();
    CHECK(!(fGetAsyncKeyState(VK_SHIFT)&0x8000) && !(fGetAsyncKeyState(VK_RSHIFT)&0x8000));
    CHECK(fPeekMessageW(&message,nullptr,0,0,PM_REMOVE) && message.wParam==VK_SHIFT && message.lParam==0x00360001);
    CHECK(!fPeekMessageW(&message,nullptr,0,0,PM_REMOVE));
    CHECK(fDestroyWindow(window));CHECK(fUnregisterClassW(cls.lpszClassName,cls.hInstance));
    FreeLibrary(records);
    puts("keyboard queue: actual record DLL, injected cross-thread key/text messages/repeats and focus loss PASS");
}
