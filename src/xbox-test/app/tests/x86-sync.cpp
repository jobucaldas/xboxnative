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
using WaitMultipleFn = DWORD (WINAPI*)(DWORD,CONST HANDLE*,BOOL,DWORD);
using WaitMultipleExFn = DWORD (WINAPI*)(DWORD,CONST HANDLE*,BOOL,DWORD,BOOL);
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
static_assert(std::is_same<decltype(&WaitForMultipleObjects),WaitMultipleFn>::value);
static_assert(std::is_same<decltype(&WaitForMultipleObjectsEx),WaitMultipleExFn>::value);
#else
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
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
 CHECK(call("waitforsingleobject",{e1,0})==0);CHECK(call("waitforsingleobject",{e2,0})==0);CHECK(call("resetevent",{e2})==1);CHECK(call("waitforsingleobject",{e1,0})==258);auto waitStart=std::chrono::steady_clock::now();CHECK(call("waitforsingleobject",{e1,50})==258);CHECK(std::chrono::steady_clock::now()-waitStart>=std::chrono::milliseconds(45));CHECK(std::chrono::steady_clock::now()-waitStart<std::chrono::milliseconds(2000));
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
 // Thread pool, InitOnce and wait routes. Guest callbacks are hand-assembled in an RWX page: each bumps a counter.
 mem.map(0x31000,0x1000,PROT_R|PROT_W|PROT_X);
 auto code=[&](uint32_t at,std::initializer_list<uint8_t> bytes){for(uint8_t b:bytes)mem.w8(at++,b);};
 auto ntcall=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"ntdll.dll",n}).fn(cpu);};
 auto body=[&](uint32_t at,uint32_t arg,uint32_t thunk){code(at,{0x68});mem.w32(at+1,arg);code(at+5,{0xb8});mem.w32(at+6,thunk);code(at+10,{0xff,0xd0,0x31,0xc0,0xc2,0x04,0x00});};
 const uint32_t workCb=0x31000,timerCb=0x31010,initCb=0x31020,waitCb=0x31040,workCount=0x31800,timerCount=0x31804,initCount=0x31808,waitCount=0x31828;
 code(workCb,{0xff,0x05,0x00,0x18,0x03,0x00,0xc3});code(timerCb,{0xff,0x05,0x04,0x18,0x03,0x00,0xc3});code(waitCb,{0xff,0x05,0x28,0x18,0x03,0x00,0xc3});
 code(initCb,{0x8b,0x44,0x24,0x0c,0xc7,0x00,0x34,0x12,0x00,0x00,0xff,0x05,0x08,0x18,0x03,0x00,0xb8,0x01,0x00,0x00,0x00,0xc3});
 uint32_t work=call("createthreadpoolwork",{workCb,0,0});CHECK(work!=0);
 for(int i=0;i<3;++i)call("submitthreadpoolwork",{work});
 CHECK(mem.r32(workCount)==0);call("waitforthreadpoolworkcallbacks",{work,0});CHECK(mem.r32(workCount)==3);
 uint32_t cancelled=call("createthreadpoolwork",{workCb,0,0});call("submitthreadpoolwork",{cancelled});call("waitforthreadpoolworkcallbacks",{cancelled,1});CHECK(mem.r32(workCount)==3);call("closethreadpoolwork",{cancelled});call("closethreadpoolwork",{work});
 uint32_t timer=call("createthreadpooltimer",{timerCb,0,0});CHECK(timer!=0);
 int64_t due=-1000000;mem.w32(0x31830,uint32_t(uint64_t(due)));mem.w32(0x31834,uint32_t(uint64_t(due)>>32));call("setthreadpooltimer",{timer,0x31830,0,0});
 call("sleep",{0});CHECK(mem.r32(timerCount)==0);call("sleep",{150});CHECK(mem.r32(timerCount)==1);call("sleep",{50});CHECK(mem.r32(timerCount)==1);call("closethreadpooltimer",{timer});
 uint32_t gate=call("createeventexw",{0,0,0,0});CHECK(gate!=0);
 uint32_t waiter=call("createthreadpoolwait",{waitCb,0,0});CHECK(waiter!=0);call("setthreadpoolwait",{waiter,gate,0});
 call("sleep",{0});CHECK(mem.r32(waitCount)==0);call("setevent",{gate});call("sleep",{0});CHECK(mem.r32(waitCount)==1);call("closethreadpoolwait",{waiter});call("closehandle",{gate});
 CHECK(call("initonceexecuteonce",{0x3180c,initCb,7,0x31814})==1);CHECK(mem.r32(initCount)==1);CHECK(mem.r32(0x31814)==0x1234);
 mem.w32(0x31814,0);CHECK(call("initonceexecuteonce",{0x3180c,initCb,7,0x31814})==1);CHECK(mem.r32(initCount)==1);CHECK(mem.r32(0x31814)==0x1234);
 uint32_t manualEvent=call("createeventexw",{0,0,3,0});CHECK(manualEvent!=0);CHECK(call("waitforsingleobject",{manualEvent,0})==0);CHECK(call("waitforsingleobject",{manualEvent,0})==0);call("closehandle",{manualEvent});
 uint32_t autoEvent=call("createeventexw",{0,0,2,0});CHECK(call("waitforsingleobject",{autoEvent,0})==0);CHECK(call("waitforsingleobject",{autoEvent,0})==258);call("closehandle",{autoEvent});
 uint32_t wsem=call("createsemaphoreexw",{0,1,2,0,0,0});CHECK(wsem!=0);CHECK(call("waitforsingleobject",{wsem,0})==0);CHECK(call("waitforsingleobject",{wsem,0})==258);call("closehandle",{wsem});CHECK(call("createsemaphoreexw",{0,3,2,0,0,0})==0);
 const uint32_t addr=0x31820,cmp=0x31824,timeoutNow=0x31838;mem.w32(addr,5);mem.w32(cmp,5);
 int64_t relative=-100000;mem.w32(timeoutNow,uint32_t(uint64_t(relative)));mem.w32(timeoutNow+4,uint32_t(uint64_t(relative)>>32));
 CHECK(ntcall("rtlwaitonaddress",{addr,cmp,4,timeoutNow})==0x102);
 mem.w32(addr,6);CHECK(ntcall("rtlwaitonaddress",{addr,cmp,4,timeoutNow})==0);mem.w32(addr,5);
 body(0x31050,addr,th.add(reg.map.at({"ntdll.dll","rtlwakeaddresssingle"})));call("createthread",{0,0,0x31050,0,0,0});CHECK(ntcall("rtlwaitonaddress",{addr,cmp,4,0})==0);
 const uint32_t lock=0x31840,cv=0x31850;cpu.fs_base=0x1000;call("initializesrwlock",{lock});call("acquiresrwlockexclusive",{lock});
 CHECK(call("sleepconditionvariablesrw",{cv,lock,10,0})==0);CHECK(call("getlasterror",{})==1460);call("releasesrwlockexclusive",{lock});
 body(0x31070,cv,th.add(reg.map.at({"kernel32.dll","wakeconditionvariable"})));call("createthread",{0,0,0x31070,0,0,0});
 call("acquiresrwlockexclusive",{lock});CHECK(call("sleepconditionvariablesrw",{cv,lock,0xffffffffu,0})==1);call("releasesrwlockexclusive",{lock});

 // Native-backed notify events and WaitForMultipleObjects* contracts (#401 / #323).
 {
  struct MockNativeEvents {
   struct State { bool manual = false; bool signaled = false; };
   std::mutex mu;
   std::condition_variable cv;
   std::map<uintptr_t, State> events;
   uintptr_t next_id = 0x9000u;
   Host::NativeEventBackend backend() {
    Host::NativeEventBackend b;
    b.create = [this](bool manual, bool initial) -> uintptr_t {
     std::lock_guard<std::mutex> lk(mu);
     uintptr_t id = ++next_id;
     events[id] = State{manual, initial};
     return id;
    };
    b.close = [this](uintptr_t h) {
     std::lock_guard<std::mutex> lk(mu);
     events.erase(h);
    };
    b.set = [this](uintptr_t h) { set(h); };
    b.reset = [this](uintptr_t h) {
     std::lock_guard<std::mutex> lk(mu);
     auto it = events.find(h);
     if (it != events.end()) it->second.signaled = false;
    };
    b.pulse = [this](uintptr_t h) {
     std::lock_guard<std::mutex> lk(mu);
     auto it = events.find(h);
     if (it != events.end()) it->second.signaled = false;
    };
    b.wait_multiple = [this](const uintptr_t* handles, uint32_t count, bool wait_all, uint32_t timeout) -> uint32_t {
     std::unique_lock<std::mutex> lk(mu);
     auto try_acquire = [&]() -> uint32_t {
      for (uint32_t i = 0; i < count; ++i)
       if (!events.count(handles[i])) return 0xffffffffu;
      if (wait_all) {
       for (uint32_t i = 0; i < count; ++i)
        if (!events[handles[i]].signaled) return 258u;
       for (uint32_t i = 0; i < count; ++i) {
        auto& st = events[handles[i]];
        if (!st.manual) st.signaled = false;
       }
       return 0u;
      }
      for (uint32_t i = 0; i < count; ++i) {
       auto& st = events[handles[i]];
       if (st.signaled) {
        if (!st.manual) st.signaled = false;
        return i;
       }
      }
      return 258u;
     };
     uint32_t r = try_acquire();
     if (r != 258u || timeout == 0u) return r;
     if (timeout == 0xffffffffu) {
      cv.wait(lk, [&]() { r = try_acquire(); return r != 258u; });
      return r;
     }
     auto dl = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
     cv.wait_until(lk, dl, [&]() { r = try_acquire(); return r != 258u; });
     return r;
    };
    return b;
   }
   void set(uintptr_t h) {
    {
     std::lock_guard<std::mutex> lk(mu);
     auto it = events.find(h);
     if (it != events.end()) it->second.signaled = true;
    }
    cv.notify_all();
   }
   bool is_signaled(uintptr_t h) {
    std::lock_guard<std::mutex> lk(mu);
    auto it = events.find(h);
    return it != events.end() && it->second.signaled;
   }
  };
  MockNativeEvents mock;
  auto ucall = [&](const char* n, std::initializer_list<uint32_t> a) {
   unsigned i = 0;
   for (uint32_t v : a) mem.w32(cpu.reg[ESP] + 4 + 4 * i++, v);
   return reg.map.at({"user32.dll", n}).fn(cpu);
  };
  const uint32_t arr = 0x3200;
  auto put_handles = [&](std::initializer_list<uint32_t> hs) {
   uint32_t i = 0;
   for (uint32_t h : hs) mem.w32(arr + 4 * i++, h);
  };

  // Create guest-only events before enabling native backend so we can test mixed guest + native waits.
  uint32_t g_auto_sig = call("createeventa", {0, 0, 1, 0});
  uint32_t g_auto_unsig = call("createeventa", {0, 0, 0, 0});
  CHECK(g_auto_sig != 0 && g_auto_unsig != 0);
  CHECK(host.dsound_event(g_auto_sig) == 0);

  host.native_events = mock.backend();
  uint32_t n_auto1 = call("createeventa", {0, 0, 0, 0});
  uint32_t n_auto2 = call("createeventa", {0, 0, 0, 0});
  uint32_t n_manual = call("createeventa", {0, 1, 0, 0});
  CHECK(n_auto1 != 0 && n_auto2 != 0 && n_manual != 0);
  uintptr_t nh1 = host.dsound_event(n_auto1), nh2 = host.dsound_event(n_auto2), nhm = host.dsound_event(n_manual);
  CHECK(nh1 != 0 && nh2 != 0 && nhm != 0);

  // 1. Error & edge cases: count==0, count>64, NULL ptr, MsgWait count>63, invalid handle, duplicate handles on wait_all.
  CHECK(call("waitformultipleobjects", {0, arr, 0, 0}) == 0xffffffffu && call("getlasterror", {}) == 87);
  CHECK(call("waitformultipleobjects", {65, arr, 0, 0}) == 0xffffffffu && call("getlasterror", {}) == 87);
  CHECK(call("waitformultipleobjects", {1, 0, 0, 0}) == 0xffffffffu && call("getlasterror", {}) == 87);
  CHECK(ucall("msgwaitformultipleobjects", {64, arr, 0, 0, 0}) == 0xffffffffu && call("getlasterror", {}) == 87);

  mock.set(nh1);
  put_handles({n_auto1, 0xdeadbeefu});
  CHECK(call("waitformultipleobjects", {2, arr, 0, 0}) == 0xffffffffu && call("getlasterror", {}) == 6);
  CHECK(mock.is_signaled(nh1));  // invalid handle failure must not consume earlier signaled event

  put_handles({n_auto1, n_auto1});
  CHECK(call("waitformultipleobjects", {2, arr, 1, 0}) == 0xffffffffu && call("getlasterror", {}) == 87);
  CHECK(mock.is_signaled(nh1));  // duplicate handle rejection on wait_all must not consume

  CHECK(call("duplicatehandle", {0xffffffffu, n_auto1, 0xffffffffu, 0x3300, 0, 0, 2}) == 1);
  uint32_t n_auto1_dup = mem.r32(0x3300);
  CHECK(n_auto1_dup != 0 && n_auto1_dup != n_auto1);
  put_handles({n_auto1, n_auto1_dup});
  CHECK(call("waitformultipleobjects", {2, arr, 1, 0}) == 0xffffffffu && call("getlasterror", {}) == 87);
  CHECK(mock.is_signaled(nh1));  // duplicated handles to same underlying object rejected on wait_all
  // Duplicate handles ARE allowed on wait_all=FALSE and return lowest index, consuming once.
  CHECK(call("waitformultipleobjects", {2, arr, 0, 0}) == 0);
  CHECK(!mock.is_signaled(nh1));
  CHECK(call("closehandle", {n_auto1_dup}) == 1);

  // 2. Native notify event signaled externally (without setting guest SyncObject::signaled):
  //    WaitForMultipleObjects, WaitForMultipleObjectsEx, MsgWaitForMultipleObjects, MsgWaitForMultipleObjectsEx.
  mock.set(nh2);
  put_handles({n_auto1, n_auto2});
  CHECK(call("waitformultipleobjects", {2, arr, 0, 0}) == 1);
  CHECK(!mock.is_signaled(nh2));
  CHECK(call("waitformultipleobjects", {2, arr, 0, 0}) == 258);

  mock.set(nh2);
  CHECK(call("waitformultipleobjectsex", {2, arr, 0, 0, 0}) == 1);
  CHECK(!mock.is_signaled(nh2));

  mock.set(nh1);
  CHECK(ucall("msgwaitformultipleobjects", {2, arr, 0, 0, 0xff}) == 0);
  CHECK(!mock.is_signaled(nh1));

  mock.set(nh2);
  CHECK(ucall("msgwaitformultipleobjectsex", {2, arr, 0, 0xff, 0}) == 1);
  CHECK(!mock.is_signaled(nh2));

  // 3. Non-consumption of unchosen auto-reset native events when an earlier handle wins (bWaitAll=FALSE).
  // 3a. Guest auto-reset event at index 0 wins over signaled native auto-reset event at index 1.
  mock.set(nh1);
  put_handles({g_auto_sig, n_auto1});
  CHECK(call("waitformultipleobjects", {2, arr, 0, 0}) == 0);
  CHECK(mock.is_signaled(nh1));  // index 1 MUST remain signaled
  CHECK(call("waitforsingleobject", {g_auto_sig, 0}) == 258);
  CHECK(call("waitforsingleobject", {n_auto1, 0}) == 0);
  CHECK(!mock.is_signaled(nh1));

  // 3b. Native auto-reset event at index 1 wins over signaled native auto-reset event at index 2.
  mock.set(nh1);
  mock.set(nh2);
  put_handles({g_auto_unsig, n_auto1, n_auto2});
  CHECK(call("waitformultipleobjects", {3, arr, 0, 0}) == 1);
  CHECK(!mock.is_signaled(nh1));
  CHECK(mock.is_signaled(nh2));  // index 2 MUST NOT be consumed
  CHECK(call("waitformultipleobjects", {3, arr, 0, 0}) == 2);
  CHECK(!mock.is_signaled(nh2));

  // 3c. Guest semaphore at index 1 wins over signaled native auto-reset event at index 2.
  uint32_t msem = call("createsemaphorea", {0, 1, 2, 0});
  CHECK(msem != 0);
  mock.set(nh2);
  put_handles({n_auto1, msem, n_auto2});
  CHECK(call("waitformultipleobjects", {3, arr, 0, 0}) == 1);
  CHECK(call("waitforsingleobject", {msem, 0}) == 258);
  CHECK(mock.is_signaled(nh2));  // index 2 MUST NOT be consumed
  CHECK(call("waitforsingleobject", {n_auto2, 0}) == 0);

  // 4. Atomic-acquire semantics on bWaitAll=TRUE: ZERO consumption on failure/timeout.
  // 4a. Native signaled + native unsignaled -> timeout consumes neither.
  mock.set(nh1);
  put_handles({n_auto1, n_auto2});
  CHECK(call("waitformultipleobjects", {2, arr, 1, 0}) == 258);
  CHECK(mock.is_signaled(nh1));
  CHECK(call("waitformultipleobjects", {2, arr, 1, 15}) == 258);
  CHECK(mock.is_signaled(nh1));

  // 4b. Mixed guest signaled auto-reset + native signaled auto-reset + native unsignaled -> timeout consumes NONE.
  CHECK(call("setevent", {g_auto_sig}) == 1);
  put_handles({g_auto_sig, n_auto1, n_auto2});
  CHECK(call("waitformultipleobjects", {3, arr, 1, 15}) == 258);
  CHECK(mock.is_signaled(nh1));
  CHECK(call("waitforsingleobject", {g_auto_sig, 0}) == 0);  // still signaled!

  // 4c. Mixed native signaled + guest ready semaphore + guest free mutex + guest unsignaled event -> timeout consumes NONE.
  CHECK(call("releasesemaphore", {msem, 1, 0}) == 1);
  uint32_t mmutex = call("createmutexa", {0, 0, 0});
  CHECK(mmutex != 0);
  put_handles({n_auto1, msem, mmutex, g_auto_unsig});
  CHECK(call("waitformultipleobjects", {4, arr, 1, 10}) == 258);
  CHECK(mock.is_signaled(nh1));
  // Verify mutex was NOT acquired (still unowned: ReleaseMutex fails with ERROR_NOT_OWNER=288).
  CHECK(call("releasemutex", {mmutex}) == 0 && call("getlasterror", {}) == 288);
  // Verify semaphore still has count==1.
  CHECK(call("waitforsingleobject", {msem, 0}) == 0);
  CHECK(call("waitforsingleobject", {msem, 0}) == 258);

  // 4d. Mixed native signaled + guest semaphore(count==0) -> timeout must NOT touch native event, and INFINITE still faults.
  put_handles({n_auto1, msem});
  CHECK(call("waitformultipleobjects", {2, arr, 1, 10}) == 258);
  CHECK(mock.is_signaled(nh1));
  bool unready_guest_fault = false;
  try { call("waitformultipleobjects", {2, arr, 1, 0xffffffffu}); }
  catch (const Fault&) { unready_guest_fault = true; }
  CHECK(unready_guest_fault);
  CHECK(mock.is_signaled(nh1));  // STILL not consumed

  // 4e. Successful atomic acquire of mixed [semaphore, abandoned mutex, native auto-reset] returns WAIT_ABANDONED_0.
  CHECK(call("releasesemaphore", {msem, 1, 0}) == 1);
  mem.w32(0x1024, 9); cpu.fs_base = 0x1000;
  CHECK(call("waitforsingleobject", {mmutex, 0}) == 0);
  g.abandon_mutexes(9);
  mem.w32(0x1024, 1);
  put_handles({msem, mmutex, n_auto1});
  CHECK(call("waitformultipleobjects", {3, arr, 1, 0}) == 0x80u);
  CHECK(!mock.is_signaled(nh1));
  CHECK(call("waitforsingleobject", {msem, 0}) == 258);
  CHECK(call("releasemutex", {mmutex}) == 1);

  // 4f. Successful wait_all on two native notify events (one auto-reset, one manual-reset).
  mock.set(nh1);
  mock.set(nhm);
  put_handles({n_auto1, n_manual});
  CHECK(call("waitformultipleobjects", {2, arr, 1, 0}) == 0);
  CHECK(!mock.is_signaled(nh1));
  CHECK(mock.is_signaled(nhm));  // manual-reset stays signaled
  CHECK(call("resetevent", {n_manual}) == 1);
  CHECK(!mock.is_signaled(nhm));

  // 5. Real blocking wait woken by another thread signaling a native notify event.
  // 5a. bWaitAll=FALSE with finite timeout wakes promptly on index 1.
  {
   put_handles({n_auto1, n_auto2});
   std::thread signaler([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    mock.set(nh2);
   });
   auto t0 = std::chrono::steady_clock::now();
   uint32_t res = call("waitformultipleobjects", {2, arr, 0, 2000});
   auto elapsed = std::chrono::steady_clock::now() - t0;
   signaler.join();
   CHECK(res == 1);
   CHECK(elapsed < std::chrono::milliseconds(1500));
   CHECK(!mock.is_signaled(nh2));
  }

  // 5b. bWaitAll=FALSE with INFINITE timeout wakes on native notify event (does NOT throw Fault).
  {
   put_handles({g_auto_unsig, n_auto1});
   std::thread signaler([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    mock.set(nh1);
   });
   uint32_t res = call("waitformultipleobjects", {2, arr, 0, 0xffffffffu});
   signaler.join();
   CHECK(res == 1);
   CHECK(!mock.is_signaled(nh1));
  }

  // 5c. bWaitAll=TRUE with INFINITE timeout on mixed [semaphore(count=1), n_auto1, n_auto2]:
  //     signaler sets n_auto1 at +20ms, then n_auto2 at +40ms -> wakes atomically when both are ready.
  {
   CHECK(call("releasesemaphore", {msem, 1, 0}) == 1);
   put_handles({msem, n_auto1, n_auto2});
   std::thread signaler([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mock.set(nh1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mock.set(nh2);
   });
   uint32_t res = call("waitformultipleobjects", {3, arr, 1, 0xffffffffu});
   signaler.join();
   CHECK(res == 0);
   CHECK(!mock.is_signaled(nh1));
   CHECK(!mock.is_signaled(nh2));
   CHECK(call("waitforsingleobject", {msem, 0}) == 258);
  }

  // 5d. bWaitAll=TRUE with finite timeout where only one native event signals before expiry:
  //     must return WAIT_TIMEOUT and MUST NOT consume the event that signaled!
  {
   put_handles({n_auto1, n_auto2});
   std::thread signaler([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    mock.set(nh1);
   });
   uint32_t res = call("waitformultipleobjects", {2, arr, 1, 50});
   signaler.join();
   CHECK(res == 258);
   CHECK(mock.is_signaled(nh1));  // n_auto1 was NOT consumed!
   CHECK(call("waitforsingleobject", {n_auto1, 0}) == 0);
   CHECK(!mock.is_signaled(nh1));
  }

  // 5e. Timeout on unsignaled native events waits for duration and returns WAIT_TIMEOUT.
  {
   put_handles({n_auto1, n_auto2});
   auto t0 = std::chrono::steady_clock::now();
   uint32_t res = call("waitformultipleobjects", {2, arr, 0, 35});
   auto elapsed = std::chrono::steady_clock::now() - t0;
   CHECK(res == 258);
   CHECK(elapsed >= std::chrono::milliseconds(25) && elapsed < std::chrono::milliseconds(1500));
   CHECK(!mock.is_signaled(nh1) && !mock.is_signaled(nh2));
  }

  // 5f. Cooperative guest worker signaling an event wakes WaitForMultipleObjects with INFINITE.
  {
   uint32_t coop = call("createeventa", {0, 0, 0, 0});
   CHECK(coop != 0);
   body(0x31090, coop, th.add(reg.map.at({"kernel32.dll", "setevent"})));
   CHECK(call("createthread", {0, 0, 0x31090, 0, 0, 0}) != 0);
   put_handles({n_auto1, coop});
   CHECK(call("waitformultipleobjects", {2, arr, 0, 0xffffffffu}) == 1);
   CHECK(call("closehandle", {coop}) == 1);
  }

  CHECK(call("closehandle", {n_auto1}) == 1);
  CHECK(call("closehandle", {n_auto2}) == 1);
  CHECK(call("closehandle", {n_manual}) == 1);
  CHECK(call("closehandle", {g_auto_sig}) == 1);
  CHECK(call("closehandle", {g_auto_unsig}) == 1);
  CHECK(call("closehandle", {msem}) == 1);
  CHECK(call("closehandle", {mmutex}) == 1);
  host.native_events = {};
 }

 std::printf("x86-sync: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
#endif
