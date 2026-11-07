#include "route.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
using WriteFn=BOOL (WINAPI*)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
using CreateFn=HANDLE (WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static WriteFn route_write() {
    auto p=kernel32_core::resolve("kernel32.dll","WriteFile"); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll","WriteFile"));
    CHECK(p==kernel32_core::resolve("api-ms-win-core-file-l1-1-0.dll","WriteFile"));
    return reinterpret_cast<WriteFn>(p);
}
static std::atomic<int> stalls,cancels;
static std::atomic<DWORD> lastElapsed,lastCancelError{99};
static void hook(const kernel32_core::WriteStall& e) {
    ++stalls; if(e.cancelled){++cancels; lastCancelError=e.cancel_error;} lastElapsed=e.elapsed_ms;
}
// FILE_MODE_INFORMATION (class 16): the synchronous-I/O bits tell a synchronous
// handle (WriteFile blocks inside NtWriteFile) from an overlapped one.
static ULONG mode_of(HANDLE h) {
    using Query=LONG (NTAPI*)(HANDLE,void*,void*,ULONG,int);
    auto query=reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationFile")));
    CHECK(query);
    struct { ULONG_PTR status; ULONG_PTR info; } iosb{}; ULONG mode=0;
    CHECK(query(h,&iosb,&mode,sizeof(mode),16)>=0);
    return mode;
}
struct PipeWrite { HANDLE pipe; DWORD size; BOOL ok; DWORD written, error; };
static DWORD WINAPI blocked_write(LPVOID p) {
    auto* w=static_cast<PipeWrite*>(p);
    std::vector<char> data(w->size,'p'); DWORD n=0;
    w->ok=route_write()(w->pipe,data.data(),w->size,&n,nullptr); w->error=w->ok?0:GetLastError(); w->written=n;
    return 0;
}
static void make_pipe(HANDLE* reader,HANDLE* writer) {
    static unsigned serial; wchar_t name[96];
    swprintf(name,96,L"\\\\.\\pipe\\xbn-write-guard-%lu-%u",GetCurrentProcessId(),serial++);
    *reader=CreateNamedPipeW(name,PIPE_ACCESS_INBOUND,PIPE_TYPE_BYTE|PIPE_WAIT,1,4096,4096,0,nullptr); CHECK(*reader!=INVALID_HANDLE_VALUE);
    *writer=CreateFileW(name,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr); CHECK(*writer!=INVALID_HANDLE_VALUE);
}
int main() {
    wchar_t temp[MAX_PATH],file[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,temp)); CHECK(GetTempFileNameW(temp,L"wgd",0,file));
    auto write=route_write();
    auto create=reinterpret_cast<CreateFn>(kernel32_core::resolve("kernel32.dll","CreateFileW")); CHECK(create);
    auto& config=kernel32_core::write_guard();
    config.hook=hook;
    // CreateFile2 conversion: a plain open is a synchronous handle, FILE_FLAG_OVERLAPPED is not,
    // and both match the native CreateFileW (this is the handle kind WriteFile blocks on).
    for (DWORD flags : {DWORD(FILE_ATTRIBUTE_NORMAL),DWORD(FILE_FLAG_OVERLAPPED),DWORD(FILE_FLAG_WRITE_THROUGH),DWORD(FILE_FLAG_SEQUENTIAL_SCAN)}) {
        HANDLE reference=CreateFileW(file,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,flags,nullptr); CHECK(reference!=INVALID_HANDLE_VALUE);
        ULONG expected=mode_of(reference); CHECK(CloseHandle(reference));
        HANDLE actual=create(file,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,flags,nullptr); CHECK(actual!=INVALID_HANDLE_VALUE);
        ULONG got=mode_of(actual);
        CHECK(got==expected);
        bool synchronous=(got&0x30)!=0;      // FILE_SYNCHRONOUS_IO_ALERT|NONALERT
        CHECK(synchronous==!(flags&FILE_FLAG_OVERLAPPED));
        CHECK(((got&2)!=0)==((flags&FILE_FLAG_WRITE_THROUGH)!=0));
        CHECK(CloseHandle(actual));
    }
    // A write bigger than the chunk lands whole, in order, and moves the file pointer.
    config.chunk=64*1024;
    std::vector<unsigned char> data(300001); for(size_t i=0;i<data.size();++i)data[i]=(unsigned char)(i*31+i/251);
    HANDLE h=create(file,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr); CHECK(h!=INVALID_HANDLE_VALUE);
    DWORD n=0; CHECK(write(h,data.data(),DWORD(data.size()),&n,nullptr)); CHECK(n==data.size());
    CHECK(write(h,data.data(),0,&n,nullptr)); CHECK(n==0);
    CHECK(write(h,data.data(),10,&n,nullptr)); CHECK(n==10);
    LARGE_INTEGER pos{},zero{}; CHECK(SetFilePointerEx(h,zero,&pos,FILE_CURRENT)); CHECK(pos.QuadPart==LONGLONG(data.size()+10));
    CHECK(SetFilePointerEx(h,zero,nullptr,FILE_BEGIN));
    std::vector<unsigned char> back(data.size()+10); CHECK(ReadFile(h,back.data(),DWORD(back.size()),&n,nullptr)); CHECK(n==back.size());
    CHECK(!memcmp(back.data(),data.data(),data.size())); CHECK(!memcmp(back.data()+data.size(),data.data(),10));
    CHECK(CloseHandle(h));
    // Cases the guard leaves to the native call behave as native does.
    h=create(file,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr); CHECK(h!=INVALID_HANDLE_VALUE);
    SetLastError(0); BOOL native=WriteFile(h,"x",1,nullptr,nullptr); DWORD native_error=GetLastError();
    SetLastError(0); CHECK(write(h,"x",1,nullptr,nullptr)==native); CHECK(GetLastError()==native_error);
    CHECK(CloseHandle(h));
    h=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr); CHECK(h!=INVALID_HANDLE_VALUE);
    SetLastError(0); CHECK(!write(h,"x",1,&n,nullptr)); CHECK(GetLastError()==ERROR_ACCESS_DENIED); CHECK(n==0);   // errors pass through
    CHECK(CloseHandle(h));
    h=create(file,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr); CHECK(h!=INVALID_HANDLE_VALUE);
    OVERLAPPED overlapped{}; overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(overlapped.hEvent);
    BOOL immediate=write(h,"overlapped",10,&n,&overlapped); if(!immediate)CHECK(GetLastError()==ERROR_IO_PENDING);
    CHECK(GetOverlappedResult(h,&overlapped,&n,TRUE)); CHECK(n==10);
    CHECK(CloseHandle(overlapped.hEvent)); CHECK(CloseHandle(h));
    CHECK(DeleteFileW(file));
    CHECK(stalls==0);   // none of the above was slow
    // A write that blocks past report_ms is reported once, not cancelled, and finishes when the peer reads.
    config.report_ms=150; config.cancel_ms=0;
    HANDLE reader,writer; make_pipe(&reader,&writer);
    PipeWrite pw{writer,64*1024,FALSE,0,0};
    HANDLE thread=CreateThread(nullptr,0,blocked_write,&pw,0,nullptr); CHECK(thread);
    for(int i=0;i<100&&stalls==0;++i)Sleep(20);
    CHECK(stalls==1); CHECK(lastElapsed>=150); CHECK(WaitForSingleObject(thread,0)==WAIT_TIMEOUT);
    std::vector<char> sink(4096); DWORD total=0;
    while(total<pw.size){ DWORD got=0; CHECK(ReadFile(reader,sink.data(),DWORD(sink.size()),&got,nullptr)); total+=got; }
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0); CHECK(pw.ok); CHECK(pw.written==pw.size); CHECK(stalls==1); CHECK(cancels==0);
    CHECK(CloseHandle(thread)); CHECK(CloseHandle(reader)); CHECK(CloseHandle(writer));
    // A stalled write is cancelled only when it is a disk file. This pipe is not, so a long stall
    // is left alone (and finishes once the peer reads); the test then opts in to cancel one.
    config.report_ms=0; config.cancel_ms=300; stalls=0;
    make_pipe(&reader,&writer);
    pw=PipeWrite{writer,64*1024,FALSE,0,0};
    thread=CreateThread(nullptr,0,blocked_write,&pw,0,nullptr); CHECK(thread);
    CHECK(WaitForSingleObject(thread,900)==WAIT_TIMEOUT); CHECK(cancels==0);
    total=0; while(total<pw.size){ DWORD got=0; CHECK(ReadFile(reader,sink.data(),DWORD(sink.size()),&got,nullptr)); total+=got; }
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0); CHECK(pw.ok); CHECK(pw.written==pw.size); CHECK(cancels==0);
    CHECK(CloseHandle(thread)); CHECK(CloseHandle(reader)); CHECK(CloseHandle(writer));
    config.cancel_any=true;
    make_pipe(&reader,&writer);
    pw=PipeWrite{writer,64*1024,FALSE,0,0};
    thread=CreateThread(nullptr,0,blocked_write,&pw,0,nullptr); CHECK(thread);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);   // cancelled: the writer thread returns
    CHECK(!pw.ok); CHECK(pw.error==ERROR_OPERATION_ABORTED); CHECK(cancels==1); CHECK(lastCancelError==0);
    CHECK(CloseHandle(thread)); CHECK(CloseHandle(reader)); CHECK(CloseHandle(writer));
    std::printf("write guard PASS %u checks\n",checks);
}
