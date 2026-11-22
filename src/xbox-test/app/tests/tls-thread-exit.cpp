// SPDX-License-Identifier: GPL-3.0-or-later
// Large guest static TLS swaps the native carrier's TEB slot for a heap block.
// On thread exit every FLS callback must still see the guest block, and the
// real carrier's own detach must get its original block back, with nothing leaked.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static LONG allocs,frees;
static LPVOID WINAPI counted_alloc(HANDLE h,DWORD f,SIZE_T n) { auto p=HeapAlloc(h,f,n); if(p) InterlockedIncrement(&allocs); return p; }
static BOOL WINAPI counted_free(HANDLE h,DWORD f,LPVOID p) { BOOL ok=HeapFree(h,f,p); if(ok) InterlockedIncrement(&frees); return ok; }
#define HeapAlloc counted_alloc
#define HeapFree counted_free
#define VirtualProtectFromApp VirtualProtect
#include "../host/wxl_mapper.h"
#undef HeapAlloc
#undef HeapFree
static void check(bool ok,const char* what) { if(!ok) { printf("TLS-THREAD-EXIT FAIL: %s\n",what); fflush(stdout); ExitProcess(1); } }
static const size_t guestSize=0x50000;   // well past the carrier's 64 KiB
static wxl::Carrier carrier;
static void (*nativeOnExit)(void (*)(DWORD));
static void (*mapperHook)(DWORD);
static BYTE* own;          // carrier's original block on the test thread
static BYTE* guest;        // swapped-in guest block on the test thread
static bool lateSawGuest,lateSawIntact,hookSawGuest,hookRestored;
static LONG hookCalls;
static volatile DWORD naturalThread;
static BYTE** slots() { return (BYTE**)__readgsqword(0x58); }
static void probe_exit(DWORD index) {
    InterlockedIncrement(&hookCalls);
    if(!mapperHook) return;
    if(index!=carrier.index() || GetCurrentThreadId()!=naturalThread) { mapperHook(index); return; }
    hookSawGuest=slots()[index]==guest;
    mapperHook(index);
    hookRestored=slots()[index]==own;
}
static void register_probe(void (*hook)(DWORD)) { mapperHook=hook; nativeOnExit(probe_exit); }
// Allocated after the mapper's own bookkeeping, like a guest CRT's FLS slot.
static VOID CALLBACK late_fls(PVOID) {
    BYTE* current=slots()[carrier.index()];
    lateSawGuest=current==guest;
    lateSawIntact=lateSawGuest && current[0]==0xAB && current[guestSize-1]==0xAB;
}
static DWORD WINAPI natural_exit(void*) {
    naturalThread=GetCurrentThreadId();
    own=carrier.block();
    guest=wxl::threadTlsBlock(carrier,carrier.index(),guestSize);
    if(!guest || guest==own || slots()[carrier.index()]!=guest) return 1;
    memset(guest,0xAB,guestSize);
    DWORD late=FlsAlloc(late_fls);
    if(late==FLS_OUT_OF_INDEXES || !FlsSetValue(late,(void*)1)) return 2;
    return 0;   // no explicit release: the normal Play worker teardown path
}
static DWORD WINAPI explicit_release(void*) {
    BYTE* original=carrier.block();
    BYTE* block=wxl::threadTlsBlock(carrier,carrier.index(),guestSize);
    if(!block || slots()[carrier.index()]!=block) return 1;
    wxl::releaseThreadTlsBlock(carrier.index());
    return slots()[carrier.index()]==original?0:2;
}
static DWORD WINAPI no_hook(void*) {
    wxl::Carrier bare=carrier; bare.onExit=nullptr;
    BYTE* original=carrier.block();
    return !wxl::threadTlsBlock(bare,carrier.index(),guestSize) && slots()[carrier.index()]==original?0:1;
}
static DWORD run(LPTHREAD_START_ROUTINE f) {
    HANDLE t=CreateThread(nullptr,0,f,nullptr,0,nullptr); check(t!=nullptr,"CreateThread");
    WaitForSingleObject(t,INFINITE); DWORD code=99; GetExitCodeThread(t,&code); CloseHandle(t); return code;
}
int main(int argc,char** argv) {
    check(argc==2,"usage: tls-thread-exit.exe carrier.dll");
    HMODULE h=LoadLibraryA(argv[1]); check(h!=nullptr,"load carrier");
    carrier.index=(DWORD(*)())GetProcAddress(h,"carrier_index");
    carrier.block=(BYTE*(*)())GetProcAddress(h,"carrier_block");
    nativeOnExit=(void(*)(void(*)(DWORD)))GetProcAddress(h,"carrier_on_exit");
    check(carrier.index && carrier.block && nativeOnExit,"carrier exports");
    carrier.onExit=register_probe;
    // The exit hook must be the carrier's FIRST TLS callback, ahead of its CRT's.
    auto nt=(IMAGE_NT_HEADERS64*)((BYTE*)h+((IMAGE_DOS_HEADER*)h)->e_lfanew);
    auto td=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    check(td.Size>=sizeof(IMAGE_TLS_DIRECTORY64),"carrier TLS directory");
    auto callbacks=(PIMAGE_TLS_CALLBACK*)((IMAGE_TLS_DIRECTORY64*)((BYTE*)h+td.VirtualAddress))->AddressOfCallBacks;
    check(callbacks && callbacks[0],"carrier TLS callbacks");
    nativeOnExit(probe_exit);
    LONG before=hookCalls; callbacks[0](h,DLL_THREAD_DETACH,nullptr);
    check(hookCalls==before+1,"carrier's first TLS callback runs the exit hook");
    check(run(no_hook)==0,"carrier without carrier_on_exit refuses a large template and keeps its slot");
    check(run(explicit_release)==0,"explicit release restores the carrier's slot");
    check(allocs==frees,"explicit release frees its block");
    check(run(natural_exit)==0,"swap guest block");
    check(lateSawGuest,"a later FLS callback sees the guest block, not the carrier's");
    check(lateSawIntact,"guest TLS contents intact during FLS callbacks");
    check(hookSawGuest,"guest block still in place when the carrier's detach begins");
    check(hookRestored,"carrier's original block restored before its CRT TLS callbacks and DllMain");
    check(allocs>0 && allocs==frees,"thread exit frees every expanded block");
    puts("TLS-THREAD-EXIT PASS: FLS callbacks see guest TLS; carrier detach gets its own block back; no leak");
    return 0;
}
