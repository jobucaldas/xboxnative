// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Keyboard and mouse from the launcher's CoreWindow into the guest's user32
// records (uwph-u32c.dll). The launcher resolves the record exports and calls
// this on its UI thread; nothing here touches native USER or a guest HWND
// outside the record DLL.
//
// Keys update the polling state (GetAsyncKeyState/GetKeyState/GetKeyboardState)
// and post WM_KEY*/WM_SYSKEY* to the guest window. WM_CHAR comes from the
// guest's own TranslateMessage, as on Windows, so characters are never posted
// twice. Mouse events move the record cursor and post button, move and wheel
// messages in client (buttons/move) or screen (wheel) coordinates.
// Not covered: raw input (WM_INPUT), relative mouse, touch and pen.
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace corewindow_input {

struct Exports {
    void (WINAPI* key)(int,BOOL)=nullptr;               // uwph_u32c_key_event
    BOOL (WINAPI* post)(HWND,UINT,WPARAM,LPARAM)=nullptr;
    HWND (WINAPI* focus)()=nullptr;
    HWND (WINAPI* foreground)()=nullptr;
    BOOL (WINAPI* is_window)(HWND)=nullptr;
    BOOL (WINAPI* client_rect)(HWND,RECT*)=nullptr;
    BOOL (WINAPI* to_screen)(HWND,POINT*)=nullptr;
    BOOL (WINAPI* to_client)(HWND,POINT*)=nullptr;
    BOOL (WINAPI* set_cursor)(int,int)=nullptr;
    BOOL (WINAPI* get_cursor)(POINT*)=nullptr;
    bool complete() const {
        return key&&post&&focus&&foreground&&is_window&&client_rect&&to_screen&&to_client&&set_cursor&&get_cursor;
    }
};

// Xbox delivers controller and navigation input as virtual keys as well; the
// controller already reaches the guest through XInput/Windows.Gaming.Input.
inline bool controller_key(unsigned vk) {
    return (vk>=0x88&&vk<=0x8F) || (vk>=0xC3&&vk<=0xDA);
}

enum : unsigned { Left=MK_LBUTTON, Right=MK_RBUTTON, Middle=MK_MBUTTON, X1=MK_XBUTTON1, X2=MK_XBUTTON2 };

struct Feed {
    Exports api;
    HWND fallback=nullptr;          // the window whose swap chain is on screen
    std::array<bool,256> down{};
    unsigned buttons=0;

    // Focus, then foreground, then the presenting window: games that never
    // call SetFocus/SetForegroundWindow still receive their input.
    HWND target() const {
        if(!api.complete())return nullptr;
        if(HWND h=api.focus())return h;
        if(HWND h=api.foreground())return h;
        return fallback&&api.is_window(fallback)?fallback:nullptr;
    }

    void set(unsigned vk,bool pressed) {
        if(vk>=down.size()||down[vk]==pressed)return;
        down[vk]=pressed;api.key(int(vk),pressed);
    }

    // One key transition. Returns false when the key is not for the guest.
    bool key(unsigned vk,unsigned scan,bool extended,bool pressed,unsigned repeats=1) {
        if(!api.complete()||vk==0||vk>=down.size()||controller_key(vk))return false;
        // CoreWindow reports generic modifiers; keep both sides as Windows does.
        unsigned side=vk;
        if(vk==VK_SHIFT)side=scan==0x36?VK_RSHIFT:VK_LSHIFT;
        else if(vk==VK_CONTROL)side=extended?VK_RCONTROL:VK_LCONTROL;
        else if(vk==VK_MENU)side=extended?VK_RMENU:VK_LMENU;
        unsigned generic=side;
        if(side==VK_LSHIFT||side==VK_RSHIFT)generic=VK_SHIFT;
        else if(side==VK_LCONTROL||side==VK_RCONTROL)generic=VK_CONTROL;
        else if(side==VK_LMENU||side==VK_RMENU)generic=VK_MENU;
        const bool wasDown=down[side];
        set(side,pressed);
        if(generic==VK_SHIFT)set(VK_SHIFT,down[VK_LSHIFT]||down[VK_RSHIFT]);
        else if(generic==VK_CONTROL)set(VK_CONTROL,down[VK_LCONTROL]||down[VK_RCONTROL]);
        else if(generic==VK_MENU)set(VK_MENU,down[VK_LMENU]||down[VK_RMENU]);
        else if(pressed&&wasDown)api.key(int(side),TRUE); // auto-repeat still counts as a press
        HWND window=target();
        if(!window)return true;
        // Alt held (or F10) makes a system key, unless Ctrl is also held.
        const bool alt=down[VK_MENU];
        const bool system=(alt&&!down[VK_CONTROL])||generic==VK_F10||(generic==VK_MENU&&!down[VK_CONTROL]);
        UINT message=system?(pressed?WM_SYSKEYDOWN:WM_SYSKEYUP):(pressed?WM_KEYDOWN:WM_KEYUP);
        DWORD bits=std::clamp(repeats,1u,0xffffu)|((scan&0xffu)<<16);
        if(extended)bits|=1u<<24;
        if(alt&&system)bits|=1u<<29;
        if(wasDown||!pressed)bits|=1u<<30;
        if(!pressed)bits|=1u<<31;
        api.post(window,message,generic,LPARAM(bits));
        return true;
    }

    // Mouse position in CoreWindow DIPs plus the full button set (MK_* bits).
    bool pointer(double x,double y,double width,double height,unsigned pressed) {
        POINT client;HWND window;
        if(!place(x,y,width,height,client,window))return false;
        const unsigned flags[]={Left,Right,Middle,X1,X2};
        const int keys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
        const UINT downs[]={WM_LBUTTONDOWN,WM_RBUTTONDOWN,WM_MBUTTONDOWN,WM_XBUTTONDOWN,WM_XBUTTONDOWN};
        const UINT ups[]={WM_LBUTTONUP,WM_RBUTTONUP,WM_MBUTTONUP,WM_XBUTTONUP,WM_XBUTTONUP};
        pressed&=Left|Right|Middle|X1|X2;
        const unsigned changed=buttons^pressed;
        const LPARAM at=MAKELPARAM(client.x,client.y);
        for(unsigned i=0;i<5;++i) if(changed&flags[i]) {
            const bool held=(pressed&flags[i])!=0;
            buttons=held?buttons|flags[i]:buttons&~flags[i];
            api.key(keys[i],held);
            WPARAM state=modifiers()|buttons;
            if(i>=3)state|=WPARAM(i==3?XBUTTON1:XBUTTON2)<<16;
            api.post(window,held?downs[i]:ups[i],state,at);
        }
        api.post(window,WM_MOUSEMOVE,modifiers()|buttons,at);
        return true;
    }

    // Wheel delta in WHEEL_DELTA units; lParam is the cursor in screen coordinates.
    bool wheel(double x,double y,double width,double height,int delta,bool horizontal) {
        POINT client;HWND window;
        if(!place(x,y,width,height,client,window))return false;
        POINT screen{};api.get_cursor(&screen);
        WPARAM state=MAKEWPARAM(WORD(modifiers()|buttons),WORD(SHORT(std::clamp(delta,-32768,32767))));
        api.post(window,horizontal?WM_MOUSEHWHEEL:WM_MOUSEWHEEL,state,MAKELPARAM(screen.x,screen.y));
        return true;
    }

    // Focus loss or the launcher menu: nothing may stay held in the guest.
    void release_all() {
        if(!api.key)return;
        for(unsigned vk=0;vk<down.size();++vk)set(vk,false);
        const int keys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
        const unsigned flags[]={Left,Right,Middle,X1,X2};
        for(unsigned i=0;i<5;++i)if(buttons&flags[i])api.key(keys[i],FALSE);
        buttons=0;
    }

private:
    unsigned modifiers() const {
        return (down[VK_SHIFT]?MK_SHIFT:0)|(down[VK_CONTROL]?MK_CONTROL:0);
    }
    // DIPs scale to the guest client area; the record cursor applies ClipCursor.
    bool place(double x,double y,double width,double height,POINT& client,HWND& window) {
        if(!std::isfinite(x)||!std::isfinite(y)||!(width>0)||!(height>0))return false;
        window=target();
        RECT rect{};
        if(!window||!api.client_rect(window,&rect))return false;
        const double cx=std::clamp(x/width,0.0,1.0)*(rect.right-rect.left);
        const double cy=std::clamp(y/height,0.0,1.0)*(rect.bottom-rect.top);
        client={rect.left+LONG(std::min(cx,double(std::max(0L,rect.right-rect.left-1)))),
                rect.top+LONG(std::min(cy,double(std::max(0L,rect.bottom-rect.top-1))))};
        POINT screen=client;
        if(!api.to_screen(window,&screen)||!api.set_cursor(screen.x,screen.y)||!api.get_cursor(&screen))return false;
        client=screen;
        return api.to_client(window,&client)!=FALSE;
    }
};

}
