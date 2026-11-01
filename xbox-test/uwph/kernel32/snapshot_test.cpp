#include "route.h"
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
int main() {
    auto p=kernel32_core::resolve("kernel32.dll","CreateToolhelp32Snapshot");CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll","CreateToolhelp32Snapshot"));
    auto snapshot=reinterpret_cast<decltype(&::CreateToolhelp32Snapshot)>(p);
    auto firstAddress=kernel32_core::resolve("kernel32.dll","Thread32First");CHECK(firstAddress);
    auto nextAddress=kernel32_core::resolve("kernel32.dll","Thread32Next");CHECK(nextAddress);
    CHECK(firstAddress==kernel32_core::resolve("kernelbase.dll","Thread32First"));
    CHECK(nextAddress==kernel32_core::resolve("kernelbase.dll","Thread32Next"));
    CHECK(!kernel32_core::resolve("user32.dll","Thread32First"));
    CHECK(!kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll","Thread32Next"));
    auto first=reinterpret_cast<decltype(&::Thread32First)>(firstAddress);
    auto next=reinterpret_cast<decltype(&::Thread32Next)>(nextAddress);
    HANDLE h=snapshot(TH32CS_SNAPTHREAD,0);CHECK(h!=INVALID_HANDLE_VALUE);
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);CHECK(first(h,&entry));
    bool found=false;unsigned count=0;
    do {found|=entry.th32ThreadID==GetCurrentThreadId()&&entry.th32OwnerProcessID==GetCurrentProcessId();CHECK(++count<100000);} while(next(h,&entry));
    CHECK(found);CHECK(GetLastError()==ERROR_NO_MORE_FILES);CHECK(first(h,&entry));CHECK(CloseHandle(h));
    SetLastError(0);CHECK(!next(h,&entry));CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    for(auto function : {first,next}) {
        THREADENTRY32 expected{};expected.dwSize=sizeof(expected);
        THREADENTRY32 actual=expected;
        auto native=function==first?&::Thread32First:&::Thread32Next;
        SetLastError(123);BOOL wanted=native(INVALID_HANDLE_VALUE,&expected);DWORD error=GetLastError();
        SetLastError(123);BOOL got=function(INVALID_HANDLE_VALUE,&actual);DWORD gotError=GetLastError();
        CHECK(got==wanted);CHECK(gotError==error);CHECK(!memcmp(&actual,&expected,sizeof actual));
        HANDLE snapshotHandle=snapshot(TH32CS_SNAPTHREAD,0);CHECK(snapshotHandle!=INVALID_HANDLE_VALUE);
        expected={};expected.dwSize=sizeof(DWORD);actual=expected;
        SetLastError(123);wanted=native(snapshotHandle,&expected);error=GetLastError();
        SetLastError(123);got=function(snapshotHandle,&actual);gotError=GetLastError();
        CHECK(got==wanted);CHECK(gotError==error);CHECK(!memcmp(&actual,&expected,sizeof actual));
        CHECK(CloseHandle(snapshotHandle));
    }
    h=snapshot(TH32CS_SNAPPROCESS,0);CHECK(h!=INVALID_HANDLE_VALUE);
    PROCESSENTRY32W process{};process.dwSize=sizeof(process);CHECK(Process32FirstW(h,&process));
    found=false;count=0;
    do {found|=process.th32ProcessID==GetCurrentProcessId();CHECK(++count<100000);} while(Process32NextW(h,&process));
    CHECK(found);CHECK(GetLastError()==ERROR_NO_MORE_FILES);CHECK(CloseHandle(h));
    for(DWORD flags : {DWORD(0),DWORD(0x80000000)}) {
        SetLastError(123);HANDLE expected=CreateToolhelp32Snapshot(flags,0);DWORD error=GetLastError();
        SetLastError(123);HANDLE actual=snapshot(flags,0);DWORD actualError=GetLastError();
        CHECK((actual==INVALID_HANDLE_VALUE)==(expected==INVALID_HANDLE_VALUE));CHECK(actualError==error);
        if(actual!=INVALID_HANDLE_VALUE)CHECK(CloseHandle(actual));
        if(expected!=INVALID_HANDLE_VALUE)CHECK(CloseHandle(expected));
    }
    CHECK(!kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll","CreateToolhelp32Snapshot"));
    std::printf("kernel32 snapshots: PASS (%u checks; off-device native thread/process IDs; mapped module snapshots unverified)\n",checks);
}
