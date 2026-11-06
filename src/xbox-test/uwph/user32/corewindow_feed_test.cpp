// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device: the launcher's CoreWindow keyboard/mouse feed against the
// packaged record DLL (window records, posted-message queue, key state).
#include "corewindow_feed.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static HMODULE dll;
template<class F> static F api(const char* name){auto f=reinterpret_cast<F>(GetProcAddress(dll,name));CHECK(f);return f;}
static LRESULT CALLBACK proc(HWND,UINT m,WPARAM,LPARAM){return m==WM_NCCREATE;}
static decltype(&PeekMessageW) peek;
static MSG next(){MSG m{};CHECK(peek(&m,nullptr,0,0,PM_REMOVE));return m;}
static bool empty(){MSG m{};return !peek(&m,nullptr,0,0,PM_REMOVE);}

int main(int argc,char** argv) {
 dll=LoadLibraryA(argc>1?argv[1]:"uwph-u32c.dll");CHECK(dll);
 peek=api<decltype(&PeekMessageW)>("PeekMessageW");
 auto translate=api<decltype(&TranslateMessage)>("TranslateMessage");
 auto async=api<decltype(&GetAsyncKeyState)>("GetAsyncKeyState");
 auto state=api<decltype(&GetKeyState)>("GetKeyState");
 auto setFocus=api<decltype(&SetFocus)>("SetFocus");
 corewindow_input::Feed feed;auto& a=feed.api;
 a.key=api<decltype(a.key)>("uwph_u32c_key_event");a.post=api<decltype(a.post)>("PostMessageW");
 a.focus=api<decltype(a.focus)>("GetFocus");a.foreground=api<decltype(a.foreground)>("GetForegroundWindow");
 a.is_window=api<decltype(a.is_window)>("IsWindow");a.client_rect=api<decltype(a.client_rect)>("GetClientRect");
 a.to_screen=api<decltype(a.to_screen)>("ClientToScreen");a.to_client=api<decltype(a.to_client)>("ScreenToClient");
 a.set_cursor=api<decltype(a.set_cursor)>("SetCursorPos");a.get_cursor=api<decltype(a.get_cursor)>("GetCursorPos");
 CHECK(a.complete());

 // No target window: key state still updates, nothing is posted.
 CHECK(feed.key('Q',0x10,false,true));CHECK(async('Q')&0x8000);CHECK(empty());
 CHECK(feed.key('Q',0x10,false,false));CHECK(!(state('Q')&0x8000));
 // Controller keys from the Xbox shell never reach the guest.
 CHECK(!feed.key(0xC3,0,false,true));CHECK(!feed.key(0x8A,0,false,true));CHECK(!(state(0xC3)&0x8000));

 WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.lpszClassName=L"feed-test";cls.hInstance=GetModuleHandleW(nullptr);
 CHECK(api<decltype(&RegisterClassW)>("RegisterClassW")(&cls));
 auto create=api<decltype(&CreateWindowExW)>("CreateWindowExW");
 HWND game=create(0,cls.lpszClassName,L"",WS_POPUP|WS_VISIBLE,100,50,1280,720,nullptr,nullptr,cls.hInstance,nullptr);CHECK(game);
 HWND child=create(0,cls.lpszClassName,L"",WS_CHILD,0,0,10,10,game,nullptr,cls.hInstance,nullptr);CHECK(child);
 // The game never sets focus: the presenting window receives input.
 feed.fallback=game;CHECK(feed.target()==game);

 // Shift+1 -> WM_KEYDOWN (generic VK_SHIFT, left side held) then '!' via TranslateMessage.
 CHECK(feed.key(VK_SHIFT,0x2A,false,true));
 MSG m=next();CHECK(m.hwnd==game&&m.message==WM_KEYDOWN&&m.wParam==VK_SHIFT&&m.lParam==0x002A0001);
 CHECK(state(VK_LSHIFT)&0x8000);CHECK(state(VK_SHIFT)&0x8000);CHECK(!(state(VK_RSHIFT)&0x8000));
 CHECK(feed.key('1',0x02,false,true));m=next();CHECK(m.message==WM_KEYDOWN&&m.wParam=='1');
 CHECK(translate(&m));MSG c=next();CHECK(c.message==WM_CHAR&&c.wParam=='!'&&c.hwnd==game);CHECK(empty());
 // Auto-repeat: previous-state bit set, press bit refreshed.
 CHECK(async('1')&1);CHECK(!(async('1')&1));
 CHECK(feed.key('1',0x02,false,true,1));m=next();CHECK(m.message==WM_KEYDOWN&&(m.lParam&(1<<30)));CHECK(async('1')&1);
 CHECK(feed.key('1',0x02,false,false));m=next();CHECK(m.message==WM_KEYUP&&(DWORD(m.lParam)&0xC0000000u)==0xC0000000u);
 CHECK(feed.key(VK_SHIFT,0x2A,false,false));m=next();CHECK(m.message==WM_KEYUP&&m.wParam==VK_SHIFT);CHECK(!(state(VK_SHIFT)&0x8000));
 // Right Ctrl (extended) keeps its side; Ctrl+C translates to 0x03.
 CHECK(feed.key(VK_CONTROL,0x1D,true,true));m=next();CHECK(m.wParam==VK_CONTROL&&(m.lParam&(1<<24)));CHECK(state(VK_RCONTROL)&0x8000);
 CHECK(feed.key('C',0x2E,false,true));m=next();CHECK(translate(&m));c=next();CHECK(c.message==WM_CHAR&&c.wParam==3);
 CHECK(feed.key('C',0x2E,false,false));next();CHECK(feed.key(VK_CONTROL,0x1D,true,false));next();
 // Alt is a system key; Alt+F4-style combinations post WM_SYSKEY* with the context bit.
 CHECK(feed.key(VK_MENU,0x38,false,true));m=next();CHECK(m.message==WM_SYSKEYDOWN&&m.wParam==VK_MENU&&(m.lParam&(1<<29)));
 CHECK(feed.key(VK_F4,0x3E,false,true));m=next();CHECK(m.message==WM_SYSKEYDOWN&&m.wParam==VK_F4&&(m.lParam&(1<<29)));
 CHECK(feed.key(VK_F4,0x3E,false,false));m=next();CHECK(m.message==WM_SYSKEYUP);
 CHECK(feed.key(VK_MENU,0x38,false,false));m=next();CHECK(m.message==WM_SYSKEYUP&&m.wParam==VK_MENU);
 CHECK(feed.key(VK_F10,0x44,false,true));m=next();CHECK(m.message==WM_SYSKEYDOWN);feed.key(VK_F10,0x44,false,false);next();
 // Caps Lock toggles through the feed.
 feed.key(VK_CAPITAL,0x3A,false,true);feed.key(VK_CAPITAL,0x3A,false,false);next();next();CHECK(state(VK_CAPITAL)&1);
 feed.key(VK_CAPITAL,0x3A,false,true);feed.key(VK_CAPITAL,0x3A,false,false);next();next();CHECK(!(state(VK_CAPITAL)&1));

 // Focus wins over the presenting window.
 CHECK(setFocus(child)==nullptr);CHECK(feed.key('Z',0x2C,false,true));m=next();CHECK(m.hwnd==child);
 feed.key('Z',0x2C,false,false);next();setFocus(nullptr);

 // Mouse: DIPs scale to the 1280x720 client area; the cursor is in screen space.
 POINT cursor{};
 CHECK(feed.pointer(960,540,1920,1080,0));m=next();CHECK(m.message==WM_MOUSEMOVE&&m.hwnd==game&&m.lParam==MAKELPARAM(640,360)&&m.wParam==0);
 CHECK(a.get_cursor(&cursor)&&cursor.x==740&&cursor.y==410);
 CHECK(feed.pointer(0,0,1920,1080,corewindow_input::Left));
 m=next();CHECK(m.message==WM_LBUTTONDOWN&&m.wParam==MK_LBUTTON&&m.lParam==MAKELPARAM(0,0));
 m=next();CHECK(m.message==WM_MOUSEMOVE&&m.wParam==MK_LBUTTON);CHECK(async(VK_LBUTTON)&0x8000);
 CHECK(feed.pointer(1920,1080,1920,1080,corewindow_input::Left|corewindow_input::X2));
 m=next();CHECK(m.message==WM_XBUTTONDOWN&&HIWORD(m.wParam)==XBUTTON2&&(m.wParam&MK_LBUTTON)&&m.lParam==MAKELPARAM(1279,719));next();
 CHECK(feed.pointer(1920,1080,1920,1080,0));
 m=next();CHECK(m.message==WM_LBUTTONUP);m=next();CHECK(m.message==WM_XBUTTONUP&&HIWORD(m.wParam)==XBUTTON2);next();
 CHECK(!(async(VK_LBUTTON)&0x8000));
 // Wheel: signed delta in the high word, screen coordinates, Shift in the state.
 feed.key(VK_SHIFT,0x2A,false,true);next();
 CHECK(feed.wheel(960,540,1920,1080,-120,false));m=next();
 CHECK(m.message==WM_MOUSEWHEEL&&SHORT(HIWORD(m.wParam))==-120&&LOWORD(m.wParam)==MK_SHIFT&&m.lParam==MAKELPARAM(740,410));
 CHECK(feed.wheel(960,540,1920,1080,120,true));m=next();CHECK(m.message==WM_MOUSEHWHEEL&&SHORT(HIWORD(m.wParam))==120);
 CHECK(!feed.pointer(NAN,0,1920,1080,0)&&!feed.pointer(0,0,0,1080,0));

 // Deactivation/menu: everything held is released in the polling state.
 feed.key('W',0x11,false,true);next();feed.pointer(10,10,1920,1080,corewindow_input::Right);next();next();
 CHECK((state('W')&0x8000)&&(async(VK_RBUTTON)&0x8000)&&(state(VK_SHIFT)&0x8000));
 feed.release_all();
 CHECK(!(state('W')&0x8000)&&!(async(VK_RBUTTON)&0x8000)&&!(state(VK_SHIFT)&0x8000)&&!(state(VK_LSHIFT)&0x8000));
 CHECK(empty());
 // A destroyed presenting window stops delivery without failing the feed.
 CHECK(api<decltype(&DestroyWindow)>("DestroyWindow")(game));while(!empty()){}
 CHECK(feed.target()==nullptr);CHECK(feed.key('A',0x1E,false,true));CHECK(empty());CHECK(!feed.pointer(1,1,2,2,0));
 printf("PASS: corewindow feed %u checks (off-device; no console input claimed)\n",checks);
}
