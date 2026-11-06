#pragma once
namespace u32c_unicode {
// Wine 10 win32u/input.c kbd_tables_vkey_to_wchar handles Ctrl letters
// before consulting layout tables. Ctrl+Alt has no character in fixed US.
// Return -1 when this narrow helper does not own the translation.
inline int control_letter(UINT vk, const BYTE* state, LPWSTR buf, int capacity) {
    if (!state || !(state[VK_CONTROL] & 0x80) || vk < 'A' || vk > 'Z') return -1;
    if (state[VK_MENU] & 0x80) return 0;
    buf[0] = WCHAR(vk - 'A' + 1);
    if (capacity > 1) buf[1] = 0;
    return 1;
}
}
