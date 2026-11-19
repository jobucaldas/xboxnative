// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
    // Control Flow Guard function table (Play only): read-only image bytes the
    // loader never reads or writes, like Wine. Span is zero when absent.
    Span cfgTable; uint64_t cfgEntries=0;
    // All loader-owned locations, including read-only inactive guard slots.
    bool protectedWrite(size_t rva,size_t n) const {
        Span s{rva,n};
        if(overlaps(s,record)||overlaps(s,cookie)) return true;
        for(auto g:slots) if(overlaps(s,g)) return true;
        return overlaps(s,cfgTable);
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
// Play also accepts Control Flow Guard metadata as Wine 10 maps it on x86_64:
// present, never enforced. Wine's loader reads only SecurityCookie (ntdll
// update_load_config); it never writes GuardCFCheck/DispatchFunctionPointer, so
// they keep their in-image defaults (a pointer slot in read-only data whose
// value is a relocated pointer into executable code, checked above and verified
// unmodified by the mapper). Accepted, additionally to the cookie-only record:
//  - the GUARD_CF DLL characteristic (0x4000), only together with a function table;
//  - GuardCFFunctionTable/Count (0x80/0x88): both nonzero, the table VA relocated
//    (DIR64), the table (count x (4 + stride nibble) bytes) wholly inside one
//    read-only, non-executable, file-backed section and clear of every loader-
//    owned location; the table is never read for decisions or written;
//  - GuardFlags = CF_INSTRUMENTED(0x100) | CF_FUNCTION_TABLE_PRESENT(0x400, iff a
//    table exists) | PROTECT_DELAYLOAD_IAT(0x1000) | DELAYLOAD_IAT_IN_ITS_OWN_SECTION
//    (0x2000) | CF_LONGJUMP_TABLE_PRESENT(0x10000, only with a table; the long-jump
//    table pointer itself stays required zero) | the stride nibble (bits 28-31,
//    only with a table). Everything else (CFW, export suppression, retpoline, RF,
//    EH-continuation, XFG, CastGuard/memcpy flags; address-taken IAT, long-jump,
//    EH-continuation, dynamic-relocation, XFG/RF pointers) stays rejected.
// Play also accepts the shorter records older MSVC linkers emit (GuardFlags
// present, i.e. 0x94..0x140 bytes, or the 0x70-byte pre-CFG record with only the
// cookie and SEH fields); fields past the record's end are absent, as on Windows. The TLS index slot is written by the loader and may sit in a
// section's zero-filled tail (common in MSVC DLLs); it needs mapped, writable,
// non-executable extent, not file bytes. The fixture contract stays 0x140 and
// file-backed.
inline bool validate(const File& f,Plan& result,bool play=false,std::string* failure=nullptr) {
    Plan p;
    const size_t recordSize=f.config.size;
    auto reject=[&](const char* detail) { if(failure) *failure=detail; return false; };
    char detail[192];
    if(!f.bytes) return reject("file bytes are null");
    if(!play && (f.dllCharacteristics&0x4000)) { snprintf(detail,sizeof detail,"GUARD_CF DLL characteristic 0x%x unsupported in cookie-only mode",f.dllCharacteristics); return reject(detail); }
    if(f.config.rva%8) { snprintf(detail,sizeof detail,"load config RVA 0x%x is not 8-byte aligned",f.config.rva); return reject(detail); }
    if(play && recordSize!=0x70 && (recordSize<0x94 || recordSize>0x140)) {
        snprintf(detail,sizeof detail,"load config Size 0x%zx must be 0x70 or in 0x94..0x140",recordSize); return reject(detail);
    }
    if(!play && recordSize!=0x140) {
        snprintf(detail,sizeof detail,"load config Size 0x%zx != required 0x140",recordSize); return reject(detail);
    }
    if(!f.preferred) return reject("preferred ImageBase is zero");
    if(f.preferred>UINT64_MAX-f.imageSize) { snprintf(detail,sizeof detail,"ImageBase 0x%llx + SizeOfImage 0x%zx overflows",(unsigned long long)f.preferred,f.imageSize); return reject(detail); }
    if(f.sections.empty()) return reject("section count is zero");
    if(f.sections.size()>96) { snprintf(detail,sizeof detail,"section count %zu exceeds 96",f.sections.size()); return reject(detail); }
    // The caller validates PE headers/alignment/W^X; also reject raw aliases,
    // raw header aliases and zero-filled tails, not just mapped-size bounds.
    for(size_t i=0;i<f.sections.size();++i) {
        auto a=f.sections[i];
        if(!bounds(a.raw,a.rawSize,f.size)) { snprintf(detail,sizeof detail,"section %zu raw 0x%x+0x%x past file size 0x%zx",i,a.raw,a.rawSize,f.size); return reject(detail); }
        if(a.rawSize && a.raw<f.headers) { snprintf(detail,sizeof detail,"section %zu raw offset 0x%x overlaps headers ending 0x%zx",i,a.raw,f.headers); return reject(detail); }
        if(!bounds(a.rva,a.virtualSize,f.imageSize)) { snprintf(detail,sizeof detail,"section %zu RVA 0x%x+0x%x past SizeOfImage 0x%zx",i,a.rva,a.virtualSize,f.imageSize); return reject(detail); }
        for(size_t j=0;j<i;++j) {
            auto b=f.sections[j];
            if(overlaps({a.raw,a.rawSize},{b.raw,b.rawSize})) { snprintf(detail,sizeof detail,"sections %zu/%zu raw spans overlap (0x%x+0x%x, 0x%x+0x%x)",j,i,b.raw,b.rawSize,a.raw,a.rawSize); return reject(detail); }
            if(overlaps({a.rva,std::max(a.rawSize,a.virtualSize)},{b.rva,std::max(b.rawSize,b.virtualSize)})) { snprintf(detail,sizeof detail,"sections %zu/%zu image spans overlap (0x%x+0x%zx, 0x%x+0x%zx)",j,i,b.rva,size_t(std::max(b.rawSize,b.virtualSize)),a.rva,size_t(std::max(a.rawSize,a.virtualSize))); return reject(detail); }
        }
    }
    auto record=f.at(f.config.rva,recordSize,read,write|execute);
    if(!record) { snprintf(detail,sizeof detail,"load config RVA 0x%x Size 0x%zx is not backed readable non-writable data",f.config.rva,recordSize); return reject(detail); }
    constexpr size_t allOffsets[]={0x58,0x70,0x78,0x118,0x120,0x128,0x130,0x138};
    std::vector<size_t> offsets;
    for(auto off:allOffsets) if(off+8<=recordSize) offsets.push_back(off);
    if(offsets.empty() || offsets[0]!=0x58) return reject("record does not contain the required SecurityCookie field at offset 0x58");
    uint32_t declared; memcpy(&declared,record,4);
    if(declared!=recordSize) { snprintf(detail,sizeof detail,"load config Size field 0x%x != directory Size 0x%zx",declared,recordSize); return reject(detail); }
    for(size_t i=4;i<recordSize;++i) {
        bool pointer=false;
        for(auto off:offsets) if(i>=off && i-off<8) pointer=true;
        // GuardCFFunctionTable/Count and GuardFlags exist in every Play record >= 0x94.
        if(play && recordSize>=0x94 && ((i>=0x80 && i<0x94) || (i>=0x100 && i<0x108))) pointer=true;
        if(!pointer && record[i]) { snprintf(detail,sizeof detail,"unsupported nonzero load-config byte at record offset 0x%zx value 0x%02x",i,unsigned(record[i])); return reject(detail); }
    }
    uint64_t cfgVa=0, cfgCount=0;
    if(play && recordSize>=0x94) {
        memcpy(&p.guardFlags,record+0x90,4);
        memcpy(&cfgVa,record+0x80,8); memcpy(&cfgCount,record+0x88,8);
        constexpr uint32_t allowed=0x100|0x400|0x1000|0x2000|0x10000|0xf0000000u;
        const bool table=cfgVa||cfgCount;
        // The flag, the table, and the DLL characteristic must tell one story.
        if(p.guardFlags&~allowed) { snprintf(detail,sizeof detail,"GuardFlags 0x%x contains unsupported bits 0x%x",p.guardFlags,p.guardFlags&~allowed); return reject(detail); }
        if(table!=!!(p.guardFlags&0x400)) { snprintf(detail,sizeof detail,"CFG table presence %u disagrees with FUNCTION_TABLE_PRESENT flag 0x%x",unsigned(table),p.guardFlags); return reject(detail); }
        if(table!=!!(f.dllCharacteristics&0x4000)) { snprintf(detail,sizeof detail,"CFG table presence %u disagrees with DLL characteristics 0x%x",unsigned(table),f.dllCharacteristics); return reject(detail); }
        if(!table && (p.guardFlags&(0x10000|0xf0000000u))) { snprintf(detail,sizeof detail,"GuardFlags 0x%x requires a CFG table",p.guardFlags); return reject(detail); }
        if(table && (!cfgVa || !cfgCount)) { snprintf(detail,sizeof detail,"CFG table VA 0x%llx and count 0x%llx must both be nonzero",(unsigned long long)cfgVa,(unsigned long long)cfgCount); return reject(detail); }
    } else if(play && (f.dllCharacteristics&0x4000)) { snprintf(detail,sizeof detail,"GUARD_CF DLL characteristic 0x%x requires a Play record with CFG fields",f.dllCharacteristics); return reject(detail); }
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
        else if(!f.va(value,8,target,read,off==0x58?execute:write|execute) || target%8) {
            snprintf(detail,sizeof detail,"load-config pointer at offset 0x%zx value 0x%llx is not a valid aligned in-image VA",off,(unsigned long long)value); return reject(detail);
        }
        if(off==0x58) {
            if(!f.at(target,8,read|write,execute)) { snprintf(detail,sizeof detail,"SecurityCookie target RVA 0x%zx is not writable file-backed data",target); return reject(detail); }
            p.cookie={target,8};
            uint64_t initial=0;
            if(!f.get(target,initial) || initial!=default_cookie || overlaps(p.cookie,p.record)) { snprintf(detail,sizeof detail,"SecurityCookie target RVA 0x%zx value 0x%llx must equal default 0x%llx and not overlap the record",target,(unsigned long long)initial,(unsigned long long)default_cookie); return reject(detail); }
        } else {
            Span slot{target,8};
            if(overlaps(slot,p.record)||overlaps(slot,p.cookie)) { snprintf(detail,sizeof detail,"guard slot RVA 0x%zx overlaps load-config record/cookie",target); return reject(detail); }
            for(auto other:p.slots) if(overlaps(slot,other)) { snprintf(detail,sizeof detail,"guard slot RVA 0x%zx overlaps existing slot RVA 0x%zx",target,other.rva); return reject(detail); }
            p.slots.push_back(slot);
            uint64_t function=0;   // a zero-filled variable reads as zero
            if(!f.get(target,function) && !castGuardVariable) { snprintf(detail,sizeof detail,"guard slot RVA 0x%zx lacks file-backed bytes",target); return reject(detail); }
            size_t code=0;
            if(off==0x130) { if(function) { snprintf(detail,sizeof detail,"inactive CastGuard slot RVA 0x%zx has nonzero value 0x%llx",target,(unsigned long long)function); return reject(detail); } } // inactive CastGuard
            else {
                if(!f.va(function,1,code,read|execute,write)) { snprintf(detail,sizeof detail,"guard pointer at config offset 0x%zx targets non-executable/non-readable VA 0x%llx",off,(unsigned long long)function); return reject(detail); }
                p.pointers.push_back({target,code,false}); expected.push_back(target);
            }
            if(off==0x130) p.pointers.push_back({target,0,true});
        }
        p.pointers.push_back({p.record.rva+off,target,false});
        expected.push_back(p.record.rva+off);
    }
    if(cfgVa) {
        // Never read or written by the loader, like Wine. Bounds only: wholly in
        // one read-only, non-executable, file-backed section, disjoint from the
        // record, cookie and guard slots; the VA field itself must be relocated.
        const size_t stride=4+(p.guardFlags>>28);
        size_t target;
        if(cfgCount>(uint64_t(1)<<22)) { snprintf(detail,sizeof detail,"CFG entry count 0x%llx exceeds 0x400000",(unsigned long long)cfgCount); return reject(detail); }
        if(!f.va(cfgVa,size_t(cfgCount)*stride,target,read,write|execute)) { snprintf(detail,sizeof detail,"CFG table VA 0x%llx count 0x%llx stride %zu is not file-backed read-only data",(unsigned long long)cfgVa,(unsigned long long)cfgCount,stride); return reject(detail); }
        Span table{target,size_t(cfgCount)*stride};
        if(overlaps(table,p.record)||overlaps(table,p.cookie)) { snprintf(detail,sizeof detail,"CFG table RVA 0x%zx size 0x%zx overlaps load-config record/cookie",target,table.size); return reject(detail); }
        for(auto slot:p.slots) if(overlaps(table,slot)) { snprintf(detail,sizeof detail,"CFG table RVA 0x%zx size 0x%zx overlaps guard slot RVA 0x%zx",target,table.size,slot.rva); return reject(detail); }
        p.cfgTable=table; p.cfgEntries=cfgCount;
        p.pointers.push_back({p.record.rva+0x80,target,false});
        expected.push_back(p.record.rva+0x80);
    }
    uint64_t volatileVa=0;
    if(play && recordSize>=0x108) memcpy(&volatileVa,record+0x100,8);
    if(volatileVa) {
        // Bounds of the outer pointer only: read-only, non-executable, backed
        // in-image bytes that the loader must never write. Never dereferenced.
        size_t target;
        if(!f.va(volatileVa,8,target,read,write|execute)) { snprintf(detail,sizeof detail,"volatile metadata VA 0x%llx is not file-backed read-only data",(unsigned long long)volatileVa); return reject(detail); }
        if(p.protectedWrite(target,8)) { snprintf(detail,sizeof detail,"volatile metadata target RVA 0x%zx aliases loader-owned bytes",target); return reject(detail); }
        p.volatileMetadata=target;
        p.pointers.push_back({p.record.rva+0x100,target,false});
        expected.push_back(p.record.rva+0x100);
    }
    // File TLS metadata is checked before any write, RNG call or import resolver.
    // Its expected relocated directory is retained for the separate prepare API.
    std::vector<Span> tlsRanges;
    if(f.tls.rva || f.tls.size) {
        if(f.tls.size!=40) { snprintf(detail,sizeof detail,"TLS directory RVA 0x%x Size 0x%x != required 0x28",f.tls.rva,f.tls.size); return reject(detail); }
        auto t=f.at(f.tls.rva,40,read,write|execute); if(!t) { snprintf(detail,sizeof detail,"TLS directory RVA 0x%x Size 0x28 is not file-backed read-only data",f.tls.rva); return reject(detail); }
        uint64_t start,end,index,callbacks; uint32_t zero,characteristics;
        memcpy(&start,t,8); memcpy(&end,t+8,8); memcpy(&index,t+16,8);
        memcpy(&callbacks,t+24,8); memcpy(&zero,t+32,4); memcpy(&characteristics,t+36,4);
        // Neither Windows nor Wine honours a TLS alignment above the heap's
        // 16 bytes (blocks are heap-allocated); real images may still declare
        // one (e.g. 32). Play accepts any IMAGE_SCN_ALIGN_* code, like them.
        if(end<start) { snprintf(detail,sizeof detail,"TLS raw-data range inverted: start VA 0x%llx > end VA 0x%llx",(unsigned long long)start,(unsigned long long)end); return reject(detail); }
        if(end-start>f.imageSize || zero>f.imageSize-(end-start)) {
            snprintf(detail,sizeof detail,"TLS template size 0x%llx + zero-fill 0x%x exceeds SizeOfImage 0x%zx",
                     (unsigned long long)(end-start),zero,f.imageSize);
            return reject(detail);
        }
        if(((characteristics>>20)&15)>(play?14u:5u)) { snprintf(detail,sizeof detail,"TLS alignment code %u exceeds policy maximum %u",(characteristics>>20)&15,play?14u:5u); return reject(detail); }
        auto& tls=p.tls; tls.present=true; tls.directory=f.tls.rva;
        tls.raw=size_t(end-start); tls.zero=zero;
        if(!f.va(start,std::max<size_t>(1,tls.raw),tls.start,read,execute)) { snprintf(detail,sizeof detail,"TLS template start VA 0x%llx raw size 0x%zx is not readable non-executable data",(unsigned long long)start,tls.raw); return reject(detail); }
        if(index<f.preferred || index-f.preferred>f.imageSize) { snprintf(detail,sizeof detail,"TLS index VA 0x%llx is outside image base 0x%llx size 0x%zx",(unsigned long long)index,(unsigned long long)f.preferred,f.imageSize); return reject(detail); }
        tls.index=size_t(index-f.preferred);
        if(tls.index%4) { snprintf(detail,sizeof detail,"TLS index RVA 0x%zx is not 4-byte aligned",tls.index); return reject(detail); }
        if(!(play ? f.mapped(tls.index,4,read|write,execute) : f.at(tls.index,4,read|write,execute)!=nullptr)) { snprintf(detail,sizeof detail,"TLS index RVA 0x%zx is not writable non-executable%s data",tls.index,play?" mapped":" file-backed"); return reject(detail); }
        memcpy(tls.expected.data(),t,40);
        tlsRanges={{f.tls.rva,40},{tls.start,std::max<size_t>(1,tls.raw)},{tls.index,4}};
        if(callbacks) {
            size_t array;
            if(!f.va(callbacks,8,array,read,write|execute)) { snprintf(detail,sizeof detail,"TLS callback-array VA 0x%llx is not readable non-writable data",(unsigned long long)callbacks); return reject(detail); }
            tls.callbackArray=array;
            bool ended=false;
            for(size_t i=0;i<256;++i) {
                uint64_t cb; if(!f.get(array+i*8,cb) || !f.at(array+i*8,8,read,write|execute)) { snprintf(detail,sizeof detail,"TLS callback-array entry RVA 0x%zx is not file-backed read-only data",array+i*8); return reject(detail); }
                if(!cb) { tlsRanges.push_back({array,(i+1)*8}); ended=true; break; }
                size_t code; if(!f.va(cb,1,code,read|execute,write)) { snprintf(detail,sizeof detail,"TLS callback %zu VA 0x%llx is not executable/readable",i,(unsigned long long)cb); return reject(detail); }
                tls.callbacks.push_back(code);
            }
            if(!ended) { snprintf(detail,sizeof detail,"TLS callback array RVA 0x%zx has no zero terminator in 256 entries",array); return reject(detail); }
        }
        for(auto s:tlsRanges) if(p.protectedWrite(s.rva,s.size)) { snprintf(detail,sizeof detail,"TLS range RVA 0x%zx Size 0x%zx overlaps loader-owned config bytes",s.rva,s.size); return reject(detail); }
        for(size_t i=0;i<tlsRanges.size();++i) for(size_t j=0;j<i;++j)
            if(overlaps(tlsRanges[i],tlsRanges[j])) { snprintf(detail,sizeof detail,"TLS ranges %zu/%zu overlap (RVA 0x%zx+0x%zx and 0x%zx+0x%zx)",j,i,tlsRanges[j].rva,tlsRanges[j].size,tlsRanges[i].rva,tlsRanges[i].size); return reject(detail); }
    }
    // Build imports from FILE bytes, not relocation-mutated descriptors/thunks.
    // Later binding uses these copied names and destinations exclusively.
    std::vector<Span> iatRanges, lazyRanges;
    auto imports=[&](Directory dir,bool delay)->bool {
        if(!dir.rva && !dir.size) return true;
        size_t stride=delay?32:20;
        if(!dir.size || !f.at(dir.rva,dir.size)) { snprintf(detail,sizeof detail,"%s import directory RVA 0x%x Size 0x%x is not file-backed",delay?"delay":"ordinary",dir.rva,dir.size); return reject(detail); }
        bool ended=false; unsigned descriptors=0;
        for(size_t pos=0;bounds(pos,stride,dir.size);pos+=stride) {
            if(++descriptors>256) { snprintf(detail,sizeof detail,"%s import descriptor count exceeds 256 at RVA 0x%zx",delay?"delay":"ordinary",size_t(dir.rva)+pos); return reject(detail); }
            uint32_t d[8]{}; memcpy(d,f.at(dir.rva+pos,stride),stride);
            uint32_t name=delay?d[1]:d[3], dst=delay?d[3]:d[4], src=delay?d[4]:d[0];
            if(!name && !dst && !src) {
                for(size_t i=0;i<stride/4;++i) if(d[i]) { snprintf(detail,sizeof detail,"%s import terminator at RVA 0x%zx has nonzero field %zu value 0x%x",delay?"delay":"ordinary",size_t(dir.rva)+pos,i,d[i]); return reject(detail); }
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
                (delay && !lazy && (d[2] || d[5] || d[6] || d[7]))) { snprintf(detail,sizeof detail,"%s import descriptor RVA 0x%zx invalid: attributes 0x%x name 0x%x source 0x%x destination 0x%x",delay?"delay":"ordinary",size_t(dir.rva)+pos,d[0],name,src,dst); return reject(detail); }
            if(lazy) {
                // The helper (not the loader) writes the module handle slot;
                // bound/unload tables are optional read-only copies it may read.
                if(d[2] && (d[2]%8 || !f.mapped(d[2],8,read|write,execute) ||
                    p.protectedWrite(d[2],8))) { snprintf(detail,sizeof detail,"delay module-handle slot RVA 0x%x is unaligned, unmapped writable data, or loader-owned",d[2]); return reject(detail); }
                if(d[2]) for(auto s:tlsRanges) if(overlaps({d[2],8},s)) { snprintf(detail,sizeof detail,"delay module-handle slot RVA 0x%x overlaps TLS range RVA 0x%zx+0x%zx",d[2],s.rva,s.size); return reject(detail); }
                for(uint32_t table:{d[5],d[6]}) if(table && !f.at(table,8,read,execute)) { snprintf(detail,sizeof detail,"delay bound/unload table RVA 0x%x is not file-backed read-only data",table); return reject(detail); }
            }
            std::string dll; if(!f.string(name,dll)) { snprintf(detail,sizeof detail,"%s import DLL name RVA 0x%x is empty, unterminated, or out of bounds",delay?"delay":"ordinary",name); return reject(detail); }
            bool terminated=false;
            for(size_t i=0;i<4096;++i) {
                size_t target=size_t(dst)+i*8;
                uint64_t thunk;
                if(!f.get(size_t(src)+i*8,thunk)) { snprintf(detail,sizeof detail,"%s import thunk source RVA 0x%zx is not file-backed",delay?"delay":"ordinary",size_t(src)+i*8); return reject(detail); }
                if(!f.at(target,8,lazy?read|write:read,execute)) { snprintf(detail,sizeof detail,"%s IAT slot RVA 0x%zx lacks required file-backed %s permissions",delay?"delay":"ordinary",target,lazy?"read/write":"read-only"); return reject(detail); }
                if(p.protectedWrite(target,8)) { snprintf(detail,sizeof detail,"%s IAT slot RVA 0x%zx overlaps loader-owned config bytes",delay?"delay":"ordinary",target); return reject(detail); }
                for(auto s:tlsRanges) if(overlaps({target,8},s)) { snprintf(detail,sizeof detail,"%s IAT slot RVA 0x%zx overlaps TLS range RVA 0x%zx+0x%zx",delay?"delay":"ordinary",target,s.rva,s.size); return reject(detail); }
                // Include even the zero terminator in alias validation.
                if(!thunk) { (lazy?lazyRanges:iatRanges).push_back({dst,(i+1)*8}); terminated=true; break; }
                Import imp; imp.destination=target; imp.dll=dll;
                if(thunk>>63) {
                    if(thunk&~(uint64_t(1)<<63|0xffff)) { snprintf(detail,sizeof detail,"import ordinal thunk 0x%llx has reserved bits set",(unsigned long long)thunk); return reject(detail); }
                    imp.ordinal=uint16_t(thunk);
                } else {
                    uint16_t hint;
                    if(thunk>UINT32_MAX || !f.get(size_t(thunk),hint) || !f.string(size_t(thunk)+2,imp.name)) { snprintf(detail,sizeof detail,"import name thunk RVA/value 0x%llx lacks a bounded hint and name",(unsigned long long)thunk); return reject(detail); }
                }
                if(lazy) continue;
                p.imports.push_back(std::move(imp));
                if(p.imports.size()>4096) { snprintf(detail,sizeof detail,"eager import count %zu exceeds 4096",p.imports.size()); return reject(detail); }
            }
            if(!terminated) { snprintf(detail,sizeof detail,"%s import thunk array at RVA 0x%x has no terminator within 4096 entries",delay?"delay":"ordinary",src); return reject(detail); }
        }
        if(!ended) { snprintf(detail,sizeof detail,"%s import directory RVA 0x%x Size 0x%x has no full zero terminator",delay?"delay":"ordinary",dir.rva,dir.size); return reject(detail); }
        return true;
    };
    if(!imports(f.imports,false) || !imports(f.delay,true)) return false;   // imports() already recorded its specific reject()
    std::sort(iatRanges.begin(),iatRanges.end(),[](Span a,Span b){return a.rva<b.rva;});
    for(size_t i=1;i<iatRanges.size();++i) if(overlaps(iatRanges[i-1],iatRanges[i])) { snprintf(detail,sizeof detail,"eager IAT spans at RVA 0x%zx+0x%zx and 0x%zx+0x%zx overlap",iatRanges[i-1].rva,iatRanges[i-1].size,iatRanges[i].rva,iatRanges[i].size); return reject(detail); }
    // Lazy delay IATs may be relocated, but never alias each other or a bound IAT.
    for(size_t i=0;i<lazyRanges.size();++i) {
        for(size_t j=0;j<i;++j) if(overlaps(lazyRanges[i],lazyRanges[j])) { snprintf(detail,sizeof detail,"lazy delay IAT spans at RVA 0x%zx+0x%zx and 0x%zx+0x%zx overlap",lazyRanges[j].rva,lazyRanges[j].size,lazyRanges[i].rva,lazyRanges[i].size); return reject(detail); }
        for(auto s:iatRanges) if(overlaps(lazyRanges[i],s)) { snprintf(detail,sizeof detail,"lazy delay IAT RVA 0x%zx+0x%zx overlaps eager IAT RVA 0x%zx+0x%zx",lazyRanges[i].rva,lazyRanges[i].size,s.rva,s.size); return reject(detail); }
    }
    // Prevalidate the ENTIRE relocation program, including non-load-config
    // destinations. Snapshot values so even self-modifying reloc data is inert.
    const size_t relocBytes=play?size_t(64)*1024*1024:size_t(1024)*1024;
    const size_t relocCount=play?size_t(16)*1024*1024:size_t(65536);
    if(!f.reloc.size) return reject("base-relocation directory is absent");
    if(f.reloc.size>relocBytes) { snprintf(detail,sizeof detail,"base-relocation Size 0x%x exceeds policy cap 0x%zx",f.reloc.size,relocBytes); return reject(detail); }
    if(!f.at(f.reloc.rva,f.reloc.size)) { snprintf(detail,sizeof detail,"base-relocation RVA 0x%x Size 0x%x is not file-backed",f.reloc.rva,f.reloc.size); return reject(detail); }
    for(size_t pos=0;pos<f.reloc.size;) {
        uint32_t page=0,length=0;
        if(!bounds(pos,8,f.reloc.size)) { snprintf(detail,sizeof detail,"relocation block header at directory offset 0x%zx exceeds Size 0x%x",pos,f.reloc.size); return reject(detail); }
        if(!f.get(f.reloc.rva+pos,page) || !f.get(f.reloc.rva+pos+4,length)) { snprintf(detail,sizeof detail,"relocation block header RVA 0x%zx is not file-backed",size_t(f.reloc.rva)+pos); return reject(detail); }
        if(length<8 || length%4 || page%4096 || !bounds(pos,length,f.reloc.size)) { snprintf(detail,sizeof detail,"relocation block RVA 0x%zx page 0x%x length 0x%x violates length/alignment/directory bounds",size_t(f.reloc.rva)+pos,page,length); return reject(detail); }
        for(size_t i=8;i<length;i+=2) {
            uint16_t word; if(!f.get(f.reloc.rva+pos+i,word)) { snprintf(detail,sizeof detail,"relocation entry RVA 0x%zx exceeds file-backed directory",size_t(f.reloc.rva)+pos+i); return reject(detail); }
            unsigned type=word>>12; if(!type) continue;
            size_t target=size_t(page)+(word&4095);
            uint64_t value;
            // Windows and Wine apply page-straddling DIR64 entries; game images
            // may contain them. The owned fixture contract stays page-local.
            if(type!=10) { snprintf(detail,sizeof detail,"relocation target RVA 0x%zx uses type %u, expected DIR64 type 10",target,type); return reject(detail); }
            if(!play && (target&4095)>4088) { snprintf(detail,sizeof detail,"DIR64 target RVA 0x%zx straddles a page in cookie-only mode",target); return reject(detail); }
            if(!f.get(target,value)) { snprintf(detail,sizeof detail,"DIR64 target RVA 0x%zx is not file-backed",target); return reject(detail); }
            if(overlaps({target,8},p.cookie)) { snprintf(detail,sizeof detail,"DIR64 target RVA 0x%zx overlaps SecurityCookie",target); return reject(detail); }
            bool known=std::find(expected.begin(),expected.end(),target)!=expected.end();
            if(p.protectedWrite(target,8) && !known) { snprintf(detail,sizeof detail,"DIR64 target RVA 0x%zx overlaps loader-owned config without an expected relocation",target); return reject(detail); }
            // TLS index and import writes cannot be relocation targets. Game
            // images legitimately relocate pointers inside the TLS template
            // (thread_local initializers); the template is copied only after
            // relocation, as on Windows. The owned fixture keeps it immutable.
            if(p.tls.present && (overlaps({target,8},{p.tls.index,4}) ||
                                (!play && overlaps({target,8},{p.tls.start,std::max<size_t>(1,p.tls.raw)})))) { snprintf(detail,sizeof detail,"DIR64 target RVA 0x%zx overlaps TLS index/template forbidden by policy",target); return reject(detail); }
            for(auto s:iatRanges) if(overlaps({target,8},s)) { snprintf(detail,sizeof detail,"DIR64 target RVA 0x%zx overlaps eager IAT RVA 0x%zx+0x%zx",target,s.rva,s.size); return reject(detail); }
            p.relocations.push_back({target,value});
            if(p.relocations.size()>relocCount) { snprintf(detail,sizeof detail,"relocation count %zu exceeds policy cap %zu",p.relocations.size(),relocCount); return reject(detail); }
        }
        pos+=length;
    }
    std::sort(p.relocations.begin(),p.relocations.end(),[](Relocation a,Relocation b){return a.rva<b.rva;});
    for(size_t i=1;i<p.relocations.size();++i)
        if(overlaps({p.relocations[i-1].rva,8},{p.relocations[i].rva,8})) { snprintf(detail,sizeof detail,"DIR64 targets RVA 0x%zx and 0x%zx overlap",p.relocations[i-1].rva,p.relocations[i].rva); return reject(detail); }
    for(auto rva:expected) {
        auto r=std::lower_bound(p.relocations.begin(),p.relocations.end(),rva,
                               [](Relocation a,size_t b){return a.rva<b;});
        if(r==p.relocations.end() || r->rva!=rva) { snprintf(detail,sizeof detail,"required DIR64 relocation for load-config pointer RVA 0x%zx is absent",rva); return reject(detail); }
    }
    // Relocations may alter TLS pointer fields/callbacks only to their declared
    // file targets; no integer fields, terminators or straddling writes.
    for(auto s:tlsRanges) {
        if(s.rva==p.tls.index || s.rva==p.tls.start) continue;
        for(auto r:p.relocations) if(overlaps({r.rva,8},s)) {
            size_t off=r.rva-s.rva;
            if(r.rva<s.rva || off%8 || !bounds(off,8,s.size) || !r.value ||
                (s.rva==p.tls.directory && off>=32)) { snprintf(detail,sizeof detail,"TLS relocation RVA 0x%zx offset 0x%zx size 0x%zx value 0x%llx is unaligned, straddling, zero, or a non-pointer TLS field",r.rva,off,s.size,(unsigned long long)r.value); return reject(detail); }
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
            if(it==p.relocations.end() || it->rva!=target) { snprintf(detail,sizeof detail,"required DIR64 relocation for TLS pointer RVA 0x%zx is absent",target); return reject(detail); }
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
