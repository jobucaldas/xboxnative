// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device checks for uwph-u32c.dll: nonclient geometry and object/system
// queries against host Wine user32; keyboard translation against fixed US
// layout values. Host theme metrics may differ from the backend's classic
// 96 DPI values, so geometry is compared by delta or against fixed values.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static HMODULE module;
template<class F> static F api(const char* name) {
 auto f = reinterpret_cast<F>(GetProcAddress(module, name)); CHECK(f); return f;
}

static unsigned metrics() {
 auto metric = api<decltype(&GetSystemMetrics)>("GetSystemMetrics");
 auto adjust = api<decltype(&AdjustWindowRectEx)>("AdjustWindowRectEx");
 unsigned n = 0;
 for (int index : {SM_CXBORDER, SM_CYBORDER, SM_CXDLGFRAME, SM_CYDLGFRAME, SM_CXEDGE, SM_CYEDGE}) {
  SetLastError(0xdead); CHECK(metric(index) == GetSystemMetrics(index)); CHECK(GetLastError() == 0xdead); ++n;
 }
 for (int index : {-1, 0x7fffffff}) { SetLastError(0xdead); CHECK(metric(index) == 0); CHECK(GetLastError() == 0xdead); ++n; }
 struct M { int index, value; };
 for (M m : {M{SM_CXFRAME, 4}, M{SM_CYFRAME, 4}, M{SM_CYCAPTION, 19}, M{SM_CYSMCAPTION, 17}, M{SM_CYMENU, 20}}) {
  CHECK(metric(m.index) == m.value); ++n;
 }
 // Each implementation's AdjustWindowRectEx must agree with its own metrics.
 for (int backend = 0; backend < 2; ++backend) {
  auto m = backend ? metric : &GetSystemMetrics;
  auto a = backend ? adjust : &AdjustWindowRectEx;
  for (DWORD style : {DWORD(WS_BORDER), DWORD(WS_DLGFRAME), DWORD(WS_CAPTION), DWORD(WS_CAPTION | WS_THICKFRAME)})
  for (DWORD ex : {DWORD(0), DWORD(WS_EX_TOOLWINDOW), DWORD(WS_EX_CLIENTEDGE)})
  for (BOOL menu : {FALSE, TRUE}) {
   RECT r{0, 0, 100, 100}; CHECK(a(&r, style, menu, ex));
   int frame = (style & WS_THICKFRAME) ? m(SM_CXFRAME) : ((style & WS_DLGFRAME) ? m(SM_CXDLGFRAME) : m(SM_CXBORDER));
   if (ex & WS_EX_CLIENTEDGE) frame += m(SM_CXEDGE);
   int caption = ((style & WS_CAPTION) == WS_CAPTION) ? m((ex & WS_EX_TOOLWINDOW) ? SM_CYSMCAPTION : SM_CYCAPTION) : 0;
   CHECK(r.left == -frame && r.right == 100 + frame && r.bottom == 100 + frame);
   CHECK(r.top == -frame - caption - (menu ? m(SM_CYMENU) : 0)); ++n;
  }
 }
 // WS_BORDER or WS_DLGFRAME alone stays symmetric: only WS_CAPTION adds a caption.
 for (DWORD border : {DWORD(0), DWORD(WS_BORDER), DWORD(WS_DLGFRAME), DWORD(WS_CAPTION)}) {
  RECT r{10, 20, 110, 120}; CHECK(adjust(&r, border, FALSE, 0));
  CHECK((20 - r.top) - (10 - r.left) == (border == WS_CAPTION ? 19 : 0)); ++n;
 }
 // WS_MINIMIZE and WS_EX_STATICEDGE change geometry exactly as in Wine.
 for (DWORD style : {DWORD(0), DWORD(WS_BORDER), DWORD(WS_DLGFRAME), DWORD(WS_CAPTION), DWORD(WS_THICKFRAME), DWORD(WS_CAPTION | WS_THICKFRAME)})
 for (DWORD ex : {DWORD(0), DWORD(WS_EX_DLGMODALFRAME), DWORD(WS_EX_CLIENTEDGE)})
 for (BOOL menu : {FALSE, TRUE})
 for (int variant = 0; variant < 2; ++variant) {
  DWORD s2 = variant ? style : style | WS_MINIMIZE, e2 = variant ? ex | WS_EX_STATICEDGE : ex;
  RECT base{10, 20, 110, 120}, changed = base, ref = base, ref_changed = base;
  CHECK(adjust(&base, style, menu, ex)); CHECK(adjust(&changed, s2, menu, e2));
  CHECK(AdjustWindowRectEx(&ref, style, menu, ex)); CHECK(AdjustWindowRectEx(&ref_changed, s2, menu, e2));
  CHECK(changed.left - base.left == ref_changed.left - ref.left);
  CHECK(changed.top - base.top == ref_changed.top - ref.top);
  CHECK(changed.right - base.right == ref_changed.right - ref.right);
  CHECK(changed.bottom - base.bottom == ref_changed.bottom - ref.bottom); ++n;
 }
 return n;
}

