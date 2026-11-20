// SPDX-License-Identifier: GPL-3.0-or-later
// SetTimer(NULL, ...) thread timers: due timers reach GetMessage/PeekMessage as WM_TIMER with
// hwnd NULL, wParam the timer id and lParam the TimerProc, and DispatchMessage calls the TimerProc.
// Filters follow GetMessage: a window hwnd or a range without 0x113 excludes them; PM_NOREMOVE
// leaves them pending and PM_REMOVE delivers them. Drives the registered thunks with a mocked clock.
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);mem.map(0x40000,0x1000,PROT_R|PROT_W|PROT_X);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;
    // The TimerProc: inc dword [0x3600]; ret
    const uint8_t stub[]={0xFF,0x05,0x00,0x36,0x00,0x00,0xC3};mem.write_bytes(0x40000,stub,sizeof stub);
    uint64_t now=0;unsigned peeks=0;
    host.tick_count=[&]{return now;};
    host.window_handles[0x1000]=0x2000;   // a real window for the window-filter case
    host.window_call=[&](Host::WindowCall& q) {
        if(q.name=="PeekMessageA"||q.name=="PeekMessageW"||q.name=="GetMessageA"||q.name=="GetMessageW") {
            // Each peek advances the clock, so the timer comes due while the loop runs.
            ++peeks; now+=4; q.supported=true; q.result=0;
            if(peeks>200){ // no delivery: stop the loop with WM_QUIT
                q.output.assign(32,0); uint32_t quit=0x0012u; memcpy(q.output.data()+8,&quit,4); q.result=1;
            }
        } else q.supported=false;
    };
    g.register_all();
    Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;cpu.fs_base=0x1000;
    auto call=[&](const char* dll,const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({dll,n}).fn(cpu);};
    // SetTimer(NULL, ignored, 10, proc): the id is generated and nonzero.
    uint32_t id=call("user32.dll","settimer",{0,0,10,0x40000u});
    CHECK(id!=0);
    const uint32_t msg=0x3000;
    now=20;   // the timer is due
    // A window filter does not admit a thread timer: GetMessage keeps looking and the loop ends at WM_QUIT.
    peeks=0;
    CHECK(call("user32.dll","getmessagea",{msg,0x1000,0,0})==0);
    CHECK(mem.r32(msg+4)==0x0012u);
    // A message range without WM_TIMER (0x113) does not admit it either.
    peeks=0;
    CHECK(call("user32.dll","getmessagea",{msg,0,0x100,0x101})==0);
    CHECK(mem.r32(msg+4)==0x0012u);
    // PM_NOREMOVE reports the timer and leaves it pending.
    peeks=0;
    CHECK(call("user32.dll","peekmessagea",{msg,0,0,0,0})==1);
    CHECK(mem.r32(msg)==0);                   // hwnd NULL
    CHECK(mem.r32(msg+4)==0x0113u);           // WM_TIMER
    CHECK(mem.r32(msg+8)==id);                // wParam: timer id
    CHECK(mem.r32(msg+12)==0x40000u);         // lParam: TimerProc
    // PM_REMOVE delivers the same timer, and DispatchMessage calls the TimerProc.
    CHECK(call("user32.dll","peekmessagea",{msg,0,0,0,1})==1);
    CHECK(mem.r32(msg+4)==0x0113u);
    CHECK(mem.r32(msg+8)==id);
    CHECK(call("user32.dll","dispatchmessagea",{msg})==0);
    CHECK(mem.r32(0x3600)==1);
    std::printf("x86-thread-timer: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
