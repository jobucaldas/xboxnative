#include <windows.h>
#include <cstdio>
#include <cstring>
#include "../../uwph/user32/route.h"

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

static int call_wvsA(char* dst,const char* fmt,...) {
    va_list args; va_start(args,fmt); int n=play_wsprintf::wvsprintfA(dst,fmt,args); va_end(args); return n;
}
static int call_wvsW(wchar_t* dst,const wchar_t* fmt,...) {
    va_list args; va_start(args,fmt); int n=play_wsprintf::wvsprintfW(dst,fmt,args); va_end(args); return n;
}

int main() {
    char a[1100]{};
    wchar_t w[1100]{};
    CHECK(user32_core::resolve("user32.dll","wsprintfA")!=0);
    CHECK(user32_core::resolve("user32.dll","wsprintfW")!=0);
    CHECK(user32_core::resolve("user32.dll","wvsprintfA")!=0);
    CHECK(user32_core::resolve("user32.dll","wvsprintfW")!=0);
    CHECK(user32_core::resolve("other.dll","wsprintfA")==0);
    CHECK(play_wsprintf::wsprintfA(a,"%c %C %d %i %u %x %X",'A',L'B',-12,34,56u,0xabu,0xabu)==19);
    CHECK(std::strcmp(a,"A B -12 34 56 ab AB")==0);
    CHECK(play_wsprintf::wsprintfA(a,"%hd %hu %ld %li %lu %lx %lX %Ix %IX",-2,65535u,-3L,4L,5UL,0xabUL,0xabUL,0x123456789abcdef0ULL,0x123456789abcdef0ULL)==55);
    CHECK(std::strcmp(a,"-2 65535 -3 4 5 ab AB 123456789abcdef0 123456789ABCDEF0")==0);
    CHECK(play_wsprintf::wsprintfA(a,"%p",reinterpret_cast<void*>(0x1234))==6);
    CHECK(std::strcmp(a,"0x1234")==0);
    CHECK(play_wsprintf::wsprintfA(a,"%.5x|%.4d|%.0u",0xabu,-2,0u)==13);
    CHECK(std::strcmp(a,"000ab|-0002|0")==0);
    CHECK(play_wsprintf::wsprintfA(a,"%#08x|%-6.4s|%.2S|%ls|%lS|%hs|%hS|%%",0x2au,"abcdef",L"WIDE",L"wide",L"wide2","ansi","ansi2")==42);
    CHECK(std::strcmp(a,"0x00002a|abcd  |WI|wide|wide2|ansi|ansi2|%")==0);
    CHECK(play_wsprintf::wsprintfW(w,L"%c %C %hc %hC %lc %lC %hs %hS %ls %lS %S",L'A','B','C','D',L'E',L'F',"ansi","narrow",L"wide",L"wide2","ansi2")==40);
    CHECK(std::wcscmp(w,L"A B C D E F ansi narrow wide wide2 ansi2")==0);
    CHECK(call_wvsA(a,"%lu",123UL)==3&&std::strcmp(a,"123")==0);
    CHECK(call_wvsW(w,L"%ls",L"wide")==4&&std::wcscmp(w,L"wide")==0);
    std::memset(a,'x',sizeof a);
    int n=play_wsprintf::wsprintfA(a,"%01050u",7u);
    CHECK(n==1023&&a[1023]=='\0');
    std::memset(w,L'x',sizeof w);
    n=play_wsprintf::wsprintfW(w,L"%01050u",7u);
    CHECK(n==1023&&w[1023]==L'\0');
    std::puts("WSPRINTF PASS documented conversions, fields and 1024-unit cap");
    return 0;
}
