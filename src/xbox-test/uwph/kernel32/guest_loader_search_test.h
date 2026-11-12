#pragma once
#ifndef PLAY_DLL
// Exercise the mapped loader, rather than the shared native-only wrapper.
static DWORD testLoaderSearch() {
    // A previous run of this case may have left the root decoy behind (a crash
    // between its copy and its delete); the plain search case below must see
    // the application directory's real semantics.
    DeleteFileW(L"search-dependency.dll");
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
    // Unity's Mono loads its runtime by relative sub-path the way
    // MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll is loaded: a
    // multi-directory relative name with LOAD_WITH_ALTERED_SEARCH_PATH, whose
    // dependency sits in that same sub-directory. The sub-path must resolve
    // against the game root, and the dependency must resolve against the
    // loading module's directory, not the root.
    if(!CreateDirectoryW(L"MonoBleedingEdge",nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return 111;
    if(!CreateDirectoryW(L"MonoBleedingEdge\\EmbedRuntime",nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return 112;
    // search-consumer.dll statically imports search-dependency.dll, so copying
    // it (not play-dependency.dll, which imports nothing) makes the sub-path
    // load actually resolve a dependency from the loading module's directory.
    // A different-answering search-dependency.dll (55, not 77) is placed at the
    // root for this case only: resolving the dependency from there instead of
    // the sub-directory fails the value check below. It is removed afterwards
    // so the plain search case at the top keeps its application-dir semantics.
    if(!CopyFileW(L"loader-search\\search-dependency-root.dll",L"search-dependency.dll",FALSE))return 113;
    if(!CopyFileW(L"loader-search\\search-consumer.dll",L"MonoBleedingEdge\\EmbedRuntime\\play-mono.dll",FALSE))return 114;
    if(!CopyFileW(L"loader-search\\search-dependency.dll",L"MonoBleedingEdge\\EmbedRuntime\\search-dependency.dll",FALSE))return 114;
    loaded=LoadLibraryExW(L"MonoBleedingEdge\\EmbedRuntime\\play-mono.dll",nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!loaded)return 115;
    auto mono_check=(DWORD(WINAPI*)())GetProcAddress(loaded,"search_consumer_value");
    if(!mono_check||mono_check()!=77)return 116;   // 55 would mean the root copy answered
    if(!GetModuleHandleW(L"search-dependency.dll"))return 117;   // dependency from the sub-directory
    if(!FreeLibrary(loaded))return 118;
    if(DeleteFileW(L"MonoBleedingEdge\\EmbedRuntime\\play-mono.dll")==0)return 119;
    if(DeleteFileW(L"MonoBleedingEdge\\EmbedRuntime\\search-dependency.dll")==0)return 120;
    if(DeleteFileW(L"search-dependency.dll")==0)return 121;   // root decoy removed
    // Negative control: with the sub-directory copy missing, the fallback must
    // pick the root copy — and the value check must see exactly that (55, not
    // 77). A loader that cached the old sub-directory mapping, or refuses the
    // fallback, fails here.
    if(!CopyFileW(L"loader-search\\search-dependency-root.dll",L"search-dependency.dll",FALSE))return 122;
    if(!CopyFileW(L"loader-search\\search-consumer.dll",L"MonoBleedingEdge\\EmbedRuntime\\play-mono.dll",FALSE))return 123;
    loaded=LoadLibraryExW(L"MonoBleedingEdge\\EmbedRuntime\\play-mono.dll",nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!loaded)return 124;
    mono_check=(DWORD(WINAPI*)())GetProcAddress(loaded,"search_consumer_value");
    if(!mono_check||mono_check()!=55)return 125;   // the root copy is the only one left
    if(!FreeLibrary(loaded))return 126;
    if(!DeleteFileW(L"MonoBleedingEdge\\EmbedRuntime\\play-mono.dll"))return 127;
    if(!DeleteFileW(L"search-dependency.dll"))return 128;   // root decoy removed
    RemoveDirectoryW(L"MonoBleedingEdge\\EmbedRuntime");
    RemoveDirectoryW(L"MonoBleedingEdge");
    if(!SetDllDirectoryW(L""))return 98;
    if(LoadLibraryW(L"play-search.dll"))return 99;
    if(!SetDllDirectoryW(nullptr))return 100;
    return 42;
}
#endif
