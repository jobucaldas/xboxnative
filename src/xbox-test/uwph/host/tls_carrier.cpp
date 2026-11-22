// Windows-managed TLS only. Use the SDK CRT's standard TLS directory/lifecycle.
#include <windows.h>
#include <cstdint>
extern "C" {
#pragma section(".tls$AAB",long,read,write)
__declspec(allocate(".tls$AAB")) __declspec(thread) __declspec(align(16)) unsigned char carrier_bytes[65536]={};
extern DWORD _tls_index;
extern const IMAGE_TLS_DIRECTORY64 _tls_used;
extern unsigned char carrier_raw[65536];
#pragma comment(linker,"/alternatename:carrier_raw=carrier_bytes")
__declspec(dllexport) DWORD carrier_index() { return _tls_index; }
__declspec(dllexport) unsigned char* carrier_block() {
    uintptr_t prefix=(uintptr_t)carrier_raw-_tls_used.StartAddressOfRawData;
    // Only the unused CRT start sentinel may precede the guest area. Never
    // overwrite another compiler/runtime TLS variable if the CRT layout changes.
    if (prefix>16) return nullptr;
    return carrier_bytes-prefix;
}
// Play may point this slot at a larger heap block. Its hook puts the original
// back from this callback, which .CRT$XLB places before the CRT's own TLS
// callbacks (.CRT$XLC/XLD) and which runs before DllMain and after every FLS
// callback, so guest FLS callbacks still see guest TLS and the CRT never does.
static void (*volatile carrier_exit_hook)(DWORD);
__declspec(dllexport) void carrier_on_exit(void (*hook)(DWORD)) {
    InterlockedExchangePointer((void* volatile*)&carrier_exit_hook,(void*)hook);
}
static void NTAPI carrier_exit(PVOID,DWORD reason,PVOID) {
    auto hook=carrier_exit_hook;
    if (hook && (reason==DLL_THREAD_DETACH || reason==DLL_PROCESS_DETACH)) hook(_tls_index);
}
#pragma section(".CRT$XLB",long,read)
__declspec(allocate(".CRT$XLB")) extern const PIMAGE_TLS_CALLBACK carrier_exit_callback=carrier_exit;
#pragma comment(linker,"/include:carrier_exit_callback")
#ifdef CARRIER_A
__declspec(dllexport) DWORD carrier_anchor_a() { return _tls_index; }
#else
__declspec(dllexport) DWORD carrier_anchor_b() { return _tls_index; }
#endif
}
