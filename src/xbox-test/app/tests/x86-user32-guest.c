// SPDX-License-Identifier: GPL-3.0-or-later
// i686 fixture for host-backed user32 timers and guest callbacks.
#include <windows.h>

static unsigned window_ticks, proc_ticks;
static DWORD window_start;
static void fail(unsigned code) { ExitProcess(code); }
static LRESULT CALLBACK window_proc(HWND h, UINT message, WPARAM wp, LPARAM lp) {
    (void)h; (void)wp; (void)lp;
    if (message == WM_TIMER) {
        ++window_ticks;
        if (GetTickCount() - window_start >= 200) PostQuitMessage(0);
    }
    return 0;
}
static void CALLBACK timer_proc(HWND h, UINT message, UINT_PTR id, DWORD tick) {
    (void)h; (void)id; (void)tick;
    if (message == WM_TIMER) ++proc_ticks;
}
void WINAPI entry(void) {
    WNDCLASSEXA cls = {0};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = "X86TimerWindow";
    if (!RegisterClassExA(&cls)) fail(1);
    HWND window = CreateWindowExA(0, cls.lpszClassName, "timer", 0, 0, 0, 1, 1, 0, 0, 0, 0);
    if (!window) fail(2);
    UINT_PTR id = SetTimer(window, 17, 20, 0);
    if (id != 17) fail(3);
    MSG message = {0};
    DWORD start = GetTickCount();
    window_start = start;
    while (GetMessageA(&message, window, 0, 0) > 0) {
        DispatchMessageA(&message);
        if (GetTickCount() - start > 500) fail(4);
    }
    if (window_ticks < 4 || window_ticks > 20) fail(5);
    if (!KillTimer(window, id)) fail(6);
    id = SetCoalescableTimer(window, 19, 20, timer_proc, 0);
    if (id != 19) fail(7);
    start = GetTickCount();
    while (proc_ticks < 6) {
        if (GetMessageA(&message, window, 0, 0) <= 0) fail(8);
        DispatchMessageA(&message);
        if (GetTickCount() - start > 500) fail(9);
    }
    if (proc_ticks < 4 || proc_ticks > 20 || !KillTimer(window, id)) fail(10);
    unsigned before = proc_ticks;
    start = GetTickCount();
    while (GetTickCount() - start < 80) {
        while (PeekMessageA(&message, window, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
        Sleep(5);
    }
    if (proc_ticks != before) fail(11);
    if (!DestroyWindow(window)) fail(12);
    ExitProcess(0);
}
