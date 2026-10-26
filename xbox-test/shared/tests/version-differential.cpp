#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "../kernel_tail.h"
int main() {
    OSVERSIONINFOEXW actual{};actual.dwOSVersionInfoSize=sizeof(actual);
    GetVersionExW((LPOSVERSIONINFOW)&actual);
    using Verify=BOOL(WINAPI*)(LPOSVERSIONINFOEXW,DWORD,DWORDLONG);
    auto wine=(Verify)kernel_tail::native("VerifyVersionInfoW");
    unsigned failures=0,seed=42;
    for(unsigned n=0;n<20000;++n) {
        auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
        OSVERSIONINFOEXW wanted=actual;
        wanted.dwMajorVersion+=random()%5-2;wanted.dwMinorVersion+=random()%5-2;
        wanted.wServicePackMajor=WORD(random()%3);wanted.wServicePackMinor=WORD(random()%3);
        wanted.dwBuildNumber+=random()%5-2;wanted.dwPlatformId=random()%4;
        wanted.wSuiteMask=WORD(random()%1024);wanted.wProductType=BYTE(random()%4);
        DWORD types=random()%256;DWORDLONG conditions=0;
        for(unsigned i=0;i<8;++i)conditions|=DWORDLONG(random()%8)<<(i*3);
        SetLastError(0);BOOL expected=wine(&wanted,types,conditions);DWORD error=GetLastError();
        SetLastError(0);BOOL observed=kernel_tail::version(&wanted,types,conditions);DWORD actual_error=GetLastError();
        if(expected!=observed||!expected&&error!=actual_error) {
            if(failures++<10)printf("DIFFERENCE case=%u types=%lx conditions=%llx expected=%d/%lu actual=%d/%lu\n",n,types,conditions,expected,error,observed,actual_error);
        }
    }
    printf("K32T VERSION DIFFERENTIAL cases=20000 failures=%u OFF-DEVICE Wine\n",failures);return failures?1:0;
}
