#pragma once
namespace u32c_unicode {
// Fixed US layout: CAPLOK letters invert Shift when Caps Lock is toggled.
// Wine 10 win32u/input.c kbd_tables_vkey_to_wchar and kbdus tables select
// the alternate letter column for Caps Lock. Other translation stays local
// to ToUnicode; this helper does not claim Ctrl/Alt or dead-key support.
inline char letter_case(char ch, const BYTE* state) {
    bool upper = state && (((state[VK_SHIFT] & 0x80) != 0) !=
                           ((state[VK_CAPITAL] & 1) != 0));
    if (ch >= 'A' && ch <= 'Z' && !upper) return char(ch + ('a' - 'A'));
    if (ch >= 'a' && ch <= 'z' && upper) return char(ch - ('a' - 'A'));
    return ch;
}
}
