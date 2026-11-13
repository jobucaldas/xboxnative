#pragma once
#include <vector>
#include <windows.h>
// Synthetic host checks. No guest entry point executes. All state changes are
// restored before returning to the Play probe; no account/session data is read.
namespace kernel_tail {
inline unsigned test(void (*log)(const char*)) {
    unsigned checks=0,failures=0;
    auto check=[&](bool pass,const char* label){++checks;if(!pass)++failures;char b[256];snprintf(b,sizeof b,"K32T CHECK %s pass=%u error=%lu",label,unsigned(pass),GetLastError());log(b);};
    using Snapshot=HANDLE (WINAPI*)(DWORD,DWORD);using Enumerate=BOOL (WINAPI*)(HANDLE,ThreadEntry*);
    auto snapshot=(Snapshot)native("CreateToolhelp32Snapshot");auto first=(Enumerate)resolve("Thread32First");auto next=(Enumerate)resolve("Thread32Next");
    check(snapshot&&first&&next,"toolhelp-exports");
    if(snapshot&&first&&next) {
        HANDLE h=snapshot(4,0);check(h!=INVALID_HANDLE_VALUE,"snapshot-real-threads");
        if(h!=INVALID_HANDLE_VALUE) {
            ThreadEntry e{};e.size=sizeof(e)-1;check(!first(h,&e)&&GetLastError()==ERROR_INSUFFICIENT_BUFFER,"thread-size-error");
            e.size=sizeof(e);bool ok=first(h,&e);check(ok,"thread-first");bool own=false;unsigned count=0;
            while(ok&&count<100000){own|=e.id==GetCurrentThreadId()&&e.process==GetCurrentProcessId();++count;ok=next(h,&e);}
            check(!ok&&GetLastError()==ERROR_NO_MORE_FILES,"thread-enumeration-end");check(own,"snapshot-contains-calling-thread");
            check(first(h,&e),"thread-first-rewinds");check(CloseHandle(h),"snapshot-close");
            e.size=sizeof(e);check(!next(h,&e)&&GetLastError()==ERROR_INVALID_HANDLE,"snapshot-closed-error");
        }
    }
    using Timer=HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES,BOOL,LPCSTR);
    auto timer=(Timer)native("CreateWaitableTimerA");check(!!timer,"timer-export");
    if(timer){HANDLE h=timer(nullptr,TRUE,nullptr);check(!!h,"timer-create");if(h){LARGE_INTEGER due;due.QuadPart=-10000;check(SetWaitableTimer(h,&due,0,nullptr,nullptr,FALSE),"timer-arm");check(WaitForSingleObject(h,2000)==WAIT_OBJECT_0,"timer-signals");check(WaitForSingleObject(h,0)==WAIT_OBJECT_0,"timer-manual-reset");check(CloseHandle(h),"timer-close");}}
    using Computer=BOOL (WINAPI*)(LPWSTR,LPDWORD);auto computer=(Computer)native("GetComputerNameW");check(!!computer,"computer-export");
    if(computer){DWORD size=0;check(!computer(nullptr,&size)&&GetLastError()==ERROR_BUFFER_OVERFLOW&&size>1,"computer-required-size");if(size>1&&size<32768){std::vector<wchar_t> name(size);DWORD cap=size;check(computer(name.data(),&cap)&&cap+1==size&&name[cap]==0,"computer-name-copy");}}
    using Startup=void (WINAPI*)(LPSTARTUPINFOA);auto startup=(Startup)native("GetStartupInfoA");check(!!startup,"startup-export");
    if(startup){STARTUPINFOA a{},again{};STARTUPINFOW w{};startup(&a);startup(&again);((void (WINAPI*)(LPSTARTUPINFOW))native("GetStartupInfoW"))(&w);check(a.cb==sizeof(a),"startup-size");check(a.dwFlags==w.dwFlags&&a.wShowWindow==w.wShowWindow,"startup-process-flags");check(a.dwX==w.dwX&&a.dwY==w.dwY&&a.dwXSize==w.dwXSize&&a.dwYSize==w.dwYSize,"startup-geometry");check(a.lpDesktop==again.lpDesktop&&a.lpTitle==again.lpTitle,"startup-stable-strings");}
    using Power=BOOL (WINAPI*)(LPSYSTEM_POWER_STATUS);auto power=(Power)native("GetSystemPowerStatus");check(!!power,"power-export");
    if(power){SYSTEM_POWER_STATUS p{};BOOL ok=power(&p);check(ok,"power-query");if(ok)check((p.ACLineStatus<=1||p.ACLineStatus==255)&&(p.BatteryLifePercent<=100||p.BatteryLifePercent==255),"power-valid-values");}
    using Directory=BOOL (WINAPI*)(LPCWSTR);using GetDirectory=DWORD (WINAPI*)(DWORD,LPWSTR);
    auto directory=(Directory)resolve("SetDllDirectoryW");auto getDirectory=(GetDirectory)resolve("GetDllDirectoryW");check(directory&&getDirectory,"dll-directory-exports");
    if(directory&&getDirectory){DWORD n=getDirectory(0,nullptr);std::vector<wchar_t> old(n+1);getDirectory(DWORD(old.size()),old.data());check(directory(L"."),"dll-directory-set");wchar_t current[16]{};check(getDirectory(16,current)==1&&!wcscmp(current,L"."),"dll-directory-observed");check(directory(old[0]?old.data():nullptr),"dll-directory-restore");}
    using Affinity=DWORD_PTR (WINAPI*)(HANDLE,DWORD_PTR);using GetAffinity=BOOL (WINAPI*)(HANDLE,PGROUP_AFFINITY);
    auto affinity=(Affinity)native("SetThreadAffinityMask");auto getAffinity=(GetAffinity)native("GetThreadGroupAffinity");check(affinity&&getAffinity,"affinity-exports");
    if(affinity&&getAffinity){GROUP_AFFINITY old{};BOOL got=getAffinity(GetCurrentThread(),&old);check(got&&old.Mask,"affinity-query");if(got&&old.Mask){DWORD_PTR one=old.Mask&(~old.Mask+1);DWORD_PTR before=affinity(GetCurrentThread(),one);check(before==old.Mask,"affinity-return-previous");GROUP_AFFINITY now{};check(getAffinity(GetCurrentThread(),&now)&&now.Mask==one,"affinity-applied");check(affinity(GetCurrentThread(),old.Mask)==one,"affinity-restored");check(!affinity(GetCurrentThread(),0)&&GetLastError()==ERROR_INVALID_PARAMETER,"affinity-zero-rejected");}}
    using Version=BOOL (WINAPI*)(LPOSVERSIONINFOEXW,DWORD,DWORDLONG);auto version=(Version)resolve("VerifyVersionInfoW");check(!!version,"version-export");
    if(version){OSVERSIONINFOEXW v{};v.dwOSVersionInfoSize=sizeof(v);v.dwMajorVersion=1;DWORDLONG conditions=((DWORDLONG (WINAPI*)(DWORDLONG,DWORD,BYTE))native("VerSetConditionMask"))(0,VER_MAJORVERSION,VER_GREATER_EQUAL);check(version(&v,VER_MAJORVERSION,conditions),"version-old-minimum");v.dwMajorVersion=0xffffffff;check(!version(&v,VER_MAJORVERSION,conditions)&&GetLastError()==ERROR_OLD_WIN_VERSION,"version-future-rejected");check(!version(&v,0,0)&&GetLastError()==ERROR_BAD_ARGUMENTS,"version-bad-arguments");}
    // IsBadStringPtrW must walk WCHAR units: a byte scan stops at the high
    // half of the first BMP character (L"A" is bytes 41 00), so a pointer
    // valid for one character would pass while Wine's debugstr_wn keeps
    // reading. Fixture: the string ends at a page boundary and the following
    // page is reserved inaccessible, so a scan reading past the terminator
    // faults and must be reported bad, not crash the donor.
    using BadA=BOOL(WINAPI*)(LPCSTR,UINT_PTR);using BadW=BOOL(WINAPI*)(LPCWSTR,UINT_PTR);
    auto badA=(BadA)resolve("IsBadStringPtrA");auto badW=(BadW)resolve("IsBadStringPtrW");
    check(!!badA&&!!badW,"badstring-exports");
    if(badA&&badW) {
        SYSTEM_INFO info{};GetSystemInfo(&info);
        auto region=VirtualAlloc(nullptr,2*info.dwPageSize,MEM_RESERVE,PAGE_NOACCESS);
        auto page=region?VirtualAlloc(region,info.dwPageSize,MEM_COMMIT,PAGE_READWRITE):nullptr;
        check(!!page,"badstring-pages");
        if(page) {
            auto end=(WCHAR*)((char*)page+info.dwPageSize);
            // A character whose low byte is zero sits last with a terminator
            // after it: the scan must continue past that zero BYTE.
            end[-2]=0x4100;end[-1]=0;
            check(!badW(end-2,64),"badstring-wide-terminates-past-zero-byte");
            char* ansiEnd=(char*)end;
            ansiEnd[-2]='x';ansiEnd[-1]=0;
            check(!badA(ansiEnd-2,64),"badstring-narrow-terminates");
            // Wine's debugstr_a/wn pass (UINT_PTR)-1 as the count. A scan that
            // forms end=p+count wraps below p for that count and would report
            // the pointer good without probing anything.
            check(!badW(end-2,0)&&!badA(ansiEnd-2,0),"badstring-count-zero-readable");
            check(!badW(end-2,1)&&!badA(ansiEnd-2,1),"badstring-count-one-readable");
            check(!badW(end-2,(UINT_PTR)-1)&&!badA(ansiEnd-2,(UINT_PTR)-1),"badstring-count-max-valid-string");
#ifdef _MSC_VER
            // No terminator: the scan runs off the page and must come back
            // bad through the SEH path. (mingw builds take the unguarded
            // scan and would die here, so they skip the faulting cases.)
            end[-1]=0x4100;
            check(badW(end-2,64),"badstring-wide-past-page-is-bad");
            ansiEnd[-2]='y';ansiEnd[-1]='z';
            check(badA(ansiEnd-2,64),"badstring-narrow-past-page-is-bad");
            // The same unterminated strings with the wrapped count: the scan
            // must still hit the inaccessible page instead of returning good.
            check(badW(end-2,(UINT_PTR)-1),"badstring-count-max-wide-past-page-is-bad");
            check(badA(ansiEnd-2,(UINT_PTR)-1),"badstring-count-max-narrow-past-page-is-bad");
#endif
            check(VirtualFree(region,0,MEM_RELEASE),"badstring-free");
        }
    }
    char line[160];snprintf(line,sizeof line,"K32T SELFTEST checks=%u failures=%u synthetic-host-calls no-guest-code",checks,failures);log(line);return failures;
}
}
