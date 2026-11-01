#pragma once
#include "keymap.h"

// Fixed US-layout names, consistent with this DLL's GetKeyboardLayout.
// ABI: user32.spec GetKeyNameTextW(long ptr long). Behavior reference:
// Wine 10 win32u/input.c NtUserGetKeyNameText and its default US keyboard.
// Do not ask host USER32: its keyboard layout belongs to the launcher.
namespace u32c_keynames {
inline const WCHAR* special(unsigned code) {
    switch (code) {
    case 0x01: return L"Esc"; case 0x0e: return L"Backspace";
    case 0x0f: return L"Tab"; case 0x1c: return L"Enter";
    case 0x1d: return L"Ctrl"; case 0x2a: return L"Shift";
    case 0x36: return L"Right Shift"; case 0x37: return L"Num *";
    case 0x38: return L"Alt"; case 0x39: return L"Space";
    case 0x3a: return L"Caps Lock"; case 0x45: return L"Pause";
    case 0x46: return L"Scroll Lock"; case 0x47: return L"Num 7";
    case 0x48: return L"Num 8"; case 0x49: return L"Num 9";
    case 0x4a: return L"Num -"; case 0x4b: return L"Num 4";
    case 0x4c: return L"Num 5"; case 0x4d: return L"Num 6";
    case 0x4e: return L"Num +"; case 0x4f: return L"Num 1";
    case 0x50: return L"Num 2"; case 0x51: return L"Num 3";
    case 0x52: return L"Num 0"; case 0x53: return L"Num Del";
    case 0x54: return L"Sys Req";
    case 0x11c: return L"Num Enter"; case 0x11d: return L"Right Ctrl";
    case 0x135: return L"Num /"; case 0x137: return L"Prnt Scrn";
    case 0x138: return L"Right Alt"; case 0x145: return L"Num Lock";
    case 0x146: return L"Break"; case 0x147: return L"Home";
    case 0x148: return L"Up"; case 0x149: return L"Page Up";
    case 0x14b: return L"Left"; case 0x14d: return L"Right";
    case 0x14f: return L"End"; case 0x150: return L"Down";
    case 0x151: return L"Page Down"; case 0x152: return L"Insert";
    case 0x153: return L"Delete"; case 0x154: return L"<00>";
    case 0x156: return L"Help"; case 0x15b: return L"Left Windows";
    case 0x15c: return L"Right Windows"; case 0x15d: return L"Application";
    default: return nullptr;
    }
}
}
extern "C" int WINAPI c_GetKeyNameTextW(LONG bits, LPWSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    unsigned code = (static_cast<DWORD>(bits) >> 16) & 0x1ff;
    if (bits & 0x02000000) {
        if (code == 0x36) code = 0x2a;
        else if (code == 0x11d) code = 0x1d;
        else if (code == 0x138) code = 0x38;
    }
    WCHAR generated[4] = {};
    const WCHAR* name = u32c_keynames::special(code);
    unsigned function = code >= 0x3b && code <= 0x44 ? code - 0x3a :
                        code == 0x57 ? 11 : code == 0x58 ? 12 :
                        code >= 0x7c && code <= 0x87 ? code - 0x7c + 13 : 0;
    if (!name && function) {
        generated[0] = L'F';
        if (function >= 10) { generated[1] = L'0' + function / 10; generated[2] = L'0' + function % 10; }
        else generated[1] = L'0' + function;
        name = generated;
    }
    if (name) {
        int length = 0;
        while (length < size - 1 && name[length]) { buffer[length] = name[length]; ++length; }
        buffer[length] = 0;
        return length;
    }
    // Wine's fallback maps the low scan byte, even for an unknown extended
    // scan code. A non-printable/unmapped code yields one NUL plus terminator.
    WCHAR ch = 0;
    for (const auto& row : u32c_keymap::rows)
        if (row.scan == (code & 0xff) && !row.ext) { ch = row.ch; break; }
    buffer[0] = size > 1 ? ch : 0;
    if (size > 1) buffer[1] = 0;
    return size > 1 ? 1 : 0;
}
