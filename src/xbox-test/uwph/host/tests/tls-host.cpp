// OFF-DEVICE native TLS probe; Wine memory API substitutions are explicit.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
#include "../tls_test_runner.h"
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE a=LoadLibraryW(L"out-tls/layout/tls-carrier-a.dll");
    HMODULE b=LoadLibraryW(L"out-tls/layout/tls-carrier-b.dll");
    bool ok=wxl::runTls(L"out-tls/layout",[](const char* s){puts(s);},a,b);
    if (a) FreeLibrary(a);
    if (b) FreeLibrary(b);
    return ok?0:1;
}
