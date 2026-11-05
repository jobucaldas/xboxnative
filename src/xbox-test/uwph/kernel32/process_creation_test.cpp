#include "route.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
int main() {
    auto child=wcsstr(GetCommandLineW(),L"--k32x-child ");
    if(child) {
        HANDLE writer=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(child+13,nullptr,10)));
        DWORD n=0;return WriteFile(writer,"child-pipe",10,&n,nullptr)&&n==10?73:74;
    }
    auto address=kernel32_core::resolve("kernel32.dll","CreateProcessW");CHECK(address);
    auto create=reinterpret_cast<decltype(&::CreateProcessW)>(address);
    CHECK(address==kernel32_core::resolve("kernelbase.dll","CreateProcessW"));
    CHECK(address==kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll","CreateProcessW"));
    wchar_t temp[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
    wchar_t missing[MAX_PATH];CHECK(GetTempFileNameW(temp,L"k32",0,missing));CHECK(DeleteFileW(missing));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION expected{},actual{};memset(&expected,0x55,sizeof(expected));actual=expected;
    SetLastError(123);BOOL ok=CreateProcessW(missing,nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&expected);DWORD error=GetLastError();CHECK(!ok);
    SetLastError(123);CHECK(create(missing,nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&actual)==ok);CHECK(GetLastError()==error);CHECK(!memcmp(&expected,&actual,sizeof(actual)));
    wchar_t executable[32768];CHECK(GetModuleFileNameW(nullptr,executable,32768));
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE reader=nullptr,writer=nullptr;
    CHECK(CreatePipe(&reader,&writer,&security,0));CHECK(SetHandleInformation(reader,HANDLE_FLAG_INHERIT,0));
    SIZE_T bytes=0;CHECK(!InitializeProcThreadAttributeList(nullptr,1,0,&bytes));CHECK(bytes>0);
    auto list=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes));CHECK(list);
    CHECK(InitializeProcThreadAttributeList(list,1,0,&bytes));
    CHECK(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&writer,sizeof(writer),nullptr,nullptr));
    STARTUPINFOEXW extended{};extended.StartupInfo.cb=sizeof(extended);extended.lpAttributeList=list;
    wchar_t command[32768];swprintf(command,32768,L"\"%ls\" --k32x-child %llu",executable,static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(writer)));
    CHECK(create(executable,command,nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,nullptr,&extended.StartupInfo,&actual));
    CHECK(actual.hProcess);CHECK(actual.hThread);CHECK(actual.dwProcessId);CHECK(actual.dwThreadId);
    CHECK(GetProcessId(actual.hProcess)==actual.dwProcessId);CHECK(GetThreadId(actual.hThread)==actual.dwThreadId);
    CHECK(CloseHandle(writer));CHECK(WaitForSingleObject(actual.hProcess,10000)==WAIT_OBJECT_0);
    DWORD code=0;CHECK(GetExitCodeProcess(actual.hProcess,&code));CHECK(code==73);
    char data[16]{};DWORD count=0;CHECK(ReadFile(reader,data,sizeof(data),&count,nullptr));CHECK(count==10);CHECK(!memcmp(data,"child-pipe",10));
    SetLastError(0);CHECK(!ReadFile(reader,data,sizeof(data),&count,nullptr));CHECK(GetLastError()==ERROR_BROKEN_PIPE);
    CHECK(CloseHandle(reader));CHECK(CloseHandle(actual.hProcess));CHECK(CloseHandle(actual.hThread));
    DeleteProcThreadAttributeList(list);CHECK(HeapFree(GetProcessHeap(),0,list));
    CHECK(!kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll","DeleteVolumeMountPointW"));
    std::printf("kernel32 process creation: PASS (%u checks; off-device child, inherited pipe and native failures)\n",checks);
}
