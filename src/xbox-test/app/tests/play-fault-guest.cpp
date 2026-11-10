// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE that loads play-fault.dll and calls into it, where
// the fault happens. Play must log a crash record naming the faulting module and
// offset, and only that guest thread must end.
//
// Exit 42 is unreachable: the fault is the point. The runner asserts the crash
// record instead, and that Play did not die with the thread.
#include <windows.h>

extern "C" {
int fault_guest_anchor;
void* fault_guest_relocation=&fault_guest_anchor;

DWORD fault_guest_entry() {
    HMODULE module=LoadLibraryW(L"play-fault.dll");
    if(!module)return 10;
    auto fault=(int(*)())GetProcAddress(module,"fault_here");
    if(!fault)return 11;
    fault();                    // faults inside the mapped DLL
    return 12;                  // never reached
}
}