static unsigned objects() {
 auto query = api<decltype(&GetUserObjectInformationA)>("GetUserObjectInformationA");
 auto desktop = api<decltype(&GetThreadDesktop)>("GetThreadDesktop");
 HANDLE guest = desktop(GetCurrentThreadId()), host = GetThreadDesktop(GetCurrentThreadId());
 unsigned n = 0;
 for (DWORD len = 0; len <= 32; ++len) {
  unsigned char actual[40], expected[40]; memset(actual, 0xa5, 40); memset(expected, 0xa5, 40);
  DWORD need = 0, ref = 0; SetLastError(0xdead);
  BOOL want = GetUserObjectInformationA(host, UOI_FLAGS, expected, len, &ref);
  DWORD error = GetLastError(); SetLastError(0xdead);
  CHECK(query(guest, UOI_FLAGS, actual, len, &need) == want);
  CHECK(need == ref && need == sizeof(USEROBJECTFLAGS));
  CHECK(GetLastError() == error); CHECK(!memcmp(actual, expected, 40)); ++n;
 }
 for (int index : {UOI_NAME, UOI_TYPE}) for (DWORD len = 0; len <= 24; ++len) for (bool null_out : {false, true}) {
  unsigned char actual[40], expected[40]; memset(actual, 0xa5, 40); memset(expected, 0xa5, 40);
  DWORD need = 0, ref = 0; SetLastError(0xdead);
  BOOL want = GetUserObjectInformationA(host, index, null_out ? nullptr : expected, len, &ref);
  DWORD error = GetLastError(); SetLastError(0xdead);
  CHECK(query(guest, index, null_out ? nullptr : actual, len, &need) == want);
  CHECK(need == ref && GetLastError() == error && !memcmp(actual, expected, 40)); ++n;
 }
 DWORD needed = 0; SetLastError(0xdead);
 CHECK(!query(guest, UOI_FLAGS, nullptr, 12, &needed) && GetLastError() == ERROR_NOACCESS && needed == 12);
 SetLastError(0xdead); CHECK(!query(reinterpret_cast<HANDLE>(1), UOI_FLAGS, nullptr, 0, &needed));
 CHECK(GetLastError() == ERROR_INVALID_HANDLE);
 return n;
}

static unsigned parameters() {
 auto query = api<decltype(&SystemParametersInfoW)>("SystemParametersInfoW");
 const struct { UINT action, value; } cases[] = {
  {SPI_GETKEYBOARDDELAY, 1}, {SPI_GETKEYBOARDSPEED, 31}, {SPI_GETMOUSEHOVERWIDTH, 4}, {SPI_GETMOUSEHOVERHEIGHT, 4},
  {SPI_GETMOUSEHOVERTIME, 400}, {SPI_GETMOUSESPEED, 10}, {SPI_GETWHEELSCROLLLINES, 3}, {SPI_GETWHEELSCROLLCHARS, 3}};
 unsigned n = 0;
 for (auto c : cases) {
  struct { UINT before, value, after; } out = {0x11223344, 0, 0x55667788};
  SetLastError(0xdead); CHECK(SystemParametersInfoW(c.action, 0, &out.value, 0)); CHECK(GetLastError() == 0xdead);
  SetLastError(0xdead); CHECK(query(c.action, 123, &out.value, 0));
  CHECK(out.value == c.value && GetLastError() == 0xdead && out.before == 0x11223344 && out.after == 0x55667788);
  SetLastError(0xdead); CHECK(!SystemParametersInfoW(c.action, 0, nullptr, 0)); CHECK(GetLastError() == 0xdead);
  SetLastError(0xdead); CHECK(!query(c.action, 0, nullptr, 0)); CHECK(GetLastError() == 0xdead); ++n;
 }
 UINT value = 0;
 SetLastError(0xdead); CHECK(!query(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(15), 0)); CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
 CHECK(query(SPI_GETMOUSESPEED, 0, &value, 0) && value == 10);
 return n;
}

