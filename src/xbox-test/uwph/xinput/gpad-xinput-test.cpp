// gpad end-to-end through the emulated XInput stack (Wine): an armed gpad
// program, ticked the way the launcher's PollGuestControllers does, must be
// visible in XInputGetState as button state changes and must be released.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpad.h"
#include "route.h"
#include <cstdio>
#include <cstring>
#define CHECK(x) do { if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
using namespace gpad;

int main() {
    const char* err = nullptr;
    std::vector<Press> prog;

    // A press with no physical pad: the launcher publishes slot 0 itself.
    CHECK(parse_gpad("A 100", 6, &prog, &err));
    Runner r; r.arm(prog, 1000);
    uint16_t buttons; uint8_t lt, rt;
    r.tick(1000); r.held(&buttons, &lt, &rt);
    CHECK(buttons == BTN_A);
    XINPUT_GAMEPAD injected{}; injected.wButtons = buttons;
    xinput_core::publish(0, &injected);
    XINPUT_STATE st{};
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.wButtons == XINPUT_GAMEPAD_A);

    // Still held inside the window, packet number stable while unchanged.
    r.tick(1050); r.held(&buttons, &lt, &rt); CHECK(buttons == BTN_A);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    DWORD packet = st.dwPacketNumber;
    xinput_core::publish(0, &injected);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.dwPacketNumber == packet);

    // Release delivered: after hold_ms the mask drops and the packet advances.
    r.tick(1101); r.held(&buttons, &lt, &rt);
    CHECK(buttons == 0);
    XINPUT_GAMEPAD released{};   // the launcher publishes the physical reading (empty here)
    xinput_core::publish(0, &released);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.wButtons == 0 && st.dwPacketNumber == packet + 1);

    // Triggers ride the analog bytes.
    CHECK(parse_gpad("LT 100", 6, &prog, &err));
    r.clear(); r.arm(prog, 2000);
    r.tick(2000); r.held(&buttons, &lt, &rt);
    CHECK(lt == 255 && rt == 0 && buttons == 0);
    XINPUT_GAMEPAD trig{}; trig.bLeftTrigger = lt;
    xinput_core::publish(0, &trig);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.bLeftTrigger == 255);

    // A physical pad and injection at once: OR, neither releases the other.
    XINPUT_GAMEPAD physical{}; physical.wButtons = XINPUT_GAMEPAD_X;
    XINPUT_GAMEPAD inj{}; inj.wButtons = XINPUT_GAMEPAD_A;
    xinput_core::publish(0, &physical);
    // (the launcher ORs inj into the physical state before publish)
    physical.wButtons |= inj.wButtons;
    xinput_core::publish(0, &physical);
    CHECK(xinput_core::get_state(0, &st) == 0 &&
        st.Gamepad.wButtons == (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_A));

    // clear() (the guest_ended path): the next publish is clean.
    r.clear();
    CHECK(!r.armed);
    xinput_core::publish(0, &released);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.wButtons == 0);

    // A full two-step sequence through the same calls the launcher makes.
    CHECK(parse_seq("A 50 20\nB 50\n", 14, &prog, &err) && prog.size() == 2);
    r.arm(prog, 3000);
    bool sawA = false, sawGap = false, sawB = false, sawEnd = false;
    for (uint64_t t = 3000; t <= 3300; t += 10) {
        r.tick(t); r.held(&buttons, &lt, &rt);
        XINPUT_GAMEPAD frame{}; frame.wButtons = buttons;
        xinput_core::publish(0, &frame);
        xinput_core::get_state(0, &st);
        if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A) sawA = true;
        else if (sawA && !(st.Gamepad.wButtons & XINPUT_GAMEPAD_B)) sawGap = true;
        if (st.Gamepad.wButtons & XINPUT_GAMEPAD_B) sawB = true;
        if (!r.armed) { sawEnd = true; break; }
    }
    CHECK(sawA && sawGap && sawB && sawEnd);

    // Dual consumer: the same injected press must show in XInputGetState
    // (buttons) and in the raw-input enumeration mask the launcher reports
    // through uwph_u32c_pads (connection only; pad_registry.h: HID reads never
    // complete, an IG_ guest reads XInputGetState). Release clears both.
    CHECK(parse_gpad("DPAD_DOWN 100", 14, &prog, &err));
    r.clear(); r.arm(prog, 4000);
    r.tick(4000); r.held(&buttons, &lt, &rt);
    CHECK(buttons == BTN_DPAD_DOWN);
    CHECK(gpad::publish_mask(0, buttons != 0) == 1u);      // enumeration: slot 0 present
    XINPUT_GAMEPAD dpad{}; dpad.wButtons = buttons;
    xinput_core::publish(0, &dpad);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.wButtons == XINPUT_GAMEPAD_DPAD_DOWN);
    r.tick(4100); r.held(&buttons, &lt, &rt);
    CHECK(buttons == 0);                                   // release delivered
    CHECK(gpad::publish_mask(0, buttons != 0) == 0u);      // enumeration: back to physical only
    XINPUT_GAMEPAD empty{};
    xinput_core::publish(0, &empty);
    CHECK(xinput_core::get_state(0, &st) == 0 && st.Gamepad.wButtons == 0);

    puts("gpad+xinput: inject, hold, release, triggers, OR precedence, sequence, enumeration mask PASS");
}
