// SPDX-License-Identifier: GPL-3.0-or-later
// A window DC from user32 GetDC takes the GDI state calls (SetBkMode, SetTextColor,
// SetBkColor, SelectObject), its default stock objects are restorable, a blit into
// it fails closed, and ReleaseDC ends it. Drives the registered thunks directly.
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;
    host.get_dc=[](uintptr_t)->uintptr_t{return 0x5678u;};
    host.release_dc=[](uintptr_t,uintptr_t dc){return dc==0x5678u;};
    g.register_all();
    Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;
    auto call=[&](const char* dll,const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({dll,n}).fn(cpu);};
    // GetDC(NULL) returns the screen DC token; the GDI calls must accept it.
    uint32_t dc=call("user32.dll","getdc",{0});
    CHECK(dc!=0);
    // SetBkMode returns the previous mode; OPAQUE (2) is the default. TRANSPARENT is 1.
    CHECK(call("gdi32.dll","setbkmode",{dc,1})==2);
    CHECK(call("gdi32.dll","settextcolor",{dc,0x00ff0000u})==0);
    CHECK(call("gdi32.dll","setbkcolor",{dc,0x00123456u})==0x00ffffffu);
    // The default brush is the stock white brush; selecting it back must work.
    uint32_t brush=call("gdi32.dll","createsolidbrush",{0x000000ffu});
    CHECK(brush!=0);
    uint32_t old=call("gdi32.dll","selectobject",{dc,brush});
    CHECK(old==0x73001000u);
    CHECK(call("gdi32.dll","selectobject",{dc,old})==brush);
    // No bitmap is selected into a window DC, so a blit into it fails closed (120).
    g.set_last_error(0);
    CHECK(call("gdi32.dll","bitblt",{dc,0,0,1,1,dc,0,0,0x00cc0020u})==0);
    CHECK(g.last_error()==120);
    // Bitmaps go only into memory DCs: selecting the stock bitmap into the window DC fails (0, 6)
    // and leaves it unchanged, so the blit above still fails closed.
    g.set_last_error(0);
    CHECK(call("gdi32.dll","selectobject",{dc,0x73000001u})==0);
    CHECK(g.last_error()==6);
    CHECK(call("gdi32.dll","bitblt",{dc,0,0,1,1,dc,0,0,0x00cc0020u})==0);
    CHECK(g.last_error()==120);
    // DeleteDC is not for a GetDC DC: it fails (0, 6) and the DC stays usable until ReleaseDC.
    g.set_last_error(0);
    CHECK(call("gdi32.dll","deletedc",{dc})==0);
    CHECK(g.last_error()==6);
    CHECK(call("gdi32.dll","setbkmode",{dc,1})==1);
    // ReleaseDC ends the DC: later state calls fail with 6.
    CHECK(call("user32.dll","releasedc",{0,dc})==1);
    g.set_last_error(0);
    CHECK(call("gdi32.dll","setbkmode",{dc,2})==0);
    CHECK(g.last_error()==6);
    // A memory DC still goes away with DeleteDC.
    uint32_t mem_dc=call("gdi32.dll","createcompatibledc",{0});
    CHECK(mem_dc!=0);
    CHECK(call("gdi32.dll","deletedc",{mem_dc})==1);
    std::printf("x86-gdi-window-dc: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
