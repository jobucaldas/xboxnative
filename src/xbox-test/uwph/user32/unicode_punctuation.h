#pragma once
namespace u32c_unicode {
// Fixed US keyboard Shift columns, matching Wine 10 win32u/input.c.
// Caps Lock affects letters only. Ctrl/Alt and dead keys remain unsupported.
inline char punctuation_case(char ch, const BYTE* state) {
    if (!state || !(state[VK_SHIFT] & 0x80)) return ch;
    // Use character pairs to keep OEM keys independent of their virtual values.
    switch (ch) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case ';': return ':';
    case '\'': return '"';
    case '`': return '~';
    case '\\': return '|';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default: return ch;
    }
}
}
