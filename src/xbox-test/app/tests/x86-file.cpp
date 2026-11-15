// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(FILE_DECLARATIONS_ONLY)
#include <windows.h>
#include <fileapi.h>
#include <timezoneapi.h>
#include <type_traits>
#include <cstddef>
using CreateFileAFn = HANDLE (WINAPI*)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using CreateFileWFn = HANDLE (WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using ReadFileFn = BOOL (WINAPI*)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
using GetFileSizeFn = DWORD (WINAPI*)(HANDLE,LPDWORD);
using GetFileTimeFn = BOOL (WINAPI*)(HANDLE,LPFILETIME,LPFILETIME,LPFILETIME);
using GetFileAttributesAFn = DWORD (WINAPI*)(LPCSTR);
using SetFilePointerFn = DWORD (WINAPI*)(HANDLE,LONG,PLONG,DWORD);
using SetFilePointerExFn = BOOL (WINAPI*)(HANDLE,LARGE_INTEGER,PLARGE_INTEGER,DWORD);
using SetEndOfFileFn = BOOL (WINAPI*)(HANDLE);
using FileTimeConvertFn = BOOL (WINAPI*)(const FILETIME*,LPSYSTEMTIME);
static_assert(std::is_same<decltype(&CreateFileA),CreateFileAFn>::value);
static_assert(std::is_same<decltype(&CreateFileW),CreateFileWFn>::value);
static_assert(std::is_same<decltype(&ReadFile),ReadFileFn>::value);
static_assert(std::is_same<decltype(&GetFileSize),GetFileSizeFn>::value);
static_assert(std::is_same<decltype(&GetFileTime),GetFileTimeFn>::value);
static_assert(std::is_same<decltype(&GetFileAttributesA),GetFileAttributesAFn>::value);
static_assert(std::is_same<decltype(&SetFilePointer),SetFilePointerFn>::value);
static_assert(std::is_same<decltype(&SetFilePointerEx),SetFilePointerExFn>::value);
static_assert(sizeof(LARGE_INTEGER)==8 && offsetof(LARGE_INTEGER,QuadPart)==0);
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
    const char* names[]={"createfilea","createfilew","readfile","getfilesize","getfiletime","getfileattributesa","setfilepointer","setfilepointerex","setendoffile","filetimetosystemtime","filetimetolocalfiletime","getfiletype","closehandle"};
    const unsigned arity[]={7,7,5,2,4,1,4,5,1,2,2,1,1};   // SetFilePointerEx: LARGE_INTEGER by value = 2 slots
    for(unsigned i=0;i<13;++i){auto it=reg.map.find({"kernel32.dll",names[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==arity[i]);}}
    uint64_t native_handle=0x123456789ull;unsigned calls=0;
    host.file_call=[&](Host::FileCall& q){++calls;switch(q.kind){
    case Host::FileCall::Kind::CreateW:CHECK(q.path_w==u"save.dat");CHECK(q.args[0]==0x80000000u);q.value=native_handle;q.succeeded=true;break;
    case Host::FileCall::Kind::Read:q.data={'o','k'};q.args[1]=2;q.succeeded=true;break;
    case Host::FileCall::Kind::Size:q.value=0xfffffff0u;q.args[1]=3;q.succeeded=true;break;
    case Host::FileCall::Kind::Time:q.data.assign(24,0x5a);q.succeeded=true;break;
    case Host::FileCall::Kind::AttributesA:q.value=0x20;q.succeeded=true;break;
    case Host::FileCall::Kind::Seek:q.value=8;q.args[1]=1;q.succeeded=true;break;
    case Host::FileCall::Kind::SeekEx:CHECK(q.handle==native_handle);CHECK(uint64_t(q.distance)==0x8877665544332211ull);if(q.method!=2){q.error=87;break;}q.position=int64_t(0x123456789abcdef0ull);q.succeeded=true;break;
    case Host::FileCall::Kind::End:q.succeeded=true;break;
    case Host::FileCall::Kind::TimeConvert:q.data.assign(16,0x36);q.succeeded=true;break;
    case Host::FileCall::Kind::TimeLocal:q.data.assign(8,0x47);q.succeeded=true;break;
    case Host::FileCall::Kind::Close:CHECK(q.handle==native_handle);q.succeeded=true;break;
    case Host::FileCall::Kind::CreateA:CHECK(q.path_a=="save.dat");q.value=native_handle+1;q.error=183;q.succeeded=true;break;
    }};
    auto call=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};
    uint32_t pathw=g.put_wstr(u"save.dat"),patha=g.put_str("save.dat");
    g.set_last_error(5);uint32_t h=call("createfilew",{pathw,0x80000000u,3,0,3,0,0});CHECK(h!=0xffffffffu&&g.file_handles[h]==native_handle);CHECK(call("getlasterror",{})==0);
    CHECK(call("getfilesize",{0xdeadbeefu,0})==0xffffffffu&&call("getlasterror",{})==6);
    CHECK(call("getfileattributesa",{patha})==0x20);
    CHECK(call("getfilesize",{h,0x3000})==0xfffffff0u&&mem.r32(0x3000)==3&&call("getlasterror",{})==0);
    CHECK(call("readfile",{h,0x3100,5,0x3110,0})==1&&mem.r32(0x3110)==2&&mem.r8(0x3100)=='o'&&mem.r8(0x3101)=='k');
    CHECK(call("getfiletime",{h,0x3120,0,0x3130})==1&&mem.r8(0x3120)==0x5a&&mem.r8(0x3130)==0x5a);
    CHECK(call("setfilepointer",{0xdeadbeefu,4,0x3140,0})==0xffffffffu&&call("getlasterror",{})==6);
    CHECK(call("setfilepointer",{h,4,0x3140,0})==8&&mem.r32(0x3140)==1&&call("getlasterror",{})==0);
    CHECK(call("setfilepointerex",{0xdeadbeefu,0x44332211u,0x88776655u,0x3190,2})==0&&call("getlasterror",{})==6);
    g.set_last_error(9);
    CHECK(call("setfilepointerex",{h,0x44332211u,0x88776655u,0x3190,2})==1&&mem.r32(0x3190)==0x9abcdef0u&&mem.r32(0x3194)==0x12345678u&&g.last_error()==9);
    CHECK(call("setfilepointerex",{h,0x44332211u,0x88776655u,0x3190,99})==0&&g.last_error()==87);
    CHECK(call("setendoffile",{h})==1);
    mem.write_bytes(0x3150,"12345678",8);CHECK(call("filetimetosystemtime",{0x3150,0x3160})==1&&mem.r8(0x3160)==0x36);
    CHECK(call("filetimetolocalfiletime",{0x3150,0x3170})==1&&mem.r8(0x3170)==0x47);
    CHECK(call("closehandle",{h})==1&&g.file_handles.count(h)==0);
    CHECK(call("getfilesize",{h,0})==0xffffffffu&&g.last_error()==6);
    uint32_t ha=call("createfilea",{patha,0x80000000u,3,0,3,0,0});CHECK(ha!=0xffffffffu&&call("getlasterror",{})==183);
    CHECK(call("getfiletype",{ha})==1);
    CHECK(call("getfiletype",{call("getstdhandle",{uint32_t(-10)})})==2);
    CHECK(call("getfiletype",{call("getstdhandle",{uint32_t(-11)})})==2);
    CHECK(call("getfiletype",{call("getstdhandle",{uint32_t(-12)})})==2);
    CHECK(call("getfiletype",{0xdeadbeefu})==0&&call("getlasterror",{})==6);
    CHECK(calls==13);
    std::printf("x86-file: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
