// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static BYTE* allocated;
static size_t allocatedSize,cookieRva;
static unsigned allocations,releases,resolutions,observations,rngCalls,indexCalls,allocationAttempts;
static unsigned rngMode;
static bool failAllocation;
static LPVOID WINAPI allocate_image(LPVOID p,SIZE_T size,DWORD type,DWORD protection) {
    ++allocationAttempts;
    if(failAllocation) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    auto base=VirtualAlloc(p,size,type,protection);
    if(base) { allocated=(BYTE*)base; allocatedSize=size; ++allocations; }
    return base;
}
static BOOL WINAPI release_image(LPVOID p,SIZE_T size,DWORD type) {
    auto ok=VirtualFree(p,size,type); if(ok) { ++releases; allocated=nullptr; } return ok;
}
static NTSTATUS WINAPI random_bytes(BCRYPT_ALG_HANDLE h,PUCHAR out,ULONG size,ULONG flags) {
    ++rngCalls;
    assert(!h && size==8 && flags==BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    uint64_t value=0;
    if(rngMode==1) return NTSTATUS(0xc0000001);
    if(rngMode==2) value=0; // exhaustion
    else if(rngMode==3) value=0x2b992ddfa232ULL; // exhaustion
    else if(rngMode==4) value=rngCalls==1?0:(rngCalls==2?0x2b992ddfa232ULL:0xffff123456789abcULL);
    else return BCryptGenRandom(h,out,size,flags);
    memcpy(out,&value,8); return 0;
}
#define VirtualAllocFromApp allocate_image
#define VirtualProtectFromApp VirtualProtect
#define VirtualFree release_image
#define BCryptGenRandom random_bytes
#include "../host/wxl_mapper.h"
#undef VirtualFree
#undef BCryptGenRandom
static wxl::Carrier carrier;
static DWORD (*nativeIndex)();
static DWORD counted_index() { ++indexCalls; return nativeIndex(); }
static void log_line(const char* message) { puts(message); }
static uint64_t current_cookie() {
    assert(allocated && cookieRva+8<=allocatedSize);
    uint64_t value; memcpy(&value,allocated+cookieRva,8);
    assert(value && value!=wxl::msvc_config::default_cookie && !(value>>48));
    return value;
}
extern "C" void observe_cookie(unsigned stage,uintptr_t value,uintptr_t complement) {
    assert(stage==observations+1 && value==current_cookie());
    if(stage==3) assert(complement==~value);
    ++observations;
}
// Test-only owned-image range recognition for the genuine static CRT unwinder.
// This is not a native fallback or a change to Play's resolver.
static PVOID WINAPI own_pc_to_header(PVOID pc,PVOID* base) {
    if(uintptr_t(pc)>=uintptr_t(allocated) && uintptr_t(pc)-uintptr_t(allocated)<allocatedSize) {
        if(base) *base=allocated; return allocated;
    }
    return RtlPcToFileHeader(pc,base);
}
static bool missingImport;
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal) {
    ++resolutions; current_cookie(); // before even the first import resolution
    assert(!observations && !ordinal);
    if(!strcmp(dll,"cookie-observer.dll") && name && !strcmp(name,"observe_cookie"))
        return missingImport?0:uintptr_t(&observe_cookie);
    if(_stricmp(dll,"kernel32.dll") || !name) return 0;
    if(!strcmp(name,"RtlPcToFileHeader")) return uintptr_t(&own_pc_to_header);
    // Only the generated fixture's finite desktop kernel32 import surface.
    static const char* allowed[]={
        "CloseHandle","CreateFileW","DeleteCriticalSection","EncodePointer","EnterCriticalSection",
        "ExitProcess","FindClose","FindFirstFileExA","FindNextFileA","FlushFileBuffers",
        "FreeEnvironmentStringsW","FreeLibrary","GetACP","GetCPInfo","GetCommandLineA","GetCommandLineW",
        "GetConsoleCP","GetConsoleMode","GetCurrentProcess","GetCurrentProcessId","GetCurrentThreadId",
        "GetEnvironmentStringsW","GetFileType","GetLastError","GetModuleFileNameA","GetModuleHandleExW",
        "GetModuleHandleW","GetOEMCP","GetProcAddress","GetProcessHeap","GetStartupInfoW","GetStdHandle",
        "GetStringTypeW","GetSystemTimeAsFileTime","HeapAlloc","HeapFree","HeapReAlloc","HeapSize",
        "InitializeCriticalSectionAndSpinCount","InitializeSListHead","InterlockedFlushSList",
        "IsDebuggerPresent","IsProcessorFeaturePresent","IsValidCodePage","LCMapStringW","LeaveCriticalSection",
        "LoadLibraryExW","MultiByteToWideChar","QueryPerformanceCounter","RaiseException","RtlCaptureContext",
        "RtlLookupFunctionEntry","RtlUnwindEx","RtlVirtualUnwind","SetFilePointerEx","SetLastError",
        "SetStdHandle","SetUnhandledExceptionFilter","TerminateProcess","TlsAlloc","TlsFree","TlsGetValue",
        "TlsSetValue","UnhandledExceptionFilter","WideCharToMultiByte","WriteConsoleW","WriteFile"};
    for(auto accepted:allowed) if(!strcmp(name,accepted))
        return uintptr_t(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),name));
    return 0;
}
struct Fixture {
    std::vector<BYTE> bytes;
    IMAGE_NT_HEADERS64& nt() { return *(IMAGE_NT_HEADERS64*)(bytes.data()+((IMAGE_DOS_HEADER*)bytes.data())->e_lfanew); }
    IMAGE_SECTION_HEADER* sections() { return IMAGE_FIRST_SECTION(&nt()); }
    IMAGE_DATA_DIRECTORY& dir(unsigned i) { return nt().OptionalHeader.DataDirectory[i]; }
    BYTE* at(size_t rva,size_t size=1) {
        for(unsigned i=0;i<nt().FileHeader.NumberOfSections;++i) {
            auto s=sections()[i];
            if(rva>=s.VirtualAddress && wxl::range(rva-s.VirtualAddress,size,s.SizeOfRawData))
                return bytes.data()+s.PointerToRawData+(rva-s.VirtualAddress);
        }
        assert(!"invalid fixture rva"); return nullptr;
    }
    template<class T> T get(size_t rva) { T value; memcpy(&value,at(rva,sizeof value),sizeof value); return value; }
    template<class T> void put(size_t rva,T value) { memcpy(at(rva,sizeof value),&value,sizeof value); }
    size_t record() { return dir(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG).VirtualAddress; }
    size_t cookie() { return size_t(get<uint64_t>(record()+0x58)-nt().OptionalHeader.ImageBase); }
    WORD* relocation(size_t rva) {
        auto rd=dir(IMAGE_DIRECTORY_ENTRY_BASERELOC);
        for(size_t pos=rd.VirtualAddress;pos<rd.VirtualAddress+rd.Size;) {
            auto b=(IMAGE_BASE_RELOCATION*)at(pos,8);
            for(size_t i=8;i<b->SizeOfBlock;i+=2) {
                auto word=(WORD*)at(pos+i,2);
                if((*word>>12)==10 && b->VirtualAddress+(*word&4095)==rva) return word;
            }
            pos+=b->SizeOfBlock;
        }
        assert(!"missing fixture relocation"); return nullptr;
    }
    void append_relocation(size_t rva,unsigned type=10) {
        auto& rd=dir(IMAGE_DIRECTORY_ENTRY_BASERELOC);
        struct Block { DWORD page,size; WORD entry,pad; } b{DWORD(rva&~size_t(4095)),12,WORD((type<<12)|(rva&4095)),0};
        memcpy(at(rd.VirtualAddress+rd.Size,12),&b,12); rd.Size+=12;
        for(unsigned i=0;i<nt().FileHeader.NumberOfSections;++i) if(sections()[i].VirtualAddress==rd.VirtualAddress)
            sections()[i].Misc.VirtualSize=std::max(sections()[i].Misc.VirtualSize,rd.Size);
    }
    std::string save(const char* directory,unsigned number) {
        std::string path=std::string(directory)+"/mutation-"+std::to_string(number)+".dll";
        std::ofstream stream(path,std::ios::binary); stream.write((const char*)bytes.data(),bytes.size()); assert(stream.good());
        return path;
    }
};
static void reset() {
    allocations=releases=resolutions=observations=rngCalls=indexCalls=allocationAttempts=0;
    rngMode=0; missingImport=failAllocation=false;
}
static void check_protections(const wxl::Image& image) {
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(image.base+image.msvc.cookie.rva,&info,sizeof info) && info.Protect==PAGE_READWRITE);
    assert(VirtualQuery(image.base+image.msvc.record.rva,&info,sizeof info) && info.Protect==PAGE_READONLY);
    for(auto s:image.msvc.slots)
        assert(VirtualQuery(image.base+s.rva,&info,sizeof info) && info.Protect==PAGE_READONLY);
    for(size_t pos=0;pos<image.size;) {
        assert(VirtualQuery(image.base+pos,&info,sizeof info));
        assert(info.Protect!=PAGE_EXECUTE_READWRITE && info.Protect!=PAGE_EXECUTE_WRITECOPY);
        pos=size_t((BYTE*)info.BaseAddress-image.base)+info.RegionSize;
    }
}
static bool load_file(const std::string& path,wxl::LoadConfig mode,bool mapped=false,bool check=false) {
    wchar_t wide[4096]; assert(MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide,4096));
    bool loaded;
    { wxl::Image image;
      loaded=wxl::load(image,wide,log_line,carrier,resolve,mapped,mode);
      if(check && loaded) {
          assert(image.cookieInitialized && wxl::msvcPointers(image));
          check_protections(image);
          assert(image.functions && image.tlsAttached && observations==3 && indexCalls==1);
          auto fn=(int(*)())image.symbol("fixture_check"); assert(fn && fn()==42);
      }
    }
    assert(allocations==releases);
    return loaded;
}
int main(int argc,char** argv) {
    assert(argc==4);
    HMODULE nativeCarrier=LoadLibraryA(argv[3]); assert(nativeCarrier);
    nativeIndex=(DWORD(*)())GetProcAddress(nativeCarrier,"carrier_index");
    carrier={counted_index,(BYTE*(*)())GetProcAddress(nativeCarrier,"carrier_block"),65536};
    assert(nativeIndex && carrier.block);
    Fixture original;
    std::ifstream input(argv[1],std::ios::binary);
    original.bytes.assign(std::istreambuf_iterator<char>(input),{}); assert(!original.bytes.empty());
    cookieRva=original.cookie();
    reset();
    for(auto mode:{wxl::LoadConfig::reject,wxl::LoadConfig::inert_wine}) for(bool mapped:{false,true})
        assert(!load_file(argv[1],mode,mapped));
    assert(!allocations && !observations && !rngCalls && !resolutions);
    // A failed explicit allocation must not leak policy into a different load on
    // the SAME Image. Both legacy policies have supported TLS metadata here;
    // mapping alone would miss stale explicit prepareTls dispatch.
    bool retriesPassed=true;
    for(unsigned kind=0;kind<3;++kind) {
        auto f=original;
        auto mode=kind==2?wxl::LoadConfig::inert_wine:wxl::LoadConfig::reject;
        if(kind!=0) {
            for(size_t off:{0x58,0x70,0x78,0x118,0x120,0x128,0x130,0x138}) {
                *f.relocation(f.record()+off)=0;
                if(off!=0x58 && off!=0x130)
                    *f.relocation(size_t(f.get<uint64_t>(f.record()+off)-f.nt().OptionalHeader.ImageBase))=0;
            }
            memset(f.at(f.record(),0x140),0,0x140); f.put<uint32_t>(f.record(),0x140);
        }
        if(mode==wxl::LoadConfig::reject) f.dir(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG)={};
        // No imports or guest execution are needed for this TLS preparation test.
        f.dir(IMAGE_DIRECTORY_ENTRY_IMPORT)={};
        auto path=f.save(argv[2],11000+kind);
        wchar_t originalWide[4096],retryWide[4096];
        assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,originalWide,4096));
        assert(MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,retryWide,4096));
        reset();
        { wxl::Image image;
          failAllocation=true;
          assert(!wxl::load(image,originalWide,log_line,carrier,resolve,true,wxl::LoadConfig::msvc_x64_cookie));
          assert(!image.base && allocationAttempts==1 && !allocations && !releases);
          assert(!rngCalls && !resolutions && !observations && !indexCalls);
          bool clean=!image.msvc.record.size && image.trustedHeaders.empty() && image.trustedConfig.empty() &&
                     !image.cookieInitialized && !image.initializedCookie && !image.configEnd;
          failAllocation=false;
          bool loaded=wxl::load(image,retryWide,log_line,carrier,nullptr,true,mode);
          bool prepared=loaded && wxl::prepareTls(image,log_line,carrier);
          printf("MSVC-COOKIE allocation retry kind=%u clean=%d loaded=%d prepared=%d\n",kind,clean,loaded,prepared);
          retriesPassed &= clean && loaded && prepared && !image.msvc.record.size &&
                           image.trustedHeaders.empty() && !image.cookieInitialized &&
                           image.tlsKey!=TLS_OUT_OF_INDEXES && !image.tlsCallbacks.empty() &&
                           image.tlsTemplate.size()>0 && indexCalls==1 && !rngCalls && !observations && !resolutions;
        }
        assert(allocations==1 && releases==1 && allocationAttempts==2);
    }
    assert(retriesPassed);
    puts("MSVC-COOKIE SAME-Image allocation failure retries passed: default spans, default TLS, inert TLS");
    reset();
    assert(load_file(argv[1],wxl::LoadConfig::msvc_x64_cookie,false,true));
    assert(rngCalls>=1 && rngCalls<=8 && resolutions>1 && observations==3);
    puts("MSVC-COOKIE owned static CRT complement, TLS-before-entry and exception/unwind passed");
    const auto explicitMode=wxl::LoadConfig::msvc_x64_cookie;
    unsigned mutation=0;
    auto reject=[&](Fixture f) {
        auto path=f.save(argv[2],mutation++);
        reset(); assert(!load_file(path,explicitMode));
        assert(!allocations && !rngCalls && !resolutions && !observations && !indexCalls);
        reset(); assert(!load_file(path,explicitMode,true));
        assert(!allocations && !rngCalls && !resolutions && !observations && !indexCalls);
    };
    // Every unsupported byte is independently rejected, including real-game
    // GuardFlags=0x100 and VolatileMetadata (which this mode does NOT support).
    constexpr size_t pointers[]={0x58,0x70,0x78,0x118,0x120,0x128,0x130,0x138};
    for(size_t off=4;off<0x140;++off) {
        bool pointer=false; for(auto p:pointers) if(off>=p && off-p<8) pointer=true;
        if(pointer) continue;
        auto f=original; f.at(f.record())[off]=1; reject(std::move(f));
    }
    { auto f=original; f.nt().OptionalHeader.DllCharacteristics|=0x4000; reject(std::move(f)); }
    { auto f=original; f.put<uint32_t>(f.record()+0x90,0x100); f.put<uint64_t>(f.record()+0x100,f.nt().OptionalHeader.ImageBase+f.record()+0x140); reject(std::move(f)); }
    { auto f=original; f.put<uint32_t>(f.record(),0x138); reject(std::move(f)); }
    { auto f=original; --f.dir(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG).Size; reject(std::move(f)); }
    for(uint32_t flag:{uint32_t(IMAGE_SCN_MEM_WRITE),uint32_t(IMAGE_SCN_MEM_EXECUTE)}) {
        auto f=original;
        for(unsigned i=0;i<f.nt().FileHeader.NumberOfSections;++i) {
            auto& s=f.sections()[i]; if(f.record()>=s.VirtualAddress && f.record()-s.VirtualAddress<s.SizeOfRawData) s.Characteristics|=flag;
        }
        reject(std::move(f));
    }
    for(bool raw:{false,true}) {
        auto f=original;
        for(unsigned i=0;i<f.nt().FileHeader.NumberOfSections;++i) {
            auto& s=f.sections()[i]; if(f.record()>=s.VirtualAddress && f.record()-s.VirtualAddress<s.SizeOfRawData) {
                auto end=DWORD(f.record()-s.VirtualAddress+0x13f);
                if(raw) s.SizeOfRawData=end; else s.Misc.VirtualSize=end;
            }
        }
        reject(std::move(f));
    }
    { auto f=original; f.sections()[1].PointerToRawData=f.sections()[0].PointerToRawData; reject(std::move(f)); }
    { auto f=original; auto s=f.sections()[0]; assert(s.SizeOfRawData>s.Misc.VirtualSize); f.nt().OptionalHeader.AddressOfEntryPoint=s.VirtualAddress+s.Misc.VirtualSize; reject(std::move(f)); }
    { auto f=original; f.dir(IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG).VirtualAddress=f.nt().OptionalHeader.SizeOfImage; reject(std::move(f)); }
    { auto f=original; f.put<uint64_t>(f.record()+0x58,f.nt().OptionalHeader.ImageBase+f.record()); reject(std::move(f)); }
    { auto f=original; f.put<uint64_t>(f.record()+0x70,f.get<uint64_t>(f.record()+0x78)); reject(std::move(f)); }
    { auto f=original; auto slot=size_t(f.get<uint64_t>(f.record()+0x70)-f.nt().OptionalHeader.ImageBase); f.put<uint64_t>(slot,f.nt().OptionalHeader.ImageBase+f.cookie()); reject(std::move(f)); }
    { auto f=original; auto slot=size_t(f.get<uint64_t>(f.record()+0x130)-f.nt().OptionalHeader.ImageBase); f.put<uint64_t>(slot,1); reject(std::move(f)); }
    // Missing/incorrect expected DIR64, duplicate, straddling and cookie aliases.
    for(auto off:pointers) {
        auto f=original; *f.relocation(f.record()+off)=0; reject(std::move(f));
        auto g=original; *g.relocation(g.record()+off)^=0x9000; reject(std::move(g));
    }
    for(long off:{-4L,0L,0x54L,0x59L,0x13cL}) {
        auto f=original; f.append_relocation(size_t(long(f.record())+off)); reject(std::move(f));
    }
    { auto f=original; f.append_relocation(f.cookie()); reject(std::move(f)); }
    { auto f=original; f.append_relocation(f.cookie()-4); reject(std::move(f)); }
    { auto f=original; f.append_relocation(f.record()+0x58); reject(std::move(f)); }
    // Ordinary and RVA-based delay imports cannot write config/cookie/guard slots,
    // including straddles and the zero thunk terminator.
    for(unsigned delay=0;delay<2;++delay) for(unsigned where=0;where<7;++where) {
        auto f=original;
        auto id=(IMAGE_IMPORT_DESCRIPTOR*)f.at(f.dir(IMAGE_DIRECTORY_ENTRY_IMPORT).VirtualAddress,20);
        size_t targets[]={f.cookie(),f.cookie()-4,f.record()+0x58,f.record()-4,
                         size_t(f.get<uint64_t>(f.record()+0x70)-f.nt().OptionalHeader.ImageBase),f.cookie()-8,
                         size_t(f.get<uint64_t>(f.record()+0x130)-f.nt().OptionalHeader.ImageBase)};
        if(!delay) id->FirstThunk=DWORD(targets[where]);
        else {
            auto& rd=f.dir(IMAGE_DIRECTORY_ENTRY_BASERELOC);
            struct Delay { DWORD attr,name,module,iat,intab,bound,unload,time; } d[2]={{1,id->Name,0,DWORD(targets[where]),id->OriginalFirstThunk,0,0,0},{}};
            memcpy(f.at(rd.VirtualAddress+rd.Size,sizeof d),d,sizeof d);
            f.dir(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)={rd.VirtualAddress+rd.Size,sizeof d};
            for(unsigned i=0;i<f.nt().FileHeader.NumberOfSections;++i) if(f.sections()[i].VirtualAddress==rd.VirtualAddress)
                f.sections()[i].Misc.VirtualSize=rd.Size+sizeof d;
        }
        reject(std::move(f));
    }
    // TLS aliasing cookie via index/template/directory (preflight, not a late
    // native index callback after relocation or import mutation).
    for(unsigned kind=0;kind<4;++kind) {
        auto f=original;
        auto td=f.dir(IMAGE_DIRECTORY_ENTRY_TLS).VirtualAddress;
        if(kind==0) f.put<uint64_t>(td+16,f.nt().OptionalHeader.ImageBase+f.cookie());
        if(kind==1) { f.put<uint64_t>(td,f.nt().OptionalHeader.ImageBase+f.cookie()); f.put<uint64_t>(td+8,f.nt().OptionalHeader.ImageBase+f.cookie()+8); }
        if(kind==2) f.dir(IMAGE_DIRECTORY_ENTRY_TLS).VirtualAddress=DWORD(f.cookie());
        if(kind==3) { f.put<uint64_t>(td,f.nt().OptionalHeader.ImageBase+f.cookie()); f.put<uint64_t>(td+8,f.nt().OptionalHeader.ImageBase+f.cookie()); }
        reject(std::move(f));
    }
    printf("MSVC-COOKIE preallocation negatives=%u (each executable and mapped-only)\n",mutation);
    // Positive control for the distinct eager RVA-based delay binding path.
    { auto f=original;
      auto id=(IMAGE_IMPORT_DESCRIPTOR*)f.at(f.dir(IMAGE_DIRECTORY_ENTRY_IMPORT).VirtualAddress,20);
      auto& rd=f.dir(IMAGE_DIRECTORY_ENTRY_BASERELOC);
      DWORD delayRva=rd.VirtualAddress+rd.Size, iatRva=delayRva+64;
      struct Delay { DWORD attr,name,module,iat,intab,bound,unload,time; } d[2]={{1,id->Name,0,iatRva,id->OriginalFirstThunk,0,0,0},{}};
      memset(f.at(delayRva,80),0,80); memcpy(f.at(delayRva,sizeof d),d,sizeof d);
      f.dir(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)={delayRva,sizeof d};
      for(unsigned i=0;i<f.nt().FileHeader.NumberOfSections;++i) if(f.sections()[i].VirtualAddress==rd.VirtualAddress)
          f.sections()[i].Misc.VirtualSize=rd.Size+80;
      auto path=f.save(argv[2],10000); reset();
      // Executable delay imports still hit the legacy gate, even in explicit mode.
      assert(!load_file(path,explicitMode,false));
      assert(!allocationAttempts && !rngCalls && !resolutions && !observations && !indexCalls);
      reset();
      { wxl::Image image;
        wchar_t wide[4096]; assert(MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide,4096));
        assert(wxl::load(image,wide,log_line,carrier,resolve,true,explicitMode));
        uintptr_t bound; memcpy(&bound,image.base+iatRva,8); assert(bound==uintptr_t(&observe_cookie));
        assert(!observations && !indexCalls);
      }
      assert(allocations==releases);
    }
    // Distinguish RNG error, zero/default exhaustion and successful finite retry.
    for(unsigned mode=1;mode<=3;++mode) for(bool mapped:{false,true}) {
        reset(); rngMode=mode;
        assert(!load_file(argv[1],explicitMode,mapped));
        assert(allocations==1 && releases==1 && rngCalls==(mode==1?1:8));
        assert(!resolutions && !observations && !indexCalls);
    }
    reset(); rngMode=4;
    assert(load_file(argv[1],explicitMode,false,true)); assert(rngCalls==3);
    for(bool mapped:{false,true}) {
        reset(); missingImport=true;
        assert(!load_file(argv[1],explicitMode,mapped));
        assert(allocations==1 && releases==1 && rngCalls && resolutions && !observations && !indexCalls);
    }
    // Later preparation must reject directory, callback ENTRY and TERMINATOR
    // mutations before touching the carrier, TLS key/template/callbacks or index.
    bool metadataRejected=true;
    for(unsigned kind=0;kind<7;++kind) {
        reset();
        { wxl::Image image;
          wchar_t wide[4096]; assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,wide,4096));
          assert(wxl::load(image,wide,log_line,carrier,resolve,true,explicitMode));
          assert(!observations && !indexCalls);
          assert(image.protect(0,image.size,PAGE_READWRITE,log_line));
          auto header=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
          if(kind==0) header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG]={};
          if(kind==1) { header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS]={DWORD(cookieRva),40}; IMAGE_FIRST_SECTION(header)[1].Characteristics|=IMAGE_SCN_MEM_WRITE; }
          if(kind==2) { auto tls=(IMAGE_TLS_DIRECTORY64*)(image.base+image.msvc.tls.directory); tls->AddressOfIndex=uintptr_t(image.base+cookieRva); }
          if(kind==3) image.base[image.msvc.record.rva+0x90]=1;
          if(kind==4) image.base[image.msvc.slots[0].rva]^=1;
          if(kind>=5) {
              auto tls=(IMAGE_TLS_DIRECTORY64*)(image.base+image.msvc.tls.directory);
              auto callbacks=(uint64_t*)uintptr_t(tls->AddressOfCallBacks);
              assert(callbacks && !image.msvc.tls.callbacks.empty());
              if(kind==5) callbacks[0]^=1;
              else { assert(!callbacks[image.msvc.tls.callbacks.size()]); callbacks[image.msvc.tls.callbacks.size()]=callbacks[0]; }
          }
          auto value=current_cookie();
          DWORD indexBefore; memcpy(&indexBefore,image.base+image.msvc.tls.index,4);
          auto rejectedCarrier=carrier; --rejectedCarrier.capacity;
          bool prepared=wxl::prepareTls(image,log_line,rejectedCarrier);
          DWORD indexAfter; memcpy(&indexAfter,image.base+image.msvc.tls.index,4);
          bool unchanged=current_cookie()==value && !indexCalls && !observations &&
                         indexAfter==indexBefore && image.tlsKey==TLS_OUT_OF_INDEXES &&
                         image.tlsTemplate.empty() && image.tlsCallbacks.empty() &&
                         image.carrier.capacity==carrier.capacity;
          printf("MSVC-COOKIE deferred metadata kind=%u rejected=%d unchanged=%d\n",kind,!prepared,unchanged);
          metadataRejected &= !prepared && unchanged;
        }
        assert(allocations==releases);
    }
    assert(metadataRejected);
    puts("MSVC-COOKIE deferred metadata mutations passed: headers/config/slots/TLS directory/callback entry/terminator, no preparation effects");
    // Untampered mapped-only load can be prepared later without consulting
    // mutable header permissions or repeating cookie/guard initialization.
    reset();
    { wxl::Image image;
      wchar_t wide[4096]; assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,wide,4096));
      assert(wxl::load(image,wide,log_line,carrier,resolve,true,explicitMode));
      check_protections(image); assert(!observations && !indexCalls);
      assert(wxl::prepareTls(image,log_line,carrier));
      assert(wxl::prepareUnwind(image,log_line));
      assert(image.attachThread(DLL_PROCESS_ATTACH)); image.tlsAttached=true;
      image.dllEntry=(wxl::Image::DllEntry)(image.base+image.entry);
      assert(image.dllEntry((HINSTANCE)image.base,DLL_PROCESS_ATTACH,nullptr)); image.attached=true;
      assert(observations==3);
      auto check=(int(*)())image.symbol("fixture_check"); assert(check && check()==42);
    }
    assert(allocations==releases);
    reset(); assert(load_file(argv[1],explicitMode,false,true));
    FreeLibrary(nativeCarrier);
    puts("MSVC-COOKIE PASS: real app mapper, owned CRT/TLS/entry/complement/unwind, bounded RNG and negative mutations");
}
