#include <windows.h>
#ifdef SEARCH_CONSUMER
extern "C" __declspec(dllimport) DWORD WINAPI search_dependency_value();
extern "C" __declspec(dllexport) DWORD WINAPI search_consumer_value() {
    return search_dependency_value();
}
#else
extern "C" __declspec(dllexport) DWORD WINAPI search_dependency_value() {
    return 77;
}
#endif
extern "C" BOOL WINAPI search_entry(HINSTANCE,DWORD,LPVOID) {return TRUE;}
// Force a real DIR64 relocation, as required by the trusted-image mapper.
extern "C" {
__declspec(dllexport) void* search_relocation=reinterpret_cast<void*>(&search_entry);
}
