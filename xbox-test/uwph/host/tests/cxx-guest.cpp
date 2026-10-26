// Microsoft exception ABI, linked into the MinGW-target native TLS guests.
// The host supplies real CRT handlers; no substitute exception behavior.
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char*);
extern "C" __declspec(dllimport) void wxl_delete(void*);
void operator delete(void* p, decltype(sizeof(0))) noexcept { wxl_delete(p); }
#ifdef TLS_DLL
struct Guard { ~Guard() { OutputDebugStringA("WXL CXX mapped DLL destructor ran"); } };
extern "C" __declspec(dllexport) void cxx_throw() noexcept(false) {
    Guard guard;
    throw 73;
}
#else
extern "C" __declspec(dllimport) void cxx_throw() noexcept(false);
extern "C" __declspec(dllexport) int cxx_catch() noexcept(false) {
    try { cxx_throw(); }
    catch (int value) {
        if (value==73) { OutputDebugStringA("WXL CXX mapped EXE caught 73"); return 1; }
    }
    return 0;
}
#endif
