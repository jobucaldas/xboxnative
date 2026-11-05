#include "backend.h"
#include <string>
#include <stdio.h>
#include <string.h>
extern "C" { uint64_t __wine_unixlib_handle; Dispatch __wine_unix_call_dispatcher; }
static uintptr_t invoke(Op op, uintptr_t a=0, uintptr_t b=0, uintptr_t c=0, uintptr_t d=0) {
 Call x{{a,b,c,d}}; return host(op,x);
}
static DWORD WINAPI worker(void* arg) {
 auto* n=(LONG*)arg; InterlockedIncrement(n); return 42;
}
struct Sync { uintptr_t ready,go,done; LONG stage=0; };
static DWORD WINAPI sync_worker(void* arg) {
 auto& s=*(Sync*)arg;
 InterlockedExchange(&s.stage,1);
 if(!invoke(Op::SetEvent,s.ready)) return 1;
 if(invoke(Op::Wait,s.go,5000)!=WAIT_OBJECT_0) return 2;
 InterlockedExchange(&s.stage,2);
 return invoke(Op::SetEvent,s.done)?0:3;
}
// Separate DLL compiled to PE code; every tested operation crosses
// the dispatch pointer. This is not an unmodified Wine-importing guest binary.
extern "C" __declspec(dllexport) int run_kernel_tests(const wchar_t* root, void (*log)(const char*), uint64_t handle, Dispatch dispatcher, Dispatch wine10) {
 __wine_unixlib_handle=handle; __wine_unix_call_dispatcher=dispatcher;
 int failures=0, checks=0;
 auto check=[&](const char* name, bool ok) { char s[256]; ++checks; if(!ok) ++failures;
 sprintf_s(s,"%s %s err=%lu",ok?"PASS":"FAIL",name,GetLastError()); log(s); };
 auto close=[&](uintptr_t h) { check("CloseHandle",invoke(Op::Close,h)!=0); };
 const auto path=std::wstring(root)+L"\\k32-test.bin", moved=path+L".moved", dir=std::wstring(root)+L"\\k32-dir";
 auto h=invoke(Op::CreateFile,(uintptr_t)path.c_str(),GENERIC_READ|GENERIC_WRITE,0,CREATE_ALWAYS);
 check("CreateFile2",h!=(uintptr_t)INVALID_HANDLE_VALUE);
 if(h!=(uintptr_t)INVALID_HANDLE_VALUE) {
  const char payload[]="uwph file roundtrip"; DWORD n=0; char buf[sizeof(payload)]{}; LARGE_INTEGER size{};
  check("WriteFile",invoke(Op::Write,h,(uintptr_t)payload,sizeof(payload),(uintptr_t)&n)&&n==sizeof(payload));
  check("FlushFileBuffers",invoke(Op::FlushFile,h)!=0);
  check("GetFileSizeEx",invoke(Op::Size,h,(uintptr_t)&size)&&size.QuadPart==sizeof(payload));
  check("SetFilePointerEx",invoke(Op::Seek,h,0,0,FILE_BEGIN)!=0);
  check("ReadFile",invoke(Op::Read,h,(uintptr_t)buf,sizeof(buf),(uintptr_t)&n)&&n==sizeof(buf)&&!memcmp(buf,payload,sizeof(buf)));
  close(h);
 }
 check("GetFileAttributesW",invoke(Op::Attributes,(uintptr_t)path.c_str())!=INVALID_FILE_ATTRIBUTES);
 check("MoveFileExW",invoke(Op::MoveFile,(uintptr_t)path.c_str(),(uintptr_t)moved.c_str(),MOVEFILE_REPLACE_EXISTING)!=0);
 check("DeleteFileW",invoke(Op::DeleteFile,(uintptr_t)moved.c_str())!=0);
 check("CreateDirectoryW",invoke(Op::CreateDir,(uintptr_t)dir.c_str())!=0);
 check("RemoveDirectoryW",invoke(Op::RemoveDir,(uintptr_t)dir.c_str())!=0);
 h=invoke(Op::CreateFile,(uintptr_t)path.c_str(),GENERIC_READ,0,OPEN_EXISTING);
 check("missing file error",h==(uintptr_t)INVALID_HANDLE_VALUE&&GetLastError()==ERROR_FILE_NOT_FOUND);
 check("GetCurrentProcessId",invoke(Op::ProcessId)!=0);
 check("GetCurrentThreadId",invoke(Op::ThreadId)!=0);
 LONG n=0; DWORD tid=0;
 h=invoke(Op::Thread,(uintptr_t)worker,(uintptr_t)&n,(uintptr_t)&tid);
 check("CreateThread",h&&tid); if(h) { check("thread join",invoke(Op::Wait,h,5000)==WAIT_OBJECT_0&&n==1); close(h); }
 h=invoke(Op::Event,CREATE_EVENT_MANUAL_RESET);
 check("CreateEventExW",h!=0); if(h) {
 check("event timeout",invoke(Op::Wait,h,0)==WAIT_TIMEOUT);
 check("SetEvent",invoke(Op::SetEvent,h)!=0);
 check("event wait",invoke(Op::Wait,h,0)==WAIT_OBJECT_0);
 check("ResetEvent",invoke(Op::ResetEvent,h)&&invoke(Op::Wait,h,0)==WAIT_TIMEOUT); close(h); }
 h=invoke(Op::Mutex); check("CreateMutexExW",h!=0); if(h) {
 check("mutex wait",invoke(Op::Wait,h,0)==WAIT_OBJECT_0);
 check("ReleaseMutex",invoke(Op::ReleaseMutex,h)!=0); close(h); }
 h=invoke(Op::Semaphore,0,1); check("CreateSemaphoreExW",h!=0); if(h) {
 check("ReleaseSemaphore",invoke(Op::ReleaseSemaphore,h,1)!=0);
 check("semaphore wait",invoke(Op::Wait,h,0)==WAIT_OBJECT_0); close(h); }
 auto tls=invoke(Op::TlsAlloc); check("TlsAlloc",tls!=TLS_OUT_OF_INDEXES);
 if(tls!=TLS_OUT_OF_INDEXES) {
 check("TlsSetValue",invoke(Op::TlsSet,tls,1234)!=0);
 check("TlsGetValue",invoke(Op::TlsGet,tls)==1234);
 check("TlsFree",invoke(Op::TlsFree,tls)!=0); }
 LARGE_INTEGER freq{}, t{};
 check("QueryPerformanceFrequency",invoke(Op::Frequency,(uintptr_t)&freq)&&freq.QuadPart>0);
 check("QueryPerformanceCounter",invoke(Op::Counter,(uintptr_t)&t)&&t.QuadPart>0);
 auto tick=invoke(Op::Tick); invoke(Op::Sleep,20);
 LARGE_INTEGER afterSleep{}; invoke(Op::Counter,(uintptr_t)&afterSleep);
 check("GetTickCount64/Sleep",invoke(Op::Tick)>=tick&&afterSleep.QuadPart>t.QuadPart);
 check("CreateProcess clean failure",!invoke(Op::Process)&&GetLastError()==ERROR_ACCESS_DENIED);
 check("RWX allocation refused",!invoke(Op::Alloc,0,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE)&&GetLastError()==ERROR_ACCESS_DENIED);
 auto p=invoke(Op::Alloc,0,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 check("VirtualAllocFromApp RW",p!=0);
 if(p) {
 MEMORY_BASIC_INFORMATION mbi{}; DWORD old=0;
 check("VirtualQuery",invoke(Op::Query,p,(uintptr_t)&mbi,sizeof(mbi))&&mbi.State==MEM_COMMIT);
 const unsigned char code[]={0xb8,42,0,0,0,0xc3}; memcpy((void*)p,code,sizeof(code));
 bool rx=invoke(Op::Protect,p,4096,PAGE_EXECUTE_READ,(uintptr_t)&old)!=0;
 check("VirtualProtectFromApp RX",rx);
 if(rx) {
 check("FlushInstructionCache",invoke(Op::Flush,p,sizeof(code))!=0);
 __try { check("execute RX value=42",((int(*)())p)()==42); }
 __except(EXCEPTION_EXECUTE_HANDLER) { check("execute RX exception",false); }
 }
 check("VirtualFree",invoke(Op::Free,p,0,MEM_RELEASE)!=0);
 }
 Call invalid{}; check("invalid opcode rejected",__wine_unix_call_dispatcher(1,999,&invalid)==LONG(0xc000000d));
 LONGLONG wineTime=0; FILETIME before{},after{};
 GetSystemTimeAsFileTime(&before);
 LONG status=wine10(10,7,&wineTime);
 GetSystemTimeAsFileTime(&after);
 auto ticks=[](FILETIME f) { return (LONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime; };
 check("Wine10 system_time_precise",status==0&&wineTime>=ticks(before)-10000000&&wineTime<=ticks(after)+10000000);
 struct Debug { const char* str; unsigned len; } debug{"uwph Wine10 debug adapter\n",26};
 check("Wine10 dbg_write byte count",wine10(10,2,&debug)==LONG(debug.len));
 check("Wine10 server call unsupported",wine10(10,3,&invalid)==LONG(0xc0000002));
 check("invalid handle rejected",__wine_unix_call_dispatcher(999,0,&invalid)==LONG(0xc000000d));

 log("WS two-thread sync begin (main + native worker)");
 Sync sync{invoke(Op::Event,CREATE_EVENT_MANUAL_RESET),invoke(Op::Event),invoke(Op::Event)};
 check("WS CreateEventW ready/go/done",sync.ready&&sync.go&&sync.done);
 auto original=sync.go;
 sync.go=invoke(Op::Duplicate,original);
 check("WS DuplicateHandle distinct entry",sync.go&&sync.go!=original);
 check("WS close original",invoke(Op::Close,original)!=0);
 check("WS stale handle rejected",invoke(Op::Wait,original,0)==WAIT_FAILED&&GetLastError()==ERROR_INVALID_HANDLE);
 auto thread=invoke(Op::Thread,(uintptr_t)sync_worker,(uintptr_t)&sync);
 check("WS CreateThread",thread!=0);
 check("WS worker ready WaitForSingleObject",invoke(Op::Wait,sync.ready,5000)==WAIT_OBJECT_0&&sync.stage==1);
 log("WS sequence: worker ready; main signals duplicated go handle");
 check("WS SetEvent duplicate survives close",invoke(Op::SetEvent,sync.go)!=0);
 uintptr_t waits[]={sync.done,thread};
 check("WS WaitForMultipleObjects all",invoke(Op::WaitMultiple,(uintptr_t)waits,2,TRUE,5000)==WAIT_OBJECT_0&&sync.stage==2);
 log("WS sequence: worker received go; done and thread signaled");
 check("WS auto-reset consumed",invoke(Op::Wait,sync.go,0)==WAIT_TIMEOUT);
 check("WS wait-any index",invoke(Op::WaitMultiple,(uintptr_t)waits,2,FALSE,0)==WAIT_OBJECT_0+1);
 check("WS wrong object type rejected",!invoke(Op::SetEvent,thread)&&GetLastError()==ERROR_INVALID_HANDLE);
 check("WS invalid wait count",invoke(Op::WaitMultiple,(uintptr_t)waits,0,FALSE,0)==WAIT_FAILED&&GetLastError()==ERROR_INVALID_PARAMETER);
 auto threadCopy=invoke(Op::Duplicate,thread);
 close(thread); check("WS duplicated thread join",invoke(Op::Wait,threadCopy,0)==WAIT_OBJECT_0); close(threadCopy);
 close(sync.ready); close(sync.go); close(sync.done);
 check("WS double close rejected",!invoke(Op::Close,sync.done)&&GetLastError()==ERROR_INVALID_HANDLE);
 log("WS two-thread sync end");
 char summary[100]; sprintf_s(summary,"checks=%d failures=%d",checks,failures); log(summary); return failures;
}
