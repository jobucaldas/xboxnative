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
// The gate's log tee: the PE reports each failing check by name through
// OutputDebugStringA, which the mapper routes to the log it was given, so the
// log passed to wxl::run is this function: it records the failing expressions
// for the caller's verdict and forwards every line to the caller's sink.
inline std::string*& gate_detail(){static thread_local std::string* p=nullptr;return p;}
inline wxl::Log& gate_forward(){static thread_local wxl::Log f=nullptr;return f;}
inline void gate_log(const char* s){
 static const char marker[]="U32C CHECK FAIL ";
 auto detail=gate_detail();
 if(detail&&!strncmp(s,marker,sizeof(marker)-1)){
  if(!detail->empty())*detail+="; ";
  std::string row=s+sizeof(marker)-1;
  if(row.size()>120)row.resize(120);
  *detail+=row;
 }
 if(gate_forward())gate_forward()(s);
}
// Gates the backend once per launcher process, like the donor probe: the test
// PE registers classes and windows in the same record core the games use, so a
// second run (the next Play after a failed launch) would see its own leftovers.
// The PE itself is re-entrant (it resets its counters and unregisters every
// class it registers), so only a PASS is cached here; a failed run is retried.
// `detail`, when given, receives the PE's own failing check expressions, so a
// caller that reports a verdict can name the stage that failed instead of
// leaving the reader with a bare exit code.
inline bool test(wxl::Log log, std::string* detail=nullptr) {
 static bool passed=false;
 if(passed){log("U32C own mapped test passed earlier in this launcher process");return true;}
 static const char* imports[]={
#include "megabonk-user32.h"
 };
 unsigned implemented=0;
 for(auto name:imports){bool found=resolve("user32.dll",name)!=0;implemented+=found;std::string line=std::string("U32C user32 ")+name+(found?" implemented":" missing");log(line.c_str());}
 char summary[160];snprintf(summary,sizeof(summary),"U32C user32 total=%u implemented=%u missing=%u (complete direct+delay PE set)",unsigned(sizeof(imports)/sizeof(*imports)),implemented,unsigned(sizeof(imports)/sizeof(*imports))-implemented);log(summary);
 if(unsigned(sizeof(imports)/sizeof(*imports))-implemented>15){log("U32C FAIL missing-import gate");return false;}

 WCHAR path[2048];DWORD n=GetModuleFileNameW(nullptr,path,2048);
 if(!n||n>=2048){log("U32C FAIL package path");return false;}
 std::wstring p(path);auto slash=p.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
 p.resize(slash+1);p+=L"u32c-test.exe";
 wxl::Image image;
 // The PE reports each failing check by name through OutputDebugStringA, which
 // the mapper routes to the log it was given. When the caller wants a verdict
 // detail, the gate's log is gate_log: it records those lines and forwards all
 // of them, so the caller's own log sees exactly what it saw before.
 std::string failures;
 if(detail){gate_detail()=&failures;gate_forward()=log;}
 wxl::Log gate=detail?&gate_log:log;
 // wxl::load installs the log it is given as the module-wide logger, and the
 // guest's OutputDebugStringA reaches that logger, so the tee has to be in
 // place for the entry call too. Put the caller's own log back afterwards so
 // nothing that runs later inherits this gate's tee.
 auto saved=wxl::logger;
 if(!wxl::run(p.c_str(),gate,&image,test_resolve)){wxl::logger=saved;gate_detail()=nullptr;gate_forward()=nullptr;log("U32C FAIL mapping own test PE");return false;}
 if(!image.entry||!image.valid(image.entry,1)){wxl::logger=saved;gate_detail()=nullptr;gate_forward()=nullptr;log("U32C FAIL test entry");return false;}
 auto entry=(DWORD(WINAPI*)())(image.base+image.entry);
 wxl::logger=gate;
 DWORD result=entry();
 wxl::logger=saved;
 gate_detail()=nullptr;
 gate_forward()=nullptr;
 char text[120];snprintf(text,sizeof(text),"U32C own mapped test entry=%lu; game entries remain uncalled",result);log(text);
 passed=result==42;
 if(detail)*detail=passed?std::string():failures;
 return passed;
}
}
