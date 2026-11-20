// SPDX-License-Identifier: GPL-3.0-or-later
// kernel32 version routes (GetFileVersionInfo*, VerQueryValue*) over the RT_VERSION resource of a PE file.
// x86-version.sh builds the two fixtures: one with a VERSIONINFO resource, one without.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); } } while (0)

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    if (argc != 3) { std::printf("usage: x86-version <with-version.exe> <no-version.exe>\n"); return 2; }
    const auto with_version = read_file(argv[1]);
    const auto no_version = read_file(argv[2]);
    if (with_version.empty() || no_version.empty()) { std::printf("FAIL cannot read fixtures\n"); return 2; }
    Memory mem;mem.map(0x1000,0x30000,PROT_R|PROT_W);ThunkTable th;Host host;Registry reg;Guest g(mem,host,reg);g.cur_teb=0x1000;g.register_all();Cpu cpu(mem,th);cpu.reg[ESP]=0x2f000;
    host.destructive_file_guard=[](const char*,const std::u16string&){return true;};
    // Handle 1 is the versioned fixture, handle 2 the one without a version resource.
    host.file_call=[&](Host::FileCall& q){
        switch(q.kind){
        case Host::FileCall::Kind::CreateW:
            if(q.path_w==u"fixture.exe"){q.value=1;}
            else if(q.path_w==u"plain.exe"){q.value=2;}
            else{q.error=2;return;}
            q.succeeded=true;return;
        case Host::FileCall::Kind::Size:
            q.value=q.handle==1?with_version.size():no_version.size();q.args[1]=0;q.succeeded=true;return;
        case Host::FileCall::Kind::Read:{
            const auto& file=q.handle==1?with_version:no_version;
            const size_t n=std::min<size_t>(q.args[0],file.size());
            q.data.assign(file.begin(),file.begin()+n);q.args[1]=uint32_t(n);q.succeeded=true;return;}
        case Host::FileCall::Kind::Close:q.succeeded=true;return;
        default:q.error=120;return;
        }};
    auto kcall=[&](const char* n,std::initializer_list<uint32_t> a){unsigned i=0;for(uint32_t v:a)mem.w32(cpu.reg[ESP]+4+4*i++,v);return reg.map.at({"kernel32.dll",n}).fn(cpu);};

    const struct{const char* n;unsigned argc;}arity[]={{"getfileversioninfosizea",2},{"getfileversioninfosizew",2},{"getfileversioninfosizeexa",3},{"getfileversioninfosizeexw",3},{"getfileversioninfoa",4},{"getfileversioninfow",4},{"getfileversioninfoexa",5},{"getfileversioninfoexw",5},{"verqueryvaluea",4},{"verqueryvaluew",4}};
    for(const auto& e:arity){auto it=reg.map.find({"kernel32.dll",e.n});CHECK(it!=reg.map.end());if(it!=reg.map.end()){CHECK(it->second.conv==Conv::Stdcall&&it->second.argc==e.argc);}}

    uint32_t fixa=g.put_str("fixture.exe"),fixw=g.put_wstr(u"fixture.exe"),plaina=g.put_str("plain.exe"),missingw=g.put_wstr(u"missing.exe");
    // Size queries: the handle output is always 0; failures carry the host or PE error.
    const uint32_t hp=0x8000;
    const uint32_t size=kcall("getfileversioninfosizew",{fixw,hp});
    CHECK(size>=52&&mem.r32(hp)==0&&g.last_error()==0);
    CHECK(kcall("getfileversioninfosizea",{fixa,hp})==size);
    CHECK(kcall("getfileversioninfosizeexw",{0,fixw,hp})==size);
    CHECK(kcall("getfileversioninfosizeexa",{0,fixa,hp})==size);
    CHECK(kcall("getfileversioninfosizew",{missingw,hp})==0&&g.last_error()==2);
    CHECK(kcall("getfileversioninfosizea",{plaina,hp})==0&&g.last_error()==1813);
    CHECK(kcall("getfileversioninfosizew",{0,hp})==0&&g.last_error()==87);

    // Block copies: the buffer must hold the whole block, and the first word is its length.
    const uint32_t buf=0x8100;
    CHECK(kcall("getfileversioninfow",{fixw,0,size,buf})==1&&g.last_error()==0);
    CHECK(mem.r(buf,2)==size);
    CHECK(kcall("getfileversioninfoa",{fixa,0,size,buf})==1);
    CHECK(kcall("getfileversioninfoexw",{0,fixw,0,size,buf})==1);
    CHECK(kcall("getfileversioninfoexa",{0,fixa,0,size,buf})==1);
    CHECK(kcall("getfileversioninfow",{fixw,0,size-1,buf})==0&&g.last_error()==122);
    CHECK(kcall("getfileversioninfow",{fixw,0,size,0})==0&&g.last_error()==122);
    CHECK(kcall("getfileversioninfow",{missingw,0,size,buf})==0&&g.last_error()==2);

    // VerQueryValue: the root is VS_FIXEDFILEINFO, sub-blocks follow the documented tree, keys fold case.
    const uint32_t out=0x8800,lenp=0x8810;
    const uint32_t rootw=g.put_wstr(u"\\"),roota=g.put_str("\\");
    CHECK(kcall("verqueryvaluew",{buf,rootw,out,lenp})==1&&mem.r32(lenp)==52);
    const uint32_t fixed=mem.r32(out);
    CHECK(mem.r32(fixed)==0xfeef04bdu&&mem.r32(fixed+8)==0x00010002u&&mem.r32(fixed+12)==0x00030004u);
    CHECK(kcall("verqueryvaluea",{buf,roota,out,lenp})==1&&mem.r32(lenp)==52&&mem.r32(out)==fixed);
    const uint32_t transw=g.put_wstr(u"\\VarFileInfo\\Translation");
    CHECK(kcall("verqueryvaluew",{buf,transw,out,lenp})==1&&mem.r32(lenp)==4);
    const uint32_t trans=mem.r32(out);
    CHECK(mem.r8(trans)==0x09&&mem.r8(trans+1)==0x04&&mem.r8(trans+2)==0xb0&&mem.r8(trans+3)==0x04);
    // Text values: the wide caller gets UTF-16 in the block, the ANSI caller gets a narrow copy; length counts the NUL.
    const uint32_t prodw=g.put_wstr(u"\\StringFileInfo\\040904b0\\ProductName");
    CHECK(kcall("verqueryvaluew",{buf,prodw,out,lenp})==1&&mem.r32(lenp)==11);
    const uint32_t prodp=mem.r32(out);
    CHECK(mem.r8(prodp)=='X'&&mem.r8(prodp+2)=='b'&&mem.r8(prodp+20)==0&&mem.r8(prodp+21)==0);
    const uint32_t proda=g.put_str("\\StringFileInfo\\040904b0\\ProductName");
    CHECK(kcall("verqueryvaluea",{buf,proda,out,lenp})==1&&mem.r32(lenp)==11);
    const uint32_t prodn=mem.r32(out);
    CHECK(prodn!=prodp&&mem.r8(prodn)=='X'&&mem.r8(prodn+1)=='b'&&mem.r8(prodn+9)=='e'&&mem.r8(prodn+10)==0);
    CHECK(kcall("verqueryvaluea",{buf,proda,out,lenp})==1&&mem.r32(out)==prodn);
    const uint32_t lowera=g.put_str("\\stringfileinfo\\040904B0\\productname");
    CHECK(kcall("verqueryvaluea",{buf,lowera,out,lenp})==1&&mem.r32(lenp)==11);
    // Misses and bad arguments; the puLen output is optional.
    const uint32_t nope=g.put_str("\\StringFileInfo\\040904b0\\Nope"),otherlang=g.put_str("\\StringFileInfo\\04e40000\\ProductName");
    CHECK(kcall("verqueryvaluea",{buf,nope,out,lenp})==0&&g.last_error()==1813);
    CHECK(kcall("verqueryvaluea",{buf,otherlang,out,lenp})==0&&g.last_error()==1813);
    CHECK(kcall("verqueryvaluea",{buf,roota,0,lenp})==0&&g.last_error()==87);
    CHECK(kcall("verqueryvaluea",{buf,roota,out,0})==1);
    CHECK(kcall("verqueryvaluew",{0x8900,rootw,out,lenp})==0&&g.last_error()==87);

    std::printf("x86-version: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
