#pragma once
namespace pe_tail {
inline unsigned test(void (*log)(const char*)) {
    unsigned checks=0,failures=0;
    auto check=[&](bool pass,const char* label){++checks;if(!pass)++failures;char line[192];snprintf(line,sizeof line,"K32T PE-CHECK %s pass=%u error=%lu",label,unsigned(pass),GetLastError());log(line);};
    auto pt=(BOOL(WINAPI*)(const RECT*,POINT))resolve("user32.dll","PtInRect");
    auto copy=(BOOL(WINAPI*)(RECT*,const RECT*))resolve("user32.dll","CopyRect");
    auto offset=(BOOL(WINAPI*)(RECT*,int,int))resolve("user32.dll","OffsetRect");
    check(pt&&copy&&offset,"rectangle-exports");
    if(pt&&copy&&offset){RECT r{10,20,30,40},out{};check(pt(&r,POINT{10,20})&&!pt(&r,POINT{30,40}),"rectangle-half-open-bounds");check(copy(&out,&r)&&!memcmp(&r,&out,sizeof(r)),"rectangle-copy");check(offset(&out,-10,5)&&out.left==0&&out.top==25&&out.right==20&&out.bottom==45,"rectangle-offset");}
    auto path=(BOOL(WINAPI*)(LPWSTR,LPCWSTR))resolve("shlwapi.dll","PathCanonicalizeW");check(!!path,"path-export");
    if(path){wchar_t out[MAX_PATH]{};check(path(out,L"C:\\one\\..\\two")&&!wcscmp(out,L"C:\\two"),"path-canonicalize");}
    auto argv=(LPWSTR*(WINAPI*)(LPCWSTR,int*))resolve("shell32.dll","CommandLineToArgvW");check(!!argv,"command-line-export");
    if(argv){int count=0;auto args=argv(L"game.exe \"two words\" end",&count);check(args&&count==3&&!wcscmp(args[0],L"game.exe")&&!wcscmp(args[1],L"two words")&&!wcscmp(args[2],L"end"),"command-line-quoted-arguments");if(args)check(!LocalFree(args),"command-line-free");}
    auto hid=(void(WINAPI*)(LPGUID))resolve("hid.dll","HidD_GetHidGuid");check(!!hid,"hid-guid-export");
    if(hid){GUID value{};hid(&value);const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};check(!memcmp(&value,&expected,sizeof(value)),"hid-standard-guid");}
    auto time=(DWORD(WINAPI*)())resolve("winmm.dll","timeGetTime");check(!!time,"multimedia-time-export");
    if(time){DWORD before=time();Sleep(2);DWORD after=time();check(DWORD(after-before)<2000,"multimedia-time-progress");}
    char line[144];snprintf(line,sizeof line,"K32T PE-SELFTEST checks=%u failures=%u synthetic-host-calls no-guest-code",checks,failures);log(line);return failures;
}
}
