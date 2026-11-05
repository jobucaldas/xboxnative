#pragma once
#ifndef PLAY_DLL
// Exercise the mapped loader, rather than the shared native-only wrapper.
static DWORD testLoaderSearch() {
    static wchar_t executable[32768];
    DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length||length>=32768)return 92;
    wchar_t* slash=nullptr;
    for(wchar_t* p=executable;*p;++p)if(*p==L'\\'||*p==L'/')slash=p;
    if(!slash)return 93;
    slash[1]=0;
    const wchar_t suffix[]=L"loader-search";
    size_t end=0;while(executable[end])++end;
    for(size_t i=0;i<sizeof(suffix)/sizeof(*suffix);++i)executable[end+i]=suffix[i];
    if(!SetDllDirectoryW(executable))return 94;
    HMODULE loaded=LoadLibraryW(L"play-search");
    if(!loaded)return 95;
    auto check=(int(*)())GetProcAddress(loaded,"dependency_check");
    if(!check||!check())return 96;
    if(!FreeLibrary(loaded)||GetModuleHandleW(L"play-search.dll"))return 97;
    loaded=LoadLibraryW(L"search-consumer.dll");
    if(!loaded)return 101;
    auto value=(DWORD(WINAPI*)())GetProcAddress(loaded,"search_consumer_value");
    if(!value||value()!=77)return 102;
    if(!GetModuleHandleW(L"search-dependency.dll"))return 103;
    if(!FreeLibrary(loaded)||GetModuleHandleW(L"search-dependency.dll"))return 104;
    // ANSI guest loaders and module queries must use the file API code page.
    char encoded[64];
    if(!WideCharToMultiByte(CP_ACP,0,L"caf\u00e9.dll",-1,encoded,sizeof(encoded),nullptr,nullptr))return 105;
    loaded=LoadLibraryA(encoded);
    if(!loaded)return 106;
    if(GetModuleHandleA(encoded)!=loaded)return 107;
    HMODULE queried=nullptr;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,encoded,&queried)||queried!=loaded)return 108;
    if(!FreeLibrary(loaded)||GetModuleHandleW(L"caf\u00e9.dll"))return 109;
    loaded=LoadLibraryExA(encoded,nullptr,0);
    if(!loaded||!FreeLibrary(loaded))return 110;
    if(!SetDllDirectoryW(L""))return 98;
    if(LoadLibraryW(L"play-search.dll"))return 99;
    if(!SetDllDirectoryW(nullptr))return 100;
    return 42;
}
#endif
