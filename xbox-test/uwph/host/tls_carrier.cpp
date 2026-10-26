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
#ifdef CARRIER_A
__declspec(dllexport) DWORD carrier_anchor_a() { return _tls_index; }
#else
__declspec(dllexport) DWORD carrier_anchor_b() { return _tls_index; }
#endif
}
