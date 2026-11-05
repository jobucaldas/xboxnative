#include "route.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static HANDLE ready,release;
static DWORD WINAPI child(void*) { SetEvent(ready); return WaitForSingleObject(release,10000)==WAIT_OBJECT_0 ? 42 : 1; }
int main() {
    ready=CreateEventW(nullptr,TRUE,FALSE,nullptr); release=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(ready); CHECK(release);
    DWORD id=0; HANDLE worker=CreateThread(nullptr,0,child,nullptr,CREATE_SUSPENDED,&id); CHECK(worker);
    auto resume=API(ResumeThread),suspend=API(SuspendThread);
    auto open=API(OpenThread);
    HANDLE thread=open(THREAD_QUERY_INFORMATION|THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_INFORMATION,FALSE,id); CHECK(thread);
    CHECK(GetThreadId(thread)==id); CHECK(WaitForSingleObject(ready,0)==WAIT_TIMEOUT);
    auto getcontext=API(GetThreadContext); CONTEXT context{}; context.ContextFlags=CONTEXT_CONTROL;
    CHECK(getcontext(thread,&context)); CHECK(context.Rip); CHECK(context.Rsp);
    CHECK(suspend(thread)==1); CHECK(resume(thread)==2); CHECK(resume(thread)==1);
    CHECK(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0);
    CHECK(suspend(thread)==0); CHECK(resume(thread)==1);
    auto setname=API(SetThreadDescription); auto getname=API(GetThreadDescription);
    CHECK(SUCCEEDED(setname(thread,L"k32x worker \u00e7")));
    PWSTR name=nullptr; CHECK(SUCCEEDED(getname(thread,&name))); CHECK(name); CHECK(!wcscmp(name,L"k32x worker \u00e7")); CHECK(!LocalFree(name));
    DWORD_PTR process_mask=0,system_mask=0; CHECK(GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask)); CHECK(process_mask);
    DWORD_PTR first=process_mask & (~process_mask+1);
    auto affinity=API(SetThreadAffinityMask); DWORD_PTR previous=affinity(thread,first); CHECK(previous); CHECK(affinity(thread,previous)==first);
    SetLastError(0); CHECK(!affinity(thread,0)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(SetEvent(release)); CHECK(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    DWORD code=0; CHECK(GetExitCodeThread(worker,&code)); CHECK(code==42); CHECK(CloseHandle(thread)); CHECK(CloseHandle(worker));
    CHECK(CloseHandle(ready)); CHECK(CloseHandle(release));
    SetLastError(0); CHECK(resume(INVALID_HANDLE_VALUE)==DWORD(-1)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(suspend(INVALID_HANDLE_VALUE)==DWORD(-1)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(!getcontext(INVALID_HANDLE_VALUE,&context)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    auto processopen=API(OpenProcess); HANDLE process=processopen(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,GetCurrentProcessId()); CHECK(process);
    CHECK(GetProcessId(process)==GetCurrentProcessId());
    CHECK(API(GetExitCodeProcess)(process,&code)); CHECK(code==STILL_ACTIVE);
    const DWORD64 source=0x123456789abcdef0ULL; DWORD64 destination=0; SIZE_T bytes=0;
    auto read=API(ReadProcessMemory); CHECK(read(process,&source,&destination,sizeof(source),&bytes)); CHECK(bytes==sizeof(source)); CHECK(destination==source);
    // -1 is the current-process pseudo handle, so use null for invalid access.
    SetLastError(0); CHECK(!read(nullptr,&source,&destination,sizeof(source),&bytes)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    PROCESS_MEMORY_COUNTERS counters{}; counters.cb=sizeof(counters);
    CHECK(API(K32GetProcessMemoryInfo)(process,&counters,sizeof(counters))); CHECK(counters.cb==sizeof(counters)); CHECK(counters.WorkingSetSize>0);
    SetLastError(0); CHECK(!API(K32GetProcessMemoryInfo)(process,&counters,1)); CHECK(GetLastError()==ERROR_INSUFFICIENT_BUFFER);
    CHECK(CloseHandle(process));
    // Terminate an unstarted thread. Windows intentionally sends no thread
    // detach callbacks for TerminateThread; no guest cleanup is fabricated.
    worker=CreateThread(nullptr,0,child,nullptr,CREATE_SUSPENDED,&id); CHECK(worker);
    CHECK(API(TerminateThread)(worker,73)); CHECK(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(worker,&code)); CHECK(code==73); CHECK(CloseHandle(worker));
    CHECK(!kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll","ReadProcessMemory"));
    std::printf("kernel32 thread/process queries: PASS (%u checks; off-device native handles)\n",checks);
}
