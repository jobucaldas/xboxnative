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
#ifndef CONFIG_TEST_MAPPER
#define CONFIG_TEST_MAPPER "../wxl_mapper.h"
#endif
#include CONFIG_TEST_MAPPER
static unsigned entries, allocations, validated;
static void log_line(const char* s) {
    if (!strcmp(s,"CONFIG attach")) ++entries;
    if (strstr(s,"VirtualAllocFromApp initial")) ++allocations;
    if (strstr(s,"inert Wine load configuration validated")) ++validated;
    puts(s);
}
static bool load_file(const std::string& path, wxl::LoadConfig policy, bool mapped=false) {
    wchar_t wide[4096];
    assert(MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide,4096));
    wxl::Image image;
    return wxl::load(image,wide,log_line,{},nullptr,mapped,policy);
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
    puts("WINE-CONFIG-MAPPER PASS: explicit inert opt-in, default rejection, 9 pre-allocation negatives");
}
