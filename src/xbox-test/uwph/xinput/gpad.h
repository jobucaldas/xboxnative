// SPDX-License-Identifier: GPL-3.0-or-later
// gpad: cmd.txt-driven synthetic pad injection for a running game guest.
//
// The console gateway can drive the launcher UI with the existing cmd.txt pad
// commands (press/back/up/...), but those only move the launcher: while a game
// guest is on screen the pad belongs to the guest, and the only in-game
// commands were gmenu/gquit/gforce/gfps (the launcher overlay). Reproducing
// the owner's Slime Rancher repro (open the game's Options, change the
// resolution, start a session) needs real button presses inside the game.
//
// This module parses the `gpad` / `gpadseq` commands and turns them into a
// timed button program. The launcher's UI loop feeds it:
//   - cmdhook() accepts a `gpad <buttons>[ hold_ms]` line (or `gpadseq <name>`
//     reading gpadseq/<name>.txt from LocalState) only while a guest is
//     running, and arms the program;
//   - PollGuestControllers() calls gpad::Runner::tick(now_ms) every frame and
//     ORs the held mask into the XINPUT_GAMEPAD it publishes through
//     xinput_core::publish, the same path the physical pad's WGI reading
//     takes, so a guest sees injected presses exactly like a real controller
//     (XInput callers, and Unity's <RI> pad, which reads XInputGetState after
//     its IG_ HID match).
//
// Precedence (docs/CMD-SCRIPTING.md): injected buttons are ORed with the
// physical pad's reading, so a tester's press can never release a button the
// player is holding; the launcher overlay still wins because the overlay
// publishes an empty pad while it owns input (inputBlocked) and gpad only
// adds to what the publisher sends.
//
// Safety: a program is armed only from cmdhook (the one-shot
// LocalState\cmd.txt file the Device Portal file channel writes) and only
// while a game guest is running; neither the game nor the network can reach
// this code path. The armed program is cleared when the guest ends
// (guest_ended -> gpad::Runner::clear), so nothing outlives the session.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gpad {

// XINPUT_GAMEPAD button bits (win/xinput.h values, mirrored so this header
// compiles where the console's headers do not reach).
enum : uint16_t {
    BTN_DPAD_UP = 0x0001, BTN_DPAD_DOWN = 0x0002, BTN_DPAD_LEFT = 0x0004,
    BTN_DPAD_RIGHT = 0x0008, BTN_START = 0x0010, BTN_BACK = 0x0020,
    BTN_LSTICK = 0x0040, BTN_RSTICK = 0x0080, BTN_LB = 0x0100, BTN_RB = 0x0200,
    BTN_A = 0x1000, BTN_B = 0x2000, BTN_X = 0x4000, BTN_Y = 0x8000,
};

// One button name to its bit; triggers are reported through *lt/*rt because
// XInput carries them as analog bytes, not bits. Returns false on an unknown
// name (only for the names this module accepts; LT/RT are matched first).
inline bool button_bit(const char* name, size_t len, uint16_t* bit, bool* lt, bool* rt) {
    *lt = *rt = false; *bit = 0;
    if (len == 2 && (name[0] == 'L' || name[0] == 'l') && (name[1] == 'T' || name[1] == 't')) { *lt = true; return true; }
    if (len == 2 && (name[0] == 'R' || name[0] == 'r') && (name[1] == 'T' || name[1] == 't')) { *rt = true; return true; }
    struct N { const char* s; uint16_t b; };
    static const N names[] = {
        {"A",BTN_A},{"B",BTN_B},{"X",BTN_X},{"Y",BTN_Y},
        {"LB",BTN_LB},{"RB",BTN_RB},
        {"DPAD_UP",BTN_DPAD_UP},{"DPAD_DOWN",BTN_DPAD_DOWN},
        {"DPAD_LEFT",BTN_DPAD_LEFT},{"DPAD_RIGHT",BTN_DPAD_RIGHT},
        {"UP",BTN_DPAD_UP},{"DOWN",BTN_DPAD_DOWN},{"LEFT",BTN_DPAD_LEFT},{"RIGHT",BTN_DPAD_RIGHT},
        {"START",BTN_START},{"BACK",BTN_BACK},
        {"LSTICK",BTN_LSTICK},{"RSTICK",BTN_RSTICK},
        {"LS",BTN_LSTICK},{"RS",BTN_RSTICK},
    };
    for (const N& n : names)
        if (strlen(n.s) == len) {
            size_t i = 0;
            while (i < len && (n.s[i] | 0x20) == (name[i] | 0x20)) ++i;
            if (i == len) { *bit = n.b; return true; }
        }
    return false;
}

// "A,B,X,Y,LB,RB,LT,RT,DPAD_*,LSTICK,START,BACK" into *mask/*lt/*rt.
// Returns false when any name is unknown, so a typo cannot silently press
// nothing: the command is rejected and the log says why.
inline bool parse_buttons(const char* s, size_t len, uint16_t* mask, uint8_t* lt, uint8_t* rt) {
    *mask = 0; *lt = 0; *rt = 0;
    size_t i = 0;
    bool any = false;
    while (i < len) {
        while (i < len && (s[i] == ',' || s[i] == ' ' || s[i] == '\t')) ++i;
        size_t start = i;
        while (i < len && s[i] != ',' && s[i] != ' ' && s[i] != '\t') ++i;
        if (i == start) break;
        uint16_t bit; bool isLt, isRt;
        if (!button_bit(s + start, i - start, &bit, &isLt, &isRt)) return false;
        if (isLt) *lt = 255; else if (isRt) *rt = 255; else *mask |= bit;
        any = true;
    }
    return any;
}