// Fixed US layout expectations (Windows kbdus, CAPLOK letters). Host Wine is
// not a usable oracle here: with a display it follows the host X keymap, and
// its built-in table ignores Shift under Caps Lock.
static void expect(UINT vk, BYTE shift, BYTE ctrl, BYTE alt, BYTE caps, int result, WCHAR ch) {
 static auto translate = api<decltype(&ToUnicode)>("ToUnicode");
 for (int capacity = 1; capacity <= 2; ++capacity) {
  BYTE state[256] = {}; state[VK_SHIFT] = shift; state[VK_CONTROL] = ctrl; state[VK_MENU] = alt; state[VK_CAPITAL] = caps;
  WCHAR out[3] = {0xaaaa, 0xbbbb, 0xcccc};
  SetLastError(0xdead);
  int got = translate(vk, 0, state, out, capacity, 0);
  if (got != result || (result > 0 && out[0] != ch))
   printf("vk=%u shift=%u ctrl=%u alt=%u caps=%u: want %d/%04x, got %d/%04x\n", vk, shift, ctrl, alt, caps, result, ch, got, out[0]);
  CHECK(got == result); CHECK(result > 0 ? out[0] == ch : out[0] == 0xaaaa);
  CHECK(out[capacity] == (capacity == 1 ? 0xbbbb : 0xcccc)); CHECK(GetLastError() == 0xdead);
 }
}

