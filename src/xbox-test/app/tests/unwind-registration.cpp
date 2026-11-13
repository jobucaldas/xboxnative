// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include "../host/wxl_mapper.h"
#include <cstdio>
#include <string>

static std::string lastLog;
static unsigned rejectLogs=0;
static void capture(const char* text) { lastLog=text; if(lastLog.rfind("WXL UNWIND REJECT",0)==0) ++rejectLogs; }

struct Fixture {
    wxl::Image image;

    Fixture() {
        image.size=0x4000;
        image.base=(BYTE*)VirtualAlloc(nullptr,image.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if (!image.base) return;
        image.log=capture;
        auto dos=(IMAGE_DOS_HEADER*)image.base;
        dos->e_magic=IMAGE_DOS_SIGNATURE;
        dos->e_lfanew=0x80;
        auto nt=(IMAGE_NT_HEADERS64*)(image.base+dos->e_lfanew);
        nt->Signature=IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.NumberOfSections=2;
        nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->FileHeader.Characteristics=IMAGE_FILE_DLL;
        nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage=(DWORD)image.size;
        nt->OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        auto sections=IMAGE_FIRST_SECTION(nt);
        sections[0].VirtualAddress=0x1000;
        sections[0].Misc.VirtualSize=0x1000;
        sections[0].SizeOfRawData=0x1000;
        sections[0].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE;
        sections[1].VirtualAddress=0x2000;
        sections[1].Misc.VirtualSize=0x1000;
        sections[1].SizeOfRawData=0x1000;
        sections[1].Characteristics=IMAGE_SCN_MEM_READ;
        BYTE* unwind=image.base+0x2020;
        unwind[0]=1;
        auto directory=&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        directory->VirtualAddress=0x2000;
        directory->Size=sizeof(RUNTIME_FUNCTION);
        auto table=(PRUNTIME_FUNCTION)(image.base+directory->VirtualAddress);
        table[0]={0x1000,0x1080,0x2020};
    }

    bool ready() const { return image.base!=nullptr; }
    void setFunctions(const RUNTIME_FUNCTION* entries,size_t count) {
        auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
        auto directory=&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        directory->Size=(DWORD)(count*sizeof(RUNTIME_FUNCTION));
        memcpy(image.base+directory->VirtualAddress,entries,directory->Size);
    }
};

static bool prepare(Fixture& fixture) {
    lastLog.clear(); rejectLogs=0;
    return wxl::prepareUnwind(fixture.image,capture);
}

int main() {
    Fixture empty;
    if (!empty.ready()) return 1;
    auto nt=(IMAGE_NT_HEADERS64*)(empty.image.base+((IMAGE_DOS_HEADER*)empty.image.base)->e_lfanew);
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]={};
    if (!prepare(empty) || empty.image.functions) return 2;
    std::puts("UNWIND-REGISTRATION PASS: empty exception directory accepted without registration");

    Fixture valid;
    if (!valid.ready() || !prepare(valid) || !valid.image.functions) return 3;
    std::puts("UNWIND-REGISTRATION PASS: valid .pdata registered");

    Fixture duplicate; if (!duplicate.ready()) return 10;
    ((BYTE*)duplicate.image.base)[0x2040]=1;
    const RUNTIME_FUNCTION identical[]={{0x1000,0x1080,0x2020},{0x1000,0x1080,0x2040}}; duplicate.setFunctions(identical,2);
    if (!prepare(duplicate) || !duplicate.image.functions || lastLog.find("WXL UNWIND DUPLICATES count=1")==std::string::npos) return 11;
    std::puts("UNWIND-REGISTRATION PASS: exact duplicate ranges registered with one bounded warning");

    Fixture overlap;
    if (!overlap.ready()) return 4;
    const RUNTIME_FUNCTION overlapping[]={
        {0x1000,0x1080,0x2020},
        {0x1070,0x1100,0x2020},
    };
    overlap.setFunctions(overlapping,2);
    if (prepare(overlap) || overlap.image.functions || rejectLogs!=1 ||
        lastLog.find("index=1 reason=begin-before-previous-end")==std::string::npos ||
        lastLog.find("previousBegin=0x00001000 previousEnd=0x00001080")==std::string::npos) return 5;
    std::puts("UNWIND-REGISTRATION PASS: sorted overlapping function ranges rejected with first-failure details");

    Fixture unsorted;
    if (!unsorted.ready()) return 6;
    const RUNTIME_FUNCTION reversed[]={
        {0x1080,0x1100,0x2020},
        {0x1000,0x1080,0x2020},
    };
    unsorted.setFunctions(reversed,2);
    if (prepare(unsorted) || unsorted.image.functions || rejectLogs!=1 ||
        lastLog.find("index=1 reason=begin-before-previous-end")==std::string::npos) return 7;
    std::puts("UNWIND-REGISTRATION PASS: unsorted table rejected before registration");

    Fixture badUnwind; if (!badUnwind.ready()) return 12;
    const RUNTIME_FUNCTION badUnwindEntry={0x1000,0x1080,0x4000}; badUnwind.setFunctions(&badUnwindEntry,1);
    if (prepare(badUnwind) || badUnwind.image.functions || rejectLogs!=1 || lastLog.find("reason=unwind-outside-image")==std::string::npos || lastLog.find("checkSection=none checkProtection=0x00000000")==std::string::npos) return 13;
    std::puts("UNWIND-REGISTRATION PASS: unwind bounds diagnostic identifies the failing RVA section");

    Fixture outOfBounds;
    if (!outOfBounds.ready()) return 8;
    const RUNTIME_FUNCTION outside={0x3ff0,0x4010,0x2020};
    outOfBounds.setFunctions(&outside,1);
    if (prepare(outOfBounds) || outOfBounds.image.functions || rejectLogs!=1 ||
        lastLog.find("index=0 reason=function-range-outside-image")==std::string::npos) return 9;
    std::puts("UNWIND-REGISTRATION PASS: out-of-image function range remains fatal");
    return 0;
}
