// Off-device only: lifecycle smoke using the same mapper as the UWP host.
#include <windows.h>
#include <cstdio>
#include <cstdint>
// Wine 10 lacks VirtualProtectFromApp. Desktop equivalents test mapping logic
// only; Xbox must separately validate the actual FromApp API and W^X policy.
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../wxl_mapper.h"
static bool attached, detached, unwind;
static void log_line(const char* s) {
    puts(s);
    if (!strcmp(s,"LIFECYCLE unwind lookup ok")) unwind=true;
    if (!strcmp(s,"LIFECYCLE attach")) attached=true;
    if (!strcmp(s,"LIFECYCLE detach")) detached=true;
}
int main(int argc, char** argv) {
    if (argc!=2) return 2;
    wchar_t path[1024];
    if (!MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path,1024)) return 2;
    bool ok=wxl::run(path,log_line);
    if (!ok || !attached || !detached || !unwind) return 1;
    puts("LIFECYCLE-OK");
}
