// Off-device only. Desktop VM APIs test mapper logic; console checks real FromApp.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
int main(int argc,char**argv){
 core=LoadLibraryW(L"uwph-u32c.dll");if(!core){printf("load error=%lu\n",GetLastError());return 1;}
 // The launcher's UI thread publishes the pad slot mask every frame before the
 // pre-launch gate maps this PE, so the gate also has to pass with pads
 // connected. `--pads <mask>` reproduces that state off-device: publish first,
 // then map and call the test exactly as the gate does.
 if(argc==3&&!strcmp(argv[1],"--pads")){
  auto publish=(void(WINAPI*)(unsigned))GetProcAddress(core,"uwph_u32c_pads");
  if(!publish){puts("no uwph_u32c_pads export");return 1;}
  publish((unsigned)strtoul(argv[2],nullptr,0));
  printf("U32C OFFDEVICE published pads=%s\n",argv[2]);
 }
 // The gate is re-entrant: the launcher calls it again in the same process when
 // the first run failed (probe.h only caches a PASS) or when a self-test step
 // follows a PE32 fixture, and the UI thread keeps publishing pads throughout.
 // `--twice` maps the PE once, publishes a pad, calls the entry twice with a
 // different pad mask published in between, and requires both to return 42.
 if(argc==2&&!strcmp(argv[1],"--twice")){
  auto publish=(void(WINAPI*)(unsigned))GetProcAddress(core,"uwph_u32c_pads");
  if(!publish){puts("no uwph_u32c_pads export");return 1;}
  wxl::Image image;if(!wxl::run(L"u32c-test.exe",sink,&image,resolve))return 1;
  auto entry=(DWORD(WINAPI*)())(image.base+image.entry);
  publish(1);
  DWORD first=entry();
  printf("U32C OFFDEVICE entry1=%lu\n",first);
  publish(5);
  DWORD second=entry();
  printf("U32C OFFDEVICE entry2=%lu\n",second);
  return first==42&&second==42?0:1;
 }
 wxl::Image image;if(!wxl::run(L"u32c-test.exe",sink,&image,resolve))return 1;DWORD r=((DWORD(WINAPI*)())(image.base+image.entry))();printf("U32C OFFDEVICE entry=%lu\n",r);return r==42?0:1;}
