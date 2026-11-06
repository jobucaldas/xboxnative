#pragma once
namespace u32c_map_char {
// Wine win32u/input.c NtUserMapVirtualKeyEx uses the unmodified US
// keyboard-table character, independent of current Num Lock/Shift state.
// Keep this separate from ToUnicode, where keypad translation is stateful.
inline UINT extra_character(UINT vk) {
 switch(vk) {
 case VK_BACK:return '\b';
 case VK_TAB:return '\t';
 case VK_RETURN:return '\r';
 case VK_ESCAPE:return 0x1b;
 case VK_MULTIPLY:return '*';
 case VK_ADD:return '+';
 case VK_SUBTRACT:return '-';
 case VK_DECIMAL:return '.';
 case VK_DIVIDE:return '/';
 default:return 0;
 }
}
}
