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
    bool valid(bool play=false) {
        file.bytes=bytes.data(); Plan plan; auto before=bytes;
        bool accepted=validate(file,plan,play); assert(bytes==before); return accepted;
    }
    // Control Flow Guard as a Microsoft-built DLL carries it (concrt140,
    // d3dcompiler_47, ucrtbase): GUARD_CF, a relocated table VA, count, flags.
    void cfg(uint64_t count=4,uint32_t flags=0x500,size_t table=0x2b00) {
        put<uint64_t>(0x2180,file.preferred+table); reloc(0x2180);
        put<uint64_t>(0x2188,count); put<uint32_t>(0x2190,flags);
        file.dllCharacteristics|=0x4000;
    }
    // Megabonk/Unity shape: GuardFlags CF_INSTRUMENTED plus a relocated
    // VolatileMetadataPointer into read-only data (structural, owned bytes).
    void instrumented(size_t metadata=0x2400,bool relocate=true) {
        put<uint32_t>(0x2190,0x100);
        put<uint64_t>(0x2200,file.preferred+metadata);
        if(relocate) reloc(0x2200);
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
    // Play profile (LoadConfig::msvc_x64): the owned cookie-only record still
    // passes; the instrumented record passes ONLY in Play; everything else rejects.
    assert(base.valid(true));
    {
        auto f=base; f.instrumented(); assert(!f.valid()); assert(f.valid(true));
        Plan plan; f.file.bytes=f.bytes.data();
        assert(validate(f.file,plan,true) && plan.guardFlags==0x100 && plan.volatileMetadata==0x2400);
        Plan strict; assert(!validate(f.file,strict));
    }
    { auto f=base; f.put<uint32_t>(0x2190,0x100); assert(!f.valid()); assert(f.valid(true)); }
    { auto f=base; f.instrumented(); f.imports(false,0x3400); f.imports(true,0x3500); f.tls(); assert(f.valid(true)); }
    for(uint32_t flags:{0x101u,0x200u,0x500u,0x10500u,0x4100u}) {
        auto f=base; f.instrumented(); f.put<uint32_t>(0x2190,flags); assert(!f.valid(true));
    }
    { auto f=base; f.instrumented(0x2400,false); assert(!f.valid(true)); } // no DIR64
    for(size_t metadata:{0x3200,0x1200,0x2120,0x2ffc,0x5000,0x4ffc}) { // RW, code, record, straddle, beyond image
        auto f=base; f.instrumented(metadata); assert(!f.valid(true));
    }
    { auto f=base; f.instrumented(); f.file.dllCharacteristics=0x4000; assert(!f.valid(true)); } // GUARD_CF
    for(size_t field:{0x2180,0x2188,0x2198,0x21a0,0x2208}) { // GuardCF table/count, after flags, after metadata
        auto f=base; f.instrumented(); f.put<uint8_t>(field,1); assert(!f.valid(true));
    }
    // CFG present, never enforced (Wine 10 x86_64): MSVC DLLs such as concrt140.dll
    // and d3dcompiler_47.dll pass ONLY in Play; the table is bounds-checked, never used.
    {
        auto f=base; f.cfg(); assert(!f.valid()); assert(f.valid(true));
        Plan plan; f.file.bytes=f.bytes.data();
        assert(validate(f.file,plan,true) && plan.guardFlags==0x500 && plan.cfgEntries==4 &&
               plan.cfgTable.rva==0x2b00 && plan.cfgTable.size==16 && plan.protectedWrite(0x2b00,4));
        Plan strict; assert(!validate(f.file,strict));
        for(uint32_t flags:{0x500u,0x3500u,0x13500u,0x10013500u,0xf0000500u,0x1500u}) {
            auto g=base; g.cfg(4,flags); assert(g.valid(true));
        }
        { auto g=base; g.cfg(5,0x10000500u); g.file.bytes=g.bytes.data(); Plan q;
          assert(validate(g.file,q,true) && q.cfgTable.size==25); } // stride nibble 1: 5 bytes each
        { auto g=base; g.cfg(0x100,0x500,0x2b00); assert(g.valid(true)); } // 1 KiB table
        { auto g=base; g.instrumented(); g.cfg(); g.imports(false,0x3400); g.imports(true,0x3500); g.tls(); assert(g.valid(true)); }
        { auto g=base; g.cfg(); g.put<uint32_t>(0x2190,0x100|0x400); assert(g.valid(true)); }
    }
    // Still rejected: every part of the CFG story that is not exactly the above.
    for(uint32_t flags:{0x700u,0x4500u,0x8500u,0x20500u,0x100500u,0x400500u,0x800500u,0x1000500u,0x2000500u,
                        0x900u,0x100u,0x0u,0x10100u,0x10000000u,0x500u|0x800u,0x500u|0x40000u}) {
        auto f=base; f.cfg(4,flags); assert(!f.valid(true)); // unknown bit, or flag/table mismatch
    }
    { auto f=base; f.cfg(); f.file.dllCharacteristics=0; assert(!f.valid(true)); } // table without GUARD_CF
    { auto f=base; f.cfg(); f.put<uint64_t>(0x2188,0); assert(!f.valid(true)); } // VA without count
    { auto f=base; f.put<uint64_t>(0x2188,4); f.put<uint32_t>(0x2190,0x500); f.file.dllCharacteristics|=0x4000; assert(!f.valid(true)); } // count without VA
    { auto f=base; f.cfg(); f.put<uint16_t>(0x4000+f.file.reloc.size-4,0); assert(!f.valid(true)); } // table VA not relocated
    { auto f=base; f.cfg(); f.reloc(0x2188); assert(!f.valid(true)); } // count is not a pointer
    { auto f=base; f.cfg(); f.reloc(0x2b04); assert(!f.valid(true)); } // table is never relocated
    { auto f=base; f.cfg(); f.reloc(0x2afc); assert(!f.valid(true)); } // straddles the table start
    { auto f=base; f.cfg(); f.reloc(0x2b0c); assert(!f.valid(true)); } // straddles the table end
    for(size_t table:{0x3200,0x1200,0x2120,0x2300,0x2308,0x2ff8,0x5000,0x4ffc,0x2000-0x10,0x3100}) {
        // RW, code, record, guard slots, past section end, beyond image, section gap, cookie
        auto f=base; f.cfg(4,0x500,table); assert(!f.valid(true));
    }
    { auto f=base; f.cfg(0x200,0x500,0x2ff0); assert(!f.valid(true)); } // too long for its section
    { auto f=base; f.cfg((uint64_t(1)<<22)+1); assert(!f.valid(true)); }
    { auto f=base; f.cfg(~uint64_t(0)); assert(!f.valid(true)); } // size overflow
    { auto f=base; f.cfg(); f.file.sections[1].flags=read|execute; assert(!f.valid(true)); }
    { auto f=base; f.cfg(); f.file.sections[1].rawSize=0x300; assert(!f.valid(true)); } // table not file-backed
    { auto f=base; f.cfg(); f.file.dllCharacteristics|=0x4000; f.put<uint8_t>(0x21a0,1); assert(!f.valid(true)); } // address-taken IAT table
    for(size_t field:{0x2194,0x21a8,0x21b0,0x21b8,0x21c0,0x21e8,0x21f0,0x21f8,0x2210,0x2220}) {
        auto f=base; f.cfg(); f.put<uint8_t>(field,1); assert(!f.valid(true)); // long-jump, EH, dynamic reloc, XFG, ... fields
    }
    { // check/dispatch pointers keep their in-image defaults: executable code, read-only slot
        auto f=base; f.cfg(); f.put<uint64_t>(0x2300,f.file.preferred+0x2000); assert(!f.valid(true)); // check ptr -> data
        auto g=base; g.cfg(); g.put<uint64_t>(0x2308,g.file.preferred+0x3000); assert(!g.valid(true)); // dispatch ptr -> RW data
        auto h=base; h.cfg(); h.put<uint64_t>(0x2300,0); assert(!h.valid(true)); // null check ptr
        auto j=base; j.cfg(); j.file.sections[1].flags=read|write; assert(!j.valid(true));
    }
    // The shorter 0x70-byte pre-CFG record (D3DCompiler_46, steam_api64): cookie and
    // SEH fields only. 0x90 and other sizes stay rejected; GUARD_CF has no table to describe.
    {
        auto f=base; f.put<uint32_t>(0x2100,0x70); f.file.config.size=0x70;
        assert(!f.valid()); assert(f.valid(true));
        Plan plan; f.file.bytes=f.bytes.data();
        assert(validate(f.file,plan,true) && plan.record.size==0x70 && plan.guardFlags==0 && plan.slots.empty());
        auto stray=f; stray.put<uint8_t>(0x2160,1); assert(!stray.valid(true));
        auto cf=f; cf.file.dllCharacteristics=0x4000; assert(!cf.valid(true));
        for(uint32_t size:{0x60u,0x6fu,0x71u,0x78u,0x90u,0x93u}) {
            auto g=base; g.put<uint32_t>(0x2100,size); g.file.config.size=size; assert(!g.valid(true));
        }
    }
    // Relocated thread_local initializers inside the TLS template are applied
    // before the template copy in Play; the TLS index never is.
    { auto f=base; f.tls(); f.reloc(0x3300); assert(!f.valid()); assert(f.valid(true)); }
    { auto f=base; f.tls(); f.reloc(0x3200); assert(!f.valid(true)); }
    // Real game DLLs (UnityPlayer, baselib, steam_api64): the TLS index lives in
    // the RW section's zero-filled tail. Play needs mapped RW extent only; the
    // fixture contract stays file-backed. Unaligned, out-of-image, RO or RX reject.
    {
        auto bss=base; bss.tls(); bss.file.sections[2].rawSize=0x800;
        bss.put<uint64_t>(0x2710,bss.file.preferred+0x3900);
        assert(!bss.valid()); assert(bss.valid(true));
        Plan plan; bss.file.bytes=bss.bytes.data();
        assert(validate(bss.file,plan,true) && plan.tls.index==0x3900);
        for(size_t index:{0x3902,0x5000,0x2900,0x1900}) {
            auto f=bss; f.put<uint64_t>(0x2710,f.file.preferred+index); assert(!f.valid(true));
        }
    }
    // Older MSVC records (discord_game_sdk: 0x100) carry only their own fields.
    {
        auto f=base; f.put<uint32_t>(0x2100,0x100); f.file.config.size=0x100;
        assert(!f.valid()); assert(f.valid(true));
        Plan plan; f.file.bytes=f.bytes.data();
        assert(validate(f.file,plan,true) && plan.record.size==0x100 && !plan.volatileMetadata);
        auto flagged=f; flagged.put<uint32_t>(0x2190,0x100); assert(flagged.valid(true));
        auto stray=f; stray.put<uint8_t>(0x2108,1); assert(!stray.valid(true)); // non-pointer field
        auto declared=f; declared.put<uint32_t>(0x2100,0x140); assert(!declared.valid(true));
        auto small=f; small.put<uint32_t>(0x2100,0x90); small.file.config.size=0x90; assert(!small.valid(true));
        auto large=base; large.put<uint32_t>(0x2100,0x148); large.file.config.size=0x148; assert(!large.valid(true));
    }
    // TLS alignment above 16 is not honoured by Windows/Wine heaps; Play accepts it.
    {
        auto f=base; f.tls(); f.put<uint32_t>(0x2724,6u<<20); assert(!f.valid()); assert(f.valid(true));
        f.put<uint32_t>(0x2724,15u<<20); assert(!f.valid(true));
    }
    // Delay imports in Play are left to the image's own helper (UnityPlayer's
    // d3d12.dll): no eager binding, a file-backed writable IAT that may be
    // relocated, and an optional writable module-handle slot (may be zero-fill).
    {
        auto lazy=base; lazy.imports(true,0x3500); lazy.put<uint32_t>(0x2608,0x3600);
        assert(!lazy.valid()); assert(lazy.valid(true));
        Plan plan; lazy.file.bytes=lazy.bytes.data();
        assert(validate(lazy.file,plan,true) && plan.imports.empty());
        auto relocated=lazy; relocated.reloc(0x3500); assert(relocated.valid(true));
        auto tail=lazy; tail.file.sections[2].rawSize=0x800; tail.put<uint32_t>(0x2608,0x3a00);
        assert(tail.valid(true));
        auto unbacked=tail; unbacked.imports(true,0x37f8); unbacked.put<uint32_t>(0x2608,0x3a00);
        assert(!unbacked.valid(true)); // IAT entries carry initial thunk VAs: file bytes
        for(uint32_t slot:{0x3604u,0x2600u,0x3100u,0x5000u}) { // unaligned, RO, cookie, beyond image
            auto g=lazy; g.put<uint32_t>(0x2608,slot); assert(!g.valid(true));
        }
        auto readonly=base; readonly.imports(true,0x2a00); assert(!readonly.valid(true)); // helper writes the IAT
        auto alias=base; alias.imports(false,0x3500); alias.imports(true,0x3500); assert(!alias.valid(true));
        auto bound=lazy; bound.put<uint32_t>(0x2614,0x2a00); assert(bound.valid(true));
        bound.put<uint32_t>(0x2614,0x1a00); assert(!bound.valid(true)); // executable bytes
    }
    // CastGuardOsDeterminedFailureMode as a writable variable (FMOD: .data,
    // often zero-fill). Play accepts it only while zero and never executable.
    {
        auto data=base; data.put<uint64_t>(0x2100+0x130,data.file.preferred+0x3500);
        assert(!data.valid()); assert(data.valid(true));
        auto tail=data; tail.file.sections[2].rawSize=0x800; tail.put<uint64_t>(0x2100+0x130,tail.file.preferred+0x3a00);
        assert(tail.valid(true));
        auto active=data; active.put<uint64_t>(0x3500,1); assert(!active.valid(true));
        auto code=base; code.put<uint64_t>(0x2100+0x130,code.file.preferred+0x1500); assert(!code.valid(true));
        auto beyond=base; beyond.put<uint64_t>(0x2100+0x130,beyond.file.preferred+0x5008); assert(!beyond.valid(true));
    }
    uint64_t value=17;
    assert(!cookie(nullptr,value) && value==17);
    assert(!cookie(+[](uint64_t&){return false;},value) && value==17);
    assert(!cookie(+[](uint64_t& v){v=0;return true;},value) && value==17);
    assert(!cookie(+[](uint64_t& v){v=default_cookie;return true;},value) && value==17);
    assert(cookie(+[](uint64_t& v){v=0xffff123456789abcULL;return true;},value) && value==0x123456789abcULL);
    puts("MSVC-CONFIG pure PASS: immutable byte validation, bounds, writes, relocations, TLS and bounded entropy");
    puts("MSVC-CONFIG play PASS: CF_INSTRUMENTED + relocated read-only volatile pointer accepted only by msvc_x64; other flags/fields/targets reject");
    puts("MSVC-CONFIG play PASS: zero-fill TLS index, 0x100 records, TLS alignment and helper-owned delay imports accepted only by msvc_x64");
    puts("MSVC-CONFIG play PASS: writable or zero-fill inactive CastGuard variable accepted only by msvc_x64");
    puts("MSVC-CONFIG play PASS: CFG function table present but unused (Wine 10 x86_64) and 0x70 records accepted only by msvc_x64; malformed tables/flags/pointers reject");
}
