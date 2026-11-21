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
    // Launches always install a destructive-file policy; a missing one fails closed (checked below).
    bool refuse=false,write_ok=false;
    std::string last_guard_op; std::u16string last_guard_path;
    host.destructive_file_guard=[&](const char* op,const std::u16string& p){last_guard_op=op?op:"";last_guard_path=p;return !refuse;};
    std::u16string last_createw_path,last_deleted_w; uint32_t last_createw_access=0,last_createw_share=0; unsigned close_count=0,createw_count=0;
    std::string mock_cwd="root";
    std::vector<std::vector<uint8_t>> read_chunks; size_t read_idx=0;
    std::vector<uint8_t> written_bytes;
    host.file_call=[&](Host::FileCall& q){++calls;switch(q.kind){
    case Host::FileCall::Kind::CreateW:
        ++createw_count;last_createw_path=q.path_w;last_createw_access=q.args[0];last_createw_share=q.args[1];
        if(q.path_w==u"root\\out.dat"||q.path_w==u"out.dat"){q.value=native_handle+1;q.succeeded=true;}
        else if(q.path_w==u"root\\c\u20ac.dat"||q.path_w==u"c\u20ac.dat"){q.value=native_handle+1;q.succeeded=true;}
        else{CHECK(q.path_w==u"save.dat"||q.path_w==u"root\\save.dat");if(calls==1)CHECK(q.args[0]==0x80000000u);q.value=native_handle;q.succeeded=true;}
        break;
    case Host::FileCall::Kind::Read:
        if(!read_chunks.empty()){
            if(read_idx<read_chunks.size()){q.data=read_chunks[read_idx++];q.args[1]=uint32_t(q.data.size());}
            else{q.data.clear();q.args[1]=0;}
            q.succeeded=true;
        }else{q.data={'o','k'};q.args[1]=2;q.succeeded=true;}
        break;
    case Host::FileCall::Kind::Write:if(!write_ok){q.error=120;break;}CHECK(q.handle==native_handle+1);written_bytes.insert(written_bytes.end(),q.data.begin(),q.data.end());q.args[0]=uint32_t(q.data.size());q.succeeded=true;break;
    case Host::FileCall::Kind::Size:q.value=0xfffffff0u;q.args[1]=3;q.succeeded=true;break;
    case Host::FileCall::Kind::Time:q.data.assign(24,0x5a);q.succeeded=true;break;
    case Host::FileCall::Kind::AttributesA:if(q.operation==Host::FileCall::Operation::CreateDirectoryA){CHECK(q.path_a=="folder");q.succeeded=true;}else if(q.operation==Host::FileCall::Operation::DeleteW){last_deleted_w=q.path_w;q.succeeded=true;}else if(q.path_a=="missing"){q.error=2;}else{q.value=q.path_a=="directory"?0x10:0x20;q.succeeded=true;if(q.args[1])q.data.assign(36,0x5b);}break;
    case Host::FileCall::Kind::Seek:q.value=8;q.args[1]=1;q.succeeded=true;break;
    case Host::FileCall::Kind::SeekEx:CHECK(q.handle==native_handle);CHECK(uint64_t(q.distance)==0x8877665544332211ull);if(q.method!=2){q.error=87;break;}q.position=int64_t(0x123456789abcdef0ull);q.succeeded=true;break;
    case Host::FileCall::Kind::End:q.succeeded=true;break;
    case Host::FileCall::Kind::TimeConvert:q.data.assign(16,0x36);q.succeeded=true;break;
    case Host::FileCall::Kind::TimeLocal:q.data.assign(8,0x47);q.succeeded=true;break;
    case Host::FileCall::Kind::Close:CHECK(q.handle==native_handle||q.handle==native_handle+1);++close_count;q.succeeded=true;break;
    case Host::FileCall::Kind::CreateA:CHECK(q.path_a=="save.dat");q.value=native_handle+1;q.error=183;q.succeeded=true;break;
    case Host::FileCall::Kind::CreateDirectoryW:CHECK(q.path_w==u"folder");q.succeeded=true;break;
    case Host::FileCall::Kind::RemoveDirectoryA:CHECK(q.path_a=="folder");q.succeeded=true;break;
    case Host::FileCall::Kind::RemoveDirectoryW:CHECK(q.path_w==u"folder");q.succeeded=true;break;
    case Host::FileCall::Kind::GetCurrentDirectoryA:q.data.assign(mock_cwd.begin(),mock_cwd.end());q.data.push_back(0);q.value=uint32_t(mock_cwd.size());q.succeeded=true;break;
    case Host::FileCall::Kind::GetCurrentDirectoryW:q.data.assign(10,0);q.data[0]='r';q.data[2]='o';q.data[4]='o';q.data[6]='t';q.value=4;q.succeeded=true;break;
    case Host::FileCall::Kind::SetCurrentDirectoryA:CHECK(q.path_a=="folder");q.succeeded=true;break;
    case Host::FileCall::Kind::SetCurrentDirectoryW:CHECK(q.path_w==u"folder");q.succeeded=true;break;
    case Host::FileCall::Kind::FindFirstA:case Host::FileCall::Kind::FindFirstExA:if(q.path_a=="missing"){q.error=2;break;}q.value=native_handle+2;q.data.assign(320,0x41);q.succeeded=true;break;
    case Host::FileCall::Kind::FindFirstW:case Host::FileCall::Kind::FindFirstExW:if(q.path_w==u"missing"){q.error=2;break;}q.value=native_handle+3;q.data.assign(592,0x57);q.succeeded=true;break;
    case Host::FileCall::Kind::FindNextA:case Host::FileCall::Kind::FindNextW:q.error=18;break;
    case Host::FileCall::Kind::FindClose:CHECK(q.handle==native_handle+2||q.handle==native_handle+3);q.succeeded=true;break;
    case Host::FileCall::Kind::SetTime:CHECK(q.handle==native_handle);CHECK(q.args[0]==1&&q.args[1]==0&&q.args[2]==1);CHECK(q.data.size()==24);q.succeeded=true;break;
    case Host::FileCall::Kind::SetAttributesW:CHECK(q.path_w==u"attr.dat");CHECK(q.args[0]==0x20);q.succeeded=true;break;
    case Host::FileCall::Kind::Copy:if(q.path_a=="missing"||q.path_w==u"missing"){q.error=2;break;}if(q.operation==Host::FileCall::Operation::CopyFileA){CHECK(q.path_a=="src.dat"&&q.path2_a=="dst.dat"&&q.args[0]==0);}else{CHECK(q.path_w==u"src.dat"&&q.path2_w==u"dst.dat"&&q.args[0]==1);}q.succeeded=true;break;
    case Host::FileCall::Kind::Move:if(q.path_a=="missing"||q.path_w==u"missing"){q.error=2;break;}if(q.operation==Host::FileCall::Operation::MoveFileA){CHECK(q.path_a=="src.dat"&&q.path2_a=="dst.dat");}else if(q.operation==Host::FileCall::Operation::MoveFileExA){CHECK(q.path_a=="src.dat"&&q.path2_a=="dst.dat"&&q.args[0]==1);}else{CHECK(q.path_w==u"src.dat"&&q.path2_w==u"dst.dat"&&q.args[0]==0);}q.succeeded=true;break;
    case Host::FileCall::Kind::Lock:case Host::FileCall::Kind::Unlock:CHECK(q.handle==native_handle);CHECK(q.args[0]==1&&q.args[1]==2&&q.args[2]==3&&q.args[3]==4);q.succeeded=true;break;
    case Host::FileCall::Kind::TempPathW:if(q.args[0]>4){q.data.resize(8);memcpy(q.data.data(),u"tmp\\",8);q.value=4;}else q.value=5;q.succeeded=true;break;
    case Host::FileCall::Kind::TempFileNameW:{
        CHECK(q.path2_w.size()<=3&&q.args[0]==0);
        const std::u16string name=q.path_w+q.path2_w+u"1234.tmp";
        q.data.resize(name.size()*2);memcpy(q.data.data(),name.data(),name.size()*2);
        q.value=0x1234;q.succeeded=true;break;}
    case Host::FileCall::Kind::DiskFreeW:if(q.path_w==u"Z:\\"){q.error=21;break;}CHECK(q.path_w==u"C:\\");q.args[0]=8;q.args[1]=512;q.args[2]=100;q.args[3]=200;q.succeeded=true;break;
    case Host::FileCall::Kind::DiskFreeExA:case Host::FileCall::Kind::DiskFreeExW:{if(q.path_a=="Z:\\"||q.path_w==u"Z:\\"){q.error=21;break;}uint64_t v[3]={1000,2000,3000};q.data.resize(24);memcpy(q.data.data(),v,24);q.succeeded=true;break;}
    case Host::FileCall::Kind::DriveTypeW:if(q.path_w==u"Z:\\"){q.error=21;break;}q.value=3;q.succeeded=true;break;
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
    refuse=true;CHECK(call("removedirectoryw",{folderw})==0&&g.last_error()==5);refuse=false;
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
    cpu.fs_base=g.cur_teb;auto path_dir_a=reg.map.find({"shlwapi.dll","pathisdirectorya"});CHECK(path_dir_a!=reg.map.end());if(path_dir_a!=reg.map.end()){CHECK(path_dir_a->second.conv==Conv::Stdcall&&path_dir_a->second.argc==1);uint32_t directory=g.put_str("directory"),file=g.put_str("file"),missing=g.put_str("missing");mem.w32(cpu.reg[ESP]+4,directory);CHECK(path_dir_a->second.fn(cpu)==1);mem.w32(cpu.reg[ESP]+4,file);CHECK(path_dir_a->second.fn(cpu)==0);mem.w32(cpu.reg[ESP]+4,missing);CHECK(path_dir_a->second.fn(cpu)==0);refuse=true;mem.w32(cpu.reg[ESP]+4,directory);CHECK(path_dir_a->second.fn(cpu)==0&&g.last_error()==5);refuse=false;}
    // Remaining file family: success, guard refusal (destructive paths) and failure paths.
    auto kcall=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};
    const struct{const char* n;unsigned argc;}extra[]={{"getfilesizeex",2},{"setfiletime",4},{"setfileattributesw",2},{"getfileattributesexa",3},{"copyfilea",3},{"copyfilew",3},{"movefilea",2},{"movefileexa",3},{"movefileexw",3},{"lockfile",5},{"unlockfile",5},{"gettemppathw",2},{"gettempfilenamew",4},{"getdiskfreespacew",5},{"getdiskfreespaceexa",4},{"getdiskfreespaceexw",4},{"getdrivetypew",1},{"createhardlinka",3},{"createhardlinkw",3},{"createsymboliclinka",3},{"createsymboliclinkw",3},{"verifyconsoleiohandle",1}};
    for(const auto& e:extra){auto it=reg.map.find({"kernel32.dll",e.n});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall&&it->second.argc==e.argc);}}
    uint32_t hs=call("createfilew",{pathw,0x80000000u,3,0,3,0,0});CHECK(hs!=0xffffffffu);
    CHECK(kcall("getfilesizeex",{hs,0x3c00})==1&&mem.r32(0x3c00)==0xfffffff0u&&mem.r32(0x3c04)==3&&g.last_error()==0);
    CHECK(kcall("getfilesizeex",{0xdeadbeefu,0x3c00})==0&&g.last_error()==6);
    CHECK(kcall("getfilesizeex",{hs,0})==0&&g.last_error()==87);
    mem.write_bytes(0x3c10,"12345678",8);
    CHECK(kcall("setfiletime",{hs,0x3c10,0,0x3c10})==1&&g.last_error()==0);
    CHECK(kcall("setfiletime",{0xdeadbeefu,0,0,0})==0&&g.last_error()==6);
    uint32_t attr_w=g.put_wstr(u"attr.dat");
    CHECK(kcall("setfileattributesw",{attr_w,0x20})==1);
    refuse=true;CHECK(kcall("setfileattributesw",{attr_w,0x20})==0&&g.last_error()==5);refuse=false;
    CHECK(kcall("setfileattributesw",{0,0x20})==0&&g.last_error()==87);
    CHECK(kcall("getfileattributesexa",{patha,0,0x3c20})==1&&mem.r8(0x3c20)==0x5b&&mem.r8(0x3c20+35)==0x5b&&g.last_error()==0);
    CHECK(kcall("getfileattributesexa",{patha,1,0x3c20})==0&&g.last_error()==87);
    CHECK(kcall("getfileattributesexa",{missinga,0,0x3c20})==0&&g.last_error()==2);
    uint32_t src_a=g.put_str("src.dat"),dst_a=g.put_str("dst.dat"),src_w=g.put_wstr(u"src.dat"),dst_w=g.put_wstr(u"dst.dat");
    CHECK(kcall("copyfilea",{src_a,dst_a,0})==1);
    CHECK(kcall("copyfilew",{src_w,dst_w,1})==1);
    CHECK(kcall("copyfilea",{missinga,dst_a,0})==0&&g.last_error()==2);
    CHECK(kcall("copyfilea",{src_a,0,0})==0&&g.last_error()==87);
    refuse=true;CHECK(kcall("copyfilew",{src_w,dst_w,0})==0&&g.last_error()==5);refuse=false;
    CHECK(kcall("movefilea",{src_a,dst_a})==1);
    CHECK(kcall("movefileexa",{src_a,dst_a,1})==1);
    CHECK(kcall("movefileexw",{src_w,dst_w,0})==1);
    CHECK(kcall("movefileexw",{missingw,dst_w,0})==0&&g.last_error()==2);
    refuse=true;CHECK(kcall("movefilea",{src_a,dst_a})==0&&g.last_error()==5);refuse=false;
    CHECK(kcall("lockfile",{hs,1,2,3,4})==1&&g.last_error()==0);
    CHECK(kcall("unlockfile",{hs,1,2,3,4})==1);
    CHECK(kcall("lockfile",{0xdeadbeefu,1,2,3,4})==0&&g.last_error()==6);
    uint32_t tbuf=0x3c40;
    CHECK(kcall("gettemppathw",{16,tbuf})==4&&mem.r8(tbuf)=='t'&&mem.r8(tbuf+2)=='m'&&mem.r8(tbuf+4)=='p'&&mem.r8(tbuf+6)=='\\'&&mem.r8(tbuf+8)==0&&mem.r8(tbuf+9)==0&&g.last_error()==0);
    CHECK(kcall("gettemppathw",{4,tbuf})==5);
    CHECK(kcall("gettemppathw",{16,0})==0&&g.last_error()==87);
    uint32_t tmpw=g.put_wstr(u"tmp\\"),pfxw=g.put_wstr(u"pfx");
    CHECK(kcall("gettempfilenamew",{tmpw,pfxw,0,0x3c80})==0x1234&&mem.r8(0x3c80+28)=='p'&&mem.r8(0x3c80+30)==0&&g.last_error()==0);
    CHECK(last_guard_op=="GetTempFileNameW"&&last_guard_path==u"tmp\\pfx0000.TMP");
    refuse=true;CHECK(kcall("gettempfilenamew",{tmpw,pfxw,0,0x3c80})==0&&g.last_error()==5);refuse=false;
    CHECK(kcall("gettempfilenamew",{tmpw,pfxw,0,0})==0&&g.last_error()==87);
    uint32_t tmpa=g.put_str("tmp\\"),pfxa=g.put_str("pfxlong"),bad_pfxa=g.put_str("..\\x");
    CHECK(kcall("gettempfilenamea",{tmpa,pfxa,0,0x3c80})==0x1234&&mem.r8(0x3c80+14)=='p'&&mem.r8(0x3c80+15)==0&&g.last_error()==0);
    CHECK(last_guard_op=="GetTempFileNameA"&&last_guard_path==u"tmp\\pfx0000.TMP");
    CHECK(kcall("gettempfilenamea",{tmpa,bad_pfxa,0,0x3c80})==0&&g.last_error()==123);
    // CP1252 0x80 (Euro sign U+20AC) decodes and re-encodes consistently in GetTempFileNameA; >=260-byte paths fail instead of truncating.
    uint32_t euro_dir=g.put_str("d\x80\\"),euro_pfx=g.put_str("\x80z");
    CHECK(kcall("gettempfilenamea",{euro_dir,euro_pfx,0,0x3c80})==0x1234&&g.last_error()==0);
    CHECK(last_guard_path==u"d\u20ac\\\u20acz0000.TMP"&&mem.r8(0x3c80+1)==0x80&&mem.r8(0x3c80+3)==0x80);
    uint32_t long_dir=g.put_str(std::string(255,'a')+"\\"); last_deleted_w.clear();
    CHECK(kcall("gettempfilenamea",{long_dir,pfxa,0,0x3c80})==0&&g.last_error()==111&&!last_deleted_w.empty());
    refuse=true;CHECK(kcall("gettempfilenamea",{tmpa,pfxa,0,0x3c80})==0&&g.last_error()==5);refuse=false;
    // Legacy OpenFile/LZOpenFileA: resolves relative paths against cwd, single CP_ACP decode shared by guard and CreateW, OF_PARSE/OF_EXIST/OF_DELETE/OF_REOPEN/OF_CREATE/share flags.
    uint32_t save_a=g.put_str("save.dat"),out_a=g.put_str("out.dat"),euro_a=g.put_str("c\x80.dat"),ofs=0x3d00;
    const uint32_t h1=kcall("openfile",{save_a,ofs,0x10u});  // OF_READ | OF_SHARE_EXCLUSIVE
    CHECK(h1!=0xffffffffu&&(h1&0xf0000000u)==0x50000000u&&last_createw_path==u"root\\save.dat"&&last_createw_share==0u&&mem.r8(ofs)==136&&mem.r8(ofs+1)==1&&mem.r8(ofs+8)=='r'&&mem.r8(ofs+21)==0);
    refuse=true;CHECK(kcall("openfile",{out_a,ofs,1})==0xffffffffu&&g.last_error()==5);refuse=false;
    // OF_CREATE alone requests GENERIC_READ|GENERIC_WRITE (0xC0000000).
    const uint32_t hcreate=kcall("openfile",{out_a,ofs,0x1000u});
    CHECK(hcreate!=0xffffffffu&&last_createw_access==0xC0000000u);
    CHECK(kcall("_lclose",{hcreate})==0);
    // CP1252 parity: both guard and CreateW receive u"root\\c\u20ac.dat".
    const uint32_t heuro=kcall("openfile",{euro_a,ofs,1});
    CHECK(heuro!=0xffffffffu&&last_guard_path==u"root\\c\u20ac.dat"&&last_createw_path==u"root\\c\u20ac.dat");
    CHECK(kcall("_lclose",{heuro})==0);
    // OF_PARSE stores resolved path without calling CreateW; OF_REOPEN keeps the parsed path even after cwd changes and rejects unterminated szPathName.
    const unsigned opens_before_parse=createw_count;
    CHECK(kcall("openfile",{save_a,ofs,0x0100u})==0&&createw_count==opens_before_parse&&mem.r8(ofs+1)==1&&mem.r8(ofs+8)=='r');
    mock_cwd="other";
    const uint32_t hreopen=kcall("openfile",{0,ofs,0x8000u});
    CHECK(hreopen!=0xffffffffu&&last_createw_path==u"root\\save.dat");
    CHECK(kcall("_lclose",{hreopen})==0);
    // Drive-relative (C:foo), root-relative (\foo), cross-drive (D:foo), and oversized cwd (>=260) handling.
    mock_cwd="C:\\dir";
    uint32_t c_rel=g.put_str("C:foo"),root_rel=g.put_str("\\bar"),d_rel=g.put_str("D:foo");
    CHECK(kcall("openfile",{c_rel,ofs,0x0100u})==0&&mem.r8(ofs+8)=='C'&&mem.r8(ofs+14)=='\\'&&mem.r8(ofs+15)=='f'&&mem.r8(ofs+18)==0);
    CHECK(kcall("openfile",{root_rel,ofs,0x0100u})==0&&mem.r8(ofs+8)=='C'&&mem.r8(ofs+9)==':'&&mem.r8(ofs+10)=='\\'&&mem.r8(ofs+11)=='b'&&mem.r8(ofs+14)==0);
    CHECK(kcall("openfile",{d_rel,ofs,0x0100u})==0xffffffffu&&g.last_error()==50);
    mock_cwd=std::string(265,'x');
    CHECK(kcall("openfile",{save_a,ofs,0x0100u})==0xffffffffu&&g.last_error()==206);
    mock_cwd="root";
    for(uint32_t i=0;i<128;++i)mem.w8(ofs+8+i,'x');
    CHECK(kcall("openfile",{0,ofs,0x8000u})==0xffffffffu&&g.last_error()==87);
    // OF_EXIST probes, closes handle immediately, and returns TRUE (1).
    const unsigned closes_before_exist=close_count;
    CHECK(kcall("openfile",{save_a,ofs,0x4000u})==1&&close_count==closes_before_exist+1);
    refuse=true;CHECK(kcall("openfile",{save_a,ofs,0x0200u})==0xffffffffu&&g.last_error()==5);refuse=false;
    last_deleted_w.clear();
    CHECK(kcall("openfile",{save_a,ofs,0x0200u})==0&&last_deleted_w==u"root\\save.dat");
    CHECK(kcall("openfile",{save_a,ofs,0x0400u})==0xffffffffu&&g.last_error()==50);  // OF_VERIFY unsupported
    const uint32_t h2=kcall("lzopenfilea",{out_a,ofs,2});
    CHECK(h2!=0xffffffffu&&h2!=h1);
    CHECK(kcall("openfile",{0,ofs,0})==0xffffffffu&&g.last_error()==87);
    // LZCopy: distinct LZERROR_* codes, multi-chunk streaming, write errors, and split-header SZDD/KWAJ rejection (-8).
    CHECK(kcall("lzcopy",{0x1,h2})==0xffffffffu&&g.last_error()==6);   // LZERROR_BADINHANDLE (-1)
    CHECK(kcall("lzcopy",{h1,0x1})==0xfffffffeu&&g.last_error()==6);   // LZERROR_BADOUTHANDLE (-2)
    read_chunks={{'h','e','l','l','o',' '},{'w','o','r','l','d','!'}}; read_idx=0; written_bytes.clear();
    CHECK(kcall("lzcopy",{h1,h2})==0xfffffffcu&&g.last_error()==120);  // LZERROR_WRITE (-4) when write_ok==false
    read_idx=0; written_bytes.clear(); write_ok=true;
    CHECK(kcall("lzcopy",{h1,h2})==12&&g.last_error()==0&&written_bytes.size()==12&&std::memcmp(written_bytes.data(),"hello world!",12)==0);
    read_chunks={{'S','Z','D','D'},{0x88,0xF0,0x27,0x33,'x'}}; read_idx=0; written_bytes.clear();
    CHECK(kcall("lzcopy",{h1,h2})==0xfffffff8u&&g.last_error()==50&&written_bytes.empty());  // LZERROR_UNKNOWNALG (-8)
    read_chunks={{'K','W','A','J'},{0x88,0xF0,0x27,0xD1,'y'}}; read_idx=0; written_bytes.clear();
    CHECK(kcall("lzcopy",{h1,h2})==0xfffffff8u&&g.last_error()==50&&written_bytes.empty());  // LZERROR_UNKNOWNALG (-8)
    read_chunks.clear(); write_ok=false;
    CHECK(kcall("lzclose",{h2})==0&&kcall("_lclose",{h2})==0xffffffffu&&g.last_error()==6);
    CHECK(kcall("_lclose",{h1})==0&&kcall("_lclose",{h1})==0xffffffffu);
    uint32_t c_w=g.put_wstr(u"C:\\"),z_w=g.put_wstr(u"Z:\\"),c_a=g.put_str("C:\\"),z_a=g.put_str("Z:\\");
    CHECK(kcall("getdiskfreespacew",{c_w,0x3c90,0x3c94,0x3c98,0x3c9c})==1&&mem.r32(0x3c90)==8&&mem.r32(0x3c94)==512&&mem.r32(0x3c98)==100&&mem.r32(0x3c9c)==200);
    CHECK(kcall("getdiskfreespacew",{z_w,0x3c90,0,0,0})==0&&g.last_error()==21);
    CHECK(kcall("getdiskfreespacew",{0,0,0,0,0})==0&&g.last_error()==87);
    CHECK(kcall("getdiskfreespaceexa",{c_a,0x3ca0,0x3ca8,0x3cb0})==1&&mem.r32(0x3ca0)==1000&&mem.r32(0x3ca4)==0&&mem.r32(0x3ca8)==2000&&mem.r32(0x3cb0)==3000);
    CHECK(kcall("getdiskfreespaceexw",{c_w,0x3ca0,0,0})==1&&mem.r32(0x3ca0)==1000);
    CHECK(kcall("getdiskfreespaceexa",{z_a,0x3ca0,0,0})==0&&g.last_error()==21);
    CHECK(kcall("getdrivetypew",{c_w})==3&&kcall("getdrivetypew",{z_w})==0&&kcall("getdrivetypew",{0})==0);
    CHECK(kcall("createhardlinka",{dst_a,src_a,0})==0&&g.last_error()==50);
    CHECK(kcall("createhardlinkw",{dst_w,src_w,0})==0&&g.last_error()==50);
    CHECK(kcall("createsymboliclinka",{dst_a,src_a,0})==0&&g.last_error()==50);
    CHECK(kcall("createsymboliclinkw",{dst_w,src_w,0})==0&&g.last_error()==50);
    refuse=true;CHECK(kcall("createsymboliclinkw",{dst_w,src_w,0})==0&&g.last_error()==5);refuse=false;
    CHECK(kcall("createhardlinkw",{0,src_w,0})==0&&g.last_error()==87);
    CHECK(kcall("verifyconsoleiohandle",{hs})==0&&g.last_error()==6);
    CHECK(call("closehandle",{hs})==1);
    std::printf("x86-file: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
