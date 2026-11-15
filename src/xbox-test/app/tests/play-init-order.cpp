#include <windows.h>
extern "C" { int play_init_anchor; void* play_init_relocation=&play_init_anchor; }

#if defined(PLAY_INIT_PARENT)
static volatile LONG ready;
extern "C" __declspec(dllexport) int play_init_parent_ready() { return ready; }
extern "C" BOOL WINAPI parent_entry(HINSTANCE, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!LoadLibraryW(L"play-init-child.dll")) return FALSE;
        InterlockedExchange(&ready, 1);
    }
    return TRUE;
}
#elif defined(PLAY_INIT_CHILD)
extern "C" BOOL WINAPI child_entry(HINSTANCE, DWORD, void*) { return TRUE; }
#elif defined(PLAY_INIT_PEER)
extern "C" __declspec(dllimport) int play_init_parent_ready();
static volatile LONG observedEarly;
static void NTAPI peer_tls(void*, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH && !play_init_parent_ready()) InterlockedExchange(&observedEarly, 1);
}
thread_local int tls_value = 73;
DWORD _tls_index;
extern BYTE tls_raw[8];
#pragma comment(linker, "/alternatename:tls_raw=tls_value")
PIMAGE_TLS_CALLBACK callbacks[] = {peer_tls, nullptr};
extern const IMAGE_TLS_DIRECTORY64 _tls_used = {
    (ULONGLONG)tls_raw, (ULONGLONG)(tls_raw + 4), (ULONGLONG)&_tls_index,
    (ULONGLONG)callbacks, 4, IMAGE_SCN_ALIGN_4BYTES};
extern "C" __declspec(dllexport) int play_init_peer_ok() { return !observedEarly; }
extern "C" BOOL WINAPI peer_entry(HINSTANCE, DWORD, void*) { return TRUE; }
#else
extern "C" __declspec(dllimport) int play_init_parent_ready();
extern "C" __declspec(dllimport) int play_init_peer_ok();
extern "C" DWORD WINAPI loader_init_entry() {
    return play_init_parent_ready() && play_init_peer_ok() ? 42 : 1;
}
#endif
