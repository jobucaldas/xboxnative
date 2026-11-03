// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <fstream>
#include <iterator>
#include <cassert>
#define VirtualAllocFromApp VirtualAlloc
#define VirtualProtectFromApp VirtualProtect
static unsigned releases;
static BOOL WINAPI counted_free(LPVOID address, SIZE_T size, DWORD type) {
    BOOL ok=VirtualFree(address,size,type);
    if(ok) ++releases;
    return ok;
}
#define VirtualFree counted_free
#ifndef CONFIG_TEST_MAPPER
#define CONFIG_TEST_MAPPER "../wxl_mapper.h"
#endif
#include CONFIG_TEST_MAPPER
static unsigned entries, allocations, validated;
#ifdef CONFIG_TEST_MAPPED
static unsigned tlsIndexCalls, tlsRejected;
#endif
static void log_line(const char* s) {
#ifdef CONFIG_TEST_MAPPED
    if (!strcmp(s,"WXL FAIL TLS write into inert load configuration")) ++tlsRejected;
#endif
    if (!strcmp(s,"CONFIG attach")) ++entries;
    if (strstr(s,"VirtualAllocFromApp initial")) ++allocations;
    if (strstr(s,"inert Wine load configuration validated")) ++validated;
    puts(s);
}
static bool load_file(const std::string& path, wxl::LoadConfig policy, bool mapped=false,
                      wxl::Resolver resolver=nullptr) {
    wchar_t wide[4096];
    assert(MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide,4096));
    unsigned beforeAllocations=allocations, beforeReleases=releases;
    bool loaded;
    {
        wxl::Image image;
        loaded=wxl::load(image,wide,log_line,{},resolver,mapped,policy);
    }
    assert(allocations-beforeAllocations==releases-beforeReleases);
    return loaded;
}
int main(int argc,char** argv) {
    assert(argc==3);
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<BYTE> original((std::istreambuf_iterator<char>(input)),{});
    assert(!original.empty());
    auto dos=(IMAGE_DOS_HEADER*)original.data();
    auto nt=(IMAGE_NT_HEADERS64*)(original.data()+dos->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    assert(dir.Size==0x140 && dir.VirtualAddress);
    auto sections=IMAGE_FIRST_SECTION(nt);
    size_t configOffset=0, sectionIndex=0;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        auto& s=sections[i];
        if(dir.VirtualAddress>=s.VirtualAddress && dir.VirtualAddress-s.VirtualAddress<s.SizeOfRawData) {
            configOffset=s.PointerToRawData+dir.VirtualAddress-s.VirtualAddress; sectionIndex=i;
        }
    }
    assert(configOffset && configOffset+dir.Size<=original.size());
    assert(!load_file(argv[1],wxl::LoadConfig::reject));
    assert(entries==0 && allocations==0 && validated==0);
#ifdef CONFIG_TEST_MAPPED
    assert(!load_file(argv[1],wxl::LoadConfig::reject,true));
    assert(entries==0 && allocations==0 && validated==0);
#endif
    assert(load_file(argv[1],wxl::LoadConfig::inert_wine));
    assert(entries==1 && allocations==1 && validated==1);
    const size_t ntOffset=dos->e_lfanew;
    for(unsigned test=0;test<9;test++) {
        auto bytes=original;
        auto header=(IMAGE_NT_HEADERS64*)(bytes.data()+ntOffset);
        auto& config=header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
        auto section=IMAGE_FIRST_SECTION(header)+sectionIndex;
        switch(test) {
        case 0: bytes[configOffset+0x58]=1; break; // security cookie
        case 1: bytes[configOffset+0x90]=1; break; // GuardFlags
        case 2: bytes[configOffset+0x13f]=1; break; // final/unknown requirement
        case 3: bytes[configOffset]=0; break; // declared size
        case 4: --config.Size; break;
        case 5: config.VirtualAddress=header->OptionalHeader.SizeOfImage; break;
        case 6: header->OptionalHeader.DllCharacteristics|=0x4000; break;
        case 7: section->Characteristics|=IMAGE_SCN_MEM_WRITE; break;
        case 8: section->SizeOfRawData=config.VirtualAddress-section->VirtualAddress+config.Size-1; break;
        }
        std::string path=std::string(argv[2])+"/config-negative-"+std::to_string(test)+".dll";
        { std::ofstream output(path,std::ios::binary); output.write((const char*)bytes.data(),bytes.size()); assert(output.good()); }
        entries=allocations=validated=0;
        assert(!load_file(path,wxl::LoadConfig::inert_wine));
        assert(entries==0 && allocations==0 && validated==0);
#ifdef CONFIG_TEST_MAPPED
        assert(!load_file(path,wxl::LoadConfig::inert_wine,true));
        assert(entries==0 && allocations==0 && validated==0);
#endif
    }
    // File-byte validation is only sound if no loader write can alter the record
    // before guest code runs: reject relocations and IAT writes into it, after
    // allocation but before any TLS callback or DllMain, and release the image.
    auto writeCase=[&](long field, unsigned kind, const char* name) {
        auto bytes=original;
        auto header=(IMAGE_NT_HEADERS64*)(bytes.data()+ntOffset);
        auto dirs=header->OptionalHeader.DataDirectory;
        auto first=IMAGE_FIRST_SECTION(header);
        auto fileOffset=[&](DWORD rva) {
            for(unsigned i=0;i<header->FileHeader.NumberOfSections;i++)
                if(rva>=first[i].VirtualAddress && rva-first[i].VirtualAddress<first[i].SizeOfRawData)
                    return size_t(first[i].PointerToRawData+rva-first[i].VirtualAddress);
            assert(!"rva outside raw data"); return size_t(0);
        };
        DWORD target=DWORD(long(dir.VirtualAddress)+field);
        if(kind) {
            auto& descriptor=*(IMAGE_IMPORT_DESCRIPTOR*)(bytes.data()+fileOffset(dirs[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress));
            assert(descriptor.FirstThunk);
            if(kind==1) descriptor.FirstThunk=target;
            else {
                // Put owned RVA-based delay descriptors in unused raw relocation padding.
                auto& reloc=dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC];
                IMAGE_SECTION_HEADER* owner=nullptr;
                for(unsigned i=0;i<header->FileHeader.NumberOfSections;i++)
                    if(reloc.VirtualAddress==first[i].VirtualAddress) owner=first+i;
                struct Delay { DWORD attributes,name,module,iat,intab,bound,unload,timestamp; };
                Delay delay[2]={{1,descriptor.Name,0,target,descriptor.OriginalFirstThunk,0,0,0},{}};
                assert(owner && reloc.Size+sizeof delay<=owner->SizeOfRawData);
                memcpy(bytes.data()+owner->PointerToRawData+reloc.Size,delay,sizeof delay);
                dirs[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT]={reloc.VirtualAddress+reloc.Size,sizeof delay};
                owner->Misc.VirtualSize=std::max<DWORD>(owner->Misc.VirtualSize,reloc.Size+sizeof delay);
            }
        } else {
            auto& reloc=dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            IMAGE_SECTION_HEADER* owner=nullptr;
            for(unsigned i=0;i<header->FileHeader.NumberOfSections;i++)
                if(reloc.VirtualAddress==first[i].VirtualAddress) owner=first+i;
            assert(owner && reloc.Size+12<=owner->SizeOfRawData);
            struct { DWORD page, size; WORD entry, pad; } block{target&~0xfffu,12,WORD(0xa000|(target&0xfff)),0};
            memcpy(bytes.data()+owner->PointerToRawData+reloc.Size,&block,12);
            reloc.Size+=12; owner->Misc.VirtualSize=std::max<DWORD>(owner->Misc.VirtualSize,reloc.Size);
        }
        std::string path=std::string(argv[2])+"/config-write-"+name+".dll";
        { std::ofstream output(path,std::ios::binary); output.write((const char*)bytes.data(),bytes.size()); assert(output.good()); }
        return path;
    };
    struct { long field; bool import; const char* name; } writes[]={
        {0x58,false,"reloc-cookie"}, {0,false,"reloc-size"}, {-4,false,"reloc-straddle-start"},
        {0x13c,false,"reloc-straddle-end"}, {0x58,true,"iat-cookie"}};
    for(auto& w:writes) {
        auto path=writeCase(w.field,w.import,w.name);
        entries=allocations=validated=0;
        assert(!load_file(path,wxl::LoadConfig::inert_wine));
        assert(entries==0 && allocations==1 && validated==1);
        entries=allocations=validated=0;
        assert(!load_file(path,wxl::LoadConfig::reject));
        assert(entries==0 && allocations==0 && validated==0);
#ifdef CONFIG_TEST_MAPPED
        assert(!load_file(path,wxl::LoadConfig::inert_wine,true));
        assert(entries==0 && allocations==1 && validated==1);
#endif
    }
#ifdef CONFIG_TEST_MAPPED
    // Delay binding is a distinct write path used by the executable Play mapper.
    // Resolve only the fixture's existing builtins, never a native fallback.
    auto fixtureResolve=+[](const char* dll,const char* name,WORD)->uintptr_t {
        if(name) for(auto& builtin:wxl::builtins)
            if(!_stricmp(dll,builtin.dll) && !strcmp(name,builtin.name)) return builtin.address;
        return 0;
    };
    entries=allocations=validated=0;
    assert(!load_file(writeCase(0x58,2,"delay-cookie"),wxl::LoadConfig::inert_wine,true,fixtureResolve));
    assert(entries==0 && allocations==1 && validated==1);
    puts("WINE-CONFIG-MAPPER PASS: mapped delay write into inert record rejected and allocation released");
    // Exercise the separate TLS preparation API with an owned pre-protection
    // view. In-memory headers are mutable, so neither a forged writable section
    // nor removal of the copied directory may invalidate the file-byte policy.
    for(long field : {0x58L,-2L}) {
        entries=allocations=validated=tlsIndexCalls=tlsRejected=0;
        unsigned beforeReleases=releases;
        {
            wchar_t wide[4096];
            assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,wide,4096));
            wxl::Image image;
            assert(wxl::load(image,wide,log_line,{},nullptr,true,wxl::LoadConfig::inert_wine));
            assert(image.protect(0,image.size,PAGE_READWRITE,log_line));
            auto copied=(IMAGE_NT_HEADERS64*)(image.base+ntOffset);
            IMAGE_FIRST_SECTION(copied)[sectionIndex].Characteristics|=IMAGE_SCN_MEM_WRITE;
            copied->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG]={};
            DWORD tlsRva=DWORD(image.size-sizeof(IMAGE_TLS_DIRECTORY64));
            assert(tlsRva>dir.VirtualAddress+dir.Size);
            auto tls=(IMAGE_TLS_DIRECTORY64*)(image.base+tlsRva);
            *tls={};
            tls->StartAddressOfRawData=tls->EndAddressOfRawData=(uintptr_t)tls;
            tls->AddressOfIndex=(uintptr_t)(image.base+size_t(long(dir.VirtualAddress)+field));
            copied->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS]={tlsRva,sizeof(*tls)};
            wxl::Carrier carrier{+[]()->DWORD { ++tlsIndexCalls; return 7; },
                                 +[]()->BYTE* { static BYTE block[8]; return block; },8};
            assert(!wxl::prepareTls(image,log_line,carrier));
            assert(tlsRejected==1 && tlsIndexCalls==0);
            assert(!memcmp(image.base+dir.VirtualAddress,original.data()+configOffset,dir.Size));
            assert(entries==0 && allocations==1 && validated==1);
        }
        assert(releases-beforeReleases==1);
    }
    puts("WINE-CONFIG-MAPPER PASS: TLS index overlap rejected with mutable headers and allocation released");
#endif
    // A relocation ending exactly at the record is outside it; the fixture pads there.
    assert(!memcmp(original.data()+configOffset-8,"\0\0\0\0\0\0\0\0",8));
    entries=allocations=validated=0;
    assert(load_file(writeCase(-8,false,"reloc-adjacent"),wxl::LoadConfig::inert_wine));
    assert(entries==1 && allocations==1 && validated==1);
    entries=allocations=validated=0;
    assert(load_file(argv[1],wxl::LoadConfig::inert_wine));
    assert(entries==1 && allocations==1 && validated==1);
    puts("WINE-CONFIG-MAPPER PASS: explicit inert opt-in, default rejection, 9 pre-allocation negatives");
    puts("WINE-CONFIG-MAPPER PASS: 5 pre-DllMain write-into-record negatives, adjacent relocation, retry");
}
