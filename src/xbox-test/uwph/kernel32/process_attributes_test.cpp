#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name));
    CHECK(p==kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll",name));
    return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
int main() {
    auto initialize=API(InitializeProcThreadAttributeList);
    auto update=API(UpdateProcThreadAttribute);
    auto destroy=API(DeleteProcThreadAttributeList);
    for(DWORD count : {DWORD(0),DWORD(1),DWORD(2),DWORD(32)}) {
        SIZE_T expected_size=0,actual_size=0;
        SetLastError(123); BOOL expected=InitializeProcThreadAttributeList(nullptr,count,0,&expected_size); DWORD error=GetLastError();
        SetLastError(123); CHECK(initialize(nullptr,count,0,&actual_size)==expected); CHECK(GetLastError()==error); CHECK(actual_size==expected_size);
    }
    SIZE_T bytes=0; CHECK(!initialize(nullptr,2,0,&bytes)); CHECK(GetLastError()==ERROR_INSUFFICIENT_BUFFER); CHECK(bytes>0);
    // HeapAlloc supplies the alignment required by the opaque native list.
    auto actual=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes)); CHECK(actual);
    auto reference=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes)); CHECK(reference);
    SIZE_T short_size=bytes-1,reference_short=short_size;
    SetLastError(123); BOOL expected=InitializeProcThreadAttributeList(reference,2,0,&reference_short); DWORD error=GetLastError();
    SetLastError(123); CHECK(initialize(actual,2,0,&short_size)==expected); CHECK(GetLastError()==error); CHECK(short_size==reference_short);
    CHECK(InitializeProcThreadAttributeList(reference,2,0,&bytes)); CHECK(initialize(actual,2,0,&bytes));
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(event);
    HANDLE handles[]={event};
    for(SIZE_T size : {SIZE_T(1),sizeof(handles)}) {
        SetLastError(123); expected=UpdateProcThreadAttribute(reference,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,size,nullptr,nullptr); error=GetLastError();
        SetLastError(123); CHECK(update(actual,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,size,nullptr,nullptr)==expected); CHECK(GetLastError()==error);
        if(size==sizeof(handles))CHECK(expected);
    }
    // Duplicate entries and unknown attributes must retain native failures.
    SetLastError(123); expected=UpdateProcThreadAttribute(reference,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr); error=GetLastError();
    SetLastError(123); CHECK(update(actual,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)==expected); CHECK(GetLastError()==error); CHECK(!expected);
    SetLastError(123); expected=UpdateProcThreadAttribute(reference,0,0x7fffffff,handles,sizeof(handles),nullptr,nullptr); error=GetLastError();
    SetLastError(123); CHECK(update(actual,0,0x7fffffff,handles,sizeof(handles),nullptr,nullptr)==expected); CHECK(GetLastError()==error); CHECK(!expected);
    destroy(actual); DeleteProcThreadAttributeList(reference);
    CHECK(HeapFree(GetProcessHeap(),0,actual)); CHECK(HeapFree(GetProcessHeap(),0,reference)); CHECK(CloseHandle(event));
    CHECK(!kernel32_core::resolve("api-ms-win-core-processthreads-l1-1-0.dll","CreatePipe"));
    std::printf("kernel32 process attributes: PASS (%u checks; off-device sizing, ownership and errors)\n",checks);
}
