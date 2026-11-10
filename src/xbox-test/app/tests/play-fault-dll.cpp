// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a DLL that faults inside itself, mapped by Play like the D3D9
// shim and the shader translator are. Play must record the crash with the
// faulting module and offset instead of the launcher dying silently.
//
// The fault is a null write at the bottom of a non-leaf frame, so the OS has to
// unwind through this image's own .pdata to reach the guest thread's handler.
// That is the property src/xbox-test/uwph/d3d9/check-unwind-data.py enforces on
// the shims: without an exception directory the fault is never dispatched and
// no record exists at all (see build.sh there).
#include <windows.h>

extern "C" {
int play_fault_anchor;
void* play_fault_relocation=&play_fault_anchor;

__declspec(noinline) static int descend(int depth) {
    volatile char pad[256];
    pad[0]=char(depth);
    if(depth<=0) {
        volatile int* nowhere=nullptr;
        *nowhere=1;             // the fault, inside this DLL
        return pad[0];
    }
    return descend(depth-1)+pad[0];
}

__declspec(dllexport) int fault_here() { return descend(3); }
}
