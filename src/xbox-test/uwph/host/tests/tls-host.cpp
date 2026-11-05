// OFF-DEVICE native TLS probe; Wine memory API substitutions are explicit.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
static std::string cleanupFailure;
static unsigned failuresInjected=0;
static DWORD mainThread=0;
static volatile LONG workerTlsSeen=0;
static unsigned workerTlsReads=0;
static unsigned releases=0;
static std::vector<void*> allocated,freed;
static HANDLE liveWorker=nullptr;
static bool failCleanup(const char* operation) {
    if (cleanupFailure!=operation || failuresInjected) return false;
    ++failuresInjected;return true;
}
static BOOL WINAPI checkedTlsFree(DWORD key) {
    if (failCleanup("tls-free")) return FALSE;
    ++releases; return TlsFree(key);
}
static BOOL WINAPI checkedDelete(PRUNTIME_FUNCTION table) {
    if (failCleanup("unwind")) return FALSE;
    ++releases; return RtlDeleteFunctionTable(table);
}
static LPVOID WINAPI checkedAlloc(LPVOID address,SIZE_T size,DWORD type,DWORD protect) {
    void* p=VirtualAlloc(address,size,type,protect);
    if (p) allocated.push_back(p);
    return p;
}
static BOOL WINAPI checkedFree(LPVOID address,SIZE_T size,DWORD type) {
    if (failCleanup("memory")) return FALSE;
    ++releases;
    BOOL ok=VirtualFree(address,size,type);
    if (ok) freed.push_back(address);
    return ok;
}
// The first main-thread read after the worker has run is the consumer's close.
static LPVOID WINAPI checkedTlsGet(DWORD key) {
    if (GetCurrentThreadId()!=mainThread) {
        InterlockedExchange(&workerTlsSeen,1);
        // Two attach reads precede the consumer's thread-detach read.
        if (++workerTlsReads==3 && failCleanup("thread-read")) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    } else if (workerTlsSeen) {
        if (cleanupFailure=="tls-read-persistent") {
            ++failuresInjected; SetLastError(ERROR_INVALID_PARAMETER); return nullptr;
        }
        if (failCleanup("tls-read")) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    }
    return TlsGetValue(key);
}
static BOOL WINAPI checkedTlsSet(DWORD key,LPVOID value) {
    if (!value && GetCurrentThreadId()!=mainThread && failCleanup("thread-clear")) return FALSE;
    return TlsSetValue(key,value);
}
// Report a failed wait while keeping a real handle, so the probe can still
// join the worker after checking that nothing it uses was released.
static DWORD WINAPI checkedWait(HANDLE handle,DWORD timeout) {
    if (cleanupFailure=="wait" && !liveWorker &&
        DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&liveWorker,0,FALSE,DUPLICATE_SAME_ACCESS) &&
        failCleanup("wait")) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    return WaitForSingleObject(handle,timeout);
}
#define TlsFree checkedTlsFree
#define TlsGetValue checkedTlsGet
#define TlsSetValue checkedTlsSet
#define RtlDeleteFunctionTable checkedDelete
#define VirtualFree checkedFree
#define VirtualAllocFromApp checkedAlloc
#define VirtualProtectFromApp VirtualProtect
#define WaitForSingleObject checkedWait
#include "../wxl_mapper.h"
#include "../tls_test_runner.h"
#undef WaitForSingleObject
static const char* const operations[]={"tls-free","unwind","memory","tls-read","tls-read-persistent","thread-read","thread-clear","wait"};
int main(int argc,char** argv) {
    if (argc>2) return 2;
    if (argc==2) {
        cleanupFailure=argv[1];
        bool known=false; for (auto o:operations) known=known || cleanupFailure==o;
        if (!known) return 2;
    }
    setvbuf(stdout,nullptr,_IONBF,0);
    mainThread=GetCurrentThreadId();
    HMODULE a=LoadLibraryW(L"out-tls/layout/tls-carrier-a.dll");
    HMODULE b=LoadLibraryW(L"out-tls/layout/tls-carrier-b.dll");
    bool workerLive=false;
    bool ok=wxl::runTls(L"out-tls/layout",[](const char* s){puts(s);},a,b,workerLive);
    unsigned releasesAtReturn=releases;
    DWORD workerExit=~0UL;
    if (liveWorker) {
        if (WaitForSingleObject(liveWorker,INFINITE)!=WAIT_OBJECT_0 || !GetExitCodeThread(liveWorker,&workerExit)) workerExit=~0UL;
        CloseHandle(liveWorker);
    }
    if (a) FreeLibrary(a);
    if (b) FreeLibrary(b);
    // The dependency (first image mapped) must outlive its consumer (second).
    bool order=true;
    if (allocated.size()>=2) {
        int dll=-1,exe=-1;
        for (size_t i=0;i<freed.size();++i) {
            if (freed[i]==allocated[0]) dll=(int)i;
            if (freed[i]==allocated[1]) exe=(int)i;
        }
        order=dll<0 ? true : exe>=0 && exe<dll;
    }
    printf("TLS-RELEASE-ORDER %s\n",order?"consumer-first":"dependency-first");
    if (!cleanupFailure.empty()) {
        bool persistent=cleanupFailure=="tls-read-persistent";
        bool rejected=!ok && failuresInjected==(persistent?2u:1u) && order;
        if (cleanupFailure=="wait") rejected=rejected && workerLive && releasesAtReturn==0 && workerExit==0;
        else if (persistent) rejected=rejected && !workerLive && releasesAtReturn==0 &&
            allocated.size()==2 && wxl::tlsDependency && wxl::tlsDependency->base==allocated[0];
        else rejected=rejected && !workerLive;
        printf("TLS-CLEANUP-%s operation=%s injected=%u live=%d releases=%u\n",rejected?"DETECTED":"MISSED",
            cleanupFailure.c_str(),failuresInjected,workerLive?1:0,releasesAtReturn);
        return rejected?0:1;
    }
    return ok && !workerLive && order?0:1;
}
