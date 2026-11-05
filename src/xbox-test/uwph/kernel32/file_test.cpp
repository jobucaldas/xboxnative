#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <string>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
int main() {
    wchar_t temp[MAX_PATH],directory[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,temp));
    CHECK(GetTempFileNameW(temp,L"k32",0,directory)); CHECK(API(DeleteFileW)(directory));
    CHECK(API(CreateDirectoryW)(directory,nullptr));
    std::wstring path=std::wstring(directory)+L"\\state.bin";
    HANDLE file=API(CreateFileW)(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(file!=INVALID_HANDLE_VALUE); CHECK(API(GetFileType)(file)==FILE_TYPE_DISK);
    DWORD count=0; CHECK(API(WriteFile)(file,"payload",7,&count,nullptr)); CHECK(count==7);
    CHECK(API(FlushFileBuffers)(file));
    LARGE_INTEGER size{}; CHECK(API(GetFileSizeEx)(file,&size)); CHECK(size.QuadPart==7);
    DWORD high=99; CHECK(API(GetFileSize)(file,&high)==7); CHECK(high==0);
    CHECK(API(SetFilePointer)(file,0,nullptr,FILE_BEGIN)==0);
    char bytes[10]{}; CHECK(API(ReadFile)(file,bytes,sizeof(bytes),&count,nullptr)); CHECK(count==7); CHECK(!memcmp(bytes,"payload",7));
    CHECK(API(ReadFile)(file,bytes,sizeof(bytes),&count,nullptr)); CHECK(count==0);
    LARGE_INTEGER position{}; position.QuadPart=4;
    CHECK(API(SetFilePointerEx)(file,position,nullptr,FILE_BEGIN)); CHECK(API(SetEndOfFile)(file));
    CHECK(API(GetFileSizeEx)(file,&size)); CHECK(size.QuadPart==4);
    BY_HANDLE_FILE_INFORMATION information{}; CHECK(API(GetFileInformationByHandle)(file,&information)); CHECK(information.nFileSizeLow==4);
    FILE_STANDARD_INFO standard{}; CHECK(API(GetFileInformationByHandleEx)(file,FileStandardInfo,&standard,sizeof(standard))); CHECK(standard.EndOfFile.QuadPart==4);
    FILE_END_OF_FILE_INFO end{}; end.EndOfFile.QuadPart=12;
    CHECK(API(SetFileInformationByHandle)(file,FileEndOfFileInfo,&end,sizeof(end)));
    CHECK(API(GetFileSizeEx)(file,&size)); CHECK(size.QuadPart==12);
    FILETIME write{},read{},creation{}; CHECK(API(GetFileTime)(file,&creation,&read,&write));
    FILETIME changed{0x12340000,0x01da0000}; CHECK(API(SetFileTime)(file,nullptr,nullptr,&changed));
    CHECK(API(GetFileTime)(file,nullptr,nullptr,&write)); CHECK(write.dwLowDateTime==changed.dwLowDateTime); CHECK(write.dwHighDateTime==changed.dwHighDateTime);
    CHECK(API(CancelIo)(file));
    SetLastError(0); CHECK(!API(CancelIoEx)(file,nullptr)); CHECK(GetLastError()==ERROR_NOT_FOUND);
    CHECK(CloseHandle(file));
    CHECK(API(SetFileAttributesW)(path.c_str(),FILE_ATTRIBUTE_READONLY));
    CHECK(API(GetFileAttributesW)(path.c_str())&FILE_ATTRIBUTE_READONLY);
    WIN32_FILE_ATTRIBUTE_DATA attributes{}; CHECK(API(GetFileAttributesExW)(path.c_str(),GetFileExInfoStandard,&attributes)); CHECK(attributes.nFileSizeLow==12);
    SetLastError(0); CHECK(!API(DeleteFileW)(path.c_str())); CHECK(GetLastError()==ERROR_ACCESS_DENIED);
    CHECK(API(SetFileAttributesW)(path.c_str(),FILE_ATTRIBUTE_NORMAL));
    WIN32_FIND_DATAW found{}; HANDLE search=API(FindFirstFileExW)(path.c_str(),FindExInfoBasic,&found,FindExSearchNameMatch,nullptr,0);
    CHECK(search!=INVALID_HANDLE_VALUE); CHECK(!wcscmp(found.cFileName,L"state.bin"));
    SetLastError(0); CHECK(!API(FindNextFileW)(search,&found)); CHECK(GetLastError()==ERROR_NO_MORE_FILES); CHECK(API(FindClose)(search));
    std::string ansi(path.size()*4+1,'\0'); CHECK(WideCharToMultiByte(CP_ACP,0,path.c_str(),-1,&ansi[0],int(ansi.size()),nullptr,nullptr));
    file=API(CreateFileA)(ansi.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr); CHECK(file!=INVALID_HANDLE_VALUE);
    OVERLAPPED overlapped{}; overlapped.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr); CHECK(overlapped.hEvent);
    BOOL immediate=API(ReadFile)(file,bytes,4,&count,&overlapped); if(!immediate)CHECK(GetLastError()==ERROR_IO_PENDING);
    CHECK(API(GetOverlappedResult)(file,&overlapped,&count,TRUE)); CHECK(count==4); CHECK(!memcmp(bytes,"payl",4));
    CHECK(CloseHandle(overlapped.hEvent)); CHECK(CloseHandle(file));
    // Compare adapters with Wine's documented APIs on the same real file.
    for (DWORD flags : {DWORD(FILE_ATTRIBUTE_NORMAL), DWORD(FILE_FLAG_OVERLAPPED),
            DWORD(FILE_FLAG_RANDOM_ACCESS), DWORD(FILE_FLAG_SEQUENTIAL_SCAN),
            DWORD(SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION)}) {
        SetLastError(123);
        HANDLE reference=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
        DWORD expected=GetLastError();
        SetLastError(123);
        HANDLE actual=API(CreateFileW)(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
        DWORD error=GetLastError();
        CHECK((reference==INVALID_HANDLE_VALUE)==(actual==INVALID_HANDLE_VALUE));
        if(reference==INVALID_HANDLE_VALUE) CHECK(error==expected);
        else { CHECK(CloseHandle(reference)); CHECK(CloseHandle(actual)); }
    }
    for (DWORD disposition : {DWORD(CREATE_NEW),DWORD(OPEN_EXISTING),DWORD(0)}) {
        HANDLE reference=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,disposition,0,nullptr);
        DWORD expected=GetLastError();
        HANDLE actual=API(CreateFileW)(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,disposition,0,nullptr);
        DWORD error=GetLastError();
        CHECK((reference==INVALID_HANDLE_VALUE)==(actual==INVALID_HANDLE_VALUE));
        if(reference==INVALID_HANDLE_VALUE) CHECK(error==expected);
        else { CHECK(CloseHandle(reference)); CHECK(CloseHandle(actual)); }
    }
    DWORD reference_high=99,actual_high=99;
    SetLastError(123); DWORD reference_size=GetFileSize(INVALID_HANDLE_VALUE,&reference_high); DWORD expected=GetLastError();
    SetLastError(123); CHECK(API(GetFileSize)(INVALID_HANDLE_VALUE,&actual_high)==reference_size); CHECK(GetLastError()==expected); CHECK(actual_high==reference_high);
    CHECK(API(DeleteFileW)(path.c_str()));
    SetLastError(0); CHECK(API(CreateFileW)(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr)==INVALID_HANDLE_VALUE); CHECK(GetLastError()==ERROR_FILE_NOT_FOUND);
    CHECK(API(RemoveDirectoryW)(directory));
    SetLastError(0); CHECK(!API(ReadFile)(INVALID_HANDLE_VALUE,bytes,4,&count,nullptr)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    CHECK(!kernel32_core::resolve("api-ms-win-core-io-l1-1-0.dll","DeleteFileW"));
    CHECK(kernel32_core::resolve("api-ms-win-core-file-l1-1-0.dll","CreateFileW"));
    HANDLE reader=nullptr,writer=nullptr; SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    CHECK(API(CreatePipe)(&reader,&writer,&security,0)); CHECK(reader); CHECK(writer);
    DWORD handle_flags=0; CHECK(GetHandleInformation(reader,&handle_flags)); CHECK(handle_flags&HANDLE_FLAG_INHERIT);
    CHECK(SetHandleInformation(reader,HANDLE_FLAG_INHERIT,0));
    auto peek=API(PeekNamedPipe); DWORD available=99,remaining=99;
    CHECK(peek(reader,nullptr,0,nullptr,&available,nullptr)); CHECK(available==0);
    CHECK(API(WriteFile)(writer,"pipe-data",9,&count,nullptr)); CHECK(count==9);
    char pipe_bytes[16]{};
    DWORD native_remaining=99;
    char reference_bytes[4]; CHECK(PeekNamedPipe(reader,reference_bytes,4,nullptr,nullptr,&native_remaining));
    CHECK(peek(reader,pipe_bytes,4,&count,&available,&remaining)); CHECK(count==4); CHECK(available==9); CHECK(remaining==native_remaining); CHECK(!memcmp(pipe_bytes,"pipe",4));
    CHECK(API(ReadFile)(reader,pipe_bytes,4,&count,nullptr)); CHECK(count==4);
    CHECK(peek(reader,nullptr,0,nullptr,&available,nullptr)); CHECK(available==5);
    CHECK(CloseHandle(writer)); CHECK(API(ReadFile)(reader,pipe_bytes,16,&count,nullptr)); CHECK(count==5); CHECK(!memcmp(pipe_bytes,"-data",5));
    SetLastError(0); CHECK(!API(ReadFile)(reader,pipe_bytes,16,&count,nullptr)); CHECK(GetLastError()==ERROR_BROKEN_PIPE);
    CHECK(CloseHandle(reader));
    SetLastError(0); CHECK(!peek(INVALID_HANDLE_VALUE,nullptr,0,nullptr,nullptr,nullptr)); CHECK(GetLastError()==ERROR_INVALID_HANDLE);
    std::printf("kernel32 files: PASS (%u checks; off-device native file ABI)\n",checks);
}
