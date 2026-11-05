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
static DWORD WINAPI worker(void*) { return 73; }
int main() {
    const wchar_t* variable=L"K32X_REGRESSION_ENV_483152";
    CHECK(API(SetEnvironmentVariableW)(variable,nullptr));
    wchar_t text[1024]{}; char ansi[1024]{};
    SetLastError(0); CHECK(!API(GetEnvironmentVariableW)(variable,text,1024)); CHECK(GetLastError()==ERROR_ENVVAR_NOT_FOUND);
    CHECK(API(SetEnvironmentVariableW)(variable,L"abc"));
    CHECK(API(GetEnvironmentVariableW)(variable,nullptr,0)==4);
    CHECK(API(GetEnvironmentVariableW)(variable,text,3)==4);
    CHECK(API(GetEnvironmentVariableW)(variable,text,1024)==3); CHECK(!wcscmp(text,L"abc"));
    CHECK(API(GetEnvironmentVariableA)("K32X_REGRESSION_ENV_483152",ansi,1024)==3); CHECK(!strcmp(ansi,"abc"));
    CHECK(API(ExpandEnvironmentStringsW)(L"pre-%K32X_REGRESSION_ENV_483152%-post",text,1024)==13); CHECK(!wcscmp(text,L"pre-abc-post"));
    LPWCH block=API(GetEnvironmentStringsW)(); CHECK(block); bool found=false;
    for(auto row=block;*row;row+=wcslen(row)+1) if(!wcscmp(row,L"K32X_REGRESSION_ENV_483152=abc")) found=true;
    CHECK(found); CHECK(API(FreeEnvironmentStringsW)(block));
    CHECK(API(SetEnvironmentVariableW)(variable,nullptr));
    DWORD required=API(GetCurrentDirectoryW)(0,nullptr); CHECK(required>1);
    CHECK(API(GetCurrentDirectoryW)(1024,text)==required-1); CHECK(API(GetCurrentDirectoryA)(1024,ansi)>0);
    wchar_t full[1024]{}; wchar_t* part=nullptr;
    CHECK(API(GetFullPathNameW)(L".\\k32x-file",1024,full,&part)>0); CHECK(part); CHECK(!wcscmp(part,L"k32x-file"));
    wchar_t temp[MAX_PATH]{}; CHECK(API(GetTempPathW)(MAX_PATH,temp)>0);
    wchar_t file[MAX_PATH]{}; CHECK(API(GetTempFileNameW)(temp,L"k32",0,file)); CHECK(GetFileAttributesW(file)!=INVALID_FILE_ATTRIBUTES); CHECK(DeleteFileW(file));
    API(SetLastError)(0x314159); CHECK(API(GetLastError)()==0x314159);
    CHECK(API(GetCurrentProcess)()==GetCurrentProcess()); CHECK(API(GetCurrentThread)()==GetCurrentThread());
    CHECK(API(GetCurrentThreadId)()==GetCurrentThreadId()); CHECK(API(GetProcessId)(API(GetCurrentProcess)())==GetCurrentProcessId());
    CHECK(API(GetThreadId)(API(GetCurrentThread)())==GetCurrentThreadId());
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,CREATE_SUSPENDED,nullptr); CHECK(thread);
    DWORD code=0; CHECK(API(GetExitCodeThread)(thread,&code)); CHECK(code==STILL_ACTIVE);
    int priority=API(GetThreadPriority)(thread); CHECK(priority!=THREAD_PRIORITY_ERROR_RETURN);
    CHECK(API(SetThreadPriority)(thread,THREAD_PRIORITY_BELOW_NORMAL)); CHECK(API(GetThreadPriority)(thread)==THREAD_PRIORITY_BELOW_NORMAL);
    CHECK(API(SetThreadPriority)(thread,priority)); CHECK(API(GetThreadId)(thread)!=GetCurrentThreadId());
    CHECK(ResumeThread(thread)==1); CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    CHECK(API(GetExitCodeThread)(thread,&code)); CHECK(code==73);
    CHECK(API(SetHandleInformation)(thread,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)); DWORD flags=0; CHECK(GetHandleInformation(thread,&flags)); CHECK(flags&HANDLE_FLAG_INHERIT);
    CHECK(CloseHandle(thread));
    DWORD mode=API(GetErrorMode)(); CHECK(API(SetErrorMode)(mode|SEM_FAILCRITICALERRORS)==mode); CHECK(API(GetErrorMode)()&SEM_FAILCRITICALERRORS); API(SetErrorMode)(mode);
    DWORD old_thread_mode=0; CHECK(API(SetThreadErrorMode)(SEM_FAILCRITICALERRORS,&old_thread_mode)); CHECK(API(SetThreadErrorMode)(old_thread_mode,nullptr));
    int value=17; void* encoded=API(EncodePointer)(&value); CHECK(API(DecodePointer)(encoded)==&value);
    CHECK(API(IsDebuggerPresent)()==IsDebuggerPresent());
    CHECK(API(IsProcessorFeaturePresent)(PF_XMMI64_INSTRUCTIONS_AVAILABLE)==IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE));
    STARTUPINFOW wide{}; API(GetStartupInfoW)(&wide); CHECK(wide.cb==sizeof(wide));
    STARTUPINFOA narrow{}; API(GetStartupInfoA)(&narrow); CHECK(narrow.cb==sizeof(narrow));
    CHECK(!kernel32_core::resolve("kernel32.dll","CreateThread")); CHECK(!kernel32_core::resolve("kernel32.dll","GetCommandLineW"));
    std::printf("kernel32 process: PASS (%u checks; off-device native process ABI)\n",checks);
}
