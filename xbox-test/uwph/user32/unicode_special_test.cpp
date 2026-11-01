#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto translate=reinterpret_cast<decltype(&ToUnicode)>(GetProcAddress(module,"ToUnicode")); CHECK(translate);
 HKL layout=LoadKeyboardLayoutW(L"00000409",0); CHECK(layout);
 unsigned checks=0;
 for(UINT vk : {UINT(VK_BACK),UINT(VK_TAB),UINT(VK_RETURN),UINT(VK_ESCAPE)}) for(unsigned shift=0;shift<2;++shift)
 for(unsigned alt=0;alt<2;++alt) for(unsigned caps=0;caps<2;++caps) for(int capacity=1;capacity<=2;++capacity) {
  BYTE state[256]={}; state[VK_SHIFT]=shift?0x80:0; state[VK_CAPITAL]=BYTE(caps); state[VK_CONTROL]=0; state[VK_MENU]=alt?0x80:0;
  WCHAR expected[3]={0xaaaa,0xbbbb,0xcccc}, actual[3]={0xaaaa,0xbbbb,0xcccc};
  int result=ToUnicodeEx(vk,MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,layout),state,expected,capacity,0,layout);
  CHECK(result==1);
  SetLastError(0xdead);
  int got=translate(vk,0,state,actual,capacity,0);
  if(got!=result || (got && actual[0]!=expected[0])) printf("vk=%u shift=%u alt=%u caps=%u capacity=%d expected=%d/%04x got=%d/%04x\n",vk,shift,alt,caps,capacity,result,expected[0],got,actual[0]);
  CHECK(got==result);
  CHECK(actual[0]==(result?expected[0]:0xaaaa));
  CHECK(actual[capacity]==(capacity==1?0xbbbb:0xcccc));
  CHECK(GetLastError()==0xdead); ++checks;
 }
 printf("PASS: %u US special-key Shift/Caps/Alt translations (off-device)\n",checks);
}
