#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// The process's one keyboard/mouse feed into these records (corewindow_feed.h),
// owned here so every injector shares it: the launcher's CoreWindow input
// (uwph_u32c_input_* exports) and the guest's SendInput. Both therefore see the
// same held-key mirror, side tracking, lParams, target window and release_all.
// Included inside namespace u32c after the window, cursor and message code;
// core.cpp includes corewindow_feed.h at file scope.
static corewindow_input::Feed& sharedFeed() {
    static corewindow_input::Feed feed=[] {
        corewindow_input::Feed f;auto& a=f.api;
        a.key=&uwph_u32c_key_event;a.post=&c_PostMessageW;a.focus=&c_GetFocus;a.foreground=&c_GetForegroundWindow;
        a.is_window=&c_IsWindow;a.client_rect=&c_GetClientRect;a.to_screen=&c_ClientToScreen;a.to_client=&c_ScreenToClient;
        a.set_cursor=&c_SetCursorPos;a.get_cursor=&c_GetCursorPos;
        return f;
    }();
    return feed;
}
extern "C" BOOL WINAPI uwph_u32c_input_key(UINT vk,UINT scan,BOOL extended,BOOL pressed,UINT repeats){std::lock_guard<std::recursive_mutex> l(mutex);return sharedFeed().key(vk,scan,extended!=FALSE,pressed!=FALSE,repeats);}
extern "C" BOOL WINAPI uwph_u32c_input_pointer(double x,double y,double w,double h,UINT pressed){std::lock_guard<std::recursive_mutex> l(mutex);return sharedFeed().pointer(x,y,w,h,pressed);}
extern "C" BOOL WINAPI uwph_u32c_input_wheel(double x,double y,double w,double h,int delta,BOOL horizontal){std::lock_guard<std::recursive_mutex> l(mutex);return sharedFeed().wheel(x,y,w,h,delta,horizontal!=FALSE);}
extern "C" void WINAPI uwph_u32c_input_release_all(){std::lock_guard<std::recursive_mutex> l(mutex);sharedFeed().release_all();}
extern "C" void WINAPI uwph_u32c_input_set_fallback(HWND h){std::lock_guard<std::recursive_mutex> l(mutex);sharedFeed().fallback=h;}
