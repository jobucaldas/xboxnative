#include "route.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name));
    CHECK(p==kernel32_core::resolve("api-ms-win-core-console-l1-1-0.dll",name));
    return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static BOOL WINAPI control(DWORD) { return FALSE; }
int main() {
    auto get=API(GetStdHandle); auto set=API(SetStdHandle);
    for(DWORD kind : {DWORD(STD_INPUT_HANDLE),DWORD(STD_OUTPUT_HANDLE),DWORD(STD_ERROR_HANDLE),DWORD(123)}) {
        SetLastError(1234); HANDLE expected=GetStdHandle(kind); DWORD error=GetLastError();
        SetLastError(1234); CHECK(get(kind)==expected); CHECK(GetLastError()==error);
    }
    HANDLE original=get(STD_INPUT_HANDLE),reader=nullptr,writer=nullptr;
    CHECK(CreatePipe(&reader,&writer,nullptr,0));
    CHECK(set(STD_INPUT_HANDLE,reader)); CHECK(get(STD_INPUT_HANDLE)==reader);
    CHECK(set(STD_INPUT_HANDLE,original)); CHECK(CloseHandle(reader)); CHECK(CloseHandle(writer));
    SetLastError(123); BOOL expected=SetStdHandle(123,nullptr); DWORD error=GetLastError();
    SetLastError(123); CHECK(set(123,nullptr)==expected); CHECK(GetLastError()==error);
    auto mode=API(GetConsoleMode); DWORD a=0x1234,b=a;
    SetLastError(123); expected=GetConsoleMode(INVALID_HANDLE_VALUE,&a); error=GetLastError();
    SetLastError(123); CHECK(mode(INVALID_HANDLE_VALUE,&b)==expected); CHECK(GetLastError()==error); CHECK(a==b);
    CHECK(API(GetConsoleOutputCP)()==GetConsoleOutputCP());
    auto read=API(ReadConsoleW); auto write=API(WriteConsoleW);
    wchar_t text[4]={L'?',L'?',L'?',0}; DWORD n=77,m=77;
    SetLastError(123); expected=ReadConsoleW(INVALID_HANDLE_VALUE,text,2,&n,nullptr); error=GetLastError();
    SetLastError(123); CHECK(read(INVALID_HANDLE_VALUE,text,2,&m,nullptr)==expected); CHECK(GetLastError()==error); CHECK(n==m);
    SetLastError(123); expected=WriteConsoleW(INVALID_HANDLE_VALUE,L"test",4,&n,nullptr); error=GetLastError();
    SetLastError(123); CHECK(write(INVALID_HANDLE_VALUE,L"test",4,&m,nullptr)==expected); CHECK(GetLastError()==error); CHECK(n==m);
    auto handler=API(SetConsoleCtrlHandler); CHECK(handler(control,TRUE)); CHECK(handler(control,FALSE));
    SetLastError(123); expected=SetConsoleCtrlHandler(control,FALSE); error=GetLastError();
    SetLastError(123); CHECK(handler(control,FALSE)==expected); CHECK(GetLastError()==error);
    auto attach=API(AttachConsole);
    SetLastError(123); expected=AttachConsole(0); error=GetLastError(); CHECK(!expected);
    SetLastError(123); CHECK(attach(0)==expected); CHECK(GetLastError()==error);
    auto allocate=API(AllocConsole);
    HANDLE savedIn=GetStdHandle(STD_INPUT_HANDLE),savedOut=GetStdHandle(STD_OUTPUT_HANDLE),savedError=GetStdHandle(STD_ERROR_HANDLE);
    SetLastError(123);expected=AllocConsole();error=GetLastError();
    if(expected)CHECK(FreeConsole());
    SetLastError(123);BOOL allocated=allocate();CHECK(allocated==expected);CHECK(GetLastError()==error);
    if(allocated) {
        SetLastError(0);CHECK(!allocate());CHECK(GetLastError()==ERROR_ACCESS_DENIED);
        CHECK(FreeConsole());
        CHECK(SetStdHandle(STD_INPUT_HANDLE,savedIn));CHECK(SetStdHandle(STD_OUTPUT_HANDLE,savedOut));CHECK(SetStdHandle(STD_ERROR_HANDLE,savedError));
    }
    std::printf("AllocConsole reference success=%d; allocation follows native host policy\n",expected);
    CHECK(!kernel32_core::resolve("api-ms-win-core-console-l1-1-0.dll","CreateFileW"));
    std::printf("kernel32 console: PASS (%u checks; off-device handle/error contracts; no console allocation claim)\n",checks);
}
