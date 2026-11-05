#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <array>
#include <vector>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static ULONGLONG ticks(FILETIME t) { return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
int main() {
    auto systemw=API(GetSystemDirectoryW),windowsw=API(GetWindowsDirectoryW);
    const std::pair<decltype(systemw),decltype(systemw)> directories[]={
        {&::GetSystemDirectoryW,systemw},{&::GetWindowsDirectoryW,windowsw}};
    for(auto pair : directories) {
        for(UINT capacity : {UINT(0),UINT(2),UINT(MAX_PATH)}) {
            std::array<wchar_t,MAX_PATH> left,right; left.fill(0x5555); right.fill(0x5555);
            SetLastError(123); UINT size=pair.first(left.data(),capacity); DWORD error=GetLastError();
            SetLastError(123); CHECK(pair.second(right.data(),capacity)==size); CHECK(GetLastError()==error); CHECK(left==right);
        }
    }
    auto systema=API(GetSystemDirectoryA); char left[MAX_PATH]{},right[MAX_PATH]{};
    CHECK(systema(right,MAX_PATH)==GetSystemDirectoryA(left,MAX_PATH)); CHECK(!strcmp(left,right));
    auto computer=API(GetComputerNameW);
    for(DWORD capacity : {DWORD(0),DWORD(1),DWORD(MAX_PATH)}) {
        std::array<wchar_t,MAX_PATH> a,b; a.fill(0x5555); b.fill(0x5555);
        DWORD n=capacity,m=capacity;
        SetLastError(123); BOOL expected=GetComputerNameW(a.data(),&n); DWORD error=GetLastError();
        SetLastError(123); CHECK(computer(b.data(),&m)==expected); CHECK(GetLastError()==error); CHECK(n==m); CHECK(a==b);
    }
    auto logical=API(GetLogicalProcessorInformationEx); DWORD size=0;
    SetLastError(0); CHECK(!logical(RelationAll,nullptr,&size)); CHECK(GetLastError()==ERROR_INSUFFICIENT_BUFFER); CHECK(size>0);
    std::vector<BYTE> data(size); CHECK(logical(RelationAll,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),&size));
    DWORD offset=0; unsigned cores=0,groups=0;
    while(offset<size) {
        CHECK(size-offset>=sizeof(LOGICAL_PROCESSOR_RELATIONSHIP)+sizeof(DWORD));
        auto record=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()+offset);
        CHECK(record->Size>=8); CHECK(record->Size<=size-offset);
        if(record->Relationship==RelationProcessorCore) ++cores;
        if(record->Relationship==RelationGroup) ++groups;
        offset+=record->Size;
    }
    CHECK(offset==size); CHECK(cores>0); CHECK(groups>0);
    auto times=API(GetSystemTimes); FILETIME idle{},kernel{},user{},idle2{},kernel2{},user2{};
    CHECK(times(&idle,&kernel,&user)); Sleep(20); CHECK(times(&idle2,&kernel2,&user2));
    CHECK(ticks(idle2)>=ticks(idle)); CHECK(ticks(kernel2)>=ticks(kernel)); CHECK(ticks(user2)>=ticks(user));
    CHECK(times(nullptr,nullptr,nullptr));
    SYSTEM_POWER_STATUS power{}; CHECK(API(GetSystemPowerStatus)(&power)); CHECK(power.ACLineStatus<=1 || power.ACLineStatus==255);
    auto version=API(GetVersionExW); OSVERSIONINFOW a{},b{}; a.dwOSVersionInfoSize=b.dwOSVersionInfoSize=sizeof(a);
    CHECK(GetVersionExW(&a)); CHECK(version(&b)); CHECK(!memcmp(&a,&b,sizeof(a)));
    a={}; b={}; SetLastError(123); BOOL expected=GetVersionExW(&a); DWORD error=GetLastError();
    SetLastError(123); CHECK(version(&b)==expected); CHECK(GetLastError()==error); CHECK(!memcmp(&a,&b,sizeof(a)));
    auto condition=API(VerSetConditionMask); ULONGLONG mask=0;
    mask=condition(mask,VER_MAJORVERSION,VER_GREATER_EQUAL);
    CHECK(mask==VerSetConditionMask(0,VER_MAJORVERSION,VER_GREATER_EQUAL));
    CHECK(condition(mask,VER_MINORVERSION,VER_LESS)==VerSetConditionMask(mask,VER_MINORVERSION,VER_LESS));
    CHECK(!kernel32_core::resolve("api-ms-win-core-sysinfo-l1-1-0.dll","DebugBreak"));
    std::printf("kernel32 system: PASS (%u checks; off-device native system queries)\n",checks);
}
