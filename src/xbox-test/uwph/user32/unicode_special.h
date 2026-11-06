#pragma once
namespace u32c_unicode {
// Wine 10 fixed US keyboard tables: these characters ignore Shift and Alt.
// Ctrl combinations remain outside this helper.
inline int special_key(UINT vk, const BYTE* state, LPWSTR buf, int capacity) {
    WCHAR ch;
    switch (vk) {
    case VK_BACK: ch = '\b'; break;
    case VK_TAB: ch = '\t'; break;
    case VK_RETURN: ch = '\r'; break;
    case VK_ESCAPE: ch = 0x1b; break;
    default: return -1;
    }
    const bool ctrl = state && (state[VK_CONTROL] & 0x80);
    // Host Wine keyboard drivers differ in Ctrl columns. Leave those to a
    // future layout-aware translator rather than assume the fallback table.
    if (ctrl) return -1;
    buf[0] = ch;
    if (capacity > 1) buf[1] = 0;
    return 1;
}
}
