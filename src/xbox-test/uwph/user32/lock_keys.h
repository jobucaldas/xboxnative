#pragma once
namespace u32c_input {
// Wine server/queue.c set_input_key_state toggles bit 0 on a fresh down.
// Only lock keys expose it here; other keys retain the existing polling
// contract. Repeats and releases must not change the latched lock state.
inline void toggle_lock(BYTE& state, int vk) {
    if ((vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) &&
        !(state & 0x80)) state ^= 1;
}
}
