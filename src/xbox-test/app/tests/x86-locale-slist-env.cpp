// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
int main(){
    Memory mem;mem.map(0x1000,0x20000,PROT_R|PROT_W);mem.map(TEB_ADDR,0x1000,PROT_R|PROT_W);
    ThunkTable th;Host host;Registry reg;Guest guest(mem,host,reg);guest.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x1f000;
    auto invoke=[&](const char* dll,const char* name,std::initializer_list<uint32_t> args){auto it=reg.map.find({dll,name});if(it==reg.map.end())return 0xffffffffu;uint32_t i=0;for(uint32_t a:args)mem.w32(cpu.reg[ESP]+4+4*i++,a);return it->second.fn(cpu);};
    int fails=0;auto check=[&](bool x,const char* what){if(!x){std::printf("FAIL %s\n",what);++fails;}};
    uint32_t locale=0x1100,out=0x1200;mem.w32(locale,uint32_t('e')|(uint32_t('n')<<16));mem.w32(locale+4,uint32_t('-')|(uint32_t('U')<<16));mem.w32(locale+8,uint32_t('S')|(uint32_t(0)<<16));
    host.nls_call=[](const Host::NlsRequest& q){Host::NlsResponse r{};if(q.operation==Host::NlsOperation::GetLocaleInfoA){r.result=4;r.last_error=55;r.output={'U','S','A',0};}else if(q.operation==Host::NlsOperation::GetLocaleInfoW||q.operation==Host::NlsOperation::GetLocaleInfoEx){r.result=4;r.last_error=66;r.output={'U',0,'S',0,'A',0,0,0};}return r;};
    check(invoke("kernel32.dll","getlocaleinfoa",{0x409,1,0,0})==4,"locale A size query");guest.set_last_error(4321);check(invoke("kernel32.dll","getlocaleinfoa",{0x409,1,out,4})==4&&mem.r8(out)=='U'&&guest.last_error()==4321,"locale A output preserves error");
    check(invoke("kernel32.dll","getlocaleinfow",{0x409,1,out,4})==4&&mem.r(out,2)=='U',"locale W output");check(invoke("kernel32.dll","getlocaleinfoex",{locale,1,out,4})==4&&mem.r(out+4,2)=='A',"locale Ex output");
    uint32_t name=0x1300,value=0x1320,buf=0x1400;mem.write_bytes(name,"XBN_TEST\0",9);mem.write_bytes(value,"works\0",6);
    check(invoke("kernel32.dll","setenvironmentvariablea",{name,value})==1,"set env A");check(invoke("kernel32.dll","getenvironmentvariablea",{name,0,0})==6,"get env size A");check(invoke("kernel32.dll","getenvironmentvariablea",{name,buf,16})==5&&mem.r8(buf)=='w',"get env A");check(invoke("kernel32.dll","getenvironmentvariablea",{name,buf,3})==6&&guest.last_error()==122,"env A short buffer");uint32_t env_a=invoke("kernel32.dll","getenvironmentstringsa",{});check(mem.r8(env_a)=='X'&&invoke("kernel32.dll","freeenvironmentstringsa",{env_a})==1,"environment strings A");
    uint32_t wname=0x1600,wvalue=0x1640,wbuf=0x1680;const char16_t wn[]=u"XBN_WIDE",wv[]=u"ok";for(unsigned i=0;i<sizeof(wn)/2;++i)mem.w(wname+2*i,wn[i],2);for(unsigned i=0;i<sizeof(wv)/2;++i)mem.w(wvalue+2*i,wv[i],2);
    check(invoke("kernel32.dll","setenvironmentvariablew",{wname,wvalue})==1,"set env W");check(invoke("kernel32.dll","getenvironmentvariablew",{wname,0,0})==3,"get env size W");check(invoke("kernel32.dll","getenvironmentvariablew",{wname,wbuf,8})==2&&mem.r(wbuf,2)=='o',"get env W");uint32_t env_w=invoke("kernel32.dll","getenvironmentstringsw",{});check(mem.r(env_w,2)=='X'&&invoke("kernel32.dll","freeenvironmentstringsw",{env_w})==1,"environment strings W");
    check(invoke("kernel32.dll","getenvironmentvariablew",{name,0,0})==0&&guest.last_error()==203,"missing env W");
    uint32_t head=0x1500,e1=0x1520,e2=0x1530;invoke("kernel32.dll","initializeslisthead",{head});check(mem.r32(head)==0&&mem.r32(head+4)==0,"SList init layout");
    mem.w32(e1,0xaaaa);mem.w32(e2,0xbbbb);check(invoke("kernel32.dll","interlockedpushentryslist",{head,e1})==0,"SList first push");check(invoke("kernel32.dll","interlockedpushentryslist",{head,e2})==e1,"SList prior head");check(invoke("kernel32.dll","querydepthslist",{head})==2,"SList depth");
    check(invoke("kernel32.dll","interlockedpopentryslist",{head})==e2&&mem.r32(head)==e1,"SList pop");check(invoke("kernel32.dll","interlockedflushslist",{head})==e1&&mem.r32(head)==0&&invoke("kernel32.dll","querydepthslist",{head})==0,"SList flush");
    const std::pair<const char*,uint8_t> slist_imports[]={{"initializeslisthead",1},{"rtlinitializeslisthead",1},{"interlockedpushentryslist",2},{"rtlinterlockedpushentryslist",2},{"interlockedpopentryslist",1},{"rtlinterlockedpopentryslist",1},{"interlockedflushslist",1},{"rtlinterlockedflushslist",1},{"querydepthslist",1},{"rtlquerydepthslist",1}};for(const auto& item:slist_imports){auto it=reg.map.find({"kernel32.dll",item.first});check(it!=reg.map.end()&&it->second.argc==item.second,"SList import and x86 arity");}
    std::printf("x86-locale-slist-env: %d failures\n",fails);return fails?1:0;
}
