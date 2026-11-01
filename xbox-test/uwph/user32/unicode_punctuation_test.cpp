#include <windows.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto translate=reinterpret_cast<decltype(&ToUnicode)>(GetProcAddress(module,"ToUnicode")); CHECK(translate);
 HKL layout=LoadKeyboardLayoutW(L"00000409",0); CHECK(layout);
 unsigned checks=0;
 const UINT keys[]={'0','1','2','3','4','5','6','7','8','9',VK_OEM_MINUS,VK_OEM_PLUS,VK_OEM_4,VK_OEM_6,VK_OEM_1,VK_OEM_7,VK_OEM_3,VK_OEM_5,VK_OEM_COMMA,VK_OEM_PERIOD,VK_OEM_2,VK_SPACE};
 for(UINT vk:keys) for(unsigned shift=0;shift<2;++shift)
 for(unsigned caps=0;caps<2;++caps) for(int capacity=1;capacity<=2;++capacity) {
  BYTE state[256]={}; state[VK_SHIFT]=shift?0x80:0; state[VK_CAPITAL]=BYTE(caps);
  WCHAR expected[3]={0xaaaa,0xbbbb,0xcccc}, actual[3]={0xaaaa,0xbbbb,0xcccc};
  CHECK(ToUnicodeEx(vk,MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,layout),state,expected,capacity,0,layout)==1);
  SetLastError(0xdead);
  CHECK(translate(vk,0,state,actual,capacity,0)==1);
  CHECK(actual[0]==expected[0] && actual[capacity]==(capacity==1?0xbbbb:0xcccc));
  CHECK(GetLastError()==0xdead); ++checks;
 }
 printf("PASS: %u US punctuation Shift/Caps Lock translations (off-device)\n",checks);
}
