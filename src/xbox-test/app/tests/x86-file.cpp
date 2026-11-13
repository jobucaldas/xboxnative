// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(FILE_DECLARATIONS_ONLY)
#include <windows.h>
#include <fileapi.h>
#include <timezoneapi.h>
#include <type_traits>
using CreateFileAFn = HANDLE (WINAPI*)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using CreateFileWFn = HANDLE (WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using ReadFileFn = BOOL (WINAPI*)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
using GetFileSizeFn = DWORD (WINAPI*)(HANDLE,LPDWORD);
using GetFileTimeFn = BOOL (WINAPI*)(HANDLE,LPFILETIME,LPFILETIME,LPFILETIME);
using GetFileAttributesAFn = DWORD (WINAPI*)(LPCSTR);
using SetFilePointerFn = DWORD (WINAPI*)(HANDLE,LONG,PLONG,DWORD);
using SetEndOfFileFn = BOOL (WINAPI*)(HANDLE);
using FileTimeConvertFn = BOOL (WINAPI*)(const FILETIME*,LPSYSTEMTIME);
static_assert(std::is_same<decltype(&CreateFileA),CreateFileAFn>::value);
static_assert(std::is_same<decltype(&CreateFileW),CreateFileWFn>::value);
static_assert(std::is_same<decltype(&ReadFile),ReadFileFn>::value);
static_assert(std::is_same<decltype(&GetFileSize),GetFileSizeFn>::value);
static_assert(std::is_same<decltype(&GetFileTime),GetFileTimeFn>::value);
static_assert(std::is_same<decltype(&GetFileAttributesA),GetFileAttributesAFn>::value);
static_assert(std::is_same<decltype(&SetFilePointer),SetFilePointerFn>::value);
static_assert(std::is_same<decltype(&SetEndOfFile),SetEndOfFileFn>::value);
static_assert(std::is_same<decltype(&FileTimeToSystemTime),FileTimeConvertFn>::value);
static_assert(std::is_same<decltype(&FileTimeToLocalFileTime),BOOL (WINAPI*)(const FILETIME*,LPFILETIME)>::value);
#else
#include <cstdio>
#include <cstring>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;
    const char* names[]={"createfilea","createfilew","readfile","getfilesize","getfiletime","getfileattributesa","setfilepointer","setendoffile","filetimetosystemtime","filetimetolocalfiletime","closehandle"};
    const unsigned arity[]={7,7,5,2,4,1,4,1,2,2,1};
    for(unsigned i=0;i<11;++i){auto it=reg.map.find({"kernel32.dll",names[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==arity[i]);}}
    uint64_t native_handle=0x123456789ull;unsigned calls=0;
    host.file_call=[&](Host::FileCall& q){++calls;switch(q.kind){
    case Host::FileCall::Kind::CreateW:CHECK(q.path_w==u"save.dat");CHECK(q.args[0]==0x80000000u);q.value=native_handle;q.succeeded=true;break;
    case Host::FileCall::Kind::Read:q.data={'o','k'};q.args[1]=2;q.succeeded=true;break;
    case Host::FileCall::Kind::Size:q.value=0xfffffff0u;q.args[1]=3;q.succeeded=true;break;
    case Host::FileCall::Kind::Time:q.data.assign(24,0x5a);q.succeeded=true;break;
    case Host::FileCall::Kind::AttributesA:q.value=0x20;q.succeeded=true;break;
    case Host::FileCall::Kind::Seek:q.value=8;q.args[1]=1;q.succeeded=true;break;
    case Host::FileCall::Kind::End:q.succeeded=true;break;
    case Host::FileCall::Kind::TimeConvert:q.data.assign(16,0x36);q.succeeded=true;break;
    case Host::FileCall::Kind::TimeLocal:q.data.assign(8,0x47);q.succeeded=true;break;
    case Host::FileCall::Kind::Close:CHECK(q.handle==native_handle);q.succeeded=true;break;
    case Host::FileCall::Kind::CreateA:break;
    }};
    auto call=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};
    uint32_t pathw=g.put_wstr(u"save.dat"),patha=g.put_str("save.dat");
    uint32_t h=call("createfilew",{pathw,0x80000000u,3,0,3,0,0});CHECK(h!=0xffffffffu&&g.file_handles[h]==native_handle);
    CHECK(call("getfileattributesa",{patha})==0x20);
    CHECK(call("getfilesize",{h,0x3000})==0xfffffff0u&&mem.r32(0x3000)==3);
    CHECK(call("readfile",{h,0x3100,5,0x3110,0})==1&&mem.r32(0x3110)==2&&mem.r8(0x3100)=='o'&&mem.r8(0x3101)=='k');
    CHECK(call("getfiletime",{h,0x3120,0,0x3130})==1&&mem.r8(0x3120)==0x5a&&mem.r8(0x3130)==0x5a);
    CHECK(call("setfilepointer",{h,4,0x3140,0})==8&&mem.r32(0x3140)==1);
    CHECK(call("setendoffile",{h})==1);
    mem.write_bytes(0x3150,"12345678",8);CHECK(call("filetimetosystemtime",{0x3150,0x3160})==1&&mem.r8(0x3160)==0x36);
    CHECK(call("filetimetolocalfiletime",{0x3150,0x3170})==1&&mem.r8(0x3170)==0x47);
    CHECK(call("closehandle",{h})==1&&g.file_handles.count(h)==0);
    CHECK(call("getfilesize",{h,0})==0xffffffffu&&g.last_error()==6);
    CHECK(calls==10);
    std::printf("x86-file: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
