#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace kernel_tail {
[[maybe_unused]] static uintptr_t native(const char* name) {
    return reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name));
}
}
[[maybe_unused]] static std::wstring wide(const char* text) {
    if(!text)return {};
    int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    if(!n)return {};
    std::wstring result(n,0);MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),n);return result;
}
static HMODULE WINAPI moduleW(LPCWSTR name) {return GetModuleHandleW(name);}
static BOOL WINAPI moduleExW(DWORD flags,LPCWSTR name,HMODULE* result) {return GetModuleHandleExW(flags,name,result);}
static HMODULE WINAPI loadExW(LPCWSTR name,HANDLE file,DWORD flags) {return LoadLibraryExW(name,file,flags);}
#include "guest_loader_ansi.h"
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s error=%lu\n",__LINE__,#x,GetLastError());std::exit(1);}}while(0)
int main() {
    wchar_t temporary[MAX_PATH],directory[MAX_PATH],system[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temporary));CHECK(GetTempFileNameW(temporary,L"ans",0,directory));
    CHECK(DeleteFileW(directory));CHECK(CreateDirectoryW(directory,nullptr));
    CHECK(GetSystemDirectoryW(system,MAX_PATH));
    std::wstring source=std::wstring(system)+L"\\version.dll";
    std::wstring path=std::wstring(directory)+L"\\caf\u00e9.dll";
    CHECK(CopyFileW(source.c_str(),path.c_str(),TRUE));
    BOOL original=AreFileApisANSI();
    for(bool ansi : {true,false}) {
        if(ansi)SetFileApisToANSI();else SetFileApisToOEM();
        char encoded[MAX_PATH*4];BOOL substituted=FALSE;
        CHECK(WideCharToMultiByte(ansi?CP_ACP:CP_OEMCP,0,path.c_str(),-1,encoded,sizeof encoded,nullptr,&substituted));
        CHECK(!substituted);
        HMODULE reference=LoadLibraryExA(encoded,nullptr,0);CHECK(reference);
        HMODULE actual=loadExA(encoded,nullptr,0);CHECK(actual==reference);
        CHECK(moduleA(encoded)==GetModuleHandleA(encoded));
        HMODULE expectedHandle=nullptr,actualHandle=nullptr;
        DWORD flags=GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        CHECK(GetModuleHandleExA(flags,encoded,&expectedHandle));
        CHECK(moduleExA(flags,encoded,&actualHandle));CHECK(actualHandle==expectedHandle);
        // FROM_ADDRESS receives an address, never an encoded string.
        flags|=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;
        CHECK(moduleExA(flags,reinterpret_cast<LPCSTR>(reference),&actualHandle));CHECK(actualHandle==reference);
        CHECK(moduleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,nullptr,&actualHandle));
        CHECK(actualHandle==GetModuleHandleW(nullptr));CHECK(moduleA(nullptr)==GetModuleHandleW(nullptr));
        CHECK(FreeLibrary(actual));CHECK(FreeLibrary(reference));
        actual=loadA(encoded);CHECK(actual);CHECK(FreeLibrary(actual));
        CHECK(!GetModuleHandleW(path.c_str()));
        for(const char* missing : {"absent-k32-ansi.dll", "caf\xe9-missing.dll"}) {
            SetLastError(73);reference=LoadLibraryExA(missing,nullptr,0);DWORD error=GetLastError();
            SetLastError(73);CHECK(loadExA(missing,nullptr,0)==reference);CHECK(GetLastError()==error);
            SetLastError(73);reference=GetModuleHandleA(missing);error=GetLastError();
            SetLastError(73);CHECK(moduleA(missing)==reference);CHECK(GetLastError()==error);
        }
    }
    if(original)SetFileApisToANSI();else SetFileApisToOEM();
    SetLastError(73);CHECK(!moduleExA(0,"kernel32.dll",nullptr));CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(DeleteFileW(path.c_str()));CHECK(RemoveDirectoryW(directory));
    std::printf("kernel32 ANSI loader: PASS (%u checks; off-device Wine differential)\n",checks);
}
