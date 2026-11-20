// SPDX-License-Identifier: GPL-3.0-or-later
// SetTimer(NULL, ...) thread timers: due timers reach GetMessage/PeekMessage as WM_TIMER with
// hwnd NULL, wParam the timer id and lParam the TimerProc, and DispatchMessage calls the TimerProc.
// Filters follow GetMessage: a window hwnd or a range without 0x113 excludes them, and such a
// GetMessage still returns WM_QUIT. PM_NOREMOVE leaves them pending and PM_REMOVE delivers them.
// Drives the registered thunks with a mocked clock; a guard turns a hang into a failure.
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);mem.map(0x40000,0x1000,PROT_R|PROT_W|PROT_X);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;
    // The TimerProc: inc dword [0x3600]; ret
    const uint8_t stub[]={0xFF,0x05,0x00,0x36,0x00,0x00,0xC3};mem.write_bytes(0x40000,stub,sizeof stub);
    uint64_t now=0;unsigned calls=0;bool quit_posted=false,guard_hit=false;
    host.tick_count=[&]{return now;};
    host.window_handles[0x1000]=0x2000;   // a real window for the window-filter case
    // Model: PostQuitMessage sets the quit flag. GetMessage returns WM_QUIT whatever its filter;
    // PeekMessage returns it only with a NULL or (HWND)-1 filter and a range that admits it. Retrieving it clears the flag.
    auto quit_out=[](Host::WindowCall& q){q.output.assign(32,0);uint32_t quit=0x0012u;memcpy(q.output.data()+8,&quit,4);q.supported=true;q.result=1;};
    host.window_call=[&](Host::WindowCall& q) {
        if(q.name=="PostQuitMessage"){quit_posted=true;q.supported=true;q.result=0;return;}
        const bool peek=q.name=="PeekMessageA"||q.name=="PeekMessageW", get=q.name=="GetMessageA"||q.name=="GetMessageW";
        if(!peek&&!get){q.supported=false;return;}
        ++calls; now+=4; q.supported=true; q.result=0;
        const bool thread_filter=q.args[1]==0||q.args[1]==uintptr_t(-1);
        const bool quit_in_range=(q.args[2]==0&&q.args[3]==0)||(q.args[2]<=0x12u&&0x12u<=q.args[3]);
        if(quit_posted&&(get||(thread_filter&&quit_in_range))){quit_posted=false;quit_out(q);return;}
        if(calls>200){ // guard: a regression that never returns is reported, not hung
            guard_hit=true;quit_out(q);
        }
    };
    g.register_all();
    Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;cpu.fs_base=0x1000;
    auto call=[&](const char* dll,const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({dll,n}).fn(cpu);};
    // SetTimer(NULL, ignored, 10, proc): the id is generated and nonzero.
    uint32_t id=call("user32.dll","settimer",{0,0,10,0x40000u});
    CHECK(id!=0);
    const uint32_t msg=0x3000;
    now=20;   // the timer is due
    // A thread timer is pending, a window filter rejects it, and WM_QUIT must still end GetMessage.
    calls=0;
    call("user32.dll","postquitmessage",{0});
    CHECK(call("user32.dll","getmessagea",{msg,0x1000,0,0})==0);
    CHECK(mem.r32(msg+4)==0x0012u);
    CHECK(!guard_hit);
    // The same with a range that excludes WM_TIMER.
    calls=0;
    call("user32.dll","postquitmessage",{0});
    CHECK(call("user32.dll","getmessagea",{msg,0,0x100,0x101})==0);
    CHECK(mem.r32(msg+4)==0x0012u);
    CHECK(!guard_hit);
    // The timer was not taken by those calls: PM_NOREMOVE reports it and leaves it pending.
    calls=0;
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
    // A NULL-hwnd range filter that admits WM_TIMER but not WM_QUIT polls for a not-yet-due
    // thread timer; WM_QUIT must still end GetMessage.
    CHECK(call("user32.dll","settimer",{0,0,1000,0x40000u})!=0);
    calls=0;
    call("user32.dll","postquitmessage",{0});
    CHECK(call("user32.dll","getmessagea",{msg,0,0x113,0x113})==0);
    CHECK(mem.r32(msg+4)==0x0012u);
    CHECK(!guard_hit);
    std::printf("x86-thread-timer: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
