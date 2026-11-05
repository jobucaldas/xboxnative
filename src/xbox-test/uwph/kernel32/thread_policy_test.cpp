#include "thread_query_route.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    auto address=kernel32_core::resolve_thread_query("kernel32.dll","SetThreadAffinityMask");CHECK(address);
    CHECK(address==kernel32_core::resolve_thread_query("api-ms-win-core-processthreads-l1-1-0.dll","SetThreadAffinityMask"));
    auto affinity=reinterpret_cast<decltype(&::SetThreadAffinityMask)>(address);
    SetLastError(73);CHECK(!affinity(GetCurrentThread(),1));CHECK(GetLastError()==ERROR_NOT_SUPPORTED);
    SetLastError(73);CHECK(!affinity(nullptr,1));CHECK(GetLastError()==ERROR_NOT_SUPPORTED);
    CHECK(!kernel32_core::resolve_thread_query("user32.dll","SetThreadAffinityMask"));
    std::printf("kernel32 UWP thread policy: PASS (%u checks; unsupported affinity fails explicitly, off-device)\n",checks);
    return 0;
}
