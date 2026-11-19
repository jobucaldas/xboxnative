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
