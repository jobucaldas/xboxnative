// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(LOCALE_DECLARATIONS_ONLY)
#include <windows.h>
#include <datetimeapi.h>
#include <cstddef>
#include <type_traits>
using GetDateFormatExFn = int (WINAPI*)(LPCWSTR,DWORD,const SYSTEMTIME*,LPCWSTR,LPWSTR,int,LPCWSTR);
using GetTimeFormatExFn = int (WINAPI*)(LPCWSTR,DWORD,const SYSTEMTIME*,LPCWSTR,LPWSTR,int);
using LcidToLocaleNameFn = int (WINAPI*)(LCID,LPWSTR,int,DWORD);
static_assert(std::is_same<decltype(&GetDateFormatEx),GetDateFormatExFn>::value);
static_assert(std::is_same<decltype(&GetTimeFormatEx),GetTimeFormatExFn>::value);
static_assert(std::is_same<decltype(&LCIDToLocaleName),LcidToLocaleNameFn>::value);
static_assert(sizeof(SYSTEMTIME)==16 && offsetof(SYSTEMTIME,wDayOfWeek)==4 && offsetof(SYSTEMTIME,wDay)==6 && offsetof(SYSTEMTIME,wMilliseconds)==14);
#else
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;
    const char* names[]={"gettimeformatw","getdateformatw"};
    for(const char* n:names){auto it=reg.map.find({"kernel32.dll",n});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==6);}}
    unsigned calls=0;
    host.locale_call=[&](Host::LocaleCall& q){++calls;CHECK(q.locale==0x0409);CHECK(q.flags==0);CHECK(q.has_system_time);CHECK(q.system_time[0]==2024&&q.system_time[1]==6&&q.system_time[2]==15);CHECK(q.has_format&&q.format==u"HH:mm");CHECK(q.has_output_buffer);CHECK(q.buffer_chars==16);
        if(q.date){q.error=87;q.succeeded=false;q.result=0;return;}
        const uint16_t text[]={u'1',u'7',u':',u'4',u'2',0};q.data.resize(sizeof(text));std::memcpy(q.data.data(),text,sizeof(text));q.result=6;q.succeeded=true;};
    auto call=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};
    const uint16_t st[]={2024,6,15,0,17,42,0,0};mem.write_bytes(0x3000,st,sizeof(st));uint32_t fmt=g.put_wstr(u"HH:mm");
    g.set_last_error(55);CHECK(call("gettimeformatw",{0x0409,0,0x3000,fmt,0x3100,16})==6);CHECK(mem.r(0x3100,2)==u'1'&&mem.r(0x310a,2)==0);CHECK(g.last_error()==55);
    CHECK(call("getdateformatw",{0x0409,0,0x3000,fmt,0x3200,16})==0);CHECK(g.last_error()==87);CHECK(calls==2);
    // Size query (cchTime 0, lpTimeStr non-null): the host returns the required
    // length and no characters, and the guest must leave lpTimeStr untouched.
    host.locale_call=[&](Host::LocaleCall& q){CHECK(q.buffer_chars==0);q.data.clear();q.result=6;q.succeeded=true;};
    mem.w(0x3300,0xAAAA,2);CHECK(call("gettimeformatw",{0x0409,0,0x3000,fmt,0x3300,0})==6);CHECK(mem.r(0x3300,2)==0xAAAA);
    host.locale_call={};
    CHECK(reg.map.at({"kernel32.dll","gettimeformatw"}).fn(cpu)==0);CHECK(g.last_error()==120);
    // GetUserGeoID: GEOCLASS is forwarded to the host; a host answer is returned as-is, and no host
    // identifier (result 0) or no host route is GEOID_NOT_AVAILABLE (0xFFFFFFFF) per MS docs.
    auto geo_it=reg.map.find({"kernel32.dll","getusergeoid"});CHECK(geo_it!=reg.map.end());
    if(geo_it!=reg.map.end()){CHECK(geo_it->second.conv==Conv::Stdcall);CHECK(geo_it->second.argc==1);}
    unsigned geo=0;
    host.nls_call=[&](const Host::NlsRequest& q){++geo;CHECK(q.operation==Host::NlsOperation::GetUserGeoID);CHECK(q.type==16);Host::NlsResponse r{};r.result=244;return r;};
    g.set_last_error(55);CHECK(call("getusergeoid",{16})==244);CHECK(g.last_error()==55);
    host.nls_call=[&](const Host::NlsRequest&){++geo;Host::NlsResponse r{};r.result=0;r.last_error=1;return r;};
    CHECK(call("getusergeoid",{16})==0xFFFFFFFFu);CHECK(g.last_error()==1);
    host.nls_call={};
    CHECK(call("getusergeoid",{16})==0xFFFFFFFFu);CHECK(g.last_error()==120);CHECK(geo==2);
    std::printf("x86-locale: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
