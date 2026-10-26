#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
// Minimal trusted-image loader: Wine virtual.c map_image_into_view's sequence
// (copy, DIR64 relocation, protections) and loader.c import thunk fixup model.
// No Wine code copied. Native static TLS uses a bounded packaged carrier slot.
namespace wxl {
using Log = void (*)(const char*);
static Log logger;
static void WINAPI debug_string(LPCSTR s) { logger(s); }
struct Builtin { const char* dll; const char* name; uintptr_t address; };
static const Builtin builtins[] = {
    {"kernel32.dll", "GetCurrentProcessId", (uintptr_t)&GetCurrentProcessId},
    {"kernel32.dll", "GetTickCount", (uintptr_t)&GetTickCount},
    {"kernel32.dll", "OutputDebugStringA", (uintptr_t)&debug_string},
    {"kernel32.dll", "RtlLookupFunctionEntry", (uintptr_t)&RtlLookupFunctionEntry},
};
inline bool range(size_t off, size_t n, size_t size) { return off <= size && n <= size - off; }
inline size_t pages(size_t n) { return (n + 4095) & ~size_t(4095); }
struct Carrier {
    DWORD (*index)() = nullptr;
    BYTE* (*block)() = nullptr;
    size_t capacity = 65536;
};
using Resolver = uintptr_t (*)(const char*,const char*, WORD);
struct Image {
    BYTE* base = nullptr; size_t size = 0;
    PRUNTIME_FUNCTION functions = nullptr;
    using DllEntry = BOOL (WINAPI*)(HINSTANCE, DWORD, LPVOID);
    DllEntry dllEntry = nullptr;
    bool attached = false;
    Log log = nullptr;
    DWORD entry = 0;
    bool dll = false;
    Carrier carrier;
    DWORD tlsKey = TLS_OUT_OF_INDEXES;
    std::vector<BYTE> tlsTemplate;
    std::vector<PIMAGE_TLS_CALLBACK> tlsCallbacks;
    bool tlsAttached = false;
    bool attachThread(DWORD reason) {
        if (tlsKey != TLS_OUT_OF_INDEXES) {
            BYTE* block=carrier.block();
            if (!block || TlsGetValue(tlsKey)) return false;
            if (tlsTemplate.size()) memcpy(block,tlsTemplate.data(),tlsTemplate.size());
            if (!TlsSetValue(tlsKey,block)) return false;
            for (auto cb:tlsCallbacks) cb(base,reason,nullptr);
        }
        if (reason==DLL_THREAD_ATTACH && attached && !dllEntry((HINSTANCE)base,reason,nullptr)) return false;
        return true;
    }
    void detachThread(DWORD reason) {
        if (reason==DLL_THREAD_DETACH && attached) dllEntry((HINSTANCE)base,reason,nullptr);
        if (tlsKey!=TLS_OUT_OF_INDEXES && TlsGetValue(tlsKey)) {
            for (auto cb:tlsCallbacks) cb(base,reason,nullptr);
            TlsSetValue(tlsKey,nullptr);
        }
    }
    uintptr_t symbol(const char* name) const {
        auto nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!valid(d.VirtualAddress,d.Size) || d.Size<sizeof(IMAGE_EXPORT_DIRECTORY)) return 0;
        auto e=(IMAGE_EXPORT_DIRECTORY*)(base+d.VirtualAddress);
        if (!valid(e->AddressOfNames,size_t(e->NumberOfNames)*4) ||
            !valid(e->AddressOfNameOrdinals,size_t(e->NumberOfNames)*2) ||
            !valid(e->AddressOfFunctions,size_t(e->NumberOfFunctions)*4)) return 0;
        for (DWORD i=0;i<e->NumberOfNames;++i) {
            auto n=str(((DWORD*)(base+e->AddressOfNames))[i]);
            WORD o=((WORD*)(base+e->AddressOfNameOrdinals))[i];
            if (n && !strcmp(n,name) && o<e->NumberOfFunctions) {
                DWORD r=((DWORD*)(base+e->AddressOfFunctions))[o];
                if (!r || !valid(r,1) || (r>=d.VirtualAddress && r-d.VirtualAddress<d.Size)) return 0;
                return (uintptr_t)(base+r);
            }
        }
        return 0;
    }
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    ~Image() {
        // Guest code and its unwind records must remain alive during detach.
        if (attached) {
            dllEntry((HINSTANCE)base, DLL_PROCESS_DETACH, nullptr);
            log("WXL DllMain DLL_PROCESS_DETACH");
        }
        if (tlsAttached) detachThread(DLL_PROCESS_DETACH);
        if (tlsKey!=TLS_OUT_OF_INDEXES) TlsFree(tlsKey);
        if (functions) {
            BOOL ok = RtlDeleteFunctionTable(functions);
            log(ok ? "WXL RtlDeleteFunctionTable ok=1" : "WXL FAIL RtlDeleteFunctionTable");
            // Never release memory still referenced by the OS unwind registry.
            if (!ok) return;
        }
        if (base) VirtualFree(base, 0, MEM_RELEASE);
    }
    bool valid(size_t rva, size_t n) const { return range(rva, n, size); }
    const char* str(size_t rva) const {
        return valid(rva, 1) && memchr(base + rva, 0, size-rva) ? (const char*)base+rva : nullptr;
    }
    bool protect(size_t rva, size_t n, DWORD prot, Log log) {
        DWORD old = 0; BOOL ok = VirtualProtectFromApp(base+rva, n, prot, &old);
        char s[256]; snprintf(s, sizeof s, "WXL VirtualProtectFromApp rva=0x%zx size=0x%zx old=0x%lx new=0x%lx ok=%d err=%lu", rva,n,old,prot,ok,ok?0:GetLastError()); log(s);
        return !!ok;
    }
};
inline bool load(Image& image, const wchar_t* path, Log log, Carrier carrier={}, Resolver resolve=nullptr, bool mapped=false) {
    logger = log;
    auto fail = [&](const char* why) { log(why); return false; };
    FILE* f = _wfopen(path, L"rb"); if (!f) return fail("WXL FAIL open LocalState image");
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f);
    if (len < sizeof(IMAGE_DOS_HEADER) || len > (mapped ? 256 : 16)*1024*1024) { fclose(f); return fail("WXL FAIL file size"); }
    std::vector<BYTE> file(len); bool read = fread(file.data(),1,len,f)==size_t(len); fclose(f);
    if (!read) return fail("WXL FAIL read");
    auto dos = (const IMAGE_DOS_HEADER*)file.data();
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || !range(dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64),file.size())) return fail("WXL FAIL DOS header");
    auto nt = (const IMAGE_NT_HEADERS64*)(file.data()+dos->e_lfanew);
    auto h = nt->OptionalHeader;
    size_t secOff = size_t(dos->e_lfanew)+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt->FileHeader.SizeOfOptionalHeader;
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || h.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->FileHeader.SizeOfOptionalHeader!=sizeof h || h.NumberOfRvaAndSizes!=IMAGE_NUMBEROF_DIRECTORY_ENTRIES || !nt->FileHeader.NumberOfSections || !range(secOff,nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER),file.size())) return fail("WXL FAIL NT header");
    if (h.SectionAlignment!=4096 || !h.SizeOfImage || h.SizeOfImage> (mapped ? 512u : 32u)*1024*1024 || h.SizeOfImage%4096 || h.SizeOfHeaders<secOff+nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER) || h.SizeOfHeaders>file.size() || pages(h.SizeOfHeaders)>h.SizeOfImage) return fail("WXL FAIL image layout");
    for (unsigned d : {IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR, IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG})
        if (!mapped && h.DataDirectory[d].Size) return fail("WXL FAIL unsupported runtime directory");
    auto sections=(const IMAGE_SECTION_HEADER*)(file.data()+secOff);
    size_t end=pages(h.SizeOfHeaders); bool entryCode=false;
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        auto s=sections[i]; size_t n=std::max(s.Misc.VirtualSize,s.SizeOfRawData);
        if (!n || s.VirtualAddress%4096 || s.VirtualAddress<end || !range(s.VirtualAddress,pages(n),h.SizeOfImage) || (s.SizeOfRawData && !range(s.PointerToRawData,s.SizeOfRawData,file.size())) || ((s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&(s.Characteristics&IMAGE_SCN_MEM_WRITE))) return fail("WXL FAIL section layout/permissions");
        end=s.VirtualAddress+pages(n);
        if ((s.Characteristics&IMAGE_SCN_MEM_EXECUTE) && h.AddressOfEntryPoint>=s.VirtualAddress && h.AddressOfEntryPoint-s.VirtualAddress<n) entryCode=true;
    }
    if (!entryCode) return fail("WXL FAIL entry outside code");
    image.log=log; image.size=h.SizeOfImage; image.carrier=carrier;
    image.entry=h.AddressOfEntryPoint; image.dll=!!(nt->FileHeader.Characteristics & IMAGE_FILE_DLL);
    image.base=(BYTE*)VirtualAllocFromApp(nullptr,image.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if (!image.base) return fail("WXL FAIL RW allocation");
    log("WXL VirtualAllocFromApp initial=0x4 (RW)");
    memcpy(image.base,file.data(),h.SizeOfHeaders);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) { auto s=sections[i]; if(s.SizeOfRawData) memcpy(image.base+s.VirtualAddress,file.data()+s.PointerToRawData,s.SizeOfRawData); }
    uintptr_t delta=(uintptr_t)image.base-h.ImageBase; unsigned relocations=0, imports=0;
    auto rd=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!rd.Size || !image.valid(rd.VirtualAddress,rd.Size)) return fail("WXL FAIL relocation directory");
    for (size_t pos=rd.VirtualAddress, stop=pos+rd.Size;pos<stop;) {
        if (!range(pos,sizeof(IMAGE_BASE_RELOCATION),stop)) return fail("WXL FAIL relocation block");
        auto b=(IMAGE_BASE_RELOCATION*)(image.base+pos);
        if (b->SizeOfBlock<sizeof(*b) || b->SizeOfBlock%2 || !range(pos,b->SizeOfBlock,stop)) return fail("WXL FAIL relocation length");
        auto words=(WORD*)(b+1);
        for (size_t j=0;j<(b->SizeOfBlock-sizeof(*b))/2;j++) {
            unsigned type=words[j]>>12; size_t target=size_t(b->VirtualAddress)+(words[j]&4095);
            if (!type) continue;
            if (type!=IMAGE_REL_BASED_DIR64 || !image.valid(target,8)) return fail("WXL FAIL relocation type/target");
            uint64_t v; memcpy(&v,image.base+target,8); v+=delta; memcpy(image.base+target,&v,8); relocations++;
        }
        pos+=b->SizeOfBlock;
    }
    auto id=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; bool terminated=false;
    if (!id.Size || !image.valid(id.VirtualAddress,id.Size)) return fail("WXL FAIL import directory");
    for(size_t pos=id.VirtualAddress;range(pos,sizeof(IMAGE_IMPORT_DESCRIPTOR),size_t(id.VirtualAddress)+id.Size);pos+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        auto d=(IMAGE_IMPORT_DESCRIPTOR*)(image.base+pos);
        if (!d->Name && !d->FirstThunk && !d->OriginalFirstThunk) { terminated=true; break; }
        auto dll=image.str(d->Name); if(!dll || !d->OriginalFirstThunk || !d->FirstThunk) return fail("WXL FAIL import descriptor");
        for(size_t j=0;;j+=8) {
            size_t src=size_t(d->OriginalFirstThunk)+j, dst=size_t(d->FirstThunk)+j;
            if (!image.valid(src,8)||!image.valid(dst,8)) return fail("WXL FAIL thunk bounds");
            uint64_t thunk; memcpy(&thunk,image.base+src,8); if(!thunk) break;
            bool ordinal = !!(thunk & IMAGE_ORDINAL_FLAG64);
            const char* name = nullptr;
            if (!ordinal) {
                if (thunk>image.size || !image.valid(size_t(thunk),3)) return fail("WXL FAIL import name bounds");
                name=image.str(size_t(thunk)+2);
                if(!name) return fail("WXL FAIL import name");
            }
            uintptr_t address=0;
            if (mapped && resolve) address=resolve(dll,name,ordinal ? WORD(thunk) : 0);
            else {
                if (!ordinal) for(auto& b:builtins) if(!_stricmp(dll,b.dll)&&!strcmp(name,b.name)) address=b.address;
                if (!address && resolve) address=resolve(dll,name,ordinal ? WORD(thunk) : 0);
            }
            if(!address) return fail("WXL FAIL unresolved builtin");
            memcpy(image.base+dst,&address,8); imports++;
        }
    }
    // Probe eagerly fixes RVA-based delay imports too; no delay helper executes.
    auto delay=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if(mapped && delay.Size) {
        struct Delay { DWORD attributes,name,module,iat,intab,bound,unload,timestamp; };
        if(!image.valid(delay.VirtualAddress,delay.Size)) return fail("WXL FAIL delay directory");
        bool ended=false;
        for(size_t pos=delay.VirtualAddress;range(pos,sizeof(Delay),size_t(delay.VirtualAddress)+delay.Size);pos+=sizeof(Delay)) {
            auto d=(Delay*)(image.base+pos);
            if(!d->name && !d->iat && !d->intab) { ended=true; break; }
            auto dll=image.str(d->name);
            if(d->attributes!=1 || !dll || !d->iat || !d->intab || !resolve) return fail("WXL FAIL delay descriptor");
            for(size_t j=0;;j+=8) {
                size_t src=size_t(d->intab)+j,dst=size_t(d->iat)+j;
                if(!image.valid(src,8)||!image.valid(dst,8)) return fail("WXL FAIL delay thunk bounds");
                uint64_t thunk; memcpy(&thunk,image.base+src,8); if(!thunk) break;
                bool ordinal=!!(thunk&IMAGE_ORDINAL_FLAG64); const char* name=nullptr;
                if(!ordinal) {
                    if(thunk>image.size || !image.valid(size_t(thunk),3)) return fail("WXL FAIL delay name bounds");
                    name=image.str(size_t(thunk)+2); if(!name) return fail("WXL FAIL delay name");
                }
                uintptr_t address=resolve(dll,name,ordinal?WORD(thunk):0);
                if(!address) return fail("WXL FAIL unresolved delay import");
                memcpy(image.base+dst,&address,8); ++imports;
            }
        }
        if(!ended) return fail("WXL FAIL delay terminator");
    }
    if(!terminated || (!mapped && (!relocations || !delta))) return fail("WXL FAIL missing terminator/relocation exercise");
    char msg[200]; snprintf(msg,sizeof msg,"WXL mapped LocalState PE relocations=%u imports=%u delta=0x%llx",relocations,imports,(unsigned long long)delta); log(msg);
    auto td=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!mapped && (td.Size || td.VirtualAddress)) {
        if (td.Size<sizeof(IMAGE_TLS_DIRECTORY64) || !image.valid(td.VirtualAddress,td.Size) ||
            !carrier.index || !carrier.block || carrier.capacity>65536) return fail("WXL FAIL TLS directory/provider");
        auto t=(IMAGE_TLS_DIRECTORY64*)(image.base+td.VirtualAddress);
        auto vaRange=[&](ULONGLONG va,size_t n) {
            return va>=(uintptr_t)image.base && image.valid(size_t(va-(uintptr_t)image.base),n);
        };
        if (t->EndAddressOfRawData<t->StartAddressOfRawData) return fail("WXL FAIL TLS template order");
        ULONGLONG raw=t->EndAddressOfRawData-t->StartAddressOfRawData;
        if (raw>carrier.capacity || t->SizeOfZeroFill>carrier.capacity-raw ||
            !vaRange(t->StartAddressOfRawData,size_t(raw)) || !vaRange(t->AddressOfIndex,4))
            return fail("WXL FAIL TLS template/index bounds");
        size_t indexRva=size_t(t->AddressOfIndex-(uintptr_t)image.base);
        bool writable=false;
        for (unsigned j=0;j<nt->FileHeader.NumberOfSections;++j) {
            auto sec=sections[j];
            if ((sec.Characteristics&IMAGE_SCN_MEM_WRITE) && indexRva>=sec.VirtualAddress &&
                range(indexRva-sec.VirtualAddress,4,std::max(sec.Misc.VirtualSize,sec.SizeOfRawData))) writable=true;
        }
        unsigned alignment=(t->Characteristics>>20)&15;
        if (!writable || alignment>5) return fail("WXL FAIL TLS index permissions/alignment");
        image.tlsTemplate.resize(size_t(raw)+t->SizeOfZeroFill);
        if (raw) memcpy(image.tlsTemplate.data(),(void*)t->StartAddressOfRawData,size_t(raw));
        DWORD nativeIndex=carrier.index();
        memcpy((void*)t->AddressOfIndex,&nativeIndex,4);
        image.tlsKey=TlsAlloc();
        if (image.tlsKey==TLS_OUT_OF_INDEXES) return fail("WXL FAIL TlsAlloc");
        if (t->AddressOfCallBacks) {
            bool terminated=false;
            for (size_t j=0;j<256;++j) {
                ULONGLONG addr=t->AddressOfCallBacks+j*8;
                if (addr<t->AddressOfCallBacks || !vaRange(addr,8)) return fail("WXL FAIL TLS callback array");
                ULONGLONG cb; memcpy(&cb,(void*)addr,8);
                if (!cb) { terminated=true; break; }
                bool code=false;
                for (unsigned k=0;k<nt->FileHeader.NumberOfSections;++k) {
                    auto sec=sections[k];
                    ULONGLONG start=(uintptr_t)image.base+sec.VirtualAddress;
                    if ((sec.Characteristics&IMAGE_SCN_MEM_EXECUTE) && cb>=start &&
                        cb-start<std::max(sec.Misc.VirtualSize,sec.SizeOfRawData)) code=true;
                }
                if (!code) return fail("WXL FAIL TLS callback outside code");
                image.tlsCallbacks.push_back((PIMAGE_TLS_CALLBACK)cb);
            }
            if (!terminated) return fail("WXL FAIL TLS callback terminator");
        }
        snprintf(msg,sizeof msg,"WXL TLS native-index=%lu dynamic-key=%lu template=%zu zero=%lu callbacks=%zu",nativeIndex,image.tlsKey,size_t(raw),t->SizeOfZeroFill,image.tlsCallbacks.size()); log(msg);
    }
    // Keep gaps inaccessible; headers and sections receive their final permissions.
    if(!image.protect(0,image.size,PAGE_NOACCESS,log) || !image.protect(0,pages(h.SizeOfHeaders),PAGE_READONLY,log)) return false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        auto s=sections[i]; DWORD p=(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)?PAGE_EXECUTE_READ:(s.Characteristics&IMAGE_SCN_MEM_WRITE)?PAGE_READWRITE:(s.Characteristics&IMAGE_SCN_MEM_READ)?PAGE_READONLY:PAGE_NOACCESS;
        if(!image.protect(s.VirtualAddress,pages(std::max(s.Misc.VirtualSize,s.SizeOfRawData)),p,log)) return false;
    }
    if(!FlushInstructionCache(GetCurrentProcess(),image.base,image.size)) return fail("WXL FAIL cache flush");
    if (mapped) {
        log("MLP mapped: TLS/unwind/load-config retained but NOT initialized; entry NOT called");
        return true;
    }
    auto xd=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (xd.Size || xd.VirtualAddress) {
        if (!xd.Size || xd.Size % sizeof(RUNTIME_FUNCTION) || xd.VirtualAddress % 4 ||
            !image.valid(xd.VirtualAddress,xd.Size)) return fail("WXL FAIL exception directory");
        // The OS requires sorted function entries. Validate each range before
        // publishing the table; the trusted compiler supplies unwind opcodes.
        auto readableRange = [&](DWORD rva, size_t size) {
            for (unsigned j=0;j<nt->FileHeader.NumberOfSections;++j) {
                auto section=sections[j];
                if ((section.Characteristics&IMAGE_SCN_MEM_READ) && rva>=section.VirtualAddress &&
                    range(rva-section.VirtualAddress,size,
                          std::max(section.Misc.VirtualSize,section.SizeOfRawData))) return true;
            }
            return false;
        };
        if (!readableRange(xd.VirtualAddress,xd.Size)) return fail("WXL FAIL unreadable function table");
        auto table=(PRUNTIME_FUNCTION)(image.base+xd.VirtualAddress);
        DWORD previousEnd=0;
        for (size_t i=0; i<xd.Size/sizeof(*table); ++i) {
            auto r=table[i];
            if (r.BeginAddress>=r.EndAddress || r.BeginAddress<previousEnd ||
                !image.valid(r.BeginAddress,r.EndAddress-r.BeginAddress) ||
                r.UnwindData%4 || !image.valid(r.UnwindData,4) || !readableRange(r.UnwindData,4))
                return fail("WXL FAIL runtime function bounds/order");
            previousEnd=r.EndAddress;
            bool executable=false, readable=false;
            for (unsigned j=0;j<nt->FileHeader.NumberOfSections;++j) {
                auto section=sections[j];
                size_t n=std::max(section.Misc.VirtualSize,section.SizeOfRawData);
                if (r.BeginAddress>=section.VirtualAddress &&
                    range(r.BeginAddress-section.VirtualAddress,r.EndAddress-r.BeginAddress,n) &&
                    (section.Characteristics&IMAGE_SCN_MEM_EXECUTE)) executable=true;
                if (xd.VirtualAddress>=section.VirtualAddress &&
                    range(xd.VirtualAddress-section.VirtualAddress,xd.Size,n) &&
                    (section.Characteristics&IMAGE_SCN_MEM_READ)) readable=true;
            }
            if (!executable || !readable) return fail("WXL FAIL runtime function permissions");
        }
        BOOL ok=RtlAddFunctionTable(table,xd.Size/sizeof(*table),(DWORD64)image.base);
        snprintf(msg,sizeof msg,"WXL RtlAddFunctionTable count=%lu ok=%d",
                 (unsigned long)(xd.Size/sizeof(*table)),ok); log(msg);
        if (!ok) return fail("WXL FAIL RtlAddFunctionTable");
        image.functions=table;
    }
    if (!image.attachThread(DLL_PROCESS_ATTACH)) return fail("WXL FAIL TLS process attach");
    image.tlsAttached=true;
    if (image.dll) {
        image.dllEntry=(Image::DllEntry)(image.base+h.AddressOfEntryPoint);
        BOOL ok=image.dllEntry((HINSTANCE)image.base,DLL_PROCESS_ATTACH,nullptr);
        snprintf(msg,sizeof msg,"WXL DllMain DLL_PROCESS_ATTACH ok=%d",ok); log(msg);
        if (!ok) return fail("WXL FAIL DllMain process attach");
        image.attached=true;
        return true;
    }
    return true;
}
inline bool run(const wchar_t* path, Log log, Image* mapped=nullptr, Resolver resolve=nullptr) {
    if (mapped) return load(*mapped,path,log,{},resolve,true);
    Image image;
    if (!load(image,path,log,{},resolve)) return false;
    if (image.dll) return true;
    char msg[200];
    DWORD result=((DWORD(WINAPI*)())(image.base+image.entry))();
    snprintf(msg,sizeof msg,"WXL RESULT entry=%lu failures=%d",result,result==42?0:1); log(msg);
    return result==42;
}
}
