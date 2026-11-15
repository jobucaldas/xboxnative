// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(FILE_DECLARATIONS_ONLY)
#include <windows.h>
#include <fileapi.h>
#include <timezoneapi.h>
#include <objbase.h>
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
using FindFirstFileAFn = HANDLE (WINAPI*)(LPCSTR,LPWIN32_FIND_DATAA);
using FindFirstFileWFn = HANDLE (WINAPI*)(LPCWSTR,LPWIN32_FIND_DATAW);
using FindFirstFileExAFn = HANDLE (WINAPI*)(LPCSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);
using FindFirstFileExWFn = HANDLE (WINAPI*)(LPCWSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);
using FindNextFileAFn = BOOL (WINAPI*)(HANDLE,LPWIN32_FIND_DATAA);
using FindNextFileWFn = BOOL (WINAPI*)(HANDLE,LPWIN32_FIND_DATAW);
static_assert(sizeof(WIN32_FIND_DATAA)==320 && sizeof(WIN32_FIND_DATAW)==592);
static_assert(std::is_same<decltype(&FindFirstFileA),FindFirstFileAFn>::value);
static_assert(std::is_same<decltype(&FindFirstFileW),FindFirstFileWFn>::value);
static_assert(std::is_same<decltype(&FindFirstFileExA),FindFirstFileExAFn>::value);
static_assert(std::is_same<decltype(&FindFirstFileExW),FindFirstFileExWFn>::value);
static_assert(std::is_same<decltype(&FindNextFileA),FindNextFileAFn>::value);
static_assert(std::is_same<decltype(&FindNextFileW),FindNextFileWFn>::value);
static_assert(std::is_same<decltype(&FindClose),BOOL (WINAPI*)(HANDLE)>::value);
static_assert(std::is_same<decltype(&CreateDirectoryA),BOOL (WINAPI*)(LPCSTR,LPSECURITY_ATTRIBUTES)>::value);
static_assert(std::is_same<decltype(&CreateDirectoryW),BOOL (WINAPI*)(LPCWSTR,LPSECURITY_ATTRIBUTES)>::value);
static_assert(std::is_same<decltype(&RemoveDirectoryA),BOOL (WINAPI*)(LPCSTR)>::value);
static_assert(std::is_same<decltype(&RemoveDirectoryW),BOOL (WINAPI*)(LPCWSTR)>::value);
static_assert(std::is_same<decltype(&CoInitialize),HRESULT (WINAPI*)(LPVOID)>::value);
static_assert(std::is_same<decltype(&CoInitializeEx),HRESULT (WINAPI*)(LPVOID,DWORD)>::value);
static_assert(std::is_same<decltype(&CoUninitialize),void (WINAPI*)()>::value);
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
    const char* names[]={"createfilea","createfilew","readfile","getfilesize","getfiletime","getfileattributesa","setfilepointer","setfilepointerex","setendoffile","filetimetosystemtime","filetimetolocalfiletime","getfiletype","closehandle","createdirectorya","createdirectoryw","removedirectorya","removedirectoryw","getcurrentdirectorya","getcurrentdirectoryw","setcurrentdirectorya","setcurrentdirectoryw","findfirstfilea","findfirstfilew","findfirstfileexa","findfirstfileexw","findnextfilea","findnextfilew","findclose","coinitialize","coinitializeex","couninitialize"};
    const unsigned arity[]={7,7,5,2,4,1,4,5,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,6,6,2,2,1,1,2,0};   // SetFilePointerEx: LARGE_INTEGER by value = 2 slots
    for(unsigned i=0;i<31;++i){auto it=reg.map.find({i>=28?"ole32.dll":"kernel32.dll",names[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==arity[i]);}}
    uint64_t native_handle=0x123456789ull;unsigned calls=0;
    host.file_call=[&](Host::FileCall& q){++calls;switch(q.kind){
    case Host::FileCall::Kind::CreateW:CHECK(q.path_w==u"save.dat");CHECK(q.args[0]==0x80000000u);q.value=native_handle;q.succeeded=true;break;
    case Host::FileCall::Kind::Read:q.data={'o','k'};q.args[1]=2;q.succeeded=true;break;
    case Host::FileCall::Kind::Size:q.value=0xfffffff0u;q.args[1]=3;q.succeeded=true;break;
    case Host::FileCall::Kind::Time:q.data.assign(24,0x5a);q.succeeded=true;break;
    case Host::FileCall::Kind::AttributesA:if(q.operation==Host::FileCall::Operation::CreateDirectoryA){CHECK(q.path_a=="folder");q.succeeded=true;}else{q.value=0x20;q.succeeded=true;}break;
    case Host::FileCall::Kind::Seek:q.value=8;q.args[1]=1;q.succeeded=true;break;
    case Host::FileCall::Kind::SeekEx:CHECK(q.handle==native_handle);CHECK(uint64_t(q.distance)==0x8877665544332211ull);if(q.method!=2){q.error=87;break;}q.position=int64_t(0x123456789abcdef0ull);q.succeeded=true;break;
    case Host::FileCall::Kind::End:q.succeeded=true;break;
    case Host::FileCall::Kind::TimeConvert:q.data.assign(16,0x36);q.succeeded=true;break;
    case Host::FileCall::Kind::TimeLocal:q.data.assign(8,0x47);q.succeeded=true;break;
    case Host::FileCall::Kind::Close:CHECK(q.handle==native_handle);q.succeeded=true;break;
    case Host::FileCall::Kind::CreateA:CHECK(q.path_a=="save.dat");q.value=native_handle+1;q.error=183;q.succeeded=true;break;
    case Host::FileCall::Kind::CreateDirectoryW:CHECK(q.path_w==u"folder");q.succeeded=true;break;
    case Host::FileCall::Kind::RemoveDirectoryA:CHECK(q.path_a=="folder");q.succeeded=true;break;
    case Host::FileCall::Kind::RemoveDirectoryW:CHECK(q.path_w==u"folder");q.succeeded=true;break;
    case Host::FileCall::Kind::GetCurrentDirectoryA:q.data.assign({'r','o','o','t',0});q.value=4;q.succeeded=true;break;
    case Host::FileCall::Kind::GetCurrentDirectoryW:q.data.assign(10,0);q.data[0]='r';q.data[2]='o';q.data[4]='o';q.data[6]='t';q.value=4;q.succeeded=true;break;
    case Host::FileCall::Kind::SetCurrentDirectoryA:CHECK(q.path_a=="folder");q.succeeded=true;break;
    case Host::FileCall::Kind::SetCurrentDirectoryW:CHECK(q.path_w==u"folder");q.succeeded=true;break;
    case Host::FileCall::Kind::FindFirstA:case Host::FileCall::Kind::FindFirstExA:if(q.path_a=="missing"){q.error=2;break;}q.value=native_handle+2;q.data.assign(320,0x41);q.succeeded=true;break;
    case Host::FileCall::Kind::FindFirstW:case Host::FileCall::Kind::FindFirstExW:if(q.path_w==u"missing"){q.error=2;break;}q.value=native_handle+3;q.data.assign(592,0x57);q.succeeded=true;break;
    case Host::FileCall::Kind::FindNextA:case Host::FileCall::Kind::FindNextW:q.error=18;break;
    case Host::FileCall::Kind::FindClose:CHECK(q.handle==native_handle+2||q.handle==native_handle+3);q.succeeded=true;break;
    }};
    auto call=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({std::string(n).rfind("co",0)==0?"ole32.dll":"kernel32.dll",n}).fn(cpu);};
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
    uint32_t foldera=g.put_str("folder"),folderw=g.put_wstr(u"folder"),patterna=g.put_str("*.dat"),patternw=g.put_wstr(u"*.dat");
    CHECK(call("createdirectorya",{foldera,0})==1);
    CHECK(call("createdirectoryw",{folderw,0})==1);
    CHECK(call("removedirectorya",{foldera})==1&&call("removedirectoryw",{folderw})==1);
    CHECK(call("setcurrentdirectorya",{foldera})==1&&call("setcurrentdirectoryw",{folderw})==1);
    CHECK(call("getcurrentdirectorya",{16,0x3200})==4&&mem.r8(0x3200)=='r'&&mem.r8(0x3204)==0);
    CHECK(call("getcurrentdirectoryw",{16,0x3220})==4&&mem.r8(0x3220)=='r'&&mem.r8(0x3222)=='o'&&mem.r8(0x3228)==0);
    mem.w8(0x3240,0x7b);CHECK(call("getcurrentdirectorya",{3,0x3240})==4&&mem.r8(0x3240)==0x7b);
    uint32_t missinga=g.put_str("missing"),missingw=g.put_wstr(u"missing");
    CHECK(call("findfirstfilea",{missinga,0x3300})==0xffffffffu&&call("getlasterror",{})==2);
    CHECK(call("findfirstfilew",{missingw,0x3500})==0xffffffffu&&call("getlasterror",{})==2);
    uint32_t fa=call("findfirstfilea",{patterna,0x3300}),fw=call("findfirstfilew",{patternw,0x3500});
    CHECK(fa!=0xffffffffu&&fw!=0xffffffffu&&g.find_handles[fa]==native_handle+2&&g.find_handles[fw]==native_handle+3);
    CHECK(mem.r8(0x3300)==0x41&&mem.r8(0x332c)==0x41&&mem.r8(0x3430)==0x41&&mem.r8(0x3500)==0x57&&mem.r8(0x352c)==0x57&&mem.r8(0x3734)==0x57&&mem.r8(0x374f)==0x57);
    uint32_t fea=call("findfirstfileexa",{patterna,1,0x3800,0,0,0}),few=call("findfirstfileexw",{patternw,1,0x3a00,0,0,0});
    CHECK(fea!=0xffffffffu&&few!=0xffffffffu&&mem.r8(0x3800)==0x41&&mem.r8(0x3a00)==0x57);
    CHECK(call("findnextfilea",{fa,0x3300})==0&&call("getlasterror",{})==18);
    CHECK(call("findnextfilew",{fw,0x3500})==0&&call("getlasterror",{})==18);
    CHECK(call("findclose",{fa})==1&&call("findclose",{fw})==1&&call("findclose",{fea})==1&&call("findclose",{few})==1&&call("findclose",{0xdeadbeefu})==0&&call("getlasterror",{})==6);
    mem.w32(g.cur_teb+0x24,55);
    CHECK(call("coinitialize",{0})==0&&call("coinitialize",{0})==1);
    CHECK(call("coinitializeex",{0,0})==0x80010106u&&call("coinitializeex",{0,0x40})==0x80070057u);
    call("couninitialize",{});CHECK(g.com_apartments[55].second==1);call("couninitialize",{});CHECK(g.com_apartments.count(55)==0);
    CHECK(call("coinitializeex",{0,0})==0);call("couninitialize",{});CHECK(g.com_apartments.count(55)==0);
    std::printf("x86-file: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
