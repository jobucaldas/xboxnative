#pragma once
#include <windows.h>
#include <array>
#include <algorithm>
namespace keyboard_core {
using KeyEvent=void(WINAPI*)(int,BOOL);
using TargetWindow=HWND(WINAPI*)();
using PostMessage=BOOL(WINAPI*)(HWND,UINT,WPARAM,LPARAM);
// UI-thread feed into the owning record registry. This contains no second
// window model or queue. Scalar key messages enter the owning user32 queue.
struct Feed {
    KeyEvent sink=nullptr;
    TargetWindow focus=nullptr,foreground=nullptr;
    PostMessage post=nullptr;
    std::array<bool,256> down{};
    void set(unsigned key,bool pressed) {
        if(key>=down.size())return;
        down[key]=pressed;if(sink)sink(key,pressed);
    }
    void key(unsigned vk,unsigned scan,bool extended,bool pressed,unsigned repeats=1) {
        if(vk>=down.size())return;
        if(vk==VK_SHIFT)vk=scan==0x36?VK_RSHIFT:VK_LSHIFT;
        else if(vk==VK_CONTROL)vk=extended?VK_RCONTROL:VK_LCONTROL;
        else if(vk==VK_MENU)vk=extended?VK_RMENU:VK_LMENU;
        const bool wasDown=down[vk];
        const bool alt=down[VK_MENU] || vk==VK_LMENU || vk==VK_RMENU;
        set(vk,pressed);
        if(vk==VK_LSHIFT || vk==VK_RSHIFT)set(VK_SHIFT,down[VK_LSHIFT]||down[VK_RSHIFT]);
        if(vk==VK_LCONTROL || vk==VK_RCONTROL)set(VK_CONTROL,down[VK_LCONTROL]||down[VK_RCONTROL]);
        if(vk==VK_LMENU || vk==VK_RMENU)set(VK_MENU,down[VK_LMENU]||down[VK_RMENU]);
        HWND target=focus?focus():nullptr;
        if(!target && foreground)target=foreground();
        if(post && target) {
            const bool system=alt || vk==VK_F10;
            UINT message=system?(pressed?WM_SYSKEYDOWN:WM_SYSKEYUP):(pressed?WM_KEYDOWN:WM_KEYUP);
            unsigned messageKey=vk;
            if(vk==VK_LSHIFT || vk==VK_RSHIFT)messageKey=VK_SHIFT;
            if(vk==VK_LCONTROL || vk==VK_RCONTROL)messageKey=VK_CONTROL;
            if(vk==VK_LMENU || vk==VK_RMENU)messageKey=VK_MENU;
            DWORD bits=std::min(0xffffu,std::max(1u,repeats)) | ((scan&0xffu)<<16);
            if(extended)bits|=1u<<24;
            if(alt)bits|=1u<<29;
            if(wasDown || !pressed)bits|=1u<<30;
            if(!pressed)bits|=1u<<31;
            post(target,message,messageKey,LPARAM(bits));
        }
    }
    void deactivate() {
        // Focus loss must never leave a held key stuck in GetAsyncKeyState.
        for(unsigned key=0;key<down.size();++key)if(down[key])set(key,false);
    }
    bool character(unsigned utf16,unsigned scan,bool extended,bool menuDown,
                   bool wasDown,bool released,unsigned repeats=1) {
        // CoreWindow supplies UTF-16 code units, including separate surrogate
        // events. Preserve them; do not translate the virtual key a second time.
        // https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.characterreceivedeventargs.keycode
        if(utf16>0xffff || !post)return false;
        HWND target=focus?focus():nullptr;
        if(!target && foreground)target=foreground();
        if(!target)return false;
        DWORD bits=std::min(0xffffu,std::max(1u,repeats)) | ((scan&0xffu)<<16);
        if(extended)bits|=1u<<24;
        if(menuDown)bits|=1u<<29;
        if(wasDown)bits|=1u<<30;
        if(released)bits|=1u<<31;
        return !!post(target,menuDown?WM_SYSCHAR:WM_CHAR,utf16,LPARAM(bits));
    }
};
}
