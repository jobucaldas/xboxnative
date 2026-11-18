// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>
#include <wincrypt.h>
#include <lmcons.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char user[16]; DWORD user_len=sizeof user;
    if(!GetUserNameA(user,&user_len)||strcmp(user,"Player")||user_len!=7)return 1;
    WCHAR userw[16]; user_len=16;
    if(!GetUserNameW(userw,&user_len)||wcscmp(userw,L"Player")||user_len!=7)return 2;

    HKEY root_a=0,root_w=0,child_a=0,child_w=0; DWORD disp=0;
    if(RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\x86fixtureA",0,NULL,0,KEY_ALL_ACCESS,NULL,&root_a,&disp)||disp!=REG_CREATED_NEW_KEY)return 3;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\x86fixtureW",0,NULL,0,KEY_ALL_ACCESS,NULL,&root_w,&disp)||disp!=REG_CREATED_NEW_KEY)return 4;
    HKEY opened=0;
    if(RegOpenKeyExA(HKEY_CURRENT_USER,"Software\\x86fixtureA",0,KEY_READ,&opened))return 5;
    if(RegCloseKey(opened))return 6;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\x86fixtureW",0,KEY_READ,&opened))return 7;
    if(RegCloseKey(opened))return 8;

    const char text_a[]="ansi"; const WCHAR text_w[]=L"wide";
    if(RegSetValueExA(root_a,"value",0,REG_BINARY,(const BYTE*)text_a,sizeof text_a))return 9;
    if(RegSetValueExW(root_w,L"value",0,REG_BINARY,(const BYTE*)text_w,sizeof text_w))return 10;
    char out_a[8]={0}; BYTE out_w[16]={0}; DWORD type=0,size=sizeof out_a;
    if(RegQueryValueExA(root_a,"value",NULL,&type,(BYTE*)out_a,&size)||type!=REG_BINARY||size!=sizeof text_a||memcmp(out_a,text_a,size))return 11;
    size=sizeof out_w;
    if(RegQueryValueExW(root_w,L"value",NULL,&type,out_w,&size)||type!=REG_BINARY||size!=sizeof text_w||memcmp(out_w,text_w,size))return 12;

    if(RegCreateKeyExA(root_a,"child",0,NULL,0,KEY_ALL_ACCESS,NULL,&child_a,&disp))return 13;
    if(RegCreateKeyExW(root_w,L"child",0,NULL,0,KEY_ALL_ACCESS,NULL,&child_w,&disp))return 14;
    char key_a[16]; WCHAR key_w[16]; DWORD key_len=sizeof key_a;
    if(RegEnumKeyExA(root_a,0,key_a,&key_len,NULL,NULL,NULL,NULL)||strcmp(key_a,"child"))return 15;
    key_len=16;
    if(RegEnumKeyExW(root_w,0,key_w,&key_len,NULL,NULL,NULL,NULL)||wcscmp(key_w,L"child"))return 16;
    char value_a[16]; WCHAR value_w[16]; key_len=sizeof value_a; size=sizeof out_a;
    if(RegEnumValueA(root_a,0,value_a,&key_len,NULL,&type,(BYTE*)out_a,&size)||strcmp(value_a,"value")||type!=REG_BINARY)return 17;
    key_len=16; size=sizeof out_w;
    if(RegEnumValueW(root_w,0,value_w,&key_len,NULL,&type,out_w,&size)||wcscmp(value_w,L"value")||type!=REG_BINARY)return 18;
    if(RegDeleteValueA(root_a,"value")||RegDeleteValueW(root_w,L"value"))return 19;
    if(RegCloseKey(child_a)||RegCloseKey(child_w))return 20;
    if(RegDeleteKeyA(root_a,"child")||RegDeleteKeyW(root_w,L"child"))return 21;
    if(RegCloseKey(root_a)||RegCloseKey(root_w))return 22;

    HCRYPTPROV provider=0; BYTE random[16]={0};
    if(!CryptAcquireContextA(&provider,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)||!provider)return 23;
    if(!CryptGenRandom(provider,sizeof random,random))return 24;
    if(!CryptReleaseContext(provider,0))return 25;
    if(!CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)||!provider)return 26;
    if(!CryptReleaseContext(provider,0))return 27;
    puts("x86-crt: advapi32 registry/user/crypto ok");
    return 42;
}
