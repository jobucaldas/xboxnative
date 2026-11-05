#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <type_traits>

static LONG checks;
#define CHECK(x) do { InterlockedIncrement(&checks); if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F routed(const char* name, F) {
    auto p=kernel32_core::resolve("kernel32.dll",name);
    CHECK(p!=0);
    CHECK(p==kernel32_core::resolve("KERNELBASE.DLL",name));
    return reinterpret_cast<F>(p);
}
#define API(name) routed(#name, &::name)
static DWORD tls,fls;
static LONG callbacks;
static void NTAPI destroy(void* value) { if(value==reinterpret_cast<void*>(23)) InterlockedIncrement(&callbacks); }
static DWORD WINAPI local_worker(void*) {
    CHECK(API(TlsGetValue)(tls)==nullptr);
    CHECK(API(FlsGetValue)(fls)==nullptr);
    CHECK(API(TlsSetValue)(tls,reinterpret_cast<void*>(19)));
    CHECK(API(FlsSetValue)(fls,reinterpret_cast<void*>(23)));
    CHECK(API(TlsGetValue)(tls)==reinterpret_cast<void*>(19));
    return 0;
}
static void join(HANDLE thread) {
    CHECK(thread!=nullptr);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    DWORD code=99; CHECK(GetExitCodeThread(thread,&code)); CHECK(code==0);
    CHECK(CloseHandle(thread));
}
static SRWLOCK lock;
static CONDITION_VARIABLE condition;
static CRITICAL_SECTION cs;
static HANDLE ready;
static bool predicate;
static DWORD WINAPI srw_worker(void*) {
    API(AcquireSRWLockExclusive)(&lock);
    CHECK(SetEvent(ready));
    while(!predicate) CHECK(API(SleepConditionVariableSRW)(&condition,&lock,5000,0));
    API(ReleaseSRWLockExclusive)(&lock);
    return 0;
}
static DWORD WINAPI shared_worker(void*) {
    API(AcquireSRWLockShared)(&lock);
    CHECK(SetEvent(ready));
    while(!predicate) CHECK(API(SleepConditionVariableSRW)(&condition,&lock,5000,CONDITION_VARIABLE_LOCKMODE_SHARED));
    API(ReleaseSRWLockShared)(&lock);
    return 0;
}
static DWORD WINAPI cs_worker(void*) {
    EnterCriticalSection(&cs);
    CHECK(SetEvent(ready));
    while(!predicate) CHECK(API(SleepConditionVariableCS)(&condition,&cs,5000));
    LeaveCriticalSection(&cs);
    return 0;
}
static LONG address_value;
static DWORD WINAPI address_worker(void*) {
    LONG compare=0;
    CHECK(SetEvent(ready));
    while(InterlockedCompareExchange(&address_value,0,0)==0)
        CHECK(API(WaitOnAddress)(&address_value,&compare,sizeof(compare),5000));
    return 0;
}
static INIT_ONCE once;
static LONG init_calls;
static BOOL CALLBACK initialize(PINIT_ONCE,PVOID parameter,PVOID* context) {
    InterlockedIncrement(&init_calls); *context=parameter; return TRUE;
}
static DWORD WINAPI once_worker(void*) {
    void* context=nullptr;
    CHECK(API(InitOnceExecuteOnce)(&once,initialize,reinterpret_cast<void*>(0x1000),&context));
    CHECK(context==reinterpret_cast<void*>(0x1000)); return 0;
}
int main() {
    CHECK(!kernel32_core::resolve(nullptr,"TlsAlloc"));
    CHECK(!kernel32_core::resolve("kernel32.dll",nullptr));
    CHECK(!kernel32_core::resolve("user32.dll","TlsAlloc"));
    CHECK(!kernel32_core::resolve("ntdll.dll","TlsAlloc"));
    CHECK(!kernel32_core::resolve("api-ms-win-core-file-l1-1-0.dll","TlsAlloc"));
    CHECK(!kernel32_core::resolve("kernel32.dll","CreateThread"));
    CHECK(!kernel32_core::resolve("api-ms-win-core-synch-l1-2-0.dll","TlsAlloc"));
    CHECK(!kernel32_core::resolve("api-ms-win-core-fibers-l1-1-0.dll","AcquireSRWLockExclusive"));
    CHECK(kernel32_core::resolve("api-ms-win-core-synch-l1-2-0.dll","AcquireSRWLockExclusive"));
    CHECK(kernel32_core::resolve("api-ms-win-core-fibers-l1-1-0.dll","FlsAlloc"));
    tls=API(TlsAlloc)(); CHECK(tls!=TLS_OUT_OF_INDEXES);
    fls=API(FlsAlloc)(destroy); CHECK(fls!=FLS_OUT_OF_INDEXES);
    SetLastError(123); CHECK(API(TlsGetValue)(tls)==nullptr); CHECK(GetLastError()==ERROR_SUCCESS);
    CHECK(API(TlsSetValue)(tls,reinterpret_cast<void*>(17)));
    CHECK(API(FlsSetValue)(fls,reinterpret_cast<void*>(29)));
    join(CreateThread(nullptr,0,local_worker,nullptr,0,nullptr));
    CHECK(callbacks==1);
    CHECK(API(TlsGetValue)(tls)==reinterpret_cast<void*>(17));
    CHECK(API(FlsGetValue)(fls)==reinterpret_cast<void*>(29));
    CHECK(API(TlsFree)(tls)); CHECK(API(FlsFree)(fls));
    SetLastError(0); CHECK(!API(TlsSetValue)(TLS_OUT_OF_INDEXES,nullptr)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!API(FlsSetValue)(FLS_OUT_OF_INDEXES,nullptr)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);

    API(InitializeSRWLock)(&lock); API(InitializeConditionVariable)(&condition);
    CHECK(API(TryAcquireSRWLockExclusive)(&lock));
    CHECK(!API(TryAcquireSRWLockExclusive)(&lock)); CHECK(!API(TryAcquireSRWLockShared)(&lock));
    API(ReleaseSRWLockExclusive)(&lock);
    API(AcquireSRWLockShared)(&lock); CHECK(API(TryAcquireSRWLockShared)(&lock));
    CHECK(!API(TryAcquireSRWLockExclusive)(&lock));
    API(ReleaseSRWLockShared)(&lock); API(ReleaseSRWLockShared)(&lock);
    API(AcquireSRWLockExclusive)(&lock);
    SetLastError(0); CHECK(!API(SleepConditionVariableSRW)(&condition,&lock,1,0)); CHECK(GetLastError()==ERROR_TIMEOUT);
    // The failed wait must reacquire the lock.
    CHECK(!API(TryAcquireSRWLockExclusive)(&lock)); API(ReleaseSRWLockExclusive)(&lock);
    ready=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(ready);
    HANDLE thread=CreateThread(nullptr,0,srw_worker,nullptr,0,nullptr);
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
    API(AcquireSRWLockExclusive)(&lock); predicate=true; API(WakeConditionVariable)(&condition); API(ReleaseSRWLockExclusive)(&lock);
    join(thread);
    predicate=false; thread=CreateThread(nullptr,0,shared_worker,nullptr,0,nullptr);
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
    API(AcquireSRWLockExclusive)(&lock); predicate=true; API(WakeAllConditionVariable)(&condition); API(ReleaseSRWLockExclusive)(&lock);
    join(thread);
    InitializeCriticalSection(&cs); EnterCriticalSection(&cs);
    SetLastError(0); CHECK(!API(SleepConditionVariableCS)(&condition,&cs,1)); CHECK(GetLastError()==ERROR_TIMEOUT);
    LeaveCriticalSection(&cs); predicate=false;
    thread=CreateThread(nullptr,0,cs_worker,nullptr,0,nullptr);
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
    EnterCriticalSection(&cs); predicate=true; API(WakeAllConditionVariable)(&condition); LeaveCriticalSection(&cs);
    join(thread); DeleteCriticalSection(&cs); CHECK(CloseHandle(ready));

    LONG compare=0;
    SetLastError(0); CHECK(!API(WaitOnAddress)(&address_value,&compare,sizeof(compare),1)); CHECK(GetLastError()==ERROR_TIMEOUT);
    SetLastError(0); CHECK(!API(WaitOnAddress)(&address_value,&compare,3,0)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    compare=1; CHECK(API(WaitOnAddress)(&address_value,&compare,sizeof(compare),0));
    ready=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(ready);
    thread=CreateThread(nullptr,0,address_worker,nullptr,0,nullptr);
    CHECK(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
    InterlockedExchange(&address_value,1); API(WakeByAddressSingle)(&address_value); API(WakeByAddressAll)(&address_value);
    join(thread); CHECK(CloseHandle(ready));

    API(InitOnceInitialize)(&once);
    HANDLE threads[4]; for(auto& t:threads)t=CreateThread(nullptr,0,once_worker,nullptr,0,nullptr);
    for(auto t:threads) { join(t); }
    CHECK(init_calls==1);
    INIT_ONCE manual=INIT_ONCE_STATIC_INIT; BOOL pending=FALSE; void* context=nullptr;
    CHECK(API(InitOnceBeginInitialize)(&manual,0,&pending,&context)); CHECK(pending);
    CHECK(API(InitOnceComplete)(&manual,INIT_ONCE_INIT_FAILED,nullptr));
    CHECK(API(InitOnceBeginInitialize)(&manual,0,&pending,&context)); CHECK(pending);
    CHECK(API(InitOnceComplete)(&manual,0,reinterpret_cast<void*>(0x2000)));
    CHECK(API(InitOnceBeginInitialize)(&manual,INIT_ONCE_CHECK_ONLY,&pending,&context)); CHECK(!pending); CHECK(context==reinterpret_cast<void*>(0x2000));
    std::printf("kernel32 sync: PASS (%ld checks; off-device native-thread ABI)\n",checks);
}
