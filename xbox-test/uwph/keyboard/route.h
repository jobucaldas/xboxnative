#pragma once
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <cstring>
#include <cstdint>
#include "../user32/keymap.h"
namespace keyboard_core {
// The record backend advertises 00000409. Names must describe that same US
// layout, independently of the launcher's UI language or host keyboard layout.
struct KeyName { unsigned scan; const wchar_t* name; };
inline const KeyName names[]={
    {0x01,L"Esc"},{0x0e,L"Backspace"},{0x0f,L"Tab"},{0x1c,L"Enter"},
    {0x1d,L"Ctrl"},{0x2a,L"Shift"},{0x36,L"Right Shift"},{0x37,L"Num *"},
    {0x38,L"Alt"},{0x39,L"Space"},{0x3a,L"Caps Lock"},{0x45,L"Pause"},
    {0x46,L"Scroll Lock"},{0x47,L"Num 7"},{0x48,L"Num 8"},{0x49,L"Num 9"},
    {0x4a,L"Num -"},{0x4b,L"Num 4"},{0x4c,L"Num 5"},{0x4d,L"Num 6"},
    {0x4e,L"Num +"},{0x4f,L"Num 1"},{0x50,L"Num 2"},{0x51,L"Num 3"},
    {0x52,L"Num 0"},{0x53,L"Num Del"},{0x54,L"Sys Req"},
    {0x11c,L"Num Enter"},{0x11d,L"Right Ctrl"},{0x135,L"Num /"},
    {0x137,L"Prnt Scrn"},{0x138,L"Right Alt"},{0x145,L"Num Lock"},
    {0x146,L"Break"},{0x147,L"Home"},{0x148,L"Up"},{0x149,L"Page Up"},
    {0x14b,L"Left"},{0x14d,L"Right"},{0x14f,L"End"},{0x150,L"Down"},
    {0x151,L"Page Down"},{0x152,L"Insert"},{0x153,L"Delete"},
    {0x15b,L"Left Windows"},{0x15c,L"Right Windows"},{0x15d,L"Application"}
};
inline int WINAPI key_name(LONG param,LPWSTR buffer,int capacity) {
    if(!buffer || capacity<=0)return 0;
    unsigned scan=(DWORD(param)>>16)&0x1ff;
    if(param & (1u<<25)) { // Do not distinguish modifier sides.
        if(scan==0x36)scan=0x2a;
        if(scan==0x11d)scan=0x1d;
        if(scan==0x138)scan=0x38;
    }
    wchar_t text[5]{};const wchar_t* name=nullptr;
    for(const auto& row:names)if(row.scan==scan){name=row.name;break;}
    if(!name) {
        unsigned function=scan>=0x3b && scan<=0x44?scan-0x3b+1:
            scan==0x57?11:scan==0x58?12:scan>=0x7c && scan<=0x87?scan-0x7c+13:0;
        if(function) {
            text[0]=L'F';unsigned pos=1;
            if(function>=10)text[pos++]=L'0'+function/10;
            text[pos]=L'0'+function%10;name=text;
        } else for(const auto& row:u32c_keymap::rows)
            if(row.scan==scan && !row.ext && row.ch){text[0]=(unsigned char)row.ch;name=text;break;}
    }
    int length=name?std::min<int>(capacity-1,(int)wcslen(name)):0;
    if(length)memcpy(buffer,name,length*sizeof(WCHAR));
    buffer[length]=0;return length;
}
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!dll || !name || _stricmp(dll,"user32.dll"))return 0;
    return !strcmp(name,"GetKeyNameTextW")?reinterpret_cast<uintptr_t>(&key_name):0;
}
}
