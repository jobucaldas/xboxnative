// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace wxl {
// Deliberately narrower than Windows: owned AMD64, no-CFG, 0x140 MSVC record.
// All offsets below are IMAGE_LOAD_CONFIG_DIRECTORY64 (Windows SDK / Wine 10).
// This is a pure file-byte preflight; no OS calls and no mapped-image reads.
namespace msvc_config {
constexpr uint64_t default_cookie = 0x2b992ddfa232ULL;
constexpr uint32_t read = 0x40000000, write = 0x80000000, execute = 0x20000000;
struct Span { size_t rva=0, size=0; };
inline bool bounds(size_t p, size_t n, size_t end) { return p<=end && n<=end-p; }
inline bool overlaps(Span a, Span b) {
    return a.size && b.size && (a.rva<=b.rva ? b.rva-a.rva<a.size : a.rva-b.rva<b.size);
}
struct Section { uint32_t rva, virtualSize, raw, rawSize, flags; };
struct Directory { uint32_t rva=0, size=0; };
struct File {
    const unsigned char* bytes=nullptr; size_t size=0, headers=0, imageSize=0;
    uint64_t preferred=0;
    unsigned dllCharacteristics=0;
    std::vector<Section> sections;
    Directory config, reloc, imports, delay, tls;
    // Every accepted byte must exist in BOTH raw and virtual section extents.
    const unsigned char* at(size_t rva, size_t n, uint32_t required=read,
                            uint32_t forbidden=0) const {
        for (auto s:sections) if (rva>=s.rva && bounds(rva-s.rva,n,s.virtualSize) &&
            bounds(rva-s.rva,n,s.rawSize) && bounds(rva,n,imageSize) &&
            (s.flags&required)==required && !(s.flags&forbidden) &&
            bounds(s.raw+(rva-s.rva),n,size)) return bytes+s.raw+(rva-s.rva);
        return nullptr;
    }
    // Loader-written locations need only mapped (virtual) extent: a section's
    // zero-filled tail is image memory even though no file byte backs it.
    bool mapped(size_t rva, size_t n, uint32_t required, uint32_t forbidden) const {
        for (auto s:sections) if (rva>=s.rva && bounds(rva-s.rva,n,s.virtualSize) &&
            bounds(rva,n,imageSize) && (s.flags&required)==required && !(s.flags&forbidden)) return true;
        return false;
    }
    template<class T> bool get(size_t rva,T& value) const {
        auto p=at(rva,sizeof value); if(!p) return false;
        memcpy(&value,p,sizeof value); return true;
    }
    bool va(uint64_t value, size_t n, size_t& rva, uint32_t required=read,
            uint32_t forbidden=0) const {
        if(value<preferred || value-preferred>imageSize) return false;
        rva=size_t(value-preferred); return at(rva,n,required,forbidden)!=nullptr;
    }
    bool string(size_t rva,std::string& out) const {
        // Import names are bounded even for a huge otherwise-valid file.
        out.clear();
        size_t scanned=0;
        while(scanned<4096) {
            const unsigned char* p=nullptr; size_t n=0;
            // Sections have already been checked for aliases. Scan one backed,
            // readable span at a time, not one full section search per byte.
            for(auto s:sections) {
                if(rva<s.rva || !bounds(rva,1,imageSize)) continue;
                size_t off=rva-s.rva;
                if(!bounds(off,1,s.virtualSize) || !bounds(off,1,s.rawSize) ||
                    !(s.flags&read) || !bounds(size_t(s.raw)+off,1,size)) continue;
                n=std::min({size_t(s.virtualSize)-off,size_t(s.rawSize)-off,
                            size-(size_t(s.raw)+off),imageSize-rva,4096-scanned});
                p=bytes+size_t(s.raw)+off; break;
            }
            if(!p) return false;
            auto end=static_cast<const unsigned char*>(memchr(p,0,n));
            out.append(reinterpret_cast<const char*>(p),end?size_t(end-p):n);
            if(end) return !out.empty();
            rva+=n; scanned+=n;
        }
        return false;
    }
};
struct Relocation { size_t rva; uint64_t value; };
struct Import { size_t destination; std::string dll,name; uint16_t ordinal=0; };
struct Pointer { size_t rva, target; bool null=false; };
struct Tls {
    bool present=false;
    size_t directory=0, start=0, raw=0, index=0, zero=0, callbackArray=0;
    std::vector<size_t> callbacks;
    std::array<unsigned char,40> expected{};
};
struct Plan {
    Span record, cookie;
    std::vector<Span> slots;
    std::vector<Pointer> pointers;
    std::vector<Relocation> relocations;
    std::vector<Import> imports;
    Tls tls;
    // Play profile only (validate(...,true)); both stay zero otherwise.
    uint32_t guardFlags=0; size_t volatileMetadata=0;
    // All loader-owned locations, including read-only inactive guard slots.
    bool protectedWrite(size_t rva,size_t n) const {
        Span s{rva,n};
        if(overlaps(s,record)||overlaps(s,cookie)) return true;
        for(auto g:slots) if(overlaps(s,g)) return true;
        return false;
    }
};
// play=false is the original owned cookie-only fixture contract. play=true is
// LoadConfig::msvc_x64 for real game images: it also accepts GuardFlags exactly
// IMAGE_GUARD_CF_INSTRUMENTED (0x100, what LINK emits for /guard:cf objects
// linked without /GUARD:CF; GUARD_CF stays clear) and a VolatileMetadataPointer.
// Neither is acted on: like Wine 10 on x86_64 the loader only initializes the
// cookie. Volatile metadata describes x64 code for ARM64 emulation and is never
// read; only its outer VA is bounds-checked. Relocation limits scale to game
// images instead of the owned fixture's small caps.
// Play also accepts the shorter records older MSVC linkers emit (GuardFlags
// present, i.e. 0x94..0x140 bytes); fields past the record's end are absent, as
// on Windows. The TLS index slot is written by the loader and may sit in a
// section's zero-filled tail (common in MSVC DLLs); it needs mapped, writable,
// non-executable extent, not file bytes. The fixture contract stays 0x140 and
// file-backed.
inline bool validate(const File& f,Plan& result,bool play=false) {
    Plan p;
    const size_t recordSize=f.config.size;
    if(!f.bytes || (f.dllCharacteristics&0x4000) || f.config.rva%8 ||
        (play ? recordSize<0x94 || recordSize>0x140 : recordSize!=0x140) ||
        !f.preferred || f.preferred>UINT64_MAX-f.imageSize ||
        f.sections.empty() || f.sections.size()>96) return false;
    // The caller validates PE headers/alignment/W^X; also reject raw aliases,
    // raw header aliases and zero-filled tails, not just mapped-size bounds.
    for(size_t i=0;i<f.sections.size();++i) {
        auto a=f.sections[i];
        if(!bounds(a.raw,a.rawSize,f.size) || (a.rawSize && a.raw<f.headers) ||
            !bounds(a.rva,a.virtualSize,f.imageSize)) return false;
        for(size_t j=0;j<i;++j) {
            auto b=f.sections[j];
            if(overlaps({a.raw,a.rawSize},{b.raw,b.rawSize}) ||
                overlaps({a.rva,std::max(a.rawSize,a.virtualSize)},
                         {b.rva,std::max(b.rawSize,b.virtualSize)})) return false;
        }
    }
    auto record=f.at(f.config.rva,recordSize,read,write|execute);
    if(!record) return false;
    constexpr size_t allOffsets[]={0x58,0x70,0x78,0x118,0x120,0x128,0x130,0x138};
    std::vector<size_t> offsets;
    for(auto off:allOffsets) if(off+8<=recordSize) offsets.push_back(off);
    if(offsets.empty() || offsets[0]!=0x58) return false; // the cookie is required
    uint32_t declared; memcpy(&declared,record,4);
    if(declared!=recordSize) return false;
    for(size_t i=4;i<recordSize;++i) {
        bool pointer=false;
        for(auto off:offsets) if(i>=off && i-off<8) pointer=true;
        if(play && ((i>=0x90 && i<0x94) || (i>=0x100 && i<0x108))) pointer=true;
        if(!pointer && record[i]) return false;
    }
    if(play) {
        memcpy(&p.guardFlags,record+0x90,4);
        if(p.guardFlags!=0 && p.guardFlags!=0x100) return false;
    }
    p.record={f.config.rva,recordSize};
    std::vector<size_t> expected;
    for(auto off:offsets) {
        uint64_t value; memcpy(&value,record+off,8);
        size_t target;
        // Play: CastGuardOsDeterminedFailureMode names a variable the OS loader
        // may write. FMOD keeps it in writable .data, often in the zero-filled
        // tail; accept that (never executable) as long as it holds zero.
        const bool castGuardVariable=play && off==0x130 && value>=f.preferred &&
            value-f.preferred<=f.imageSize && (value-f.preferred)%8==0 &&
            f.mapped(size_t(value-f.preferred),8,read|write,execute);
        if(castGuardVariable) target=size_t(value-f.preferred);
        else if(!f.va(value,8,target,read,off==0x58?execute:write|execute) || target%8)
            return false;
        if(off==0x58) {
            if(!f.at(target,8,read|write,execute)) return false;
            p.cookie={target,8};
            uint64_t initial;
            if(!f.get(target,initial) || initial!=default_cookie || overlaps(p.cookie,p.record)) return false;
        } else {
            Span slot{target,8};
            if(overlaps(slot,p.record)||overlaps(slot,p.cookie)) return false;
            for(auto other:p.slots) if(overlaps(slot,other)) return false;
            p.slots.push_back(slot);
            uint64_t function=0;   // a zero-filled variable reads as zero
            if(!f.get(target,function) && !castGuardVariable) return false;
            size_t code=0;
            if(off==0x130) { if(function) return false; } // inactive CastGuard
            else {
                if(!f.va(function,1,code,read|execute,write)) return false;
                p.pointers.push_back({target,code,false}); expected.push_back(target);
            }
            if(off==0x130) p.pointers.push_back({target,0,true});
        }
        p.pointers.push_back({p.record.rva+off,target,false});
        expected.push_back(p.record.rva+off);
    }
    uint64_t volatileVa=0;
    if(play && recordSize>=0x108) memcpy(&volatileVa,record+0x100,8);
    if(volatileVa) {
        // Bounds of the outer pointer only: read-only, non-executable, backed
        // in-image bytes that the loader must never write. Never dereferenced.
        size_t target;
        if(!f.va(volatileVa,8,target,read,write|execute) ||
            p.protectedWrite(target,8)) return false;
        p.volatileMetadata=target;
        p.pointers.push_back({p.record.rva+0x100,target,false});
        expected.push_back(p.record.rva+0x100);
    }
    // File TLS metadata is checked before any write, RNG call or import resolver.
    // Its expected relocated directory is retained for the separate prepare API.
    std::vector<Span> tlsRanges;
    if(f.tls.rva || f.tls.size) {
        if(f.tls.size!=40) return false;
        auto t=f.at(f.tls.rva,40,read,write|execute); if(!t) return false;
        uint64_t start,end,index,callbacks; uint32_t zero,characteristics;
        memcpy(&start,t,8); memcpy(&end,t+8,8); memcpy(&index,t+16,8);
        memcpy(&callbacks,t+24,8); memcpy(&zero,t+32,4); memcpy(&characteristics,t+36,4);
        // Neither Windows nor Wine honours a TLS alignment above the heap's
        // 16 bytes (blocks are heap-allocated); real images may still declare
        // one (e.g. 32). Play accepts any IMAGE_SCN_ALIGN_* code, like them.
        if(end<start || end-start>65536 || zero>65536-(end-start) ||
            ((characteristics>>20)&15)>(play?14u:5u)) return false;
        auto& tls=p.tls; tls.present=true; tls.directory=f.tls.rva;
        tls.raw=size_t(end-start); tls.zero=zero;
        if(!f.va(start,std::max<size_t>(1,tls.raw),tls.start,read,execute) ||
            index<f.preferred || index-f.preferred>f.imageSize) return false;
        tls.index=size_t(index-f.preferred);
        if(tls.index%4 || !(play ? f.mapped(tls.index,4,read|write,execute)
                                 : f.at(tls.index,4,read|write,execute)!=nullptr)) return false;
        memcpy(tls.expected.data(),t,40);
        tlsRanges={{f.tls.rva,40},{tls.start,std::max<size_t>(1,tls.raw)},{tls.index,4}};
        if(callbacks) {
            size_t array;
            if(!f.va(callbacks,8,array,read,write|execute)) return false;
            tls.callbackArray=array;
            bool ended=false;
            for(size_t i=0;i<256;++i) {
                uint64_t cb; if(!f.get(array+i*8,cb) || !f.at(array+i*8,8,read,write|execute)) return false;
                if(!cb) { tlsRanges.push_back({array,(i+1)*8}); ended=true; break; }
                size_t code; if(!f.va(cb,1,code,read|execute,write)) return false;
                tls.callbacks.push_back(code);
            }
            if(!ended) return false;
        }
        for(auto s:tlsRanges) if(p.protectedWrite(s.rva,s.size)) return false;
        for(size_t i=0;i<tlsRanges.size();++i) for(size_t j=0;j<i;++j)
            if(overlaps(tlsRanges[i],tlsRanges[j])) return false;
    }
    // Build imports from FILE bytes, not relocation-mutated descriptors/thunks.
    // Later binding uses these copied names and destinations exclusively.
    std::vector<Span> iatRanges, lazyRanges;
    auto imports=[&](Directory dir,bool delay)->bool {
        if(!dir.rva && !dir.size) return true;
        size_t stride=delay?32:20;
        if(!dir.size || !f.at(dir.rva,dir.size)) return false;
        bool ended=false; unsigned descriptors=0;
        for(size_t pos=0;bounds(pos,stride,dir.size);pos+=stride) {
            if(++descriptors>256) return false;
            uint32_t d[8]{}; memcpy(d,f.at(dir.rva+pos,stride),stride);
            uint32_t name=delay?d[1]:d[3], dst=delay?d[3]:d[4], src=delay?d[4]:d[0];
            if(!name && !dst && !src) {
                for(size_t i=0;i<stride/4;++i) if(d[i]) return false;
                ended=true; break;
            }
            // Play leaves delay imports to the image's own delay-load helper, as
            // Windows and Wine do: it resolves on first call through the routed
            // LoadLibraryExA/GetProcAddress, so an optional DLL (UnityPlayer's
            // d3d12.dll) is needed only if actually used. The loader never
            // writes the delay IAT, whose initial in-image thunk VAs are DIR64
            // relocation targets, so it is not an eager-binding range.
            const bool lazy=delay && play;
            if(!name || !src || !dst || (delay && d[0]!=1) ||
                (delay && !lazy && (d[2] || d[5] || d[6] || d[7]))) return false;
            if(lazy) {
                // The helper (not the loader) writes the module handle slot;
                // bound/unload tables are optional read-only copies it may read.
                if(d[2] && (d[2]%8 || !f.mapped(d[2],8,read|write,execute) ||
                    p.protectedWrite(d[2],8))) return false;
                if(d[2]) for(auto s:tlsRanges) if(overlaps({d[2],8},s)) return false;
                for(uint32_t table:{d[5],d[6]}) if(table && !f.at(table,8,read,execute)) return false;
            }
            std::string dll; if(!f.string(name,dll)) return false;
            bool terminated=false;
            for(size_t i=0;i<4096;++i) {
                size_t target=size_t(dst)+i*8;
                uint64_t thunk;
                if(!f.get(size_t(src)+i*8,thunk) ||
                    !f.at(target,8,lazy?read|write:read,execute) ||
                    p.protectedWrite(target,8)) return false;
                for(auto s:tlsRanges) if(overlaps({target,8},s)) return false;
                // Include even the zero terminator in alias validation.
                if(!thunk) { (lazy?lazyRanges:iatRanges).push_back({dst,(i+1)*8}); terminated=true; break; }
                Import imp; imp.destination=target; imp.dll=dll;
                if(thunk>>63) {
                    if(thunk&~(uint64_t(1)<<63|0xffff)) return false;
                    imp.ordinal=uint16_t(thunk);
                } else {
                    uint16_t hint;
                    if(thunk>UINT32_MAX || !f.get(size_t(thunk),hint) || !f.string(size_t(thunk)+2,imp.name)) return false;
                }
                if(lazy) continue;
                p.imports.push_back(std::move(imp));
                if(p.imports.size()>4096) return false;
            }
            if(!terminated) return false;
        }
        return ended;
    };
    if(!imports(f.imports,false) || !imports(f.delay,true)) return false;
    std::sort(iatRanges.begin(),iatRanges.end(),[](Span a,Span b){return a.rva<b.rva;});
    for(size_t i=1;i<iatRanges.size();++i) if(overlaps(iatRanges[i-1],iatRanges[i])) return false;
    // Lazy delay IATs may be relocated, but never alias each other or a bound IAT.
    for(size_t i=0;i<lazyRanges.size();++i) {
        for(size_t j=0;j<i;++j) if(overlaps(lazyRanges[i],lazyRanges[j])) return false;
        for(auto s:iatRanges) if(overlaps(lazyRanges[i],s)) return false;
    }
    // Prevalidate the ENTIRE relocation program, including non-load-config
    // destinations. Snapshot values so even self-modifying reloc data is inert.
    const size_t relocBytes=play?size_t(64)*1024*1024:size_t(1024)*1024;
    const size_t relocCount=play?size_t(16)*1024*1024:size_t(65536);
    if(!f.reloc.size || f.reloc.size>relocBytes || !f.at(f.reloc.rva,f.reloc.size)) return false;
    for(size_t pos=0;pos<f.reloc.size;) {
        uint32_t page,length;
        if(!bounds(pos,8,f.reloc.size) || !f.get(f.reloc.rva+pos,page) ||
            !f.get(f.reloc.rva+pos+4,length) || length<8 || length%4 || page%4096 ||
            !bounds(pos,length,f.reloc.size)) return false;
        for(size_t i=8;i<length;i+=2) {
            uint16_t word; if(!f.get(f.reloc.rva+pos+i,word)) return false;
            unsigned type=word>>12; if(!type) continue;
            size_t target=size_t(page)+(word&4095);
            uint64_t value;
            // Windows and Wine apply page-straddling DIR64 entries; game images
            // may contain them. The owned fixture contract stays page-local.
            if(type!=10 || (!play && (target&4095)>4088) || !f.get(target,value) || overlaps({target,8},p.cookie)) return false;
            bool known=std::find(expected.begin(),expected.end(),target)!=expected.end();
            if(p.protectedWrite(target,8) && !known) return false;
            // TLS index and import writes cannot be relocation targets. Game
            // images legitimately relocate pointers inside the TLS template
            // (thread_local initializers); the template is copied only after
            // relocation, as on Windows. The owned fixture keeps it immutable.
            if(p.tls.present && (overlaps({target,8},{p.tls.index,4}) ||
                                (!play && overlaps({target,8},{p.tls.start,std::max<size_t>(1,p.tls.raw)})))) return false;
            for(auto s:iatRanges) if(overlaps({target,8},s)) return false;
            p.relocations.push_back({target,value});
            if(p.relocations.size()>relocCount) return false;
        }
        pos+=length;
    }
    std::sort(p.relocations.begin(),p.relocations.end(),[](Relocation a,Relocation b){return a.rva<b.rva;});
    for(size_t i=1;i<p.relocations.size();++i)
        if(overlaps({p.relocations[i-1].rva,8},{p.relocations[i].rva,8})) return false;
    for(auto rva:expected) {
        auto r=std::lower_bound(p.relocations.begin(),p.relocations.end(),rva,
                               [](Relocation a,size_t b){return a.rva<b;});
        if(r==p.relocations.end() || r->rva!=rva) return false;
    }
    // Relocations may alter TLS pointer fields/callbacks only to their declared
    // file targets; no integer fields, terminators or straddling writes.
    for(auto s:tlsRanges) {
        if(s.rva==p.tls.index || s.rva==p.tls.start) continue;
        for(auto r:p.relocations) if(overlaps({r.rva,8},s)) {
            size_t off=r.rva-s.rva;
            if(r.rva<s.rva || off%8 || !bounds(off,8,s.size) || !r.value ||
                (s.rva==p.tls.directory && off>=32)) return false;
        }
    }
    if(p.tls.present) {
        // Require a DIR64 for every nonzero directory VA and every callback.
        std::vector<size_t> tlsPointers;
        for(size_t off:{size_t(0),size_t(8),size_t(16),size_t(24)}) {
            uint64_t v; memcpy(&v,p.tls.expected.data()+off,8);
            if(v) tlsPointers.push_back(p.tls.directory+off);
        }
        uint64_t array; memcpy(&array,p.tls.expected.data()+24,8);
        if(array) for(size_t i=0;i<p.tls.callbacks.size();++i) tlsPointers.push_back(size_t(array-f.preferred)+i*8);
        for(auto target:tlsPointers) {
            auto it=std::lower_bound(p.relocations.begin(),p.relocations.end(),target,
                                    [](Relocation a,size_t b){return a.rva<b;});
            if(it==p.relocations.end() || it->rva!=target) return false;
        }
    }
    result=std::move(p); return true;
}
// Entropy is supplied by the mapper (BCryptGenRandom only). Bounded retries,
// 48 significant bits on x64; no clock/PID/address/native-CRT fallback.
using Random = bool (*)(uint64_t&);
inline bool cookie(Random random,uint64_t& value) {
    for(unsigned attempt=0;attempt<8;++attempt) {
        uint64_t candidate=0;
        if(!random || !random(candidate)) return false;
        candidate&=0xffffffffffffULL;
        if(candidate && candidate!=default_cookie) { value=candidate; return true; }
    }
    return false;
}
} // namespace msvc_config
} // namespace wxl
