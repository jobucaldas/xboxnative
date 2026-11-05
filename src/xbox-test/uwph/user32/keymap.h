#pragma once
// Standard IBM PC/AT scan-code-set-1 <-> US-layout virtual-key table (public
// hardware spec, not Wine source). Covers the subset item 44 needs: letters,
// digits, navigation, function keys, modifiers, numpad. Unknown codes fail
// honestly (0/FALSE) rather than fabricate a result.
namespace u32c_keymap {
struct Row { BYTE scan; bool ext; WORD vk; char ch; };
// ch==0 means "no printable character" for MAPVK_VK_TO_CHAR.
static const Row rows[] = {
 {0x01,false,VK_ESCAPE,0},{0x0E,false,VK_BACK,0},{0x0F,false,VK_TAB,0},{0x1C,false,VK_RETURN,0},
 {0x39,false,VK_SPACE,' '},{0x3A,false,VK_CAPITAL,0},{0x45,false,VK_NUMLOCK,0},{0x46,false,VK_SCROLL,0},
 {0x2A,false,VK_LSHIFT,0},{0x36,false,VK_RSHIFT,0},{0x1D,false,VK_LCONTROL,0},{0x1D,true,VK_RCONTROL,0},
 {0x38,false,VK_LMENU,0},{0x38,true,VK_RMENU,0},
 {0x48,true,VK_UP,0},{0x50,true,VK_DOWN,0},{0x4B,true,VK_LEFT,0},{0x4D,true,VK_RIGHT,0},
 {0x52,true,VK_INSERT,0},{0x53,true,VK_DELETE,0},{0x47,true,VK_HOME,0},{0x4F,true,VK_END,0},
 {0x49,true,VK_PRIOR,0},{0x51,true,VK_NEXT,0},{0x1C,true,VK_RETURN,0},{0x35,true,VK_DIVIDE,0},
 {0x02,false,'1','1'},{0x03,false,'2','2'},{0x04,false,'3','3'},{0x05,false,'4','4'},{0x06,false,'5','5'},
 {0x07,false,'6','6'},{0x08,false,'7','7'},{0x09,false,'8','8'},{0x0A,false,'9','9'},{0x0B,false,'0','0'},
 {0x0C,false,VK_OEM_MINUS,'-'},{0x0D,false,VK_OEM_PLUS,'='},
 {0x10,false,'Q','Q'},{0x11,false,'W','W'},{0x12,false,'E','E'},{0x13,false,'R','R'},{0x14,false,'T','T'},
 {0x15,false,'Y','Y'},{0x16,false,'U','U'},{0x17,false,'I','I'},{0x18,false,'O','O'},{0x19,false,'P','P'},
 {0x1A,false,VK_OEM_4,'['},{0x1B,false,VK_OEM_6,']'},
 {0x1E,false,'A','A'},{0x1F,false,'S','S'},{0x20,false,'D','D'},{0x21,false,'F','F'},{0x22,false,'G','G'},
 {0x23,false,'H','H'},{0x24,false,'J','J'},{0x25,false,'K','K'},{0x26,false,'L','L'},
 {0x27,false,VK_OEM_1,';'},{0x28,false,VK_OEM_7,'\''},{0x29,false,VK_OEM_3,'`'},{0x2B,false,VK_OEM_5,'\\'},
 {0x2C,false,'Z','Z'},{0x2D,false,'X','X'},{0x2E,false,'C','C'},{0x2F,false,'V','V'},{0x30,false,'B','B'},
 {0x31,false,'N','N'},{0x32,false,'M','M'},
 {0x33,false,VK_OEM_COMMA,','},{0x34,false,VK_OEM_PERIOD,'.'},{0x35,false,VK_OEM_2,'/'},
 {0x3B,false,VK_F1,0},{0x3C,false,VK_F2,0},{0x3D,false,VK_F3,0},{0x3E,false,VK_F4,0},{0x3F,false,VK_F5,0},
 {0x40,false,VK_F6,0},{0x41,false,VK_F7,0},{0x42,false,VK_F8,0},{0x43,false,VK_F9,0},{0x44,false,VK_F10,0},
 {0x57,false,VK_F11,0},{0x58,false,VK_F12,0},
 {0x47,false,VK_NUMPAD7,0},{0x48,false,VK_NUMPAD8,0},{0x49,false,VK_NUMPAD9,0},{0x4A,false,VK_SUBTRACT,0},
 {0x4B,false,VK_NUMPAD4,0},{0x4C,false,VK_NUMPAD5,0},{0x4D,false,VK_NUMPAD6,0},{0x4E,false,VK_ADD,0},
 {0x4F,false,VK_NUMPAD1,0},{0x50,false,VK_NUMPAD2,0},{0x51,false,VK_NUMPAD3,0},{0x52,false,VK_NUMPAD0,0},
 {0x53,false,VK_DECIMAL,0},{0x37,false,VK_MULTIPLY,0},
};
static constexpr unsigned count = sizeof(rows)/sizeof(rows[0]);
// Windows' non-EX scan<->vk mapping never differentiates left/right pairs.
inline WORD generic(WORD vk) {
 switch (vk) { case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
  case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
  case VK_LMENU: case VK_RMENU: return VK_MENU; default: return vk; }
}
}
