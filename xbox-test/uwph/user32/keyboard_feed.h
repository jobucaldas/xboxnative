#pragma once
#include <windows.h>
#include <array>

namespace user32_core {
// UI-thread adapter for CoreWindow's virtual keys and physical key status.
// The sink owns synchronization with guest Get(Async)KeyState callers.
class KeyboardFeed {
public:
    using Sink = void (WINAPI*)(int, BOOL);
    explicit KeyboardFeed(Sink sink = nullptr) : sink_(sink) {}
    void key(unsigned vk, unsigned scan, bool extended, bool down) {
        if (vk == VK_SHIFT) vk = scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        else if (vk == VK_CONTROL) vk = extended ? VK_RCONTROL : VK_LCONTROL;
        else if (vk == VK_MENU) vk = extended ? VK_RMENU : VK_LMENU;
        if (!sink_ || vk >= held_.size()) return;
        // Wine server/queue.c refreshes the async press bit on repeated downs.
        if (down && held_[vk]) sink_(static_cast<int>(vk), TRUE);
        else set(vk, down);
        switch (vk) {
        case VK_LSHIFT: case VK_RSHIFT: aggregate(VK_SHIFT, VK_LSHIFT, VK_RSHIFT); break;
        case VK_LCONTROL: case VK_RCONTROL: aggregate(VK_CONTROL, VK_LCONTROL, VK_RCONTROL); break;
        case VK_LMENU: case VK_RMENU: aggregate(VK_MENU, VK_LMENU, VK_RMENU); break;
        }
    }
    void release_all() {
        for (unsigned vk = 0; vk < held_.size(); ++vk) set(vk, false);
    }
private:
    Sink sink_;
    std::array<bool, 256> held_{};
    void set(unsigned vk, bool down) {
        // Aggregate modifiers and releases only need state transitions.
        if (held_[vk] == down) return;
        held_[vk] = down;
        if (sink_) sink_(static_cast<int>(vk), down ? TRUE : FALSE);
    }
    void aggregate(unsigned generic, unsigned left, unsigned right) {
        set(generic, held_[left] || held_[right]);
    }
};
}
