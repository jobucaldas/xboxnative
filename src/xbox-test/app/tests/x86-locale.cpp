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
    // LocaleNameToLCID / IsValidLocaleName forward the name (and flags) to the host; no host means ERROR_CALL_NOT_IMPLEMENTED.
    uint32_t en_us = g.put_wstr(u"en-US");
    unsigned locale_calls = 0;
    host.nls_call = [&](const Host::NlsRequest& q) {
        ++locale_calls; Host::NlsResponse r{};
        if (q.operation == Host::NlsOperation::LocaleNameToLCID) { CHECK(q.flags == 0x20); CHECK(q.locale_name == u"en-US"); r.result = 0x409; return r; }
        CHECK(q.operation == Host::NlsOperation::IsValidLocaleName); CHECK(q.locale_name == u"en-US"); r.result = 1; return r; };
    CHECK(call("localenametolcid", {en_us, 0x20}) == 0x409);
    CHECK(call("isvalidlocalename", {en_us}) == 1);
    CHECK(locale_calls == 2);
    host.nls_call = {};
    g.set_last_error(0);CHECK(call("localenametolcid", {en_us, 0}) == 0);CHECK(g.last_error() == 120);
    CHECK(call("isvalidlocalename", {en_us}) == 0);
    // LCMapStringA: one byte per output character (unit 1), copied back from the host's bytes.
    host.nls_call = [&](const Host::NlsRequest& q) {
        Host::NlsResponse r{};
        CHECK(q.operation == Host::NlsOperation::LCMapStringA); CHECK(q.locale == 0x409); CHECK(q.flags == 0x200);
        CHECK(q.input.size() == 2 && q.input[0] == 'a' && q.input[1] == 'b');
        r.result = 2; r.output = {'A', 'B'}; return r; };
    mem.write_bytes(0x3000, "ab", 2);
    CHECK(call("lcmapstringa", {0x409, 0x200, 0x3000, 2, 0x3100, 8}) == 2);CHECK(mem.r(0x3100, 1) == 'A' && mem.r(0x3101, 1) == 'B');
    // CompareStringA: both strings travel as bytes; CSTR_LESS (1) is returned unchanged.
    host.nls_call = [&](const Host::NlsRequest& q) {
        Host::NlsResponse r{};
        CHECK(q.operation == Host::NlsOperation::CompareStringA); CHECK(q.locale == 0x409); CHECK(q.flags == 0);
        CHECK(q.input.size() == 3 && q.input[2] == 'c'); CHECK(q.input2.size() == 3 && q.input2[2] == 'd');
        r.result = 1; return r; };
    mem.write_bytes(0x3000, "abc", 3); mem.write_bytes(0x3010, "abd", 3);
    CHECK(call("comparestringa", {0x409, 0, 0x3000, 3, 0x3010, 3}) == 1);
    // lstrcmpiA: CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE) on NUL-terminated input, minus 2.
    host.nls_call = [&](const Host::NlsRequest& q) {
        Host::NlsResponse r{};
        CHECK(q.operation == Host::NlsOperation::CompareStringA); CHECK(q.locale == 0x400); CHECK(q.flags == 1);
        CHECK(q.input.size() == 4 && q.input[3] == 0 && q.input2.size() == 4);
        r.result = 2; return r; };
    mem.write_bytes(0x3020, "Abc", 4); mem.write_bytes(0x3030, "abc", 4);
    CHECK(call("lstrcmpia", {0x3020, 0x3030}) == 0);
    host.nls_call = [&](const Host::NlsRequest&) { Host::NlsResponse r{}; r.result = 3; return r; };
    CHECK(call("lstrcmpia", {0x3020, 0x3030}) == 1);
    host.nls_call = [&](const Host::NlsRequest&) { Host::NlsResponse r{}; r.last_error = 87; return r; };
    g.set_last_error(0);CHECK(call("lstrcmpia", {0x3020, 0x3030}) == 0);CHECK(g.last_error() == 87);
    // GetStringTypeExA is the same host operation as GetStringTypeA: one WORD of type bits per input byte.
    host.nls_call = [&](const Host::NlsRequest& q) {
        Host::NlsResponse r{};
        CHECK(q.operation == Host::NlsOperation::GetStringTypeA); CHECK(q.locale == 0x409); CHECK(q.type == 1);
        r.result = 1; r.output = {0x01, 0x00, 0x02, 0x00}; return r; };
    mem.write_bytes(0x3040, "ab", 2);
    CHECK(call("getstringtypeexa", {0x409, 1, 0x3040, 2, 0x3100}) == 1);CHECK(mem.r(0x3100, 2) == 1 && mem.r(0x3102, 2) == 2);
    host.nls_call = {};
    // HeapValidate: NULL validates the heap; an address that is not a live allocation fails with ERROR_INVALID_ADDRESS.
    CHECK(call("heapvalidate", {0x1, 0, 0}) == 1);
    g.set_last_error(0);CHECK(call("heapvalidate", {0x1, 0, 0x3000}) == 0);CHECK(g.last_error() == 487);
    CHECK(call("heapwalk", {0x1, 0}) == 0);CHECK(g.last_error() == 120);
    CHECK(call("heapcompact", {0x1, 0}) == 0);CHECK(g.last_error() == 120);
    // IsBadReadPtr: FALSE over mapped pages (0x1000..0x31000 in this harness), TRUE across an unmapped page.
    CHECK(call("isbadreadptr", {0x3000, 16}) == 0);CHECK(call("isbadreadptr", {0x3000, 0}) == 0);
    CHECK(call("isbadreadptr", {0x40000, 4}) == 1);CHECK(call("isbadreadptr", {0x30ff8, 16}) == 1);
    // EnumSystemLocalesEx: the user's default locale is passed once to the guest callback as a NUL-terminated UTF-16 string.
    mem.map(0x50000, 0x1000, PROT_R | PROT_W | PROT_X);
    // callback: inc [0x330c]; store [esp+4] (the name) at 0x3300; return TRUE (ret 12)
    const std::vector<uint8_t> enum_true{0xFF,0x05,0x0C,0x33,0x00,0x00, 0x8B,0x44,0x24,0x04, 0xA3,0x00,0x33,0x00,0x00, 0xB8,0x01,0x00,0x00,0x00, 0xC2,0x0C,0x00};
    // callback: inc [0x330c]; return FALSE (ret 12)
    const std::vector<uint8_t> enum_false{0xFF,0x05,0x0C,0x33,0x00,0x00, 0x31,0xC0, 0xC2,0x0C,0x00};
    mem.write_bytes(0x50000, enum_true.data(), enum_true.size());
    mem.write_bytes(0x50100, enum_false.data(), enum_false.size());
    unsigned enum_host_calls = 0;
    host.nls_call = [&](const Host::NlsRequest& q) {
        ++enum_host_calls; Host::NlsResponse r{};
        CHECK(q.operation == Host::NlsOperation::GetUserDefaultLocaleName);
        r.result = 6; r.output.assign({'e',0,'n',0,'-',0,'U',0,'S',0,0,0}); return r; };
    mem.w32(0x330c, 0);
    CHECK(call("enumsystemlocalesex", {0x50000, 0x10, 0x1234, 0}) == 1);
    CHECK(mem.r32(0x330c) == 1 && enum_host_calls == 1);
    const uint32_t enum_name = mem.r32(0x3300);
    CHECK(mem.r(enum_name, 2) == 'e' && mem.r(enum_name + 2, 2) == 'n' && mem.r(enum_name + 8, 2) == 'S' && mem.r(enum_name + 10, 2) == 0);
    mem.w32(0x330c, 0);
    CHECK(call("enumsystemlocalesex", {0x50100, 0, 0, 0}) == 1);CHECK(mem.r32(0x330c) == 1);
    g.set_last_error(0);CHECK(call("enumsystemlocalesex", {0, 0, 0, 0}) == 0);CHECK(g.last_error() == 87);
    host.nls_call = {};
    std::printf("x86-locale: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
