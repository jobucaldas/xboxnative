#include <windows.h>
#include <cstdio>

using LockFn = void (__cdecl *)(int);
static LockFn lockit_lock;
static LockFn lockit_unlock;
static LONG entries;

static DWORD WINAPI contend(void*) {
    for (int i = 0; i < 50000; ++i) {
        lockit_lock(0);
        InterlockedIncrement(&entries);
        lockit_unlock(0);
    }
    return 0;
}

int main() {
    if (!SetDllDirectoryW(L".")) return 1;
    HMODULE runtime = LoadLibraryW(L"msvcp140.dll");
    if (!runtime) {
        std::fprintf(stderr, "FAIL load msvcp140.dll error=%lu\n", GetLastError());
        return 1;
    }
    lockit_lock = reinterpret_cast<LockFn>(GetProcAddress(runtime, "?_Lockit_ctor@_Lockit@std@@SAXH@Z"));
    lockit_unlock = reinterpret_cast<LockFn>(GetProcAddress(runtime, "?_Lockit_dtor@_Lockit@std@@SAXH@Z"));
    if (!lockit_lock || !lockit_unlock) {
        std::fprintf(stderr, "FAIL resolve msvcp140 _Lockit exports error=%lu\n", GetLastError());
        return 1;
    }

    HANDLE threads[2] = {
        CreateThread(nullptr, 0, contend, nullptr, 0, nullptr),
        CreateThread(nullptr, 0, contend, nullptr, 0, nullptr)
    };
    if (!threads[0] || !threads[1]) {
        std::fprintf(stderr, "FAIL create contention threads error=%lu\n", GetLastError());
        return 1;
    }
    DWORD wait = WaitForMultipleObjects(2, threads, TRUE, 30000);
    if (wait != WAIT_OBJECT_0 || entries != 100000) {
        std::fprintf(stderr, "FAIL _Lockit contention wait=%lu entries=%ld error=%lu\n", wait, entries, GetLastError());
        return 1;
    }
    CloseHandle(threads[0]);
    CloseHandle(threads[1]);
    FreeLibrary(runtime);
    std::printf("Wine msvcp140 _Lockit contention: PASS (2 threads, %ld entries)\n", entries);
    return 0;
}
