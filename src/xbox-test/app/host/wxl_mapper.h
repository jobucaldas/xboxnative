#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
#include "../../shared/wine_load_config.h"
#include "../../shared/msvc_load_config.h"
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif
// Minimal trusted-image loader: Wine virtual.c map_image_into_view's sequence
// (copy, DIR64 relocation, protections) and loader.c import thunk fixup model.
// No Wine code copied. Native static TLS uses a bounded packaged carrier slot.
namespace wxl {
using Log = void (*)(const char*);
static Log logger;
static thread_local bool resolvingDelay=false;   // the resolver is binding a delay import
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
    // Retain file-validated bounds: runtime headers may have been relocated.
    size_t configStart = 0, configEnd = 0;
    // Only the explicit fixture policy populates these FILE-derived snapshots.
    // Guest headers are never authority for a later TLS preparation call.
    msvc_config::Plan msvc;
    std::vector<BYTE> trustedHeaders, trustedConfig;
    bool cookieInitialized = false;
    uint64_t initializedCookie = 0;
    bool configWrite(size_t rva, size_t n) const {
        return (configEnd && rva<configEnd && configStart<rva+n) ||
               msvc.protectedWrite(rva,n);
    }
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
    bool initThreadStorage() {
        if (tlsKey != TLS_OUT_OF_INDEXES) {
            BYTE* block=carrier.block();
            if (!block || TlsGetValue(tlsKey)) return false;
            if (tlsTemplate.size()) memcpy(block,tlsTemplate.data(),tlsTemplate.size());
            if (!TlsSetValue(tlsKey,block)) return false;
        }
        return true;
    }
    bool notifyThread(DWORD reason) {
        if (tlsKey != TLS_OUT_OF_INDEXES) {
            if (!TlsGetValue(tlsKey)) return false;
            for (auto cb:tlsCallbacks) cb(base,reason,nullptr);
        }
        if (reason==DLL_THREAD_ATTACH && attached) dllEntry((HINSTANCE)base,reason,nullptr);
        return true;
    }
    bool attachThread(DWORD reason) {
        return initThreadStorage() && notifyThread(reason);
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
// The CRT, not the loader, sets __security_cookie_complement on entry when
// the loader already changed __security_cookie. Never infer a complement RVA.
inline bool systemCookieRandom(uint64_t& value) {
    return BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&value),sizeof value,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
}
inline bool msvcPointers(const Image& image) {
    for(auto p:image.msvc.pointers) {
        uint64_t value=0; memcpy(&value,image.base+p.rva,8);
        if(value!=(p.null?0:uint64_t(uintptr_t(image.base)+p.target))) return false;
    }
    return true;
}
inline bool prepareMsvcTls(Image& image, Log log, Carrier carrier) {
    auto fail=[&](){log("WXL FAIL explicit MSVC trusted TLS metadata/provider");return false;};
    if(!image.cookieInitialized || image.trustedHeaders.empty() ||
        memcmp(image.base,image.trustedHeaders.data(),image.trustedHeaders.size()) ||
        memcmp(image.base+image.msvc.record.rva,image.trustedConfig.data(),image.trustedConfig.size()) ||
        !msvcPointers(image)) return fail();
    uint64_t cookie; memcpy(&cookie,image.base+image.msvc.cookie.rva,8);
    if(cookie!=image.initializedCookie) return fail();
    auto& t=image.msvc.tls;
    if(!t.present) return true;
    if(memcmp(image.base+t.directory,t.expected.data(),t.expected.size()) ||
        !carrier.index || !carrier.block || carrier.capacity>65536 ||
        t.raw+t.zero>carrier.capacity || image.tlsKey!=TLS_OUT_OF_INDEXES) return fail();
    // Retained callback RVAs are execution authority, but the mapped array must
    // still match the file plan, including its zero terminator, before ANY effect.
    if(t.callbackArray) for(size_t i=0;i<=t.callbacks.size();++i) {
        uint64_t value; memcpy(&value,image.base+t.callbackArray+i*8,8);
        uint64_t expected=i<t.callbacks.size()?uint64_t(uintptr_t(image.base)+t.callbacks[i]):0;
        if(value!=expected) return fail();
    }
    // All addresses, permissions and disjointness came from the immutable file.
    // No header, TLS-directory or callback-array mutation can authorize this write.
    image.carrier=carrier;
    image.tlsTemplate.resize(t.raw+t.zero);
    if(t.raw) memcpy(image.tlsTemplate.data(),image.base+t.start,t.raw);
    image.tlsKey=TlsAlloc();
    if(image.tlsKey==TLS_OUT_OF_INDEXES) return fail();
    DWORD nativeIndex=carrier.index();
    if(nativeIndex==TLS_OUT_OF_INDEXES) return fail();
    memcpy(image.base+t.index,&nativeIndex,4);
    for(auto rva:t.callbacks) image.tlsCallbacks.push_back((PIMAGE_TLS_CALLBACK)(image.base+rva));
    return true;
}
// Runtime preparation is separate from mapping: all sibling exports must exist
// before any TLS callback or DllMain can execute.
inline bool prepareTls(Image& image, Log log, Carrier carrier) {
    if(image.msvc.record.size) return prepareMsvcTls(image,log,carrier);
    auto fail=[&](const char* why){log(why);return false;};
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto h=nt->OptionalHeader;
    auto sections=IMAGE_FIRST_SECTION(nt);
    image.carrier=carrier;
    char msg[256];
    auto td=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (td.Size || td.VirtualAddress) {
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
        if (image.configWrite(indexRva,4)) return fail("WXL FAIL TLS write into inert load configuration");
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
    return true;
}
inline bool prepareUnwind(Image& image, Log log) {
    auto fail=[&](const char* why){log(why);return false;};
    if(image.msvc.record.size && (image.trustedHeaders.empty() ||
        memcmp(image.base,image.trustedHeaders.data(),image.trustedHeaders.size())))
        return fail("WXL FAIL explicit MSVC trusted unwind headers");
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto h=nt->OptionalHeader;
    auto sections=IMAGE_FIRST_SECTION(nt);
    char msg[256];
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
    return true;
}
inline bool load(Image& image, const wchar_t* path, Log log, Carrier carrier={}, Resolver resolve=nullptr, bool mapped=false, LoadConfig config=LoadConfig::reject) {
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
    for (unsigned d : {IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT, IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR})
        if (!mapped && h.DataDirectory[d].Size) return fail("WXL FAIL unsupported runtime directory");
    auto configDir=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    auto sections=(const IMAGE_SECTION_HEADER*)(file.data()+secOff);
    const bool hasConfig=configDir.VirtualAddress || configDir.Size;
    auto inertConfig=[&]() {
        bool inert=false;
        for (unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
            auto s=sections[i];
            if (configDir.VirtualAddress<s.VirtualAddress) continue;
            size_t offset=size_t(configDir.VirtualAddress)-s.VirtualAddress;
            if (!range(offset,configDir.Size,s.SizeOfRawData) ||
                (s.Characteristics&(IMAGE_SCN_MEM_WRITE|IMAGE_SCN_MEM_EXECUTE))) continue;
            inert=inert_wine_load_config(file.data()+s.PointerToRawData+offset,
                                        configDir.Size,h.DllCharacteristics);
        }
        return inert;
    };
    // Play (msvc_x64) takes source-built Wine donors through the unchanged
    // inert path and every other present record through the MSVC preflight.
    const bool playMsvc=config==LoadConfig::msvc_x64;
    const bool playInert=playMsvc && hasConfig && inertConfig();
    const bool explicitMsvc=config==LoadConfig::msvc_x64_cookie || (playMsvc && hasConfig && !playInert);
    if (hasConfig && config!=LoadConfig::inert_wine && !playInert && !explicitMsvc)
        return fail("WXL FAIL unsupported runtime directory");
    if (explicitMsvc && (image.base || !configDir.VirtualAddress || !configDir.Size ||
        h.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress ||
        h.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size))
        return fail("WXL FAIL explicit MSVC runtime directory/state");
    // Preflight belongs to this attempt, not to Image until allocation succeeds.
    // A failed explicit allocation may be retried with a different policy.
    msvc_config::Plan msvcPlan;
    size_t configStart=0, configEnd=0;
    size_t end=pages(h.SizeOfHeaders); bool entryCode=false;
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        auto s=sections[i]; size_t n=std::max(s.Misc.VirtualSize,s.SizeOfRawData);
        if (!n || s.VirtualAddress%4096 || s.VirtualAddress<end || !range(s.VirtualAddress,pages(n),h.SizeOfImage) || (s.SizeOfRawData && !range(s.PointerToRawData,s.SizeOfRawData,file.size())) || ((s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&(s.Characteristics&IMAGE_SCN_MEM_WRITE))) return fail("WXL FAIL section layout/permissions");
        end=s.VirtualAddress+pages(n);
        if ((s.Characteristics&IMAGE_SCN_MEM_EXECUTE) && h.AddressOfEntryPoint>=s.VirtualAddress && h.AddressOfEntryPoint-s.VirtualAddress<n) entryCode=true;
    }
    if (!entryCode && !(mapped && (nt->FileHeader.Characteristics&IMAGE_FILE_DLL) && !h.AddressOfEntryPoint)) return fail("WXL FAIL entry outside code");
    if (explicitMsvc) {
        msvc_config::File view;
        view.bytes=file.data(); view.size=file.size(); view.headers=h.SizeOfHeaders;
        view.imageSize=h.SizeOfImage; view.preferred=h.ImageBase;
        view.dllCharacteristics=h.DllCharacteristics;
        auto directory=[&](unsigned i) { auto d=h.DataDirectory[i]; return msvc_config::Directory{d.VirtualAddress,d.Size}; };
        view.config=directory(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG);
        view.reloc=directory(IMAGE_DIRECTORY_ENTRY_BASERELOC);
        view.imports=directory(IMAGE_DIRECTORY_ENTRY_IMPORT);
        view.delay=directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
        view.tls=directory(IMAGE_DIRECTORY_ENTRY_TLS);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
            auto s=sections[i];
            view.sections.push_back({s.VirtualAddress,s.Misc.VirtualSize,s.PointerToRawData,s.SizeOfRawData,s.Characteristics});
        }
        if((h.AddressOfEntryPoint && !view.at(h.AddressOfEntryPoint,1,msvc_config::read|msvc_config::execute,msvc_config::write)) ||
            !msvc_config::validate(view,msvcPlan,playMsvc)) return fail("WXL FAIL explicit MSVC file preflight");
        log("WXL explicit MSVC file/write plan validated");
        if (playMsvc) {
            char msg[160];
            snprintf(msg,sizeof msg,"WXL Play MSVC load config GuardFlags=0x%x volatileMetadata=%s cfgTable=%s (cookie only; guard slots, CFG table and metadata untouched, as Wine)",
                     msvcPlan.guardFlags,msvcPlan.volatileMetadata?"present":"absent",msvcPlan.cfgEntries?"present":"absent");
            log(msg);
        }
    } else if (hasConfig) {
        if (!inertConfig()) return fail("WXL FAIL non-inert Wine load configuration");
        configStart=configDir.VirtualAddress; configEnd=configStart+configDir.Size;
        log("WXL inert Wine load configuration validated; no loader work requested");
    }
    auto base=(BYTE*)VirtualAllocFromApp(nullptr,h.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if (!base) return fail("WXL FAIL RW allocation");
    image.base=base;
    image.log=log; image.size=h.SizeOfImage; image.carrier=carrier;
    image.entry=h.AddressOfEntryPoint; image.dll=!!(nt->FileHeader.Characteristics & IMAGE_FILE_DLL);
    image.configStart=configStart; image.configEnd=configEnd;
    // Keep one-shot binding data local, including while resolvers reenter host
    // code. Retained images need only the immutable deferred/runtime authority.
    std::vector<msvc_config::Relocation> relocationPlan;
    std::vector<msvc_config::Import> importPlan;
    relocationPlan.swap(msvcPlan.relocations); importPlan.swap(msvcPlan.imports);
    image.msvc=std::move(msvcPlan);
    image.trustedHeaders.clear(); image.trustedConfig.clear();
    image.cookieInitialized=false; image.initializedCookie=0;
    if(explicitMsvc) image.trustedHeaders.assign(file.begin(),file.begin()+h.SizeOfHeaders);
    log("WXL VirtualAllocFromApp initial=0x4 (RW)");
    memcpy(image.base,file.data(),h.SizeOfHeaders);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) { auto s=sections[i]; if(s.SizeOfRawData) memcpy(image.base+s.VirtualAddress,file.data()+s.PointerToRawData,s.SizeOfRawData); }
    uintptr_t delta=(uintptr_t)image.base-h.ImageBase; unsigned relocations=0, imports=0, unresolved=0, deferred=0;
    bool terminated=false;
    if(explicitMsvc) {
        for(auto r:relocationPlan) {
            uint64_t value=r.value+delta;
            memcpy(image.base+r.rva,&value,8); ++relocations;
        }
        if(!msvcPointers(image)) return fail("WXL FAIL explicit MSVC relocated pointer mismatch");
        auto& t=image.msvc.tls;
        if(t.present) {
            for(size_t off:{size_t(0),size_t(8),size_t(16),size_t(24)}) {
                uint64_t value; memcpy(&value,t.expected.data()+off,8);
                if(value) { value+=delta; memcpy(t.expected.data()+off,&value,8); }
            }
            if(memcmp(image.base+t.directory,t.expected.data(),t.expected.size()))
                return fail("WXL FAIL explicit MSVC relocated TLS mismatch");
        }
        image.trustedConfig.assign(image.base+image.msvc.record.rva,
                                   image.base+image.msvc.record.rva+image.msvc.record.size);
        uint64_t cookie;
        if(!msvc_config::cookie(systemCookieRandom,cookie)) return fail("WXL FAIL explicit MSVC system RNG");
        memcpy(image.base+image.msvc.cookie.rva,&cookie,8);
        image.initializedCookie=cookie; image.cookieInitialized=true;
        // This is deliberately before the first resolver: resolution may execute
        // guest dependency initialization, not just return an address.
        for(auto& imp:importPlan) {
            uintptr_t address=0;
            const char* name=imp.name.empty()?nullptr:imp.name.c_str();
            if(mapped && resolve) address=resolve(imp.dll.c_str(),name,imp.ordinal);
            else {
                if(name) for(auto& b:builtins)
                    if(!_stricmp(imp.dll.c_str(),b.dll) && !strcmp(name,b.name)) address=b.address;
                if(!address && resolve) address=resolve(imp.dll.c_str(),name,imp.ordinal);
            }
            // Keep binding after a miss so one run reports every import the
            // console lacks; the image still fails before anything runs.
            if(!address) {++unresolved;continue;}
            memcpy(image.base+imp.destination,&address,8); ++imports;
        }
        if(unresolved) return fail("WXL FAIL explicit MSVC unresolved import");
        terminated=true;
        log("WXL explicit MSVC cookie initialized; inactive guard slots unchanged");
    } else {
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
            if (image.configWrite(target,8)) return fail("WXL FAIL relocation into inert load configuration");
            uint64_t v; memcpy(&v,image.base+target,8); v+=delta; memcpy(image.base+target,&v,8); relocations++;
        }
        pos+=b->SizeOfBlock;
    }
    auto id=h.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; terminated=!id.Size;
    if ((!id.Size && !mapped) || !image.valid(id.VirtualAddress,id.Size)) return fail("WXL FAIL import directory");
    for(size_t pos=id.VirtualAddress;range(pos,sizeof(IMAGE_IMPORT_DESCRIPTOR),size_t(id.VirtualAddress)+id.Size);pos+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        auto d=(IMAGE_IMPORT_DESCRIPTOR*)(image.base+pos);
        if (!d->Name && !d->FirstThunk && !d->OriginalFirstThunk) { terminated=true; break; }
        auto dll=image.str(d->Name); if(!dll || !d->OriginalFirstThunk || !d->FirstThunk) return fail("WXL FAIL import descriptor");
        for(size_t j=0;;j+=8) {
            size_t src=size_t(d->OriginalFirstThunk)+j, dst=size_t(d->FirstThunk)+j;
            if (!image.valid(src,8)||!image.valid(dst,8)) return fail("WXL FAIL thunk bounds");
            if (image.configWrite(dst,8)) return fail("WXL FAIL import write into inert load configuration");
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
            if(!address) {++unresolved;continue;}   // reported by the resolver; fail after the rest
            memcpy(image.base+dst,&address,8); imports++;
        }
    }
    if(unresolved) return fail("WXL FAIL unresolved builtin");
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
                if(image.configWrite(dst,8)) return fail("WXL FAIL delay write into inert load configuration");
                uint64_t thunk; memcpy(&thunk,image.base+src,8); if(!thunk) break;
                bool ordinal=!!(thunk&IMAGE_ORDINAL_FLAG64); const char* name=nullptr;
                if(!ordinal) {
                    if(thunk>image.size || !image.valid(size_t(thunk),3)) return fail("WXL FAIL delay name bounds");
                    name=image.str(size_t(thunk)+2); if(!name) return fail("WXL FAIL delay name");
                }
                resolvingDelay=true;
                uintptr_t address=resolve(dll,name,ordinal?WORD(thunk):0);
                resolvingDelay=false;
                // As on Windows, a delay import binds on first call: one not
                // found now (its DLL in a folder the game adds with
                // SetDllDirectory before calling it) keeps its stub, which runs
                // the game's own delay-load helper through Play's loader.
                if(!address) {++deferred;continue;}
                memcpy(image.base+dst,&address,8); ++imports;
            }
        }
        if(!ended) return fail("WXL FAIL delay terminator");
        if(deferred){char b[96];snprintf(b,sizeof b,"WXL delay imports left for first call: %u",deferred);log(b);}
    }
    } // Legacy/default/inert import and relocation path.
    if(!terminated || (!mapped && (!relocations || !delta))) return fail("WXL FAIL missing terminator/relocation exercise");
    char msg[200]; snprintf(msg,sizeof msg,"WXL mapped LocalState PE relocations=%u imports=%u delta=0x%llx",relocations,imports,(unsigned long long)delta); log(msg);
    if (!mapped && !prepareTls(image,log,carrier)) return false;
    // Keep gaps inaccessible; headers and sections receive their final permissions.
    if(!image.protect(0,image.size,PAGE_NOACCESS,log) || !image.protect(0,pages(h.SizeOfHeaders),PAGE_READONLY,log)) return false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        auto s=sections[i]; DWORD p=(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)?PAGE_EXECUTE_READ:(s.Characteristics&IMAGE_SCN_MEM_WRITE)?PAGE_READWRITE:(s.Characteristics&IMAGE_SCN_MEM_READ)?PAGE_READONLY:PAGE_NOACCESS;
        if(!image.protect(s.VirtualAddress,pages(std::max(s.Misc.VirtualSize,s.SizeOfRawData)),p,log)) return false;
    }
    if(!FlushInstructionCache(GetCurrentProcess(),image.base,image.size)) return fail("WXL FAIL cache flush");
    if (mapped) {
        log(explicitMsvc ? "MLP mapped: explicit MSVC cookie initialized; TLS/unwind/entry NOT called" :
                           "MLP mapped: TLS/unwind/load-config retained but NOT initialized; entry NOT called");
        return true;
    }
    if (!prepareUnwind(image,log)) return false;
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
inline bool run(const wchar_t* path, Log log, Image* mapped=nullptr, Resolver resolve=nullptr, LoadConfig config=LoadConfig::reject) {
    if (mapped) return load(*mapped,path,log,{},resolve,true,config);
    Image image;
    if (!load(image,path,log,{},resolve)) return false;
    if (image.dll) return true;
    char msg[200];
    DWORD result=((DWORD(WINAPI*)())(image.base+image.entry))();
    snprintf(msg,sizeof msg,"WXL RESULT entry=%lu failures=%d",result,result==42?0:1); log(msg);
    return result==42;
}
}
