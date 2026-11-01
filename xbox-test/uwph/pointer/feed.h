#pragma once
#include "../keyboard/feed.h"
#include <cmath>
#include <climits>
namespace pointer_core {
// CoreWindow coordinates are DIPs. Scale to the owning guest client rectangle,
// then use its coordinate conversions; never pass record HWNDs to native USER.
struct Feed {
    keyboard_core::TargetWindow foreground=nullptr;
    keyboard_core::PostMessage post=nullptr;
    keyboard_core::KeyEvent key=nullptr;
    BOOL (WINAPI* client_rect)(HWND,RECT*)=nullptr;
    BOOL (WINAPI* to_screen)(HWND,POINT*)=nullptr;
    BOOL (WINAPI* to_client)(HWND,POINT*)=nullptr;
    BOOL (WINAPI* set_position)(int,int)=nullptr;
    BOOL (WINAPI* get_position)(POINT*)=nullptr;
    SHORT (WINAPI* key_state)(int)=nullptr;
    unsigned buttons=0;
    void deactivate() {
        const unsigned flags[]={MK_LBUTTON,MK_RBUTTON,MK_MBUTTON,MK_XBUTTON1,MK_XBUTTON2};
        const int keys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
        for(unsigned i=0;i<5;++i)if((buttons&flags[i]) && key)key(keys[i],FALSE);
        buttons=0;
    }
    bool update(double x,double y,double width,double height,unsigned pressed) {
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)
            return false;
        HWND window=foreground?foreground():nullptr;
        if(!window || !client_rect || !to_screen || !to_client || !set_position || !get_position)return false;
        RECT rect{};
        if(!client_rect(window,&rect))return false;
        // Keep conversions defined even for a malformed or zero-sized record.
        double px=rect.left+x/width*(double(rect.right)-rect.left);
        double py=rect.top+y/height*(double(rect.bottom)-rect.top);
        if(!std::isfinite(px)||!std::isfinite(py)||px<LONG_MIN||px>LONG_MAX||py<LONG_MIN||py>LONG_MAX)return false;
        POINT point{LONG(px),LONG(py)};
        if(!to_screen(window,&point)||!set_position(point.x,point.y)||!get_position(&point))return false;
        // Read back owner clipping before constructing client-coordinate messages.
        if(!to_client(window,&point))return false;
        const unsigned flags[]={MK_LBUTTON,MK_RBUTTON,MK_MBUTTON,MK_XBUTTON1,MK_XBUTTON2};
        const int keys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
        const UINT down[]={WM_LBUTTONDOWN,WM_RBUTTONDOWN,WM_MBUTTONDOWN,WM_XBUTTONDOWN,WM_XBUTTONDOWN};
        const UINT up[]={WM_LBUTTONUP,WM_RBUTTONUP,WM_MBUTTONUP,WM_XBUTTONUP,WM_XBUTTONUP};
        const unsigned changed=buttons^pressed;
        buttons=pressed&(MK_LBUTTON|MK_RBUTTON|MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2);
        bool accepted=post!=nullptr;
        unsigned modifiers=0;
        if(key_state && (key_state(VK_SHIFT)&0x8000))modifiers|=MK_SHIFT;
        if(key_state && (key_state(VK_CONTROL)&0x8000))modifiers|=MK_CONTROL;
        LPARAM position=MAKELPARAM(point.x,point.y);
        for(unsigned i=0;i<5;++i)if(changed&flags[i]) {
            bool held=buttons&flags[i];
            if(key)key(keys[i],held);
            WPARAM state=buttons|modifiers;
            if(i>=3)state|=WPARAM(i==3?XBUTTON1:XBUTTON2)<<16;
            if(post && !post(window,held?down[i]:up[i],state,position))accepted=false;
        }
        if(post && !post(window,WM_MOUSEMOVE,buttons|modifiers,position))accepted=false;
        return accepted;
    }
};
}
