#pragma once
namespace u32c_unicode {
// Wine win32u/input.c fixed US VK_TO_WCHARS2 operator columns.
// Shift and Caps Lock do not alter these characters. Ctrl/Alt combinations
// remain outside this helper until their layout-specific behavior is supplied.
inline int keypad_operator(UINT vk, const BYTE* state, LPWSTR buf, int capacity) {
    WCHAR ch;
    switch (vk) {
    case VK_MULTIPLY: ch = '*'; break;
    case VK_ADD: ch = '+'; break;
    case VK_SUBTRACT: ch = '-'; break;
    case VK_DIVIDE: ch = '/'; break;
    default: return -1;
    }
    if (state && ((state[VK_CONTROL] | state[VK_MENU]) & 0x80)) return -1;
    buf[0] = ch;
    if (capacity > 1) buf[1] = 0;
    return 1;
}
}
