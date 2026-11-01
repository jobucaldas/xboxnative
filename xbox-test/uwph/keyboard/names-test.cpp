#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
    unsigned cases=0;
    // Initialize Wine's keyboard driver before comparing LastError. Its lazy
    // first-call loading may change LastError independently of this API.
    wchar_t warmup[64];GetKeyNameTextW(0x1e<<16,warmup,64);
    // Headless Wine uses its built-in US layout, matching the record backend.
    for(unsigned scan:{0x01u,0x0eu,0x0fu,0x1cu,0x1du,0x2au,0x36u,0x38u,
        0x39u,0x3au,0x3bu,0x44u,0x57u,0x58u,0x7cu,0x87u,0x1eu,0x10u,0x02u,
        0x0cu,0x27u,0x11du,0x138u,0x147u,0x148u,0x14bu,0x14du,0x14fu,
        0x150u,0x151u,0x152u,0x153u,0x15bu,0x15cu,0x15du})
    for(unsigned collapse:{0u,1u<<25})for(int size:{0,2,5,64}) {
        wchar_t actual[64],expected[64];
        for(unsigned i=0;i<64;++i)actual[i]=expected[i]=0xcccc;
        LONG param=(scan<<16)|collapse;
        SetLastError(0x12345);int ref=GetKeyNameTextW(param,expected,size);DWORD error=GetLastError();
        SetLastError(0x12345);int ret=keyboard_core::key_name(param,actual,size);
        if(ret!=ref || memcmp(actual,expected,sizeof actual) || GetLastError()!=error) {
            printf("scan=%x collapse=%x size=%d actual=%d expected=%d first=%x/%x error=%lx/%lx\n",scan,collapse,size,ret,ref,actual[0],expected[0],GetLastError(),error); for(unsigned i=0;i<64;++i)if(actual[i]!=expected[i])printf("diff %u %x/%x\n",i,actual[i],expected[i]);return 1;
        }
        ++cases;
    }
    // Wine 10's fallback printable-key path leaves len uninitialized at size=1
    // (win32u/input.c:NtUserGetKeyNameText). Test the Win32 one-character
    // buffer contract directly, without calling that unsafe oracle branch.
    for(unsigned scan=0;scan<512;++scan) {
        wchar_t guarded[3]={0x1111,0x2222,0x3333};
        CHECK(!keyboard_core::key_name(scan<<16,guarded+1,1));
        CHECK(guarded[0]==0x1111 && guarded[1]==0 && guarded[2]==0x3333);
    }
    wchar_t out[4]={1,2,3,4};
    CHECK(!keyboard_core::key_name(0,&out[0],4) && out[0]==0 && out[1]==2);
    CHECK(!keyboard_core::key_name(0,nullptr,3));
    CHECK(!keyboard_core::key_name(0,out,-1));
    CHECK(keyboard_core::resolve("USER32.dll","GetKeyNameTextW"));
    CHECK(!keyboard_core::resolve("hid.dll","GetKeyNameTextW"));
    CHECK(!keyboard_core::resolve(nullptr,"GetKeyNameTextW"));
    printf("keyboard names: %u Wine differential cases, bounds and routing PASS\n",cases);
}
