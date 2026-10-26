// Off-device only. Desktop VM APIs test mapper logic; console checks real FromApp.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
#include "../../app/host/wxl_mapper.h"
static HMODULE core;
static void sink(const char* s){puts(s);}
static uintptr_t resolve(const char* dll,const char* name,WORD){
 if(!name)return 0;
 if(!_stricmp(dll,"user32.dll")||!_stricmp(dll,"uwph-u32c.dll"))return (uintptr_t)GetProcAddress(core,name);
 if(!_stricmp(dll,"kernel32.dll")){
  if(!strcmp(name,"OutputDebugStringA"))return (uintptr_t)&sink;
  return (uintptr_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name);
 }
 return 0;
}
int main(){core=LoadLibraryW(L"uwph-u32c.dll");if(!core){printf("load error=%lu\n",GetLastError());return 1;}wxl::Image image;if(!wxl::run(L"u32c-test.exe",sink,&image,resolve))return 1;DWORD r=((DWORD(WINAPI*)())(image.base+image.entry))();printf("U32C OFFDEVICE entry=%lu\n",r);return r==42?0:1;}
