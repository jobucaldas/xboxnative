// SPDX-License-Identifier: GPL-3.0-or-later
#include "../msvc_load_config.h"
#include <cassert>
#include <cstdio>
using namespace wxl::msvc_config;
struct Fixture {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x4400);
    File file;
    Fixture() {
        file.size=bytes.size(); file.headers=0x400; file.imageSize=0x5000; file.preferred=0x180000000;
        file.sections={{0x1000,0x1000,0x400,0x1000,read|execute},
                       {0x2000,0x1000,0x1400,0x1000,read},
                       {0x3000,0x1000,0x2400,0x1000,read|write},
                       {0x4000,0x1000,0x3400,0x1000,read}};
        file.config={0x2100,0x140}; file.reloc={0x4000,0};
        put<uint32_t>(0x2100,0x140); put<uint64_t>(0x3100,default_cookie);
        size_t slot=0x2300;
        for(size_t off:{0x58,0x70,0x78,0x118,0x120,0x128,0x130,0x138}) {
            size_t target=off==0x58?0x3100:slot;
            put<uint64_t>(0x2100+off,file.preferred+target); reloc(0x2100+off);
            if(off!=0x58) {
                if(off!=0x130) { put<uint64_t>(slot,file.preferred+0x1100); reloc(slot); }
                slot+=8;
            }
        }
    }
    size_t offset(size_t rva) { return rva-0x1000+0x400; }
    template<class T> void put(size_t rva,T value) { memcpy(bytes.data()+offset(rva),&value,sizeof value); }
    void reloc(size_t rva,unsigned type=10) {
        auto pos=0x4000+file.reloc.size;
        put<uint32_t>(pos,uint32_t(rva&~size_t(4095))); put<uint32_t>(pos+4,12);
        put<uint16_t>(pos+8,uint16_t(type<<12|(rva&4095))); put<uint16_t>(pos+10,0);
        file.reloc.size+=12;
    }
    void imports(bool delay,size_t destination) {
        put<uint64_t>(0x2800,0x2900); put<uint64_t>(0x2808,0);
        memcpy(bytes.data()+offset(0x2850),"fixture.dll",12);
        memcpy(bytes.data()+offset(0x2902),"observe",8);
        if(delay) {
            file.delay={0x2600,64};
            put<uint32_t>(0x2600,1); put<uint32_t>(0x2604,0x2850);
            put<uint32_t>(0x260c,uint32_t(destination)); put<uint32_t>(0x2610,0x2800);
        } else {
            file.imports={0x2500,40};
            put<uint32_t>(0x2500,0x2800); put<uint32_t>(0x250c,0x2850);
            put<uint32_t>(0x2510,uint32_t(destination));
        }
    }
    void tls() {
        file.tls={0x2700,40};
        put<uint64_t>(0x2700,file.preferred+0x3300);
        put<uint64_t>(0x2708,file.preferred+0x3308);
        put<uint64_t>(0x2710,file.preferred+0x3200);
        put<uint64_t>(0x2718,file.preferred+0x2780);
        put<uint64_t>(0x2780,file.preferred+0x1100);
        for(size_t rva:{0x2700,0x2708,0x2710,0x2718,0x2780}) reloc(rva);
    }
    bool valid() {
        file.bytes=bytes.data(); Plan plan; auto before=bytes;
        bool accepted=validate(file,plan); assert(bytes==before); return accepted;
    }
};
static void strings() {
    // Adjacent virtual sections need not be adjacent (or ordered) in the file.
    std::vector<unsigned char> bytes(0x3000,'a');
    File f; f.bytes=bytes.data(); f.size=bytes.size(); f.imageSize=0x3000;
    f.sections={{0x1000,0x800,0x2000,0x800,read},
                {0x1800,0x800,0,0x800,read},
                {0x2000,0x1000,0x1000,0x1000,read}};
    auto check=[&](size_t rva,bool accepted,size_t length) {
        std::string out="stale";
        assert(f.string(rva,out)==accepted && out==std::string(length,'a'));
    };
    bytes[0]=0;
    check(0x17fe,true,2); // terminator in the next section
    check(0x1800,false,0); // nonempty is mandatory
    bytes[0]='a'; bytes[1]=0;
    check(0x17fe,true,3);
    bytes[1]='a'; bytes[0x27ff]=0;
    check(0x17fe,true,1); // terminator at the last byte of a span
    bytes[0x27ff]='a';
    for(unsigned kind=0;kind<6;++kind) {
        auto saved=f;
        if(kind==0) ++f.sections[1].rva; // virtual hole, not a contiguous file scan
        if(kind==1) f.sections[1].flags=write|execute; // no read permission
        if(kind==2) --f.sections[0].rawSize; // virtual tail without raw bytes
        if(kind==3) --f.sections[0].virtualSize; // raw padding without virtual bytes
        if(kind==4) f.size=0x27ff; // physical file ends before the virtual span
        if(kind==5) f.imageSize=0x17ff; // image ends before the section does
        check(0x17fe,false,kind<2?2:1);
        f=saved;
    }
    // Readable writable/executable names are not newly forbidden by scanning.
    f.sections[1].flags=read|write|execute; bytes[0]=0;
    check(0x17fe,true,2); bytes[0]='a';
    // The 4096-byte budget INCLUDES the terminator, across three raw spans.
    bytes[0x1001]=0; check(0x1002,true,4095);
    bytes[0x1001]='a'; bytes[0x1002]=0; check(0x1002,false,4096);
    check(0x1003,true,4095);
    check(f.imageSize,false,0); check(SIZE_MAX,false,0);
    // Maximum section/name fanout, without a machine-dependent timing gate.
    std::vector<unsigned char> large(96*4096,'a'); large.back()=0;
    File many; many.bytes=large.data(); many.size=large.size(); many.imageSize=0x1000+large.size();
    for(unsigned i=0;i<96;++i) many.sections.push_back({0x1000+i*4096,4096,i*4096,4096,read});
    std::string name;
    for(unsigned i=0;i<4096;++i)
        assert(many.string(0x1000+95*4096,name) && name==std::string(4095,'a'));
    puts("MSVC-CONFIG strings PASS: nonempty, adjacent spans, holes, permissions, raw/virtual/file/image bounds, 4096-byte budget");
}
int main() {
    strings();
    Fixture base; assert(base.valid());
    { auto f=base; f.imports(false,0x3400); f.imports(true,0x3500); f.tls(); assert(f.valid()); }
    // Retain the callback array location even when it contains only a terminator;
    // distinguish that from an absent array for deferred mapped-image checks.
    for(unsigned kind=0;kind<3;++kind) {
        auto f=base; f.tls();
        if(kind) {
            f.put<uint64_t>(0x2780,0);
            f.put<uint16_t>(0x4000+f.file.reloc.size-4,0); // no callback DIR64
        }
        if(kind==2) {
            f.put<uint64_t>(0x2718,0);
            f.put<uint16_t>(0x4000+f.file.reloc.size-16,0); // no array VA DIR64
        }
        assert(f.valid()); Plan plan;
        assert(validate(f.file,plan) && plan.tls.present);
        assert(plan.tls.callbackArray==(kind==2?0:0x2780));
        assert(plan.tls.callbacks.size()==(kind==0?1:0));
        if(!kind) assert(plan.tls.callbacks[0]==0x1100);
    }
    // Every sensitive span is tested with an otherwise valid plan, including
    // overlaps at either end, exact adjacency, IAT terminators and TLS aliases.
    for(bool delay:{false,true}) for(size_t target:{0x30fc,0x3100,0x3104,0x2158,0x2300,0x2328,0x2329,0x30f8}) {
        auto f=base; f.imports(delay,target); assert(!f.valid());
    }
    for(bool delay:{false,true}) {
        auto f=base; f.imports(delay,0x30f0); assert(f.valid());
        auto g=base; g.imports(delay,0x3108); assert(g.valid());
    }
    for(size_t target:{0x30fc,0x3100,0x3104,0x20fc,0x2159,0x223c,0x2301,0x2328,0x2329,0x2ffd}) {
        auto f=base; f.reloc(target); assert(!f.valid());
    }
    { auto f=base; f.reloc(0x2158); assert(!f.valid()); }
    { auto f=base; f.put<uint16_t>(0x4008,0); assert(!f.valid()); }
    { auto f=base; f.reloc(0x3fff); assert(!f.valid()); }
    { auto f=base; f.reloc(0x3108); assert(f.valid()); }
    { auto f=base; f.reloc(0x30f8); assert(f.valid()); }
    { auto f=base; f.imports(false,0x3400); f.reloc(0x3400); assert(!f.valid()); }
    { auto f=base; f.imports(false,0x3400); f.reloc(0x3408); assert(!f.valid()); }
    { auto f=base; f.imports(false,0x3400); f.imports(true,0x3400); assert(!f.valid()); }
    for(size_t offset:{size_t(0),size_t(8),size_t(16)}) {
        auto f=base; f.tls();
        f.put<uint64_t>(0x2700+offset,f.file.preferred+0x3100);
        if(offset==0) f.put<uint64_t>(0x2708,f.file.preferred+0x3108);
        assert(!f.valid());
    }
    { auto f=base; f.tls(); f.put<uint64_t>(0x2708,f.file.preferred+0x3300); assert(f.valid()); }
    { auto f=base; f.tls(); f.put<uint64_t>(0x2700,f.file.preferred+0x3100); f.put<uint64_t>(0x2708,f.file.preferred+0x3100); assert(!f.valid()); }
    for(size_t target:{0x2701,0x2720,0x2788,0x3200,0x32fc,0x3300}) {
        auto f=base; f.tls(); f.reloc(target); assert(!f.valid());
    }
    // Cookie permissions / raw AND virtual bounds / raw aliases and holes.
    { auto f=base; f.file.sections[2].flags=read; assert(!f.valid()); }
    { auto f=base; f.file.sections[1].flags=read|execute; assert(!f.valid()); }
    { auto f=base; f.file.sections[1].flags=0; assert(!f.valid()); }
    { auto f=base; f.file.sections[0].flags=read; assert(!f.valid()); }
    for(bool raw:{false,true}) for(size_t end:{0x240,0x307}) {
        auto f=base;
        if(raw) f.file.sections[1].rawSize=uint32_t(end); else f.file.sections[1].virtualSize=uint32_t(end);
        assert(!f.valid());
    }
    { auto f=base; f.file.sections[2].raw=f.file.sections[1].raw; assert(!f.valid()); }
    { auto f=base; f.file.sections[2].raw=0; assert(!f.valid()); }
    { auto f=base; f.file.sections[2].rva=0x2800; assert(!f.valid()); }
    { auto f=base; f.put<uint64_t>(0x2158,f.file.preferred+0x5000); assert(!f.valid()); }
    { auto f=base; f.file.dllCharacteristics=0x4000; assert(!f.valid()); }
    { auto f=base; f.put<uint32_t>(0x2190,0x100); f.put<uint64_t>(0x2200,f.file.preferred+0x2400); assert(!f.valid()); }
    uint64_t value=17;
    assert(!cookie(nullptr,value) && value==17);
    assert(!cookie(+[](uint64_t&){return false;},value) && value==17);
    assert(!cookie(+[](uint64_t& v){v=0;return true;},value) && value==17);
    assert(!cookie(+[](uint64_t& v){v=default_cookie;return true;},value) && value==17);
    assert(cookie(+[](uint64_t& v){v=0xffff123456789abcULL;return true;},value) && value==0x123456789abcULL);
    puts("MSVC-CONFIG pure PASS: immutable byte validation, bounds, writes, relocations, TLS and bounded entropy");
}
