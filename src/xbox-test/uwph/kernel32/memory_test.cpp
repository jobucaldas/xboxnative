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
int main() {
    HANDLE heap=API(GetProcessHeap)(); CHECK(heap==GetProcessHeap());
    auto data=static_cast<BYTE*>(API(HeapAlloc)(heap,HEAP_ZERO_MEMORY,32)); CHECK(data);
    for(unsigned i=0;i<32;++i)CHECK(data[i]==0);
    CHECK(API(HeapSize)(heap,0,data)>=32); data[0]=17;
    data=static_cast<BYTE*>(API(HeapReAlloc)(heap,HEAP_ZERO_MEMORY,data,128)); CHECK(data); CHECK(data[0]==17);
    for(unsigned i=32;i<128;++i)CHECK(data[i]==0);
    ULONG mode=0; SIZE_T length=0;
    CHECK(API(HeapQueryInformation)(heap,HeapCompatibilityInformation,&mode,sizeof(mode),&length)); CHECK(length==sizeof(mode));
    CHECK(API(HeapFree)(heap,0,data));
    HGLOBAL global=API(GlobalAlloc)(GMEM_MOVEABLE|GMEM_ZEROINIT,64); CHECK(global);
    data=static_cast<BYTE*>(API(GlobalLock)(global)); CHECK(data); CHECK(data[0]==0); data[0]=19;
    SetLastError(123); CHECK(!API(GlobalUnlock)(global)); CHECK(GetLastError()==ERROR_SUCCESS);
    SetLastError(123); CHECK(!API(GlobalUnlock)(global)); CHECK(GetLastError()==ERROR_NOT_LOCKED);
    data=static_cast<BYTE*>(API(GlobalLock)(global)); CHECK(data); CHECK(data[0]==19);
    CHECK(!API(GlobalUnlock)(global)); CHECK(API(GlobalFree)(global)==nullptr);
    HLOCAL local=API(LocalAlloc)(LMEM_FIXED|LMEM_ZEROINIT,48); CHECK(local); CHECK(static_cast<BYTE*>(local)[47]==0);
    CHECK(API(LocalFree)(local)==nullptr); CHECK(API(LocalFree)(nullptr)==nullptr);
    // Compare resize/zero growth through the route and Wine's native local
    // allocator. Fixed and movable handles must keep their original data.
    for(UINT flags : {UINT(LMEM_FIXED),UINT(LMEM_MOVEABLE)}) {
        for(bool routed : {false,true}) {
            auto resize=routed ? API(LocalReAlloc) : &::LocalReAlloc;
            HLOCAL block=LocalAlloc(flags|LMEM_ZEROINIT,32); CHECK(block);
            auto bytes=static_cast<BYTE*>(LocalLock(block)); CHECK(bytes);
            for(unsigned i=0;i<32;++i) bytes[i]=BYTE(i+1);
            if(flags==LMEM_MOVEABLE) LocalUnlock(block);
            block=resize(block,512,LMEM_MOVEABLE|LMEM_ZEROINIT); CHECK(block);
            bytes=static_cast<BYTE*>(LocalLock(block)); CHECK(bytes);
            for(unsigned i=0;i<32;++i) CHECK(bytes[i]==BYTE(i+1));
            for(unsigned i=32;i<512;++i) CHECK(bytes[i]==0);
            if(flags==LMEM_MOVEABLE) LocalUnlock(block);
            CHECK(!LocalFree(block));
        }
    }
    SYSTEM_INFO info{},native{}; API(GetSystemInfo)(&info); API(GetNativeSystemInfo)(&native);
    CHECK(info.dwPageSize>0); CHECK(native.dwPageSize==info.dwPageSize); CHECK(info.dwAllocationGranularity>=info.dwPageSize);
    void* pages=VirtualAlloc(nullptr,info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(pages);
    MEMORY_BASIC_INFORMATION region{};
    CHECK(API(VirtualQuery)(pages,&region,sizeof(region))==sizeof(region)); CHECK(region.State==MEM_COMMIT);
    CHECK(region.AllocationBase==pages); CHECK(region.Protect==PAGE_READWRITE); CHECK(region.RegionSize>=info.dwPageSize);
    SetLastError(0); CHECK(!API(VirtualFree)(pages,info.dwPageSize,MEM_RELEASE)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    CHECK(API(VirtualFree)(pages,0,MEM_RELEASE));
    CHECK(API(VirtualQuery)(pages,&region,sizeof(region))==sizeof(region)); CHECK(region.State==MEM_FREE);
    MEMORYSTATUSEX status{}; status.dwLength=sizeof(status); CHECK(API(GlobalMemoryStatusEx)(&status));
    CHECK(status.ullTotalPhys>0); CHECK(status.ullAvailPhys<=status.ullTotalPhys); CHECK(status.dwMemoryLoad<=100);
    HANDLE mapping=API(CreateFileMappingW)(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,info.dwPageSize,nullptr); CHECK(mapping);
    auto first=static_cast<BYTE*>(API(MapViewOfFile)(mapping,FILE_MAP_WRITE,0,0,0)); CHECK(first); CHECK(first[0]==0); first[0]=47;
    auto second=static_cast<BYTE*>(API(MapViewOfFile)(mapping,FILE_MAP_READ,0,0,0)); CHECK(second); CHECK(second[0]==47);
    auto copy=static_cast<BYTE*>(API(MapViewOfFile)(mapping,FILE_MAP_COPY,0,0,0)); CHECK(copy); CHECK(copy[0]==47); copy[0]=11; CHECK(first[0]==47);
    SetLastError(0); CHECK(!API(MapViewOfFile)(mapping,FILE_MAP_READ,0,1,0)); CHECK(GetLastError()==ERROR_MAPPED_ALIGNMENT);
    CHECK(API(UnmapViewOfFile)(first)); CHECK(API(UnmapViewOfFile)(second)); CHECK(API(UnmapViewOfFile)(copy)); CHECK(CloseHandle(mapping));
    SetLastError(0); CHECK(!API(UnmapViewOfFile)(nullptr)); CHECK(GetLastError()==ERROR_INVALID_ADDRESS);
    wchar_t mapping_name[96]; swprintf(mapping_name,96,L"Local\\k32x-map-%lu",GetCurrentProcessId());
    mapping=API(CreateFileMappingW)(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,mapping_name); CHECK(mapping);
    auto open=API(OpenFileMappingW); HANDLE alias=open(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,mapping_name); CHECK(alias);
    first=static_cast<BYTE*>(API(MapViewOfFile)(mapping,FILE_MAP_WRITE,0,0,4096)); CHECK(first);
    second=static_cast<BYTE*>(API(MapViewOfFile)(alias,FILE_MAP_READ,0,0,4096)); CHECK(second);
    first[17]=93; CHECK(second[17]==93);
    CHECK(API(UnmapViewOfFile)(first)); CHECK(API(UnmapViewOfFile)(second)); CHECK(CloseHandle(mapping)); CHECK(CloseHandle(alias));
    SetLastError(0); CHECK(!open(FILE_MAP_READ,FALSE,mapping_name)); CHECK(GetLastError()==ERROR_FILE_NOT_FOUND);
    SetLastError(123); HANDLE reference=OpenFileMappingW(0,FALSE,mapping_name); DWORD error=GetLastError();
    SetLastError(123); CHECK(open(0,FALSE,mapping_name)==reference); CHECK(GetLastError()==error);
    CHECK(!kernel32_core::resolve("kernel32.dll","VirtualAlloc"));
    CHECK(!kernel32_core::resolve("kernel32.dll","VirtualProtect"));
    CHECK(!kernel32_core::resolve("api-ms-win-core-heap-l1-1-0.dll","VirtualFree"));
    CHECK(kernel32_core::resolve("api-ms-win-core-memory-l1-1-0.dll","VirtualQuery"));
    std::printf("kernel32 memory: PASS (%u checks; off-device native memory ABI)\n",checks);
}
