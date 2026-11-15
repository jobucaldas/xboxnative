// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(SYNC_DECLARATIONS_ONLY)
#include <windows.h>
#include <synchapi.h>
#include <type_traits>
using CreateEventFn = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCSTR);
using CreateEventWFn = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR);
using OpenEventWFn = HANDLE (WINAPI*)(DWORD,BOOL,LPCWSTR);
using SetStdHandleFn = BOOL (WINAPI*)(DWORD,HANDLE);
using GetStdHandleFn = HANDLE (WINAPI*)(DWORD);
using GetConsoleModeFn = BOOL (WINAPI*)(HANDLE,LPDWORD);
using SetConsoleModeFn = BOOL (WINAPI*)(HANDLE,DWORD);
using CreateMutexFn = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES,BOOL,LPCSTR);
using CreateSemaphoreFn = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES,LONG,LONG,LPCSTR);
using OpenEventFn = HANDLE (WINAPI*)(DWORD,BOOL,LPCSTR);
using SetEventFn = BOOL (WINAPI*)(HANDLE);
using ResetEventFn = SetEventFn;
using PulseEventFn = SetEventFn;
using ReleaseMutexFn = SetEventFn;
using ReleaseSemaphoreFn = BOOL (WINAPI*)(HANDLE,LONG,LPLONG);
using WaitFn = DWORD (WINAPI*)(HANDLE,DWORD);
static_assert(std::is_same<decltype(&CreateEventA),CreateEventFn>::value);
static_assert(std::is_same<decltype(&CreateEventW),CreateEventWFn>::value);
static_assert(std::is_same<decltype(&OpenEventW),OpenEventWFn>::value);
static_assert(std::is_same<decltype(&GetStdHandle),GetStdHandleFn>::value);
static_assert(std::is_same<decltype(&SetStdHandle),SetStdHandleFn>::value);
static_assert(std::is_same<decltype(&GetConsoleMode),GetConsoleModeFn>::value);
static_assert(std::is_same<decltype(&SetConsoleMode),SetConsoleModeFn>::value);
static_assert(std::is_same<decltype(&CreateMutexA),CreateMutexFn>::value);
static_assert(std::is_same<decltype(&CreateSemaphoreA),CreateSemaphoreFn>::value);
static_assert(std::is_same<decltype(&OpenEventA),OpenEventFn>::value);
static_assert(std::is_same<decltype(&SetEvent),SetEventFn>::value);
static_assert(std::is_same<decltype(&ResetEvent),ResetEventFn>::value);
static_assert(std::is_same<decltype(&PulseEvent),PulseEventFn>::value);
static_assert(std::is_same<decltype(&ReleaseMutex),ReleaseMutexFn>::value);
static_assert(std::is_same<decltype(&ReleaseSemaphore),ReleaseSemaphoreFn>::value);
static_assert(std::is_same<decltype(&WaitForSingleObject),WaitFn>::value);
#else
#include <chrono>
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main(){
 Memory mem;mem.map(0x1000,0x20000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x1f000;
 const char* names[]={"createeventa","createeventw","createmutexa","createsemaphorea","openeventa","openeventw","setevent","resetevent","pulseevent","releasemutex","releasesemaphore","waitforsingleobject","waitformultipleobjects","getstdhandle","setstdhandle","getconsolecp","getconsoleoutputcp","getconsolemode","setconsolemode","getuserdefaultlcid","getsystemdefaultlcid","getthreadlocale","rtlunwind","getmodulehandleexa","disablethreadlibrarycalls","getversionexw","globalmemorystatusex","freelibraryandexitthread","debugbreak","closehandle"};const unsigned arity[]={4,4,3,4,3,3,1,1,1,1,3,2,4,1,2,0,0,2,2,0,0,0,4,3,1,1,1,2,0,1};
 for(unsigned i=0;i<30;++i){auto it=reg.map.find({"kernel32.dll",names[i]});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall);CHECK(it->second.argc==arity[i]);}}
 auto call=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};
 CHECK(call("getstdhandle",{uint32_t(-10)})==Host::H_STDIN);CHECK(call("getstdhandle",{uint32_t(-11)})==Host::H_STDOUT);CHECK(call("getstdhandle",{uint32_t(-12)})==Host::H_STDERR);
 CHECK(call("setstdhandle",{uint32_t(-11),Host::H_STDERR})==1);CHECK(call("getstdhandle",{uint32_t(-11)})==Host::H_STDERR);CHECK(call("setstdhandle",{uint32_t(-11),Host::H_STDOUT})==1);
 CHECK(call("setstdhandle",{0,Host::H_STDOUT})==0);CHECK(call("getlasterror",{})==87);
 CHECK(call("getconsolecp",{})==0);CHECK(call("getlasterror",{})==6);CHECK(call("getconsoleoutputcp",{})==0);CHECK(call("getlasterror",{})==6);
 CHECK(call("getconsolemode",{Host::H_STDOUT,0x3000})==0);CHECK(call("getlasterror",{})==6);CHECK(call("setconsolemode",{Host::H_STDOUT,7})==0);CHECK(call("getlasterror",{})==6);
 CHECK(call("getuserdefaultlcid",{})==0);CHECK(call("getlasterror",{})==120);CHECK(call("getsystemdefaultlcid",{})==0);CHECK(call("getlasterror",{})==120);CHECK(call("getthreadlocale",{})==0);CHECK(call("getlasterror",{})==120);
 mem.w32(0x3000,276);CHECK(call("getversionexw",{0x3000})==1);CHECK(mem.r32(0x3004)==10&&mem.r32(0x300c)==19045);
 uint32_t moduleName=g.put_str("kernel32.dll");CHECK(call("getmodulehandleexa",{0,moduleName,0x3004})==1);CHECK(mem.r32(0x3004)!=0);
 CHECK(call("disablethreadlibrarycalls",{g.image_base})==1);
 CHECK(call("globalmemorystatusex",{0x3000})==0);CHECK(call("getlasterror",{})==120);
 bool debug_break=false;try{call("debugbreak",{});}catch(const Fault& e){debug_break=std::string(e.what()).find("DebugBreak")!=std::string::npos;}CHECK(debug_break);
 uint32_t name=g.put_str("shared-event");CHECK(call("setlasterror",{55})==0);uint32_t e1=call("createeventa",{0,1,1,name});CHECK(e1!=0);CHECK(call("getlasterror",{})==0);
 uint32_t e2=call("createeventa",{0,0,0,name});CHECK(e2!=0&&e2!=e1);CHECK(call("getlasterror",{})==183);
 uint32_t wideName=g.put_wstr(u"wide-event"),ew=call("createeventw",{0,1,0,wideName});CHECK(ew!=0);
 uint32_t ew2=call("openeventw",{0x001f0003u,0,wideName});CHECK(ew2!=0&&ew2!=ew);
 uint32_t other=call("createeventa",{0,1,0,g.put_str("other-event")});CHECK(other!=0);
 mem.w32(0x3010,ew);mem.w32(0x3014,other);CHECK(call("waitformultipleobjects",{2,0x3010,0,0})==258);
 CHECK(call("setevent",{ew})==1);CHECK(call("waitformultipleobjects",{2,0x3010,0,0})==0);
 CHECK(call("waitformultipleobjects",{2,0x3010,1,0})==258);CHECK(call("setevent",{other})==1);
 CHECK(call("waitformultipleobjects",{2,0x3010,1,0})==0);CHECK(call("resetevent",{ew})==1);CHECK(call("resetevent",{other})==1);
 CHECK(call("waitforsingleobject",{e1,0})==0);CHECK(call("waitforsingleobject",{e2,0})==0);CHECK(call("resetevent",{e2})==1);CHECK(call("waitforsingleobject",{e1,0})==258);auto waitStart=std::chrono::steady_clock::now();CHECK(call("waitforsingleobject",{e1,5000})==258);CHECK(std::chrono::steady_clock::now()-waitStart<std::chrono::milliseconds(250));
 CHECK(call("setevent",{e1})==1);CHECK(call("waitforsingleobject",{e2,0})==0);CHECK(call("pulseevent",{e1})==1);CHECK(call("waitforsingleobject",{e1,0})==258);CHECK(call("waitforsingleobject",{e1,1})==258);bool blocked=false;std::string diagnostic;try{call("waitforsingleobject",{e1,0xffffffffu});}catch(const Fault& e){blocked=true;diagnostic=e.what();}CHECK(blocked&&diagnostic.find("WaitForSingleObject")!=std::string::npos&&diagnostic.find(std::to_string(e1))!=std::string::npos);
 uint32_t autoName=g.put_str("auto-event"),automatic=call("createeventa",{0,0,1,autoName});CHECK(call("waitforsingleobject",{automatic,0})==0);CHECK(call("waitforsingleobject",{automatic,0})==258);CHECK(call("setevent",{automatic})==1);CHECK(call("waitforsingleobject",{automatic,0})==0);CHECK(call("waitforsingleobject",{automatic,0})==258);
 uint32_t yielded=call("createeventa",{0,1,0,0});CHECK(yielded!=0);mem.map(0x30000,0x1000,PROT_R|PROT_W|PROT_X);uint32_t setThunk=th.add(reg.map.at({"kernel32.dll","setevent"}));std::vector<uint8_t> signalCode={0x68,0,0,0,0,0xb8,0,0,0,0,0xff,0xd0,0x31,0xc0,0xc2,0x04,0x00};mem.write_bytes(0x30000,signalCode.data(),uint32_t(signalCode.size()));mem.w32(0x30001,yielded);mem.w32(0x30006,setThunk);uint32_t worker=call("createthread",{0,0,0x30000,yielded,0,0});CHECK(worker!=0);CHECK(call("waitforsingleobject",{yielded,5000})==0);
 CHECK(call("createmutexa",{0,0,name})==0);CHECK(call("getlasterror",{})==6);CHECK(call("setlasterror",{55})==0);uint32_t freshMutex=call("createmutexa",{0,0,0});CHECK(freshMutex!=0);CHECK(call("getlasterror",{})==0);CHECK(call("closehandle",{freshMutex})==1);
 uint32_t opened=call("openeventa",{0x001F0003u,0,name});CHECK(opened!=0);CHECK(call("duplicatehandle",{0xffffffffu,e1,0xffffffffu,0x3004,0,0,2})==1);uint32_t duplicate=mem.r32(0x3004);CHECK(duplicate&&duplicate!=e1);CHECK(call("closehandle",{e1})==1);CHECK(call("waitforsingleobject",{duplicate,0})==258);CHECK(call("setevent",{duplicate})==1);CHECK(call("waitforsingleobject",{opened,0})==0);CHECK(call("closehandle",{duplicate})==1);
 uint32_t mutex=call("createmutexa",{0,1,0});CHECK(mutex!=0);CHECK(call("waitforsingleobject",{mutex,0})==0);mem.w32(0x1024,2);cpu.fs_base=0x1000;CHECK(call("releasemutex",{mutex})==0);cpu.fs_base=0;CHECK(call("releasemutex",{mutex})==1);CHECK(call("releasemutex",{mutex})==1);CHECK(call("releasemutex",{mutex})==0);
 CHECK(call("setlasterror",{55})==0);uint32_t freshSem=call("createsemaphorea",{0,0,1,0});CHECK(freshSem!=0);CHECK(call("getlasterror",{})==0);CHECK(call("closehandle",{freshSem})==1);uint32_t semName=g.put_str("shared-semaphore"),sem=call("createsemaphorea",{0,1,2,semName});CHECK(sem!=0);uint32_t sem2=call("createsemaphorea",{0,0,1,semName});CHECK(sem2!=0&&sem2!=sem);CHECK(call("getlasterror",{})==183);CHECK(call("waitforsingleobject",{sem,0})==0);CHECK(call("waitforsingleobject",{sem2,0})==258);
 CHECK(call("releasesemaphore",{sem,2,0x3000})==1);CHECK(mem.r32(0x3000)==0);CHECK(call("releasesemaphore",{sem,1,0})==0);
 CHECK(call("waitforsingleobject",{sem,0})==0);CHECK(call("waitforsingleobject",{sem2,0})==0);CHECK(call("waitforsingleobject",{sem,0})==258);
 CHECK(call("createsemaphorea",{0,3,2,0})==0);CHECK(call("releasesemaphore",{sem,0,0})==0);
 CHECK(call("openeventa",{0x001F0003u,0,g.put_str("missing-event")})==0);
 CHECK(call("closehandle",{e2})==1);CHECK(call("closehandle",{opened})==1);CHECK(call("setevent",{opened})==0);
 CHECK(call("closehandle",{ew})==1);CHECK(call("closehandle",{ew2})==1);CHECK(call("closehandle",{other})==1);
 CHECK(call("closehandle",{automatic})==1);CHECK(call("closehandle",{mutex})==1);CHECK(call("closehandle",{sem2})==1);CHECK(call("closehandle",{sem})==1);CHECK(call("waitforsingleobject",{0x1234,0})==0xffffffffu);
 std::printf("x86-sync: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
