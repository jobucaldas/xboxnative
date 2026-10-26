#pragma once
// Execute ONLY our CRT-free test PE. This does not initialize any game module.
namespace user32_core {
static uintptr_t test_resolve(const char* dll,const char* name,WORD ordinal) {
 if(auto p=resolve(dll,name))return p;
 if(name&&!_stricmp(dll,"uwph-u32c.dll")){static HMODULE module=LoadPackagedLibrary(L"uwph-u32c.dll",0);return module?(uintptr_t)GetProcAddress(module,name):0;}
 if(name&&!_stricmp(dll,"kernel32.dll")) {
  if(!strcmp(name,"OutputDebugStringA"))return (uintptr_t)&wxl::debug_string;
  static HMODULE base=LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(base)return (uintptr_t)GetProcAddress(base,name);
 }
 return 0;
}
inline bool test(wxl::Log log) {
 static const char* imports[]={
#include "megabonk-user32.h"
 };
 unsigned implemented=0;
 for(auto name:imports){bool found=resolve("user32.dll",name)!=0;implemented+=found;std::string line=std::string("U32C user32 ")+name+(found?" implemented":" missing");log(line.c_str());}
 char summary[160];snprintf(summary,sizeof(summary),"U32C user32 total=%u implemented=%u missing=%u (complete direct+delay PE set)",unsigned(sizeof(imports)/sizeof(*imports)),implemented,unsigned(sizeof(imports)/sizeof(*imports))-implemented);log(summary);
 if(unsigned(sizeof(imports)/sizeof(*imports))-implemented>91){log("U32C FAIL missing-import gate");return false;}

 WCHAR path[2048];DWORD n=GetModuleFileNameW(nullptr,path,2048);
 if(!n||n>=2048){log("U32C FAIL package path");return false;}
 std::wstring p(path);auto slash=p.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
 p.resize(slash+1);p+=L"u32c-test.exe";
 wxl::Image image;
 if(!wxl::run(p.c_str(),log,&image,test_resolve)){log("U32C FAIL mapping own test PE");return false;}
 if(!image.entry||!image.valid(image.entry,1)){log("U32C FAIL test entry");return false;}
 auto entry=(DWORD(WINAPI*)())(image.base+image.entry);
 DWORD result=entry();
 char text[120];snprintf(text,sizeof(text),"U32C own mapped test entry=%lu; game entries remain uncalled",result);log(text);
 return result==42;
}
}
