// SPDX-License-Identifier: GPL-3.0-or-later
// Guest named pipes: a bare \\.\pipe\<name> lives under \\.\pipe\LOCAL\ (the
// AppContainer namespace) for the creator and its clients alike.
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
    using kernel32_core::local_pipe;
    CHECK(local_pipe(L"\\\\.\\pipe\\crashpad_1_ABC")==L"\\\\.\\pipe\\LOCAL\\crashpad_1_ABC");
    CHECK(local_pipe(L"\\\\.\\PIPE\\x")==L"\\\\.\\pipe\\LOCAL\\x");
    CHECK(local_pipe(L"\\\\.\\pipe\\LOCAL\\x").empty()&&local_pipe(L"\\\\.\\pipe\\local\\x").empty());
    CHECK(local_pipe(L"\\\\.\\pipe\\").empty()&&local_pipe(L"C:\\pipe\\x").empty()&&local_pipe(nullptr).empty());
    // The extended form names the same object; both Discord SDKs build it for
    // discord-ipc-N, so a guest's connect lands on the LOCAL\ namespace too.
    CHECK(local_pipe(L"\\\\?\\pipe\\discord-ipc-0")==L"\\\\.\\pipe\\LOCAL\\discord-ipc-0");
    CHECK(local_pipe(L"\\\\?\\PIPE\\discord-ipc-9")==L"\\\\.\\pipe\\LOCAL\\discord-ipc-9");
    CHECK(local_pipe(L"\\\\?\\pipe\\LOCAL\\x").empty()&&local_pipe(L"\\\\?\\pipe\\").empty());
    CHECK(kernel32_core::resolve("api-ms-win-core-namedpipe-l1-2-2.dll","CreateNamedPipeW")==kernel32_core::resolve("kernel32.dll","CreateNamedPipeW"));
    wchar_t unique[64];std::swprintf(unique,64,L"\\\\.\\pipe\\xbn-pipe-test-%lu",GetCurrentProcessId());
    // Created and opened under the bare name; both land on the LOCAL\ pipe.
    HANDLE server=API(CreateNamedPipeW)(unique,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT,1,512,512,0,nullptr);
    CHECK(server!=INVALID_HANDLE_VALUE);
    CHECK(API(WaitNamedPipeW)(unique,1000));
    HANDLE client=API(CreateFileW)(unique,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(client!=INVALID_HANDLE_VALUE);
    std::wstring local=local_pipe(unique);
    HANDLE direct=CreateFileW(local.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(direct==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_PIPE_BUSY);   // one instance, already taken: it is that pipe
    DWORD count=0;char bytes[8]{};
    CHECK(API(WriteFile)(client,"ping",4,&count,nullptr)&&count==4);
    CHECK(API(ReadFile)(server,bytes,sizeof bytes,&count,nullptr)&&count==4&&!memcmp(bytes,"ping",4));
    CHECK(CloseHandle(client)&&CloseHandle(server));
    std::string ansi(unique,unique+wcslen(unique));ansi+="-a";
    server=API(CreateNamedPipeA)(ansi.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_WAIT,1,64,64,0,nullptr);
    CHECK(server!=INVALID_HANDLE_VALUE);CHECK(API(WaitNamedPipeA)(ansi.c_str(),1000));
    client=API(CreateFileA)(ansi.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(client!=INVALID_HANDLE_VALUE);CHECK(CloseHandle(client)&&CloseHandle(server));
    std::printf("kernel32 pipes: PASS (%u checks; off-device, LOCAL\\ pipe namespace)\n",checks);
}
