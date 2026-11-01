#include <initializer_list>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); exit(1); } } while (0)
int main() {
 HMODULE module=LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
 auto raw=reinterpret_cast<decltype(&GetRawInputData)>(GetProcAddress(module,"GetRawInputData")); CHECK(raw);
 auto info=reinterpret_cast<decltype(&GetRawInputDeviceInfoW)>(GetProcAddress(module,"GetRawInputDeviceInfoW")); CHECK(info);
 auto buffer=reinterpret_cast<decltype(&GetRawInputBuffer)>(GetProcAddress(module,"GetRawInputBuffer")); CHECK(buffer);
 unsigned checks=0;
 // No raw-input message has been dispatched: even NULL query buffers must
 // reject unknown handles before examining the command or header size.
 for(auto fn : {&GetRawInputData,raw})
 for(UINT command : {UINT(RID_INPUT),UINT(RID_HEADER),0xffffffffu})
 for(UINT header : {UINT(sizeof(RAWINPUTHEADER)),0u})
 for(bool output : {false,true}) {
  RAWINPUT data={}; UINT size=0x12345678;
  SetLastError(0xdead);
  CHECK(fn(reinterpret_cast<HRAWINPUT>(UINT_PTR(0x1234)),command,output?&data:nullptr,&size,header)==UINT(-1));
  CHECK(GetLastError()==ERROR_INVALID_HANDLE && size==0x12345678 && data.header.dwSize==0);
  ++checks;
 }
 for(auto fn : {&GetRawInputDeviceInfoW,info})
 for(UINT command : {UINT(RIDI_DEVICENAME),UINT(RIDI_DEVICEINFO),UINT(RIDI_PREPARSEDDATA),0xffffffffu})
 for(bool sizePresent : {false,true}) {
  UINT size=0x12345678; BYTE data[32]={}; SetLastError(0xdead);
  CHECK(fn(reinterpret_cast<HANDLE>(UINT_PTR(0x1234)),command,data,sizePresent?&size:nullptr)==UINT(-1));
  CHECK(GetLastError()==(!sizePresent?ERROR_NOACCESS:command==0xffffffffu?ERROR_INVALID_PARAMETER:ERROR_INVALID_HANDLE));
  CHECK(size==0x12345678 && data[0]==0); ++checks;
 }
 // A backend with no producer has no next record, so required bytes are zero.
 // Only test the backend here: desktop Wine may have real queued input.
 for(bool output : {false,true}) {
  RAWINPUT data={}; UINT size=sizeof(data); SetLastError(0xdead);
  CHECK(buffer(output?&data:nullptr,&size,sizeof(RAWINPUTHEADER))==0);
  CHECK(size==0 && GetLastError()==0xdead && data.header.dwSize==0); ++checks;
 }
 UINT size=123; SetLastError(0xdead);
 CHECK(buffer(nullptr,&size,0)==UINT(-1) && GetLastError()==ERROR_INVALID_PARAMETER && size==123);
 CHECK(buffer(nullptr,nullptr,sizeof(RAWINPUTHEADER))==UINT(-1) && GetLastError()==ERROR_INVALID_PARAMETER);
 printf("PASS: %u raw query contracts, invalid buffer arguments (off-device)\n",checks);
}
