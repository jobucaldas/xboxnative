#pragma once
// Included inside play_runtime, after child-thread module initialization.
// Native ExitThread cannot see mapped PE images. Deliver their detach notices
// while static TLS carriers are still alive, then let Windows end the thread
// and run native FLS cleanup. Never substitute ExitProcess or TerminateThread.
#include "guest_thread_notifications.h"
static void detachGuestThread() {
    static thread_local bool detaching=false;
    if(detaching)return;
    detaching=true;
    notifyGuestThreadDetach();
}
static void WINAPI kernelExitThread(DWORD code) {
    using Exit=void (WINAPI*)(DWORD);
    auto nativeExit=(Exit)kernel_tail::native("ExitThread");
    if(!nativeExit) {stopMissing("kernel32.dll!ExitThread");return;}
    if(guestThread)detachGuestThread();
    if(GetCurrentThreadId()==state->thread.load()) {
        // The runtime monitor waits for this worker, even when its guest EXE
        // explicitly exits the initial thread rather than returning its entry.
        state->code=code;state->success=true;
        state->event("EXE thread exited code="+std::to_string(code));
        state->result=state->snapshot();state->finished=true;
    }
    nativeExit(code);
}
static void WINAPI kernelFreeLibraryAndExitThread(HMODULE module,DWORD code) {
    // Return into runtime code before releasing guest executable pages. Native
    // FreeLibraryAndExitThread cannot unload images absent from the OS loader.
    freeLibrary(module);
    kernelExitThread(code);
}
