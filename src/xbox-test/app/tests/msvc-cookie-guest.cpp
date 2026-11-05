// SPDX-License-Identifier: GPL-3.0-or-later
// Owned MSVC-ABI fixture. The real static CRT supplies its load-config record,
// cookie and complement; tests neither infer offsets nor patch CRT/game files.
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
// Check the pure validator's bounded prefix against the real SDK, not a guessed
// CRT layout. Newer SDK structures may extend beyond the accepted 0x140 record.
#define LC_OFFSET(field, offset) static_assert(offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64,field)==offset,"SDK load-config offset: " #field)
LC_OFFSET(SecurityCookie,0x58);
LC_OFFSET(GuardCFCheckFunctionPointer,0x70);
LC_OFFSET(GuardCFDispatchFunctionPointer,0x78);
LC_OFFSET(GuardFlags,0x90);
LC_OFFSET(VolatileMetadataPointer,0x100);
LC_OFFSET(GuardXFGCheckFunctionPointer,0x118);
LC_OFFSET(GuardXFGDispatchFunctionPointer,0x120);
LC_OFFSET(GuardXFGTableDispatchFunctionPointer,0x128);
LC_OFFSET(CastGuardOsDeterminedFailureMode,0x130);
LC_OFFSET(GuardMemcpyFunctionPointer,0x138);
#undef LC_OFFSET
extern "C" uintptr_t __security_cookie, __security_cookie_complement;
extern "C" __declspec(dllimport) void observe_cookie(unsigned,uintptr_t,uintptr_t);
extern "C" BOOL WINAPI _DllMainCRTStartup(HINSTANCE,DWORD,LPVOID);
static void NTAPI tls_observer(void*,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH) observe_cookie(1,__security_cookie,0);
}
#pragma section(".CRT$XLB",long,read)
extern "C" __declspec(allocate(".CRT$XLB")) PIMAGE_TLS_CALLBACK fixture_tls_callback=tls_observer;
#pragma comment(linker,"/include:fixture_tls_callback")
#pragma comment(linker,"/include:_tls_used")
extern "C" BOOL WINAPI fixture_entry(HINSTANCE module,DWORD reason,LPVOID reserved) {
    if(reason==DLL_PROCESS_ATTACH) observe_cookie(2,__security_cookie,0);
    return _DllMainCRTStartup(module,reason,reserved);
}
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) observe_cookie(3,__security_cookie,__security_cookie_complement);
    return TRUE;
}
static int destructors;
struct Guard { ~Guard(){ ++destructors; } };
__declspec(noinline) static void raise_value() { Guard guard; throw 73; }
extern "C" __declspec(dllexport) int fixture_check() {
    // These are actual linker symbols, not an assumed adjacency contract.
    if(!__security_cookie || __security_cookie==0x2b992ddfa232ULL ||
       (__security_cookie>>48) || __security_cookie_complement!=~__security_cookie) return 1;
    destructors=0;
    try { raise_value(); } catch(int value) { return value==73 && destructors==1 ? 42 : 2; }
    return 3;
}
