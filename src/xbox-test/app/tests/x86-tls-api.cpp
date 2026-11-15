// SPDX-License-Identifier: GPL-3.0-or-later
// Guest TlsAlloc/TlsFree/TlsGetValue/TlsSetValue follow Windows semantics:
// lowest free slot (never 0), a freed slot is reused, TlsGetValue clears
// LastError on success, TlsFree of an unallocated slot fails.
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;cpu.fs_base=0x1000;
    auto call=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};
    uint32_t a=call("tlsalloc",{}),b=call("tlsalloc",{});
    CHECK(a!=0&&a!=0xFFFFFFFFu&&b!=0&&b!=a);
    CHECK(call("tlssetvalue",{a,0x1234})==1);
    g.set_last_error(5);
    CHECK(call("tlsgetvalue",{a})==0x1234&&g.last_error()==0);
    g.set_last_error(5);
    CHECK(call("tlsgetvalue",{b})==0&&g.last_error()==0);
    CHECK(call("tlsfree",{a})==1);
    CHECK(call("tlsfree",{a})==0&&g.last_error()==87);
    CHECK(call("tlsfree",{0})==0);
    uint32_t c=call("tlsalloc",{});
    CHECK(c==a&&call("tlsgetvalue",{c})==0);
    std::printf("x86-tls-api: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
