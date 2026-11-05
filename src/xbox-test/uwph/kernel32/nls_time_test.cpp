#include "route.h"
#include <cstdio>
#include <cstdlib>
#include <string>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); std::exit(1); } } while(0)
template<class F> F api(const char* name,F) {
    auto p=kernel32_core::resolve("kernel32.dll",name); CHECK(p);
    CHECK(p==kernel32_core::resolve("kernelbase.dll",name)); return reinterpret_cast<F>(p);
}
#define API(n) api(#n,&::n)
static std::wstring locales;
static BOOL CALLBACK collect_locale(LPWSTR name) { locales+=name; locales+=L';';return TRUE; }
static BOOL CALLBACK stop_locale(LPWSTR name) { locales+=name;return FALSE; }
int main() {
    SYSTEMTIME epoch{}; epoch.wYear=1601; epoch.wMonth=1; epoch.wDay=1;
    FILETIME file{}; CHECK(API(SystemTimeToFileTime)(&epoch,&file)); CHECK(file.dwHighDateTime==0); CHECK(file.dwLowDateTime==0);
    SYSTEMTIME civil{}; CHECK(API(FileTimeToSystemTime)(&file,&civil)); CHECK(civil.wYear==1601); CHECK(civil.wDayOfWeek==1);
    epoch.wYear=2024; epoch.wMonth=2; epoch.wDay=29; epoch.wHour=23; epoch.wMinute=59; epoch.wSecond=59; epoch.wMilliseconds=123;
    CHECK(API(SystemTimeToFileTime)(&epoch,&file)); CHECK(API(FileTimeToSystemTime)(&file,&civil));
    CHECK(civil.wYear==2024); CHECK(civil.wMonth==2); CHECK(civil.wDay==29); CHECK(civil.wMilliseconds==123);
    epoch.wYear=2023;
    FILETIME reference{}; SetLastError(0); BOOL valid=SystemTimeToFileTime(&epoch,&reference); DWORD expected=GetLastError();
    SetLastError(0); CHECK(API(SystemTimeToFileTime)(&epoch,&file)==valid); CHECK(GetLastError()==expected);
    if(valid) { CHECK(file.dwHighDateTime==reference.dwHighDateTime); CHECK(file.dwLowDateTime==reference.dwLowDateTime); }
    epoch.wMonth=13; SetLastError(0); CHECK(!API(SystemTimeToFileTime)(&epoch,&file)); CHECK(GetLastError()==ERROR_INVALID_PARAMETER);
    epoch.wYear=2024; epoch.wMonth=2;
    TIME_ZONE_INFORMATION zone{}; zone.Bias=120;
    CHECK(API(SystemTimeToTzSpecificLocalTime)(&zone,&epoch,&civil)); CHECK(civil.wHour==21); CHECK(civil.wDay==29);
    CHECK(API(GetTimeZoneInformation)(&zone)!=TIME_ZONE_ID_INVALID);
    DYNAMIC_TIME_ZONE_INFORMATION dynamic{}; CHECK(API(GetDynamicTimeZoneInformation)(&dynamic)!=TIME_ZONE_ID_INVALID);
    CHECK(API(GetTimeZoneInformationForYear)(2024,&dynamic,&zone));
    API(GetSystemTime)(&civil); CHECK(civil.wYear>=2026); CHECK(civil.wMonth>=1&&civil.wMonth<=12);
    API(GetLocalTime)(&civil); CHECK(civil.wYear>=2026); CHECK(civil.wDay>=1&&civil.wDay<=31);
    API(GetSystemTimeAsFileTime)(&file); CHECK(API(FileTimeToSystemTime)(&file,&civil)); CHECK(civil.wYear>=2026);
    LARGE_INTEGER frequency{},start{},finish{}; CHECK(API(QueryPerformanceFrequency)(&frequency)); CHECK(frequency.QuadPart>0);
    CHECK(API(QueryPerformanceCounter)(&start)); Sleep(2); CHECK(API(QueryPerformanceCounter)(&finish)); CHECK(finish.QuadPart>=start.QuadPart);
    wchar_t text[128]{};
    CHECK(API(GetDateFormatW)(MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT),0,&epoch,L"yyyy-MM-dd",text,128)==11);
    CHECK(!wcscmp(text,L"2024-02-29"));
    CHECK(API(GetTimeFormatW)(LOCALE_INVARIANT,0,&epoch,L"HH:mm:ss",text,128)==9); CHECK(!wcscmp(text,L"23:59:59"));
    CHECK(API(CompareStringEx)(L"en-US",NORM_IGNORECASE,L"Game",-1,L"game",-1,nullptr,nullptr,0)==CSTR_EQUAL);
    CHECK(API(CompareStringW)(LOCALE_INVARIANT,0,L"a",1,L"b",1)==CSTR_LESS_THAN);
    CHECK(API(GetACP)()>0); CHECK(API(GetOEMCP)()>0);
    CPINFO cp{}; CHECK(API(GetCPInfo)(CP_UTF8,&cp)); CHECK(cp.MaxCharSize==4);
    CHECK(API(IsValidCodePage)(CP_UTF8)); CHECK(!API(IsValidCodePage)(0xffffffff));
    CHECK(API(IsValidLocale)(0x0409,LCID_SUPPORTED)); CHECK(!API(IsValidLocale)(0xffffffff,LCID_SUPPORTED));
    CHECK(API(GetLocaleInfoW)(LOCALE_INVARIANT,LOCALE_SDECIMAL,text,128)==2); CHECK(!wcscmp(text,L"."));
    WORD types[3]{}; CHECK(API(GetStringTypeW)(CT_CTYPE1,L"A1 ",3,types)); CHECK(types[0]&C1_ALPHA); CHECK(types[1]&C1_DIGIT); CHECK(types[2]&C1_SPACE);
    CHECK(API(GetThreadLocale)()!=0); CHECK(API(GetUserDefaultLCID)()!=0); CHECK(API(GetUserDefaultLocaleName)(text,128)>1);
    CHECK(API(LCMapStringEx)(L"en-US",LCMAP_UPPERCASE,L"game",-1,text,128,nullptr,nullptr,0)==5); CHECK(!wcscmp(text,L"GAME"));
    CHECK(API(LCMapStringW)(LOCALE_INVARIANT,LCMAP_LOWERCASE,L"GAME",-1,text,128)==5); CHECK(!wcscmp(text,L"game"));
    const char utf8[]="\xc3\xa7\xf0\x9f\x8e\xae";
    CHECK(API(MultiByteToWideChar)(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,text,128)==4); CHECK(text[0]==0xe7); CHECK(text[1]==0xd83c); CHECK(text[2]==0xdfae);
    char bytes[32]{}; CHECK(API(WideCharToMultiByte)(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,bytes,32,nullptr,nullptr)==7); CHECK(!strcmp(bytes,utf8));
    SetLastError(0); CHECK(!API(MultiByteToWideChar)(CP_UTF8,MB_ERR_INVALID_CHARS,"\xff",1,text,128)); CHECK(GetLastError()==ERROR_NO_UNICODE_TRANSLATION);
    SetLastError(0); CHECK(!API(WideCharToMultiByte)(CP_UTF8,WC_ERR_INVALID_CHARS,L"\xd800",1,bytes,32,nullptr,nullptr)); CHECK(GetLastError()==ERROR_NO_UNICODE_TRANSLATION);
    SetLastError(0); CHECK(!API(GetLocaleInfoW)(LOCALE_INVARIANT,LOCALE_SDECIMAL,text,1)); CHECK(GetLastError()==ERROR_INSUFFICIENT_BUFFER);
    CHECK(API(GetOEMCP)()==GetOEMCP()); CHECK(API(GetUserDefaultLCID)()==GetUserDefaultLCID());
    for (LCID locale : {LCID(LOCALE_INVARIANT),LCID(LOCALE_USER_DEFAULT),LCID(0x0409),LCID(0xffffffff)}) {
        for (int capacity : {0,1,128}) {
            wchar_t reference[128]{},actual[128]{};
            SetLastError(123); int count=GetDateFormatW(locale,0,&epoch,L"yyyy-MM-dd",reference,capacity); DWORD error=GetLastError();
            SetLastError(123); CHECK(API(GetDateFormatW)(locale,0,&epoch,L"yyyy-MM-dd",actual,capacity)==count);
            if(!count)CHECK(GetLastError()==error); else if(capacity>=count)CHECK(!wcscmp(actual,reference));
            SetLastError(123); count=GetTimeFormatW(locale,0,&epoch,L"HH:mm:ss",reference,capacity); error=GetLastError();
            SetLastError(123); CHECK(API(GetTimeFormatW)(locale,0,&epoch,L"HH:mm:ss",actual,capacity)==count);
            if(!count)CHECK(GetLastError()==error); else if(capacity>=count)CHECK(!wcscmp(actual,reference));
            SetLastError(123); count=LCMapStringW(locale,LCMAP_UPPERCASE,L"game",-1,reference,capacity); error=GetLastError();
            SetLastError(123); CHECK(API(LCMapStringW)(locale,LCMAP_UPPERCASE,L"game",-1,actual,capacity)==count);
            if(!count)CHECK(GetLastError()==error); else if(capacity>=count)CHECK(!wcscmp(actual,reference));
        }
    }
    auto enumerate=API(EnumSystemLocalesW);
    for(DWORD flags : {DWORD(LCID_INSTALLED),DWORD(LCID_SUPPORTED),DWORD(0x80000000)}) {
        locales.clear(); SetLastError(123); BOOL expected=EnumSystemLocalesW(collect_locale,flags);
        DWORD error=GetLastError(); auto reference=locales;
        locales.clear(); SetLastError(123); CHECK(enumerate(collect_locale,flags)==expected); CHECK(GetLastError()==error); CHECK(locales==reference);
        std::printf("locale enumeration flags=0x%lx success=%d characters=%zu\n",flags,expected,locales.size());
        if(expected && flags!=0x80000000)CHECK(!locales.empty());
    }
    locales.clear(); BOOL stopped=EnumSystemLocalesW(stop_locale,LCID_SUPPORTED); auto stopped_locales=locales;
    locales.clear(); CHECK(enumerate(stop_locale,LCID_SUPPORTED)==stopped); CHECK(locales==stopped_locales); CHECK(!locales.empty());
    std::printf("kernel32 NLS/time: PASS (%u checks; off-device OS locale/time ABI)\n",checks);
}
