// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(X86_TIME_DECLARATIONS_ONLY)
#include <windows.h>
#include <fileapi.h>
#include <timezoneapi.h>
#include <mmsystem.h>
#include <type_traits>
using PeriodFn = MMRESULT (WINAPI*)(UINT);
using TimeGetTimeFn = DWORD (WINAPI*)(void);
using TimeGetDevCapsFn = MMRESULT (WINAPI*)(LPTIMECAPS, UINT);
using SystemTimeToFileTimeFn = BOOL (WINAPI*)(const SYSTEMTIME*, LPFILETIME);
using SystemTimeToTzFn = BOOL (WINAPI*)(const TIME_ZONE_INFORMATION*, const SYSTEMTIME*, LPSYSTEMTIME);
static_assert(std::is_same<decltype(&timeBeginPeriod),PeriodFn>::value);
static_assert(std::is_same<decltype(&timeEndPeriod),PeriodFn>::value);
static_assert(std::is_same<decltype(&timeGetTime),TimeGetTimeFn>::value);
static_assert(std::is_same<decltype(&timeGetDevCaps),TimeGetDevCapsFn>::value);
static_assert(std::is_same<decltype(&SystemTimeToFileTime),SystemTimeToFileTimeFn>::value);
static_assert(std::is_same<decltype(&SystemTimeToTzSpecificLocalTime),SystemTimeToTzFn>::value);
#else
#include <cstdio>
#include <cstring>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;
    const char* names[]={"timebeginperiod","timeendperiod","timegettime","timegetdevcaps","systemtimetofiletime","systemtimetotzspecificlocaltime","createdirectorya","deletefilea","deletefilew"};
    const unsigned arity[]={1,1,0,2,2,3,2,1,1};
    for(unsigned i=0;i<9;++i){auto it=reg.map.find({i<4?"winmm.dll":"kernel32.dll",names[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==arity[i]);}}
    uint64_t now=0x1234567887654321ull;host.tick_count=[&](){return now;};
    unsigned calls=0;
    host.destructive_file_guard=[](const char*,const std::u16string&){return true;};
    host.file_call=[&](Host::FileCall& q){++calls;switch(q.kind){
    case Host::FileCall::Kind::TimeConvert:CHECK(q.operation==Host::FileCall::Operation::SystemToFileTime);CHECK(q.data.size()==16);CHECK(q.data[0]==0xe8);if(q.data[2]==13){q.error=87;break;}CHECK(q.data[2]==3);q.data.assign(8,0x5a);q.succeeded=true;break;
    case Host::FileCall::Kind::TimeLocal:CHECK(q.operation==Host::FileCall::Operation::SystemToTzLocal);CHECK(q.data.size()==(q.args[0]?188u:16u));if(q.args[0])CHECK(q.args[0]==1);if(q.data[2]==13){q.error=87;break;}q.data.assign(16,0x36);q.succeeded=true;break;
    case Host::FileCall::Kind::AttributesA:
        if(q.operation==Host::FileCall::Operation::CreateDirectoryA){CHECK(q.path_a=="saved");q.succeeded=true;}
        else if(q.operation==Host::FileCall::Operation::DeleteA){CHECK(q.path_a=="old.dat");q.succeeded=true;}
        else if(q.operation==Host::FileCall::Operation::DeleteW){CHECK(q.path_w==u"old-w.dat");q.succeeded=true;}
        else { CHECK(false); }
        break;
    default:CHECK(false);break;
    }};
    auto call=[&](const char* dll,const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({dll,n}).fn(cpu);};
    CHECK(call("kernel32.dll","gettickcount",{})==uint32_t(now)&&call("winmm.dll","timegettime",{})==uint32_t(now));
    CHECK(call("kernel32.dll","gettickcount64",{})==uint32_t(now)&&cpu.reg[EDX]==uint32_t(now>>32));
    CHECK(call("winmm.dll","timebeginperiod",{1})==0&&call("winmm.dll","timebeginperiod",{1000000})==0);
    CHECK(call("winmm.dll","timebeginperiod",{0})==97&&call("winmm.dll","timebeginperiod",{1000001})==97);
    CHECK(call("winmm.dll","timeendperiod",{1})==0&&call("winmm.dll","timeendperiod",{0})==97);
    CHECK(call("winmm.dll","timegetdevcaps",{0x3000,8})==0&&mem.r32(0x3000)==1&&mem.r32(0x3004)==1000000);
    CHECK(call("winmm.dll","timegetdevcaps",{0,8})==129&&call("winmm.dll","timegetdevcaps",{0x3000,4})==129);
    mem.w(0x3100,2024,2);mem.w(0x3102,3,2);mem.w(0x3104,1,2);
    CHECK(call("kernel32.dll","systemtimetofiletime",{0x3100,0x3120})==1&&mem.r8(0x3120)==0x5a);
    mem.w(0x3102,13,2);CHECK(call("kernel32.dll","systemtimetofiletime",{0x3100,0x3120})==0&&g.last_error()==87);
    mem.w(0x3102,3,2);CHECK(call("kernel32.dll","systemtimetotzspecificlocaltime",{0x3200,0x3100,0x3140})==1&&mem.r8(0x3140)==0x36);
    mem.w(0x3102,13,2);CHECK(call("kernel32.dll","systemtimetotzspecificlocaltime",{0,0x3100,0x3140})==0&&g.last_error()==87);
    uint32_t dir=g.put_str("saved"),del=g.put_str("old.dat"),delw=g.put_wstr(u"old-w.dat");
    CHECK(call("kernel32.dll","createdirectorya",{dir,0})==1);
    CHECK(call("kernel32.dll","deletefilea",{del})==1&&call("kernel32.dll","deletefilew",{delw})==1);
    CHECK(calls==7);
    // GetSystemTimePreciseAsFileTime writes the same modelled FILETIME as GetSystemTimeAsFileTime; one CPU means processor 0.
    mem.w32(0x3200, 0); mem.w32(0x3204, 0);
    call("kernel32.dll", "getsystemtimepreciseasfiletime", {0x3200});
    CHECK(mem.r(0x3200, 4) == 0x80000000u && mem.r(0x3204, 4) == 0x01D00000u);
    CHECK(call("kernel32.dll", "getcurrentprocessornumber", {}) == 0);
    CHECK(call("kernel32.dll", "flushprocesswritebuffers", {}) == 0);
    // RtlTimeToSecondsSince1970: 100 ns ticks since 1601 (LARGE_INTEGER by pointer) to seconds since 1970; 1970-01-01 is 11644473600 s after 1601.
    mem.w32(0x3220, 0xd53e8000u); mem.w32(0x3224, 0x019db1deu);  // 116444736000000000 ticks = 1970-01-01T00:00:00Z
    CHECK(call("ntdll.dll", "rtltimetosecondssince1970", {0x3220, 0x3228}) == 1 && mem.r32(0x3228) == 0);
    mem.w32(0x3220, 0xd53e8000u + 10000000u); mem.w32(0x3224, 0x019db1deu);
    CHECK(call("ntdll.dll", "rtltimetosecondssince1970", {0x3220, 0x3228}) == 1 && mem.r32(0x3228) == 1);
    mem.w32(0x3220, 0u); mem.w32(0x3224, 0u);
    CHECK(call("ntdll.dll", "rtltimetosecondssince1970", {0x3220, 0x3228}) == 0);
    std::printf("x86-time: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
