// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in present pacing for guest games (LocalState\present.txt, read once at
// swap-chain creation; absent file or `off` = today's behaviour). Some games
// present with SyncInterval 0 well above the display rate (Unity menu with
// vSyncCount 0: ~120 fps Presents into a 60 Hz flip-model chain), and the
// uneven frame delivery is visible as judder. Two independent levers:
// `sync=N` rewrites the SyncInterval the guest passes to every Present, and
// `fpscap=N` spaces the guest's Presents at least one 1/N s slot apart with a
// QPC-based limiter that sleeps then spins to the next slot.
//
// Pure and freestanding (no WinRT/COM/windows.h): parse() and effectiveSync()
// are plain functions and Limiter takes its tick source as a template
// parameter, so tests drive it with a fake clock. The real QPC source lives
// with the wiring (guest_overlay.h).
#pragma once
#include <cstdint>
#include <string>

namespace present_pacing {

// The parsed file. enabled=false everywhere is "off": hooks forward the
// game's Present untouched.
struct Config {
    bool enabled=false;
    bool syncSet=false;
    uint32_t sync=0;      // forced SyncInterval when syncSet (clamped to 0..4)
    uint32_t fpscap=0;    // max guest Presents per second; 0 = uncapped
};

// All digits, no sign, no trailing junk; overflow-safe for the values here.
inline bool parse_uint(const char* s,uint32_t& out) {
    if(!*s)return false;
    uint32_t v=0;
    for(;*s;++s) {
        if(*s<'0'||*s>'9')return false;
        if(v>100000000u)return false;   // 1e8 fps is far past any sane cap
        v=v*10u+uint32_t(*s-'0');
    }
    out=v;return true;
}

// Keys are whitespace/newline-separated tokens: `off`, `sync=N`, `fpscap=N`.
// Unknown tokens are ignored (swapchain.txt's leniency); a malformed known
// key — "sync=", "sync=abc", "fpscap=0" — is a parse error and falls back to
// off, so a half-written file can never half-apply. `off` wins outright.
// sync values above 4 are clamped to 4 (DXGI's SyncInterval range), not
// treated as an error: the intent "vsync on" survives a fat-fingered value.
inline Config parse(const std::string& text) {
    Config c;
    size_t i=0,n=text.size();
    while(i<n) {
        while(i<n&&(text[i]==' '||text[i]=='\t'||text[i]=='\r'||text[i]=='\n'))++i;
        size_t b=i;
        while(i<n&&text[i]!=' '&&text[i]!='\t'&&text[i]!='\r'&&text[i]!='\n')++i;
        if(b==i)break;
        std::string tok=text.substr(b,i-b);
        if(tok=="off")return Config{};
        if(tok.rfind("sync=",0)==0) {
            uint32_t v;
            if(!parse_uint(tok.c_str()+5,c.sync))return Config{};
            c.syncSet=true;c.enabled=true;
            if(c.sync>4)c.sync=4;
        } else if(tok.rfind("fpscap=",0)==0) {
            uint32_t v;
            if(!parse_uint(tok.c_str()+7,v)||v==0)return Config{};   // a 0 fps cap is meaningless, not "uncapped"
            c.fpscap=v;c.enabled=true;
        }
    }
    return c;
}

// The SyncInterval the guest's Present actually gets: the forced value when
// one is configured (clamped to DXGI's 0..4), the game's own otherwise.
inline uint32_t effectiveSync(const Config& c,uint32_t requested) {
    if(!c.enabled||!c.syncSet)return requested;
    return c.sync>4u?4u:c.sync;
}

// Spaces Presents at least one slot apart. Absolute-slot bookkeeping without
// catch-up bursts: a present that arrives late (game stall) resets the grid
// from its own time, so a stall is never followed by two Presents inside one
// slot. The caller's clock supplies now(), ticks_per_second() and
// wait_until() (sleep-then-spin on the host; the test's fake clock just
// jumps). Not thread-safe: the present hooks, like the rest of the overlay,
// assume the game's single Present thread.
template<typename Clock>
class Limiter {
public:
    explicit Limiter(Clock& clock):clock(clock){}
    // fps=0 disables (the pass-through the default config gives).
    void set_cap(uint32_t fps) {
        slot=fps?int64_t(clock.ticks_per_second()/fps):0;
        next=-1;
    }
    bool active() const {return slot!=0;}
    int64_t slot_ticks() const {return slot;}
    // Blocks until the present may proceed; returns the tick it proceeded at.
    int64_t acquire() {
        if(!slot)return clock.now();
        if(next<0)next=clock.now();
        if(clock.now()<next)clock.wait_until(next);
        int64_t at=clock.now();
        next=at+slot;
        return at;
    }
private:
    Clock& clock;
    int64_t slot=0;   // ticks per slot; 0 = no cap
    int64_t next=-1;  // tick the next present may proceed at
};

}
