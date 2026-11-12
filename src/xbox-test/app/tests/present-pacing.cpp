// SPDX-License-Identifier: GPL-3.0-or-later
// Contract tests for host/present_pacing.h: the present.txt parser, the
// SyncInterval override, and the fps-cap limiter driven by a fake clock. Pure
// host logic — no WinRT/COM/windows.h includes needed by the header itself —
// so this builds and runs anywhere mingw g++ runs (CI, no Wine needed, but it
// runs fine under Wine too like the other app tests).
#include <cstdio>
#include <string>
#include <vector>

#include "../host/present_pacing.h"

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

// ---- fake clock: deterministic QPC stand-in -------------------------------
struct FakeClock {
    int64_t t=0;                 // current tick
    std::vector<int64_t> waits;  // every wait_until target, in order
    int64_t now() const {return t;}
    int64_t ticks_per_second() const {return 1000000;}   // 1 tick = 1 us
    void wait_until(int64_t until) {
        waits.push_back(until);
        if(t<until)t=until;      // a real clock advances; the fake jumps
    }
};

int main() {
    using namespace present_pacing;

    // ---- parse: absent-equivalent and off --------------------------------
    {
        Config c=parse("");
        CHECK(!c.enabled&&!c.syncSet&&c.sync==0&&c.fpscap==0);
        c=parse("off");
        CHECK(!c.enabled);
        c=parse("  off  \n");
        CHECK(!c.enabled);
    }
    // ---- parse: sync=N ----------------------------------------------------
    {
        Config c=parse("sync=1");
        CHECK(c.enabled&&c.syncSet&&c.sync==1&&c.fpscap==0);
        c=parse("sync=0");
        CHECK(c.enabled&&c.syncSet&&c.sync==0);   // sync=0 is a real choice (undo a forced vsync)
        c=parse("sync=4");
        CHECK(c.enabled&&c.syncSet&&c.sync==4);
        // Above DXGI's range: clamped to 4, not an error.
        c=parse("sync=7");
        CHECK(c.enabled&&c.syncSet&&c.sync==4);
        c=parse("sync=100000");
        CHECK(c.enabled&&c.syncSet&&c.sync==4);
        // Newlines and extra tokens.
        c=parse("sync=2\nfpscap=60");
        CHECK(c.enabled&&c.syncSet&&c.sync==2&&c.fpscap==60);
        c=parse("\tsync=3  ");
        CHECK(c.enabled&&c.syncSet&&c.sync==3);
    }
    // ---- parse: fpscap=N --------------------------------------------------
    {
        Config c=parse("fpscap=60");
        CHECK(c.enabled&&!c.syncSet&&c.fpscap==60);
        c=parse("fpscap=1");
        CHECK(c.enabled&&c.fpscap==1);
        c=parse("fpscap=120");
        CHECK(c.enabled&&c.fpscap==120);
    }
    // ---- parse: errors fall back to off -----------------------------------
    {
        // A malformed known key is a parse error: never half-apply.
        Config c=parse("sync=");
        CHECK(!c.enabled);
        c=parse("sync=abc");
        CHECK(!c.enabled);
        c=parse("sync=1x");
        CHECK(!c.enabled);
        c=parse("fpscap=");
        CHECK(!c.enabled);
        c=parse("fpscap=0");
        CHECK(!c.enabled);
        c=parse("fpscap=-1");
        CHECK(!c.enabled);
        c=parse("fpscap=60 sync=");
        CHECK(!c.enabled);   // one bad key poisons the whole file
        c=parse("sync=2 fpscap=0");
        CHECK(!c.enabled);
        // Unknown tokens are ignored (swapchain.txt's leniency).
        c=parse("whatever sync=1");
        CHECK(c.enabled&&c.syncSet&&c.sync==1);
    }
    // ---- effectiveSync ----------------------------------------------------
    {
        Config off;
        CHECK(effectiveSync(off,0)==0);      // off: the game's own interval
        CHECK(effectiveSync(off,2)==2);
        Config s=parse("sync=1");
        CHECK(effectiveSync(s,0)==1);        // forced over the game's request
        CHECK(effectiveSync(s,3)==1);
        Config s0=parse("sync=0");
        CHECK(effectiveSync(s0,1)==0);       // forced 0 also overrides
        Config f=parse("fpscap=60");
        CHECK(effectiveSync(f,0)==0);        // fpscap alone leaves the interval alone
        CHECK(effectiveSync(f,1)==1);
    }
    // ---- limiter: cap 60 spaces frames >= 16.6 ms on a 1 MHz fake clock ---
    {
        FakeClock k;Limiter<FakeClock> lim(k);
        lim.set_cap(60);
        CHECK(lim.active());
        CHECK(lim.slot_ticks()==1000000/60);   // 16666 us
        // A burst of instant presents is spread one slot apart.
        std::vector<int64_t> at;
        for(int i=0;i<5;++i)at.push_back(lim.acquire());
        for(size_t i=1;i<at.size();++i)
            CHECK(at[i]-at[i-1]>=1000000/60);
        // Five presents at cap 60 take >= 4 slots (the first is free).
        CHECK(at.back()>=4*(1000000/60));
        // No two presents ever share a slot: with slot 16666, each tick
        // floor(at/16666) must be distinct.
        for(size_t i=1;i<at.size();++i)
            CHECK(at[i]/(1000000/60)>at[i-1]/(1000000/60));
    }
    // ---- limiter: a late present resets the grid, no catch-up burst ------
    {
        FakeClock k;Limiter<FakeClock> lim(k);
        lim.set_cap(60);
        (void)lim.acquire();                       // t=0
        k.t=1000000;                               // game stalled 1 s
        int64_t a=lim.acquire();                   // proceeds immediately
        CHECK(a==1000000);
        int64_t b=lim.acquire();                   // next one waits a full slot from there
        CHECK(b-a>=1000000/60);
        // ...and not one tick earlier than the slot from the late present.
        CHECK(b==a+1000000/60);
    }
    // ---- limiter: cap 30 gives ~33.3 ms slots -----------------------------
    {
        FakeClock k;Limiter<FakeClock> lim(k);
        lim.set_cap(30);
        CHECK(lim.slot_ticks()==1000000/30);
        int64_t a=lim.acquire(),b=lim.acquire();
        CHECK(b-a>=1000000/30);
    }
    // ---- limiter: fps=0 is a pass-through ---------------------------------
    {
        FakeClock k;Limiter<FakeClock> lim(k);
        lim.set_cap(0);
        CHECK(!lim.active());
        int64_t a=lim.acquire(),b=lim.acquire();
        CHECK(a==0&&b==0);           // no waiting, clock never advanced
        CHECK(k.waits.empty());
    }
    // ---- limiter: an exactly-on-slot present does not wait ----------------
    {
        FakeClock k;Limiter<FakeClock> lim(k);
        lim.set_cap(60);
        int64_t a=lim.acquire();     // t=0, next slot at 16666
        k.t=1000000/60;              // the game arrives exactly at the slot
        int64_t b=lim.acquire();
        CHECK(b==k.t);               // no wait needed
        CHECK(k.waits.empty());
    }

    // ---- effectiveFlags: forced sync>0 clears ALLOW_TEARING only ----------
    {
        const uint32_t TEAR=PRESENT_ALLOW_TEARING, OTHER=1u|TEAR;
        Config c=parse("sync=1");
        // forced non-zero sync: the tearing flag is dropped, the rest survive
        CHECK(effectiveFlags(c,1,TEAR)==0);
        CHECK(effectiveFlags(c,1,OTHER)==1u);
        CHECK(effectiveFlags(c,4,TEAR)==0);
        // forced sync=0: the flag stays (DXGI allows tearing with interval 0)
        CHECK(effectiveFlags(c,0,TEAR)==TEAR);
        CHECK(effectiveFlags(c,0,OTHER)==OTHER);
        // the bit is DXGI's own ALLOW_TEARING (0x200), not RESTART (0x4): a
        // wrong bit would let tearing through AND strip RESTART
        CHECK(TEAR==0x200u);
        CHECK(effectiveFlags(c,1,0x204u)==0x4u);   // RESTART kept, TEARING dropped
        // opposite direction: a game requesting sync 0 with tearing and no
        // override is forwarded unchanged
        Config off=parse("");
        CHECK(effectiveFlags(off,0,TEAR)==TEAR);
        CHECK(effectiveFlags(off,0,OTHER)==OTHER);
        // an fpscap-only config never touches flags
        Config cap=parse("fpscap=60");
        CHECK(effectiveFlags(cap,1,TEAR)==TEAR);
        // the game's own non-zero sync with tearing (no override) also passes
        // through untouched: the flag pairing is the game's own bug then
        CHECK(effectiveFlags(off,1,TEAR)==TEAR);
    }

    // ---- interval_stats: present timestamps only, frameMs never mixed in --
    {
        // Two populations must stay separate: frame times of 8.4 ms and
        // present-to-present deltas of 16.7 ms. The known sequence's deltas
        // are 16.6, 16.7, 16.8, 33.4 (one stall) -> avg=20.875, p50=16.75,
        // p99=33.4, one interval over 25 ms. A wrong implementation that
        // seeded with frameMs (8.4 x4) would give a far smaller avg.
        IntervalStats s=interval_stats({0.0,16.6,33.3,50.1,83.5});
        CHECK(s.over25==1);
        CHECK(s.avg>20.0&&s.avg<21.8);
        CHECK(s.p50>16.5&&s.p50<17.0);
        CHECK(s.p99>33.0&&s.p99<=33.5);
        // evenly spaced 60 Hz: all intervals 16.7, none over 25
        std::vector<double> at;
        for(int i=0;i<61;++i)at.push_back(i*16.7);
        s=interval_stats(at);
        CHECK(s.over25==0);
        CHECK(s.avg>16.6&&s.avg<16.8);
        CHECK(s.p50>16.6&&s.p50<16.8);
        // a 120 fps presenter into a 60 Hz chain: p50 8.35, still no >25
        at.clear();
        for(int i=0;i<121;++i)at.push_back(i*8.35);
        s=interval_stats(at);
        CHECK(s.over25==0);
        CHECK(s.p50>8.3&&s.p50<8.4);
        // fewer than two timestamps: no interval exists, all zeros
        s=interval_stats({});
        CHECK(s.avg==0&&s.p50==0&&s.p99==0&&s.over25==0);
        s=interval_stats({42.0});
        CHECK(s.avg==0&&s.p50==0&&s.p99==0&&s.over25==0);
    }

    if(failures){printf("present-pacing: %u checks, %u FAIL\n",checks,failures);return 1;}
    printf("present-pacing: %u checks — parse/off/clamp, effectiveSync, effectiveFlags, interval_stats, fake-clock limiter PASS\n",checks);
    return 0;
}
