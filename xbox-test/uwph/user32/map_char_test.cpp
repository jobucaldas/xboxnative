#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto call=reinterpret_cast<decltype(&MapVirtualKeyW)>(GetProcAddress(module,"MapVirtualKeyW")); CHECK(call);
 auto ex=reinterpret_cast<decltype(&MapVirtualKeyExA)>(GetProcAddress(module,"MapVirtualKeyExA")); CHECK(ex);
 HKL layout=LoadKeyboardLayoutW(L"00000409",0); CHECK(layout);
 unsigned checks=0;
 for(UINT vk : {UINT(VK_BACK),UINT(VK_TAB),UINT(VK_RETURN),UINT(VK_ESCAPE),
     UINT(VK_NUMPAD0),UINT(VK_NUMPAD1),UINT(VK_NUMPAD2),UINT(VK_NUMPAD3),UINT(VK_NUMPAD4),
     UINT(VK_NUMPAD5),UINT(VK_NUMPAD6),UINT(VK_NUMPAD7),UINT(VK_NUMPAD8),UINT(VK_NUMPAD9),
     UINT(VK_MULTIPLY),UINT(VK_ADD),UINT(VK_SUBTRACT),UINT(VK_DECIMAL),UINT(VK_DIVIDE),
     UINT('A'),UINT('Z'),UINT('0'),UINT(VK_OEM_MINUS),UINT(VK_SPACE),UINT(VK_F1),UINT(0xffff)}) {
  UINT expected=MapVirtualKeyExW(vk,MAPVK_VK_TO_CHAR,layout);
  SetLastError(0xdead);
  UINT actual=call(vk,MAPVK_VK_TO_CHAR);
  if(actual!=expected) printf("vk=%u expected=%04x actual=%04x\n",vk,expected,actual);
  CHECK(actual==expected); CHECK(GetLastError()==0xdead);
  CHECK(ex(vk,MAPVK_VK_TO_CHAR,layout)==expected); CHECK(GetLastError()==0xdead);
  ++checks;
 }
 // Character mapping must not alter the scan-code mapping modes.
 CHECK(call(VK_BACK,MAPVK_VK_TO_VSC)==0x0e);
 CHECK(call(0x0e,MAPVK_VSC_TO_VK)==VK_BACK);
 CHECK(call(VK_NUMPAD0,MAPVK_VK_TO_VSC)==0x52);
 CHECK(call(0x52,MAPVK_VSC_TO_VK_EX)==VK_NUMPAD0);
 printf("PASS: %u US virtual-key character mappings, two exports, scan-mode guards (off-device)\n",checks);
}
