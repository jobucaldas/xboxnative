#pragma once
#include "wsprintf.h"
// Shared by launcher and host MLP probes. Never changes the CoreWindow adapter.
namespace user32_core {
inline uintptr_t resolve(const char* dll,const char* name) {
 if(!name||_stricmp(dll,"user32.dll"))return 0;
 if(!strcmp(name,"wsprintfA")||!strcmp(name,"wsprintfW")||!strcmp(name,"wvsprintfA")||!strcmp(name,"wvsprintfW")) {
  static HMODULE native=LoadLibraryExW(L"user32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(native)if(auto address=GetProcAddress(native,name))return (uintptr_t)address;
  if(!strcmp(name,"wsprintfA"))return (uintptr_t)&play_wsprintf::wsprintfA;
  if(!strcmp(name,"wsprintfW"))return (uintptr_t)&play_wsprintf::wsprintfW;
  if(!strcmp(name,"wvsprintfA"))return (uintptr_t)&play_wsprintf::wvsprintfA;
  return (uintptr_t)&play_wsprintf::wvsprintfW;
 }
 static HMODULE module=LoadPackagedLibrary(L"uwph-u32c.dll",0);
 return module?(uintptr_t)GetProcAddress(module,name):0;
}
}
