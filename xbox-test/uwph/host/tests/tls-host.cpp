// OFF-DEVICE native TLS probe; Wine memory API substitutions are explicit.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
static std::string cleanupFailure;
static unsigned failuresInjected=0;
static bool failCleanup(const char* operation) {
    if (cleanupFailure!=operation || failuresInjected) return false;
    ++failuresInjected;return true;
}
static BOOL WINAPI checkedTlsFree(DWORD key) {
    return failCleanup("tls-free")?FALSE:TlsFree(key);
}
static BOOL WINAPI checkedDelete(PRUNTIME_FUNCTION table) {
    return failCleanup("unwind")?FALSE:RtlDeleteFunctionTable(table);
}
static BOOL WINAPI checkedFree(LPVOID address,SIZE_T size,DWORD type) {
    return failCleanup("memory")?FALSE:VirtualFree(address,size,type);
}
#define TlsFree checkedTlsFree
#define RtlDeleteFunctionTable checkedDelete
#define VirtualFree checkedFree
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
#include "../tls_test_runner.h"
int main(int argc,char** argv) {
    if (argc>2) return 2;
    if (argc==2) {
        cleanupFailure=argv[1];
        if (cleanupFailure!="tls-free" && cleanupFailure!="unwind" && cleanupFailure!="memory") return 2;
    }
    setvbuf(stdout,nullptr,_IONBF,0);
    HMODULE a=LoadLibraryW(L"out-tls/layout/tls-carrier-a.dll");
    HMODULE b=LoadLibraryW(L"out-tls/layout/tls-carrier-b.dll");
    bool ok=wxl::runTls(L"out-tls/layout",[](const char* s){puts(s);},a,b);
    if (a) FreeLibrary(a);
    if (b) FreeLibrary(b);
    if (!cleanupFailure.empty()) {
        bool rejected=!ok && failuresInjected==1;
        printf("TLS-CLEANUP-%s operation=%s injected=%u\n",rejected?"DETECTED":"MISSED",cleanupFailure.c_str(),failuresInjected);
        return rejected?0:1;
    }
    return ok?0:1;
}