static unsigned keyboard() {
 unsigned n = 0;
 const struct { UINT vk; char plain, shifted; } keys[] = {
  {'0','0',')'},{'1','1','!'},{'2','2','@'},{'3','3','#'},{'4','4','$'},{'5','5','%'},{'6','6','^'},{'7','7','&'},
  {'8','8','*'},{'9','9','('},{VK_OEM_MINUS,'-','_'},{VK_OEM_PLUS,'=','+'},{VK_OEM_4,'[','{'},{VK_OEM_6,']','}'},
  {VK_OEM_1,';',':'},{VK_OEM_7,'\'','"'},{VK_OEM_3,'`','~'},{VK_OEM_5,'\\','|'},{VK_OEM_COMMA,',','<'},
  {VK_OEM_PERIOD,'.','>'},{VK_OEM_2,'/','?'},{VK_SPACE,' ',' '}};
 for (BYTE shift : {0, 0x80}) for (BYTE caps : {0, 1}) {
  for (UINT vk = 'A'; vk <= 'Z'; ++vk) {
   bool upper = (shift != 0) != (caps != 0);
   expect(vk, shift, 0, 0, caps, 1, WCHAR(upper ? vk : vk + 32)); // Caps Lock inverts Shift
   expect(vk, shift, 0x80, 0, caps, 1, WCHAR(vk - 'A' + 1));      // Ctrl letters: control codes
   expect(vk, shift, 0x80, 0x80, caps, 0, 0);                     // Ctrl+Alt: no character
   n += 3;
  }
  for (auto k : keys) { expect(k.vk, shift, 0, 0, caps, 1, WCHAR(shift ? k.shifted : k.plain)); ++n; }
  const struct { UINT vk; WCHAR ch; } fixed[] = {{VK_MULTIPLY,'*'},{VK_ADD,'+'},{VK_SUBTRACT,'-'},{VK_DIVIDE,'/'},
   {VK_BACK,'\b'},{VK_TAB,'\t'},{VK_RETURN,'\r'},{VK_ESCAPE,0x1b}};
  for (auto f : fixed) for (BYTE alt : {0, 0x80}) {
   if (alt && f.vk <= VK_DIVIDE && f.vk >= VK_MULTIPLY) continue; // Alt keypad operators: not claimed
   expect(f.vk, shift, 0, alt, caps, 1, f.ch); ++n;
  }
  expect(VK_F1, shift, 0, 0, caps, 0, 0); ++n;
 }
 auto map = api<decltype(&MapVirtualKeyW)>("MapVirtualKeyW");
 const struct { UINT vk, ch; } chars[] = {{VK_BACK,'\b'},{VK_TAB,'\t'},{VK_RETURN,'\r'},{VK_ESCAPE,0x1b},{VK_MULTIPLY,'*'},
  {VK_ADD,'+'},{VK_SUBTRACT,'-'},{VK_DECIMAL,'.'},{VK_DIVIDE,'/'},{'A','A'},{'0','0'},{VK_OEM_MINUS,'-'},{VK_SPACE,' '},
  {VK_F1,0},{0xffff,0}};
 for (auto c : chars) { SetLastError(0xdead); CHECK(map(c.vk, MAPVK_VK_TO_CHAR) == c.ch); CHECK(GetLastError() == 0xdead); ++n; }
 CHECK(map(VK_BACK, MAPVK_VK_TO_VSC) == 0x0e && map(0x0e, MAPVK_VSC_TO_VK) == VK_BACK);
 CHECK(map(VK_NUMPAD0, MAPVK_VK_TO_VSC) == 0x52 && map(0x52, MAPVK_VSC_TO_VK_EX) == VK_NUMPAD0);
 // Lock keys latch the toggle bit on a fresh press only; repeats and releases keep it.
 auto key = api<void (WINAPI*)(int, BOOL)>("uwph_u32c_key_event");
 auto state = api<SHORT (WINAPI*)(int)>("GetKeyState");
 for (int vk : {VK_CAPITAL, VK_NUMLOCK, VK_SCROLL}) {
  CHECK(state(vk) == 0);
  for (int cycle = 0; cycle < 4; ++cycle) {
   SHORT toggle = SHORT((cycle + 1) & 1);
   key(vk, TRUE); CHECK(state(vk) == SHORT(0x8000 | toggle));
   key(vk, TRUE); CHECK(state(vk) == SHORT(0x8000 | toggle));
   key(vk, FALSE); CHECK(state(vk) == toggle); ++n;
  }
 }
 key('A', TRUE); CHECK(state('A') == SHORT(0x8000)); key('A', FALSE); CHECK(state('A') == 0);
 return n;
}

// LoadStringW reads string tables (parity_strings.rc, linked into this test)
// from a module's resource directory; GetKeyboardType reports one keyboard.
static unsigned resources() {
 auto load = api<decltype(&LoadStringW)>("LoadStringW");
 auto type = api<decltype(&GetKeyboardType)>("GetKeyboardType");
 HINSTANCE self = GetModuleHandleW(nullptr); unsigned n = 0;
 for (UINT id : {1u, 2u, 3u, 16u, 17u, 31u, 4000u}) for (int max : {64, 4}) {   // (max 1: Wine leaves the buffer as is, Windows writes "")
  WCHAR a[64], b[64]; wmemset(a, L'x', 64); wmemset(b, L'x', 64);
  int ra = load(self, id, a, max), rb = LoadStringW(self, id, b, max);
  CHECK(ra == rb); CHECK(!memcmp(a, b, sizeof a)); ++n;
 }
 for (UINT id : {1u, 3u, 31u}) {   // max 0: a read-only pointer into the table
  const WCHAR *a = nullptr, *b = nullptr;
  int ra = load(self, id, (LPWSTR)&a, 0), rb = LoadStringW(self, id, (LPWSTR)&b, 0);
  CHECK(ra == rb && a == b && (id == 3 ? !ra : ra > 0)); ++n;
 }
 for (int t : {0, 1, 2}) { CHECK(type(t) == GetKeyboardType(t)); ++n; }
 return n;
}
int main() {
 module = LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 unsigned geometry = metrics(), object = objects(), parameter = parameters(), keys = keyboard(), strings = resources();
 printf("PASS: Wine parity: %u metrics/geometry, %u object queries, %u parameters, %u keyboard checks, %u string/keyboard-type checks (off-device)\n",
        geometry, object, parameter, keys, strings);
}
