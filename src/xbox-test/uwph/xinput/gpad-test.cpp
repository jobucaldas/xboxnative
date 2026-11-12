// gpad parser and runner regression (host, no Windows): the command text a
// tester writes must arm exactly the presses a guest's XInputGetState will
// see, and a program must release what it pressed. The dual-consumer checks
// at the end pin the raw-input enumeration mask (uwph_u32c_pads): slot 0
// appears while injection holds buttons and only then. The XInputGetState
// assertions live in gpad-xinput-test.cpp (Wine; route.h needs windows.h).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpad.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
using namespace gpad;

static std::vector<Press> out;        // last successful parse
static const char* err = nullptr;     // last parse error
static bool g(const char* s) { err = nullptr; return parse_gpad(s, strlen(s), &out, &err); }
static bool seq(const char* s) { err = nullptr; return parse_seq(s, strlen(s), &out, &err); }

int main() {
    // gpad command: button list, optional hold.
    CHECK(g("A") && out.size() == 1);
    CHECK(out[0].buttons == BTN_A && out[0].hold_ms == 120 && out[0].lt == 0 && out[0].rt == 0);
    CHECK(g("DPAD_DOWN,A 250") && out.size() == 1);
    CHECK(out[0].buttons == (BTN_DPAD_DOWN | BTN_A) && out[0].hold_ms == 250);
    CHECK(g("a,b") && out[0].buttons == (BTN_A | BTN_B));   // case-insensitive
    CHECK(g("START,BACK,LSTICK,RSTICK,LB,RB"));
    CHECK(out[0].buttons == (BTN_START | BTN_BACK | BTN_LSTICK | BTN_RSTICK | BTN_LB | BTN_RB));
    CHECK(g("up,down") && out[0].buttons == (BTN_DPAD_UP | BTN_DPAD_DOWN));
    CHECK(!g("up down") && err);   // space ends the button list; a second name there is a bad number
    CHECK(g("X 60000") && out[0].hold_ms == 60000);
    CHECK(g("X 999999") && out[0].hold_ms == 60000);   // a stuck press is clamped

    // Triggers are analog bytes, not bits.
    CHECK(g("LT,RT") && out[0].buttons == 0 && out[0].lt == 255 && out[0].rt == 255);
    CHECK(g("A,LT 200") && out[0].buttons == BTN_A && out[0].lt == 255 && out[0].hold_ms == 200);

    // Rejections: a typo must never silently press nothing.
    CHECK(!g("NOPE") && err);
    CHECK(!g("A,NOPE") && err);
    CHECK(!g("") && err);
    CHECK(!g("A x") && err);   // non-numeric hold
    CHECK(!g("A 1 2 3") && err);   // at most two numbers

    // gpadseq file: steps, comments, blanks, caps.
    CHECK(seq("# repro: options then session\n"
              "DPAD_DOWN 150\n"
              "\n"
              "A 200 100\n"
              "LT 500\n") && out.size() == 3);
    CHECK(out[0].buttons == BTN_DPAD_DOWN && out[0].hold_ms == 150);
    CHECK(out[1].buttons == BTN_A && out[1].hold_ms == 200 && out[1].gap_ms == 100);
    CHECK(out[2].lt == 255 && out[2].hold_ms == 500);
    const char* long_line = "A 111111111111111111111111111111111111111111111";   // > 40 chars
    CHECK(!seq(long_line) && err);
    std::string many;
    for (int i = 0; i < 70; ++i) many += "A\n";
    CHECK(parse_seq(many.data(), many.size(), &out, &err) && out.size() == 64);   // cmd.txt's own cap
    CHECK(!seq("") && err);

    // Runner: press, hold, release, and a release is actually delivered.
    Runner r;
    uint16_t buttons; uint8_t lt, rt;
    r.held(&buttons, &lt, &rt);
    CHECK(buttons == 0 && !r.armed);
    r.tick(1000); r.held(&buttons, &lt, &rt);
    CHECK(buttons == 0);   // disarmed runner never holds
    g("A 200");
    r.arm(out, 1000);
    CHECK(r.armed);
    r.tick(1000); r.held(&buttons, &lt, &rt);
    CHECK(buttons == BTN_A);                       // pressed on arm
    r.tick(1150); r.held(&buttons, &lt, &rt);
    CHECK(buttons == BTN_A);                       // still held inside the window
    r.tick(1200); r.held(&buttons, &lt, &rt);
    CHECK(buttons == 0);                           // released after hold_ms
    r.tick(5000);
    CHECK(!r.armed);                               // program finished and self-cleared

    // Two steps with a gap: the release between steps is observable.
    seq("A 100 50\nB 100\n");
    r.arm(out, 0);
    r.tick(0); r.held(&buttons, &lt, &rt); CHECK(buttons == BTN_A);
    r.tick(100); r.held(&buttons, &lt, &rt); CHECK(buttons == 0);    // released, gap running
    r.tick(149); r.held(&buttons, &lt, &rt); CHECK(buttons == 0);
    r.tick(150); r.held(&buttons, &lt, &rt); CHECK(buttons == BTN_B); // second step pressed
    r.tick(250); r.held(&buttons, &lt, &rt); CHECK(buttons == 0);
    r.tick(1000); CHECK(!r.armed);

    // Re-arming mid-program replaces it (a new cmd.txt command wins).
    g("A 1000"); r.arm(out, 0);
    r.tick(0);
    g("B 100"); r.arm(out, 10);
    r.tick(10); r.held(&buttons, &lt, &rt); CHECK(buttons == BTN_B);
    r.tick(200); CHECK(!r.armed);

    // clear(): the safety path guest_ended takes.
    g("A 1000"); r.arm(out, 0); r.tick(0);
    r.clear(); r.tick(50); r.held(&buttons, &lt, &rt);
    CHECK(!r.armed && buttons == 0);

    // Both consumer classes: the raw-input enumeration (uwph_u32c_pads mask)
    // must present slot 0 while injection holds buttons so a guest finds the
    // IG_ device it reads through XInput — and must not add anything when
    // injection is idle. Button bits themselves flow only through
    // XInputGetState (pad_registry.h: reads never complete), asserted in
    // gpad-xinput-test.cpp.
    CHECK(gpad::publish_mask(0, false) == 0);
    CHECK(gpad::publish_mask(0, true) == 1u);              // no physical pad: slot 0 appears
    CHECK(gpad::publish_mask(0x2 | 0x8, true) == (0x2u | 0x8u | 1u));   // physical pads kept
    CHECK(gpad::publish_mask(0x2 | 0x8, false) == (0x2u | 0x8u));
    g("DPAD_DOWN 100");
    r.arm(out, 0); r.tick(0); r.held(&buttons, &lt, &rt);
    CHECK(buttons == BTN_DPAD_DOWN);
    CHECK(gpad::publish_mask(0, buttons != 0) == 1u);
    r.tick(100); r.held(&buttons, &lt, &rt);
    CHECK(buttons == 0);                                   // release delivered
    CHECK(gpad::publish_mask(0, buttons != 0) == 0u);      // enumeration back to physical only

    puts("gpad: parse, reject, trigger bytes, hold/release timing, gap, re-arm, clear and dual-consumer mask PASS");
}
