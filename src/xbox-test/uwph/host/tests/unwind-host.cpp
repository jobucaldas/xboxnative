// OFF-DEVICE ONLY: Wine harness substitutes desktop allocation APIs for FromApp.
// Console execution must use the real UWP main.cpp without these substitutions.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
static bool throwThroughGuest=false;
static void output(const char* message) {
    puts(message);
    if (throwThroughGuest && !strcmp(message,"WXL DLL fixture ATTACH")) throw 73;
}
int wmain(int argc, wchar_t** argv) {
    if (argc!=2 && argc!=3) return 2;
    throwThroughGuest=argc==3 && !wcscmp(argv[2],L"--throw-through-guest");
    try {
        bool ok=wxl::run(argv[1],output);
        return throwThroughGuest?3:ok?0:1;
    } catch (int value) {
        if (!throwThroughGuest || value!=73) return 4;
        puts("OFF-DEVICE C++ exception unwound through mapped DLL and was caught in host");
        return 0;
    }
}
