#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <algorithm>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s error=%lu\n",__LINE__,#x,GetLastError()); return 1; } } while (0)
int main() {
    HMODULE module = LoadLibraryW(L"uwph-u32c.dll");
    CHECK(module);
    using Query = int (WINAPI*)(LONG,LPWSTR,int);
    auto query = reinterpret_cast<Query>(GetProcAddress(module,"GetKeyNameTextW"));
    CHECK(query);
    struct Case { unsigned code; const WCHAR* text; bool generic = false; };
    const Case cases[] = {
        {0x1e,L"A"},{0x10,L"Q"},{0x02,L"1"},{0x0c,L"-"},{0x29,L"`"},
        {0x1c,L"Enter"},{0x11c,L"Num Enter"},{0x39,L"Space"},
        {0x2a,L"Shift"},{0x36,L"Right Shift"},{0x36,L"Shift",true},
        {0x11d,L"Right Ctrl"},{0x11d,L"Ctrl",true},{0x138,L"Right Alt"},{0x138,L"Alt",true},
        {0x47,L"Num 7"},{0x147,L"Home"},{0x145,L"Num Lock"},{0x45,L"Pause"},
        {0x3b,L"F1"},{0x44,L"F10"},{0x57,L"F11"},{0x58,L"F12"},{0x7c,L"F13"},{0x87,L"F24"},
        {0x15b,L"Left Windows"},{0x15c,L"Right Windows"},{0x15d,L"Application"},
        {0x11e,L"A"} // Wine's fallback strips the extended flag.
    };
    unsigned count = 0;
    for (auto item : cases) for (int size : {0,1,2,3,8,32}) {
        WCHAR out[40]; std::fill_n(out,40,WCHAR(0xcccc));
        LONG bits = LONG(item.code << 16) | (item.generic ? 0x02000000 : 0) | 0xc0000001;
        SetLastError(0xdead);
        int length = query(bits,out+1,size);
        int expected = size ? std::min(size-1,int(std::wcslen(item.text))) : 0;
        CHECK(length == expected && GetLastError() == 0xdead);
        CHECK(out[0] == 0xcccc);
        if (size) {
            CHECK(!std::wmemcmp(out+1,item.text,expected) && out[1+expected] == 0);
            for (int i=expected+2;i<40;++i) CHECK(out[i] == 0xcccc);
        } else for (auto ch : out) CHECK(ch == 0xcccc);
        ++count;
    }
    // Compare layout-independent printable names and truncation against host
    // Wine. Special-key spelling depends on its display driver, so those use
    // the fixed US backend contract above instead of the host locale.
    auto host = reinterpret_cast<Query>(GetProcAddress(LoadLibraryW(L"user32.dll"),"GetKeyNameTextW"));
    CHECK(host);
    WCHAR warmup[16]; host(0x1e0000,warmup,16);
    for (unsigned scan : {0x02,0x10,0x1e,0x2c,0x30}) for (int size : {2,3,8}) {
        WCHAR expected[16], actual[16];
        std::fill_n(expected,16,WCHAR(0xcccc)); std::fill_n(actual,16,WCHAR(0xcccc));
        SetLastError(0xdead); int length = host(LONG(scan<<16),expected,size); DWORD error = GetLastError();
        SetLastError(0xdead);
        int got = query(LONG(scan<<16),actual,size);
        if (got != length || GetLastError() != error) std::printf("oracle scan=%x size=%d expected=%d got=%d first=%x/%x\n",scan,size,length,got,expected[0],actual[0]);
        CHECK(got == length && GetLastError() == error);
        CHECK(!std::wmemcmp(expected,actual,16));
    }
    WCHAR untouched = 0xcccc;
    SetLastError(0xdead);
    CHECK(!query(0,nullptr,8) && !query(0,&untouched,-1) && untouched == 0xcccc);
    CHECK(GetLastError() == 0xdead);
    WCHAR unknown[3] = {0xcccc,0xcccc,0xcccc};
    CHECK(query(0,unknown,3) == 1 && !unknown[0] && !unknown[1] && unknown[2] == 0xcccc);
    std::printf("KEY NAMES OFFDEVICE PASS %u US-layout cases, 15 Wine comparisons, buffer guards, last error\n",count);
}
