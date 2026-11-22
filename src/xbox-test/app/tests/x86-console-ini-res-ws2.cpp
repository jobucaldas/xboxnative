// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)
int main() {
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);mem.map(0x400000,0x3000,PROT_R|PROT_W);
    ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.image_base=0x400000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;cpu.fs_base=g.cur_teb;
    auto call=[&](const char* dll,const char* name,std::initializer_list<uint32_t> args){unsigned i=0;for(uint32_t v:args)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({dll,name}).fn(cpu);};
    const char* console[]={"setconsoletitlea","setconsoletitlew","getconsoletitlea","getconsoletitlew","allocconsole","freeconsole","attachconsole"};
    for(unsigned i=0;i<7;++i)CHECK(reg.map.count({"kernel32.dll",console[i]})==1);
    const char* ini[]={"getprivateprofilestringa","getprivateprofilestringw","getprivateprofileinta","getprivateprofileintw","writeprivateprofilestringa","writeprivateprofilestringw","getprivateprofilesectiona","getprivateprofilesectionw"};
    for(unsigned i=0;i<8;++i)CHECK(reg.map.count({"kernel32.dll",ini[i]})==1);
    const char* resources[]={"findresourcea","findresourcew","findresourceexa","findresourceexw","loadresource","lockresource","sizeofresource","freeresource"};
    for(unsigned i=0;i<8;++i)CHECK(reg.map.count({"kernel32.dll",resources[i]})==1);
    CHECK(reg.map.count({"ws2_32.dll","#115"})==1&&reg.map.count({"ws2_32.dll","#116"})==1&&reg.map.count({"ws2_32.dll","#111"})==1);
    uint32_t title=g.put_str("title");CHECK(call("kernel32.dll","setconsoletitlea",{title})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","allocconsole",{})==0&&g.last_error()==5);
    CHECK(call("kernel32.dll","attachconsole",{0xffffffffu})==0&&g.last_error()==6);
    // No console behind any handle: input fails with ERROR_INVALID_HANDLE; WriteConsoleA takes the stdout/stderr path.
    uint32_t inbuf=0x3400,nread=0x3410,msg=g.put_str("hi");
    CHECK(call("kernel32.dll","readconsolea",{0x10,inbuf,16,nread,0})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","readconsolew",{0x10,inbuf,16,nread,0})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","readconsoleinputa",{0x10,inbuf,1,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","readconsoleinputw",{0x10,inbuf,1,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","peekconsoleinputa",{0x10,inbuf,1,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","getnumberofconsoleinputevents",{0x10,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","writeconsolea",{0x10,msg,2,nread,0})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","writeconsolea",{Host::H_STDOUT,msg,(1u<<24)+1,nread,0})==0&&g.last_error()==87);
    CHECK(call("kernel32.dll","writeconsolea",{Host::H_STDOUT,0,2,nread,0})==0&&g.last_error()==87);
    CHECK(call("kernel32.dll","writeconsolea",{Host::H_STDOUT,msg,2,nread,0})==1&&mem.r32(nread)==2);
    CHECK(host.out.size()>=2&&host.out.compare(host.out.size()-2,2,"hi")==0);
    // Child processes are refused (ERROR_ACCESS_DENIED) with only the executable token logged (arguments redacted).
    uint32_t cmd=g.put_wstr(u"cmd.exe /c secret_token"),cmd_unterm=g.put_wstr(u"\"unterm.exe --pw=topsecret");
    CHECK(call("kernel32.dll","createprocessw",{0,cmd,0,0,0,0,0,0,0,0})==0&&g.last_error()==5);
    CHECK(host.err.find("CreateProcessW refused")!=std::string::npos&&host.err.find("cmd.exe")!=std::string::npos&&host.err.find("secret_token")==std::string::npos);
    host.err.clear();
    CHECK(call("kernel32.dll","createprocessw",{0,cmd_unterm,0,0,0,0,0,0,0,0})==0&&g.last_error()==5);
    CHECK(host.err.find("CreateProcessW refused")!=std::string::npos&&host.err.find("topsecret")==std::string::npos&&host.err.find("unterm.exe")==std::string::npos);
    CHECK(call("kernel32.dll","createprocessw",{0,0,0,0,0,0,0,0,0,0})==0&&g.last_error()==87);
    CHECK(call("kernel32.dll","getexitcodeprocess",{0xffffffffu,nread})==1&&mem.r32(nread)==259&&g.last_error()==0);
    CHECK(call("kernel32.dll","getexitcodeprocess",{0x10,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","getexitcodeprocess",{0xffffffffu,0})==0&&g.last_error()==87);
    // OpenProcess: only the guest's own id exists; its handle is closable and usable, other ids are not processes.
    const uint32_t self=call("kernel32.dll","getcurrentprocessid",{});
    uint32_t proc=call("kernel32.dll","openprocess",{0x1fffffu,0,self});
    CHECK(proc!=0&&proc!=0xffffffffu&&g.last_error()==0);
    CHECK(call("kernel32.dll","getexitcodeprocess",{proc,nread})==1&&mem.r32(nread)==259);
    CHECK(call("kernel32.dll","closehandle",{proc})==1&&call("kernel32.dll","getexitcodeprocess",{proc,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","openprocess",{0x1fffffu,0,self+4})==0&&g.last_error()==87);
    CHECK(call("kernel32.dll","openprocess",{0x1fffffu,0,0})==0&&g.last_error()==87);
    // Process handles (pseudo -1 or OpenProcess): GetProcessId, a distinct DuplicateHandle, waits that time out, and handle validation.
    const uint32_t out=0x3500,list=0x3510;
    CHECK(call("kernel32.dll","getprocessid",{0xffffffffu})==self);
    CHECK(call("kernel32.dll","getprocessid",{0x10})==0&&g.last_error()==6);
    proc=call("kernel32.dll","openprocess",{0x1fffffu,0,self});
    CHECK(call("kernel32.dll","getprocessid",{proc})==self);
    CHECK(call("kernel32.dll","duplicatehandle",{0xffffffffu,proc,0xffffffffu,out,0,0,2})==1);
    const uint32_t dup=mem.r32(out);
    CHECK(dup!=0&&dup!=proc&&dup!=0xffffffffu&&call("kernel32.dll","getprocessid",{dup})==self);
    CHECK(call("kernel32.dll","closehandle",{dup})==1);
    CHECK(call("kernel32.dll","getexitcodeprocess",{proc,nread})==1&&mem.r32(nread)==259&&call("kernel32.dll","getprocessid",{proc})==self);
    CHECK(call("kernel32.dll","getprocessid",{dup})==0&&g.last_error()==6&&call("kernel32.dll","getexitcodeprocess",{dup,nread})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","duplicatehandle",{0xffffffffu,0xffffffffu,0xffffffffu,out,0,0,2})==1);
    const uint32_t pseudo_dup=mem.r32(out);
    CHECK(pseudo_dup!=0xffffffffu&&pseudo_dup!=dup&&call("kernel32.dll","getprocessid",{pseudo_dup})==self&&call("kernel32.dll","closehandle",{pseudo_dup})==1);
    CHECK(call("kernel32.dll","getprocessid",{pseudo_dup})==0&&g.last_error()==6&&call("kernel32.dll","getprocessid",{0xffffffffu})==self);
    // A live process never signals: timeout 0 is WAIT_TIMEOUT (258), a finite wait ends in 258, an unknown handle is ERROR_INVALID_HANDLE, an infinite wait fails closed.
    CHECK(call("kernel32.dll","waitforsingleobject",{0xffffffffu,0})==258&&call("kernel32.dll","waitforsingleobject",{proc,0})==258&&call("kernel32.dll","waitforsingleobjectex",{proc,0,0})==258);
    CHECK(call("kernel32.dll","waitforsingleobject",{proc,1})==258);
    CHECK(call("kernel32.dll","waitforsingleobject",{0x10,0})==0xffffffffu&&g.last_error()==6);
    bool infinite_faulted=false;
    try{call("kernel32.dll","waitforsingleobject",{proc,0xffffffffu});}catch(const Fault&){infinite_faulted=true;}
    CHECK(infinite_faulted);
    mem.w32(list,0xffffffffu);mem.w32(list+4,proc);
    CHECK(call("kernel32.dll","waitformultipleobjects",{2,list,0,0})==258&&call("kernel32.dll","waitformultipleobjects",{2,list,1,0})==258);
    CHECK(call("kernel32.dll","waitformultipleobjects",{1,list,1,0})==258);
    mem.w32(list+4,0xffffffffu);
    CHECK(call("kernel32.dll","waitformultipleobjects",{2,list,1,0})==0xffffffffu&&g.last_error()==87);
    CHECK(call("kernel32.dll","terminateprocess",{0x10,1})==0&&g.last_error()==6);
    CHECK(call("kernel32.dll","closehandle",{proc})==1&&call("kernel32.dll","getprocessid",{proc})==0&&g.last_error()==6&&call("kernel32.dll","waitforsingleobject",{proc,0})==0xffffffffu&&g.last_error()==6);
    // UnmapViewOfFile accepts views mapped via NtMapViewOfSection and rejects non-view addresses with ERROR_INVALID_ADDRESS.
    CHECK(call("kernel32.dll","unmapviewoffile",{0x400000})==0&&g.last_error()==487);
    CHECK(call("kernel32.dll","unmapviewoffile",{0})==0&&g.last_error()==487);
    const uint32_t sec_h=0x3440,sec_sz=0x3448,sec_base=0x3450,sec_vsz=0x3454;
    mem.w32(sec_sz,0x1000);mem.w32(sec_sz+4,0);mem.w32(sec_base,0);mem.w32(sec_vsz,0);
    CHECK(call("ntdll.dll","ntcreatesection",{sec_h,0,0,sec_sz,4,0,0})==0);
    CHECK(call("ntdll.dll","ntmapviewofsection",{mem.r32(sec_h),0xffffffffu,sec_base,0,0,0,sec_vsz,2,0,4})==0&&mem.r32(sec_base)>=0x64000000u);
    CHECK(call("kernel32.dll","unmapviewoffile",{mem.r32(sec_base)})==1&&g.last_error()==0);
    CHECK(call("kernel32.dll","unmapviewoffile",{mem.r32(sec_base)+0x2000u})==0&&g.last_error()==487);
    // GetOverlappedResult over an OVERLAPPED {Internal, InternalHigh, Offset, OffsetHigh, hEvent}.
    const uint32_t ov=0x3420;
    for(uint32_t i=0;i<20;i+=4)mem.w32(ov+i,0);
    mem.w32(ov+4,17);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,0})==1&&mem.r32(nread)==17&&g.last_error()==0);
    mem.w32(ov,0x103);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,0})==0&&g.last_error()==996);
    mem.w32(ov,0x80000005u);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,0})==0&&g.last_error()==234&&mem.r32(nread)==17);
    mem.w32(ov,0xC0000011u);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,1})==0&&g.last_error()==38&&mem.r32(nread)==17);
    mem.w32(ov,0xC0000120u);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,0})==0&&g.last_error()==995);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,0,nread,0})==0&&g.last_error()==87);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,0,0})==0&&g.last_error()==87);
    // A pending request completed by its signaled event: bWait returns the recorded result.
    const uint32_t ev=call("kernel32.dll","createeventa",{0,1,1,0});
    mem.w32(ov,0x103);mem.w32(ov+16,ev);mem.w32(ov+4,9);
    mem.w32(nread,0);
    CHECK(call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,1})==0&&g.last_error()==996&&mem.r32(nread)==9);
    mem.w32(ov+16,0);bool wait_faulted=false;try{call("kernel32.dll","getoverlappedresult",{0x10,ov,nread,1});}catch(const Fault& f){wait_faulted=f.kind==FaultKind::Unsupported;}
    CHECK(wait_faulted);   // nothing can complete the request: fail closed instead of hanging
    CHECK(call("kernel32.dll","createpipe",{nread,nread,0,0})==0&&g.last_error()==50);
    CHECK(call("kernel32.dll","beep",{800,100})==1);
    CHECK(call("kernel32.dll","seterrormode",{0x8001})==0&&call("kernel32.dll","seterrormode",{0x2})==0x8001&&call("kernel32.dll","seterrormode",{0})==0x2);
    bool dl_faulted=false;try{call("kernel32.dll","delayloadfailurehook",{msg,msg});}catch(const Fault& f){dl_faulted=f.kind==FaultKind::Unsupported;}
    CHECK(dl_faulted);
    // RTL_BITMAP over guest memory: bit i is bit i%32 of the DWORD at Buffer + 4*(i/32).
    const uint32_t bmap=0x3500,bbuf=0x3510,vfmt=g.put_str("v=%d"),vap=0x3540,vout=0x3600;
    mem.w32(bbuf,0);mem.w32(bbuf+4,0);mem.w32(vap,42);
    call("ntdll.dll","rtlinitializebitmap",{bmap,bbuf,64});
    CHECK(mem.r32(bmap)==64&&mem.r32(bmap+4)==bbuf);
    CHECK(call("ntdll.dll","rtlarebitsclear",{bmap,0,64})==1&&call("ntdll.dll","rtlarebitsset",{bmap,0,0})==0&&call("ntdll.dll","rtlarebitsclear",{bmap,0,0})==0);
    call("ntdll.dll","rtlsetbits",{bmap,3,8});
    CHECK(mem.r32(bbuf)==0x7f8u&&call("ntdll.dll","rtlarebitsset",{bmap,3,8})==1&&call("ntdll.dll","rtlarebitsset",{bmap,2,8})==0);
    CHECK(call("ntdll.dll","rtlarebitsclear",{bmap,11,5})==1&&call("ntdll.dll","rtlarebitsclear",{bmap,10,1})==0);
    CHECK(call("ntdll.dll","rtlarebitsclear",{bmap,60,8})==0&&call("ntdll.dll","rtlarebitsset",{bmap,60,8})==0);
    call("ntdll.dll","rtlsetbits",{bmap,30,4});
    CHECK(mem.r32(bbuf)==0xc00007f8u&&mem.r32(bbuf+4)==0x3u);
    // ntdll's _vsnprintf: writes min(len, cap) bytes, appends NUL only when len < cap, returns -1 on truncation.
    CHECK(call("ntdll.dll","_vsnprintf",{vout,8,vfmt,vap})==4&&mem.r8(vout)=='v'&&mem.r8(vout+3)=='2'&&mem.r8(vout+4)==0);
    for(uint32_t i=0;i<6;++i)mem.w8(vout+i,0x55);
    CHECK(call("ntdll.dll","_vsnprintf",{vout,4,vfmt,vap})==4&&mem.r8(vout)=='v'&&mem.r8(vout+1)=='='&&mem.r8(vout+2)=='4'&&mem.r8(vout+3)=='2'&&mem.r8(vout+4)==0x55);
    for(uint32_t i=0;i<6;++i)mem.w8(vout+i,0x55);
    CHECK(call("ntdll.dll","_vsnprintf",{vout,3,vfmt,vap})==0xffffffffu&&mem.r8(vout)=='v'&&mem.r8(vout+1)=='='&&mem.r8(vout+2)=='4'&&mem.r8(vout+3)==0x55);
    CHECK(call("ntdll.dll","_vsnprintf",{0,0,vfmt,vap})==4);

    std::string profile="; comment\r\n[Video]\r\nWidth = 1280\r\nTitle=\"A test\"\r\n[Other]\r\nWidth=1\r\n";
    bool saw_wide_path=false;host.file_call=[&](Host::FileCall& q){if(q.kind==Host::FileCall::Kind::CreateW){std::string path(q.path_w.begin(),q.path_w.end());saw_wide_path|=path=="settings.ini";if(path=="missing.ini"){q.error=2;return;}q.value=42;if(q.args[2]==2)profile.clear();q.succeeded=true;}else if(q.kind==Host::FileCall::Kind::Size){q.value=profile.size();q.succeeded=q.handle==42;}else if(q.kind==Host::FileCall::Kind::Read){q.data.assign(profile.begin(),profile.end());q.args[1]=profile.size();q.succeeded=q.handle==42;}else if(q.kind==Host::FileCall::Kind::Write){profile.assign(q.data.begin(),q.data.end());q.args[0]=q.data.size();q.succeeded=q.handle==42;}else if(q.kind==Host::FileCall::Kind::Close)q.succeeded=q.handle==42;};
    CHECK(Guest::profile_value("; c\n[One]\n;key\nName = \"hello\"\n[Two]\nname=bad\n","one","name","fallback")=="hello");
    uint32_t section=g.put_str("video"),key=g.put_str("width"),def=g.put_str("640"),file=g.put_str("settings.ini"),missing=g.put_str("missing.ini");
    CHECK(call("kernel32.dll","getprivateprofileinta",{section,key,640,missing})==640);
    CHECK(call("kernel32.dll","getprivateprofilestringa",{section,key,def,0x3000,8,file})==4);
    CHECK(mem.r8(0x3000)=='1'&&mem.r8(0x3001)=='2'&&mem.r8(0x3004)==0);
    CHECK(call("kernel32.dll","getprivateprofileinta",{section,key,640,file})==1280);
    CHECK(call("kernel32.dll","getprivateprofilestringa",{section,key,def,0x3000,3,file})==2&&mem.r8(0x3002)==0);
    CHECK(call("kernel32.dll","getprivateprofilesectiona",{section,0x3300,32,file})>0&&mem.r8(0x3300)=='W');
    uint32_t new_key=g.put_str("Height"),new_value=g.put_str("720");
    CHECK(call("kernel32.dll","writeprivateprofilestringa",{section,new_key,new_value,file})==1);
    CHECK(call("kernel32.dll","getprivateprofileinta",{section,new_key,0,file})==720);
    uint32_t wsec=g.put_wstr(u"Video"),wkey=g.put_wstr(u"Title"),wdef=g.put_wstr(u"none"),wfile=g.put_wstr(u"settings.ini");
    CHECK(call("kernel32.dll","getprivateprofilestringw",{wsec,wkey,wdef,0x3100,5,wfile})==4);
    CHECK(mem.r(0x3100,2)=='A'&&mem.r(0x3108,2)==0&&saw_wide_path);

    // Minimal mapped PE32 resource tree: type 10 / name 101 / LANGID 0x0409.
    uint32_t base=g.image_base,nt=base+0x80,opt=nt+24,root=base+0x1000;
    mem.w32(base+0x3c,0x80);mem.w32(nt,0x4550);mem.w32(opt+112,0x1000);
    mem.w(root+12,0,2);mem.w(root+14,1,2);mem.w32(root+16,10);mem.w32(root+20,0x80000020);
    mem.w(root+0x20+12,0,2);mem.w(root+0x20+14,1,2);mem.w32(root+0x20+16,101);mem.w32(root+0x20+20,0x80000040);
    mem.w(root+0x40+12,0,2);mem.w(root+0x40+14,1,2);mem.w32(root+0x40+16,0x409);mem.w32(root+0x40+20,0x60);
    mem.w32(root+0x60,0x2000);mem.w32(root+0x64,4);mem.w32(base+0x2000,0x12345678);
    uint32_t rs=call("kernel32.dll","findresourcea",{0,101,10});CHECK(rs!=0);
    CHECK(call("kernel32.dll","sizeofresource",{0,rs})==4);
    CHECK(call("kernel32.dll","lockresource",{call("kernel32.dll","loadresource",{0,rs})})==base+0x2000);
    CHECK(call("kernel32.dll","findresourceexa",{0,10,101,0x409})!=0);
    CHECK(call("kernel32.dll","freeresource",{rs})==0);

    host.socket_call=[](Host::SocketCall& q){if(q.kind==Host::SocketCall::Kind::Startup){q.result=0;q.data.assign(400,0);q.data[0]=2;q.data[1]=2;}else q.result=0;};
    uint32_t wsadata=0x3200;CHECK(call("ws2_32.dll","#115",{0x202,wsadata})==0&&mem.r(wsadata,2)==0x202);
    CHECK(call("ws2_32.dll","#116",{})==0&&call("ws2_32.dll","#111",{})==0);
    std::printf("x86-console-ini-res-ws2: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
