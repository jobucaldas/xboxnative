#pragma once
#include "keyboard_feed.h"

namespace user32_core {
// Mouse button snapshots contain held state, not new presses on every move.
// Keep this adapter on the UI thread alongside CoreWindowKeyboard.
class PointerButtons {
    KeyboardFeed feed_;
    unsigned held_ = 0;
public:
    explicit PointerButtons(KeyboardFeed::Sink sink = nullptr) : feed_(sink) {}
    void snapshot(bool left, bool right, bool middle, bool x1, bool x2) {
        const unsigned keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        const bool down[] = {left, right, middle, x1, x2};
        unsigned next = 0;
        for (unsigned i = 0; i < 5; ++i) {
            if (down[i]) next |= 1u << i;
            if (down[i] != ((held_ & (1u << i)) != 0))
                feed_.key(keys[i], 0, false, down[i]);
        }
        held_ = next;
    }
    void release_all() { snapshot(false, false, false, false, false); }
};
}
