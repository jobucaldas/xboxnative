// Header ABI and safe fallback semantics; OFF-DEVICE, never game code.
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include "../entry_kernel.h"
#include "../../../shared/kernel_tail_test.h"
#define ABI(impl,api) static_assert(std::is_same<decltype(&entry_kernel::impl),decltype(&api)>::value,#api " ABI")
ABI(alloc,VirtualAlloc); ABI(protect,VirtualProtect); ABI(process,CreateProcessW);
int main() {
 unsigned failures=0;
 #define CHECK(x) do{if(!(x)){printf("FAIL %s\n",#x);++failures;}}while(0)
 PROCESS_INFORMATION p;memset(&p,0xff,sizeof p);
 CHECK(!entry_kernel::process(nullptr,nullptr,nullptr,nullptr,FALSE,0,nullptr,nullptr,nullptr,&p));
 CHECK(GetLastError()==ERROR_ACCESS_DENIED);CHECK(!p.hProcess&&!p.hThread&&!p.dwProcessId&&!p.dwThreadId);
 failures += kernel_tail::test([](const char* line){puts(line);});
 CHECK(!entry_kernel::alloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
 CHECK(GetLastError()==ERROR_ACCESS_DENIED);
 printf("ENTRY-KERNEL-TEST failures=%u (OFF-DEVICE)\n",failures);return failures?1:0;
}
