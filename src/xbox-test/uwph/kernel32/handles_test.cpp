#include "route.h"
#include <cstdio>
#include <cstdlib>
static LONG checks;
#define CHECK(x) do { InterlockedIncrement(&checks); if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static CRITICAL_SECTION section;
static HANDLE ready;
static DWORD WINAPI contender(void*) {
    CHECK(!API(TryEnterCriticalSection)(&section)); CHECK(SetEvent(ready));
    API(EnterCriticalSection)(&section); API(LeaveCriticalSection)(&section); return 0;
}
static LONG apcs;
static void CALLBACK apc(ULONG_PTR parameter) { if(parameter==77)InterlockedIncrement(&apcs); }
static void close(HANDLE handle) { CHECK(API(CloseHandle)(handle)); }
int main() {
    HANDLE manual=API(CreateEventW)(nullptr,TRUE,FALSE,nullptr); CHECK(manual);
    CHECK(API(WaitForSingleObject)(manual,0)==WAIT_TIMEOUT);
    CHECK(API(SetEvent)(manual)); CHECK(API(WaitForSingleObjectEx)(manual,0,FALSE)==WAIT_OBJECT_0);
    CHECK(API(WaitForSingleObject)(manual,0)==WAIT_OBJECT_0);
    CHECK(API(ResetEvent)(manual)); CHECK(API(WaitForSingleObject)(manual,0)==WAIT_TIMEOUT);
    HANDLE automatic=API(CreateEventA)(nullptr,FALSE,TRUE,nullptr); CHECK(automatic);
    CHECK(API(WaitForSingleObject)(automatic,0)==WAIT_OBJECT_0);
    CHECK(API(WaitForSingleObject)(automatic,0)==WAIT_TIMEOUT);
    HANDLE extended=API(CreateEventExW)(nullptr,nullptr,CREATE_EVENT_INITIAL_SET,EVENT_ALL_ACCESS); CHECK(extended);
    CHECK(API(WaitForSingleObject)(extended,0)==WAIT_OBJECT_0);
    HANDLE duplicate=nullptr;
    CHECK(API(DuplicateHandle)(GetCurrentProcess(),manual,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS));
    CHECK(API(SetEvent)(duplicate)); CHECK(API(WaitForSingleObject)(manual,0)==WAIT_OBJECT_0);
    HANDLE pair[]={manual,automatic}; CHECK(API(WaitForMultipleObjects)(2,pair,FALSE,0)==WAIT_OBJECT_0);
    CHECK(API(WaitForMultipleObjectsEx)(2,pair,TRUE,0,FALSE)==WAIT_TIMEOUT);
    CHECK(API(SetEvent)(automatic)); CHECK(API(WaitForMultipleObjectsEx)(2,pair,TRUE,0,FALSE)==WAIT_OBJECT_0);
    close(duplicate); close(extended); close(automatic); close(manual);
    SetLastError(0); CHECK(API(WaitForSingleObject)(nullptr,0)==WAIT_FAILED); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(!API(CloseHandle)(nullptr)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    HANDLE mutex=API(CreateMutexA)(nullptr,TRUE,nullptr); CHECK(mutex);
    CHECK(API(WaitForSingleObject)(mutex,0)==WAIT_OBJECT_0); // Recursive acquisition.
    CHECK(API(ReleaseMutex)(mutex)); CHECK(API(ReleaseMutex)(mutex));
    SetLastError(0); CHECK(!API(ReleaseMutex)(mutex)); CHECK(GetLastError()==ERROR_NOT_OWNER); close(mutex);
    HANDLE semaphore=API(CreateSemaphoreExW)(nullptr,0,2,nullptr,0,SEMAPHORE_ALL_ACCESS); CHECK(semaphore);
    LONG previous=-1; CHECK(API(ReleaseSemaphore)(semaphore,2,&previous)); CHECK(previous==0);
    SetLastError(0); CHECK(!API(ReleaseSemaphore)(semaphore,1,nullptr)); CHECK(GetLastError()==ERROR_TOO_MANY_POSTS);
    CHECK(API(WaitForSingleObject)(semaphore,0)==WAIT_OBJECT_0); CHECK(API(WaitForSingleObject)(semaphore,0)==WAIT_OBJECT_0);
    CHECK(API(WaitForSingleObject)(semaphore,0)==WAIT_TIMEOUT); close(semaphore);
    HANDLE timer=API(CreateWaitableTimerA)(nullptr,TRUE,nullptr); CHECK(timer);
    LARGE_INTEGER due; due.QuadPart=-10000; CHECK(API(SetWaitableTimer)(timer,&due,0,nullptr,nullptr,FALSE));
    CHECK(API(WaitForSingleObject)(timer,5000)==WAIT_OBJECT_0); CHECK(API(CancelWaitableTimer)(timer)); close(timer);
    timer=API(CreateWaitableTimerExW)(nullptr,nullptr,0,TIMER_ALL_ACCESS); CHECK(timer);
    due.QuadPart=-1000000000; CHECK(API(SetWaitableTimer)(timer,&due,0,nullptr,nullptr,FALSE));
    CHECK(API(CancelWaitableTimer)(timer)); CHECK(API(WaitForSingleObject)(timer,0)==WAIT_TIMEOUT); close(timer);
    CHECK(QueueUserAPC(apc,GetCurrentThread(),77)); CHECK(API(SleepEx)(5000,TRUE)==WAIT_IO_COMPLETION); CHECK(apcs==1);
    API(Sleep)(0); CHECK(API(SleepEx)(0,FALSE)==0); (void)API(SwitchToThread)();
    API(InitializeCriticalSection)(&section); API(DeleteCriticalSection)(&section);
    CHECK(API(InitializeCriticalSectionAndSpinCount)(&section,32)); CHECK(API(SetCriticalSectionSpinCount)(&section,64)==32);
    API(DeleteCriticalSection)(&section); CHECK(API(InitializeCriticalSectionEx)(&section,0,0));
    API(EnterCriticalSection)(&section); CHECK(API(TryEnterCriticalSection)(&section)); API(LeaveCriticalSection)(&section);
    ready=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(ready);
    HANDLE thread=CreateThread(nullptr,0,contender,nullptr,0,nullptr); CHECK(thread);
    CHECK(API(WaitForSingleObject)(ready,5000)==WAIT_OBJECT_0); API(LeaveCriticalSection)(&section);
    CHECK(API(WaitForSingleObject)(thread,5000)==WAIT_OBJECT_0);
    DWORD result=99; CHECK(GetExitCodeThread(thread,&result)); CHECK(result==0);
    close(thread); close(ready); API(DeleteCriticalSection)(&section);
    CHECK(!kernel32_core::resolve("api-ms-win-core-handle-l1-1-0.dll","SetEvent"));
    CHECK(!kernel32_core::resolve("api-ms-win-core-synch-l1-2-0.dll","CloseHandle"));
    CHECK(kernel32_core::resolve("api-ms-win-core-handle-l1-1-0.dll","CloseHandle"));
    std::printf("kernel32 handles: PASS (%ld checks; off-device native-handle ABI)\n",checks);
}
