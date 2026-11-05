#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static unsigned callbacks;
static DWORD CALLBACK cancel_copy(LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,
        DWORD,DWORD,HANDLE,HANDLE,LPVOID opaque) {
    ++callbacks;
    return *static_cast<DWORD*>(opaque);
}
static void write(const std::wstring& path,const char* data,DWORD length) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr); CHECK(f!=INVALID_HANDLE_VALUE);
    DWORD n; CHECK(WriteFile(f,data,length,&n,nullptr)); CHECK(n==length); CHECK(CloseHandle(f));
}
static std::string read(const std::wstring& path) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr); CHECK(f!=INVALID_HANDLE_VALUE);
    DWORD n; char bytes[32]{}; CHECK(ReadFile(f,bytes,sizeof(bytes),&n,nullptr)); CHECK(CloseHandle(f));
    return std::string(bytes,n);
}
int main() {
    wchar_t temp[MAX_PATH],dir[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,temp)); CHECK(GetTempFileNameW(temp,L"ops",0,dir));
    CHECK(DeleteFileW(dir)); CHECK(CreateDirectoryW(dir,nullptr));
    std::wstring source=std::wstring(dir)+L"\\source.bin",dest=std::wstring(dir)+L"\\dest.bin";
    std::wstring moved=std::wstring(dir)+L"\\moved.bin",backup=std::wstring(dir)+L"\\backup.bin";
    auto unmount=API(DeleteVolumeMountPointW);
    // Ordinary test directories and absent paths are not volume mount points.
    // Never create/remove a real mount or touch a drive root in this test.
    std::wstring ordinaryMount=std::wstring(dir)+L"\\";
    std::wstring absentMount=std::wstring(dir)+L"\\absent\\";
    for(const auto& path : {ordinaryMount,absentMount,std::wstring(L"relative-mount\\")}) {
        SetLastError(123);BOOL expected=DeleteVolumeMountPointW(path.c_str());DWORD error=GetLastError();CHECK(!expected);
        SetLastError(123);CHECK(unmount(path.c_str())==expected);CHECK(GetLastError()==error);
    }
    DWORD attributes=GetFileAttributesW(dir);CHECK(attributes!=INVALID_FILE_ATTRIBUTES);CHECK(attributes&FILE_ATTRIBUTE_DIRECTORY);
    write(source,"payload",7);
    WIN32_FIND_DATAW native{},actual{};
    auto find=API(FindFirstFileW);
    HANDLE a=FindFirstFileW(source.c_str(),&native),b=find(source.c_str(),&actual);
    CHECK(a!=INVALID_HANDLE_VALUE); CHECK(b!=INVALID_HANDLE_VALUE);
    CHECK(!wcscmp(native.cFileName,actual.cFileName)); CHECK(native.nFileSizeLow==actual.nFileSizeLow);
    CHECK(native.dwFileAttributes==actual.dwFileAttributes); CHECK(FindClose(a)); CHECK(FindClose(b));
    SetLastError(123); a=FindFirstFileW(dest.c_str(),&native); DWORD error=GetLastError();
    SetLastError(123); b=find(dest.c_str(),&actual); CHECK(a==INVALID_HANDLE_VALUE); CHECK(b==a); CHECK(GetLastError()==error);
    char ansi[MAX_PATH*4]; CHECK(WideCharToMultiByte(CP_ACP,0,source.c_str(),-1,ansi,sizeof(ansi),nullptr,nullptr));
    CHECK(API(GetFileAttributesA)(ansi)==GetFileAttributesW(source.c_str()));
    ULARGE_INTEGER available{},total{},free{};
    CHECK(API(GetDiskFreeSpaceExW)(dir,&available,&total,&free)); CHECK(total.QuadPart>0);
    CHECK(available.QuadPart<=free.QuadPart); CHECK(free.QuadPart<=total.QuadPart);
    auto disk=API(GetDiskFreeSpaceExW);
    SetLastError(123); BOOL expected=GetDiskFreeSpaceExW(dest.c_str(),&available,&total,&free); error=GetLastError();
    SetLastError(123); CHECK(disk(dest.c_str(),&available,&total,&free)==expected); CHECK(GetLastError()==error);
    auto drive=API(GetDriveTypeW); CHECK(drive(temp)==GetDriveTypeW(temp));
    CHECK(API(GetLogicalDrives)()==GetLogicalDrives());
    auto longpath=API(GetLongPathNameW);
    for(DWORD capacity : {DWORD(0),DWORD(2),DWORD(MAX_PATH)}) {
        wchar_t left[MAX_PATH],right[MAX_PATH];
        std::wmemset(left,0x5555,MAX_PATH); std::wmemset(right,0x5555,MAX_PATH);
        SetLastError(123); DWORD n=GetLongPathNameW(source.c_str(),left,capacity); error=GetLastError();
        SetLastError(123); CHECK(longpath(source.c_str(),right,capacity)==n); CHECK(GetLastError()==error);
        CHECK(!memcmp(left,right,sizeof(left)));
    }
    auto copy=API(CopyFileW); CHECK(copy(source.c_str(),dest.c_str(),TRUE)); CHECK(read(dest)=="payload");
    SetLastError(0); CHECK(!copy(source.c_str(),dest.c_str(),TRUE)); CHECK(GetLastError()==ERROR_FILE_EXISTS);
    write(dest,"old",3); CHECK(copy(source.c_str(),dest.c_str(),FALSE)); CHECK(read(dest)=="payload"); CHECK(DeleteFileW(dest.c_str()));
    auto copyex=API(CopyFileExW);
    BOOL cancel=FALSE; DWORD action=PROGRESS_CONTINUE;
    CHECK(CopyFileExW(source.c_str(),dest.c_str(),cancel_copy,&action,&cancel,COPY_FILE_FAIL_IF_EXISTS));
    unsigned reference_callbacks=callbacks; callbacks=0; CHECK(DeleteFileW(dest.c_str()));
    CHECK(copyex(source.c_str(),dest.c_str(),cancel_copy,&action,&cancel,COPY_FILE_FAIL_IF_EXISTS));
    CHECK(callbacks==reference_callbacks);
    if(std::getenv("K32_REQUIRE_COPY_PROGRESS")) CHECK(callbacks>0);
    CHECK(read(dest)=="payload"); CHECK(DeleteFileW(dest.c_str()));
    // A larger file ensures a callback can request cancellation before the
    // transfer completes. Both cancellation paths must remove the destination.
    std::vector<char> large(1024*1024,'x'); write(source,large.data(),DWORD(large.size()));
    action=PROGRESS_CANCEL; callbacks=0;
    SetLastError(0); BOOL native_cancel=CopyFileExW(source.c_str(),dest.c_str(),cancel_copy,&action,&cancel,0);
    error=GetLastError(); reference_callbacks=callbacks;
    if(native_cancel) CHECK(DeleteFileW(dest.c_str()));
    callbacks=0; SetLastError(0);
    CHECK(copyex(source.c_str(),dest.c_str(),cancel_copy,&action,&cancel,0)==native_cancel);
    CHECK(GetLastError()==error); CHECK(callbacks==reference_callbacks);
    if(!native_cancel) { CHECK(error==ERROR_REQUEST_ABORTED); CHECK(GetFileAttributesW(dest.c_str())==INVALID_FILE_ATTRIBUTES); }
    else { CHECK(DeleteFileW(dest.c_str())); std::puts("CopyFileExW progress/cancel unsupported by reference; full contract remains console pending"); }
    if(std::getenv("K32_REQUIRE_COPY_PROGRESS")) { CHECK(!native_cancel); CHECK(callbacks>0); }
    COPYFILE2_EXTENDED_PARAMETERS parameters{}; parameters.dwSize=sizeof(parameters); parameters.dwCopyFlags=COPY_FILE_FAIL_IF_EXISTS;
    auto copy2=API(CopyFile2); CHECK(SUCCEEDED(copy2(source.c_str(),dest.c_str(),&parameters)));
    CHECK(copy2(source.c_str(),dest.c_str(),&parameters)==HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));
    CHECK(DeleteFileW(dest.c_str()));
    write(source,"replacement",11); write(dest,"original",8);
    auto replace=API(ReplaceFileW); CHECK(replace(dest.c_str(),source.c_str(),backup.c_str(),0,nullptr,nullptr));
    CHECK(read(dest)=="replacement"); CHECK(read(backup)=="original"); CHECK(GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES);
    auto move=API(MoveFileExW); CHECK(move(dest.c_str(),moved.c_str(),0)); CHECK(read(moved)=="replacement");
    write(dest,"overwrite",9); SetLastError(0); CHECK(!move(moved.c_str(),dest.c_str(),0)); CHECK(GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(move(moved.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)); CHECK(read(dest)=="replacement");
    CHECK(DeleteFileW(dest.c_str())); CHECK(DeleteFileW(backup.c_str())); CHECK(RemoveDirectoryW(dir));
    CHECK(!kernel32_core::resolve("api-ms-win-core-io-l1-1-0.dll","CopyFileW"));
    std::printf("kernel32 file operations: PASS (%u checks; off-device native file ABI)\n",checks);
}
