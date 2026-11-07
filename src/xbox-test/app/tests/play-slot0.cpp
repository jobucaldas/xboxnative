// SPDX-License-Identifier: GPL-3.0-or-later
// Owned fixture: a guest EXE whose code reads its static TLS the way MSVC /GA
// does, through TEB slot 0 (gs:[0x58] -> [0]) and never through _tls_index, with
// the _Init_thread_epoch-style template value INT_MIN at offset 0x70.
#include <windows.h>
#define INT_MIN (-2147483647-1)
extern "C" {
DWORD _tls_index;
struct Template {unsigned char pad[0x70];int epoch;int marker;};
Template slot0_template={{},INT_MIN,0x5a5a};
extern const IMAGE_TLS_DIRECTORY64 _tls_used={
    (ULONGLONG)&slot0_template,(ULONGLONG)(&slot0_template+1),(ULONGLONG)&_tls_index,0,8,IMAGE_SCN_ALIGN_16BYTES};
}
static int slot0Read(long long offset) {
    int value;
    __asm__ volatile("movq %%gs:0x58,%%rax\n\tmovq (%%rax),%%rax\n\tmovl (%%rax,%1),%0":"=r"(value):"r"(offset):"rax","memory");
    return value;
}
static void slot0Write(long long offset,int value) {
    __asm__ volatile("movq %%gs:0x58,%%rax\n\tmovq (%%rax),%%rax\n\tmovl %1,(%%rax,%0)"::"r"(offset),"r"(value):"rax","memory");
}
// Every thread starts from the template (never a sibling's write) and keeps
// its own copy: the zero-fill after the raw data is zero too.
static bool pristine() {return slot0Read(0x70)==INT_MIN&&slot0Read(0x74)==0x5a5a&&slot0Read(0x78)==0&&slot0Read(0x7c)==0;}
static DWORD WINAPI child(void* value) {
    if(!pristine())return 1;
    slot0Write(0x70,(int)(INT_PTR)value);
    return slot0Read(0x70)==(int)(INT_PTR)value?42:2;
}
extern "C" DWORD slot0_exe_entry() {
    if(_tls_index!=0)return 10;     // the guest's own index word, set by Play
    if(!pristine())return 11;
    slot0Write(0x70,7);
    for(int i=0;i<2;++i) {          // two Play-created threads, one after another
        HANDLE thread=CreateThread(nullptr,0,child,(void*)(INT_PTR)(100+i),0,nullptr);
        DWORD code=0;
        if(!thread||WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0||!GetExitCodeThread(thread,&code))return 12;
        CloseHandle(thread);
        if(code!=42)return 20+code;
    }
    return slot0Read(0x70)==7?42:13;   // this thread's copy was not disturbed
}