// One timed step: buttons (and trigger bytes) held for hold_ms, then released,
// then gap_ms of dead time before the next step.
struct Press {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    uint32_t hold_ms = 120;
    uint32_t gap_ms = 0;
    bool haveHold = false;   // parse bookkeeping: the first number is the hold
    bool haveGap = false;    // and the second is the gap
};

inline uint32_t clamp_ms(unsigned long v) { return v > 60000 ? 60000 : (uint32_t)v; }

// A gpadseq step line: "<buttons> [hold_ms [gap_ms]]". The 120 ms default hold
// is what a human tap measures on the console's frame budget.
inline bool parse_step(const char* s, size_t len, Press* out, const char** err) {
    *err = nullptr;
    const uint32_t defaultHold = out->hold_ms ? out->hold_ms : 120;
    *out = Press{};
    out->hold_ms = defaultHold;
    while (len && (*s == ' ' || *s == '\t')) { ++s; --len; }
    size_t sp = 0;
    while (sp < len && s[sp] != ' ' && s[sp] != '\t') ++sp;
    if (!parse_buttons(s, sp, &out->buttons, &out->lt, &out->rt)) { *err = "unknown or empty button list"; return false; }
    size_t i = sp;
    while (i < len) {
        while (i < len && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= len || !s[i]) break;   // i==len or a trailing NUL (a caller may pass sizeof, not strlen)
        if (s[i] < '0' || s[i] > '9') { *err = "expected a number"; return false; }
        char* end = nullptr;
        unsigned long v = strtoul(s + i, &end, 10);
        if (!out->haveHold) { out->hold_ms = clamp_ms(v); out->haveHold = true; }
        else if (!out->haveGap) { out->gap_ms = clamp_ms(v); out->haveGap = true; }
        else { *err = "at most hold_ms and gap_ms"; return false; }
        i = (size_t)(end - s);
    }
    return true;
}

// "gpad A,B 200" -> a one-step program.
inline bool parse_gpad(const char* s, size_t len, std::vector<Press>* out, const char** err) {
    Press p;   // default hold 120 ms
    if (!parse_step(s, len, &p, err)) return false;
    out->assign(1, p);
    return true;
}

// A gpadseq file body: one step per line, "<buttons> [hold_ms [gap_ms]]";
// blank lines and lines starting with '#' are comments. Caps mirror cmd.txt's
// own: at most 64 commands, each at most 40 characters.
inline bool parse_seq(const char* s, size_t len, std::vector<Press>* out, const char** err) {
    out->clear();
    size_t pos = 0;
    while (pos <= len && out->size() < 64) {
        size_t e = pos;
        while (e < len && s[e] != '\n') ++e;
        size_t a = pos, b = e;
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
        if (b > a && s[a] != '#' && b - a <= 40 && s[a]) {   // s[a]: skip a trailing NUL (len may be sizeof)
            Press p;
            if (!parse_step(s + a, b - a, &p, err)) return false;
            out->push_back(p);
        }
        if (e >= len) break;
        pos = e + 1;
    }
    if (out->empty()) { *err = "no steps"; return false; }
    return true;
}

// The armed program. One instance lives in the launcher's App; a test
// constructs its own. All times are milliseconds from a monotonic clock the
// caller owns (GetTickCount64 on the launcher, a synthetic clock in tests).
struct Runner {
    bool armed = false;
    size_t step = 0;
    bool down = false;       // the current step's buttons are being held
    bool gap = false;        // released; waiting out gap_ms before the next step
    uint64_t phase_ms = 0;   // when the current hold or gap began
    std::vector<Press> program;
    // What the publisher should OR into the pad state this frame.
    void held(uint16_t* buttons, uint8_t* lt, uint8_t* rt) const {
        *buttons = 0; *lt = 0; *rt = 0;
        if (!armed || !down || step >= program.size()) return;
        const Press& p = program[step];
        *buttons = p.buttons; *lt = p.lt; *rt = p.rt;
    }
    void arm(std::vector<Press> p, uint64_t now_ms) {
        program = std::move(p);
        armed = !program.empty();
        step = 0; down = false; gap = false; phase_ms = now_ms;
    }
    void clear() { armed = false; down = false; gap = false; program.clear(); }
    // Advance to now_ms. The caller calls this every frame before reading held().
    void tick(uint64_t now_ms) {
        if (!armed) return;
        for (;;) {
            if (step >= program.size()) { clear(); return; }
            const Press& p = program[step];
            if (down) {
                if (now_ms - phase_ms < p.hold_ms) return;   // still holding
                down = false; gap = true; phase_ms = now_ms; // release
            } else if (gap) {
                if (now_ms - phase_ms < p.gap_ms) return;    // gap still running
                gap = false; ++step;                         // next step presses on the next tick
            } else {
                down = true; phase_ms = now_ms;              // press
                return;
            }
        }
    }
};

// The slot mask the launcher reports to the guest's raw-input enumeration
// (uwph_u32c_pads): the physical pads, plus slot 0 while an injection program
// holds buttons. The enumeration carries connection state only — button bits
// never flow through it (pad_registry.h: reads never complete; a guest whose
// enumerated device path carries "IG_" reads buttons through XInputGetState,
// which publish feeds) — but a guest must be able to enumerate the IG_ device
// to find the XInput pad it reads, so an automated run with no physical pad
// connected still needs slot 0 present while injection is active.
inline unsigned publish_mask(unsigned physical_mask, bool injecting) {
    return injecting ? (physical_mask | 1u) : physical_mask;
}

} // namespace gpad
