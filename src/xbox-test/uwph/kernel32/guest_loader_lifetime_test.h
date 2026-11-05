#pragma once
// Included in both mapped fixtures. These observations live in the EXE so
// process-detach evidence survives release of the DLL's executable pages.
#ifdef PLAY_DLL
static void observeLoaderDetach(void* image,DWORD reason,bool dll) {
    if(reason!=DLL_PROCESS_DETACH)return;
    using Observe=void(WINAPI*)(HMODULE,BOOL);
    auto observe=(Observe)GetProcAddress(GetModuleHandleW(nullptr),"loader_detach_observe");
    if(observe)observe((HMODULE)image,dll);
}
#else
__declspec(dllimport) int dependency_check();
static HMODULE loaderTracked;
static volatile LONG loaderDetachDll,loaderDetachTls,loaderDetachBad,loaderNestedLoaded;
__declspec(dllexport) void WINAPI loader_detach_observe(HMODULE image,BOOL dll) {
    if(image!=loaderTracked)return;
    HMODULE visible=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)image,&visible)||visible!=image)++loaderDetachBad;
    if(dll) {
        if(loaderDetachDll||loaderDetachTls)++loaderDetachBad;
        ++loaderDetachDll;
        HMODULE nested=LoadLibraryW(L"play-detach-target.dll");
        auto check=nested?(int(*)())GetProcAddress(nested,"dependency_check"):nullptr;
        if(!check||!check()||!FreeLibrary(nested))++loaderDetachBad;
        else ++loaderNestedLoaded;
    } else {
        if(loaderDetachDll!=1||loaderDetachTls)++loaderDetachBad;
        ++loaderDetachTls;
    }
}
static DWORD WINAPI loaderExitWorker(void* module) {
    FreeLibraryAndExitThread((HMODULE)module,79);
    return 80;
}
static DWORD testLoaderLifetime() {
    HMODULE main=GetModuleHandleW(nullptr), result=nullptr;
    SetLastError(73);
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,nullptr,&result)||result!=main||GetLastError()!=73)return 50;
    result=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCWSTR)&testLoaderLifetime,&result)||result!=main)return 51;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         nullptr,&result)||result!=main)return 71;
    result=(HMODULE)1;SetLastError(0);
    if(GetModuleHandleExW(0x80000000,L"play-dependency.dll",&result)||GetLastError()!=ERROR_INVALID_PARAMETER||result)return 52;
    result=(HMODULE)1;SetLastError(0);
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         L"play-dependency.dll",&result)||GetLastError()!=ERROR_INVALID_PARAMETER||result)return 53;
    SetLastError(0);
    if(GetModuleHandleExW(0,L"play-dependency.dll",nullptr)||GetLastError()!=ERROR_INVALID_PARAMETER)return 54;
    SetLastError(0);
    if(LoadLibraryExW(nullptr,nullptr,0)||GetLastError()!=ERROR_INVALID_PARAMETER)return 76;
    SetLastError(0);
    if(LoadLibraryExW(L"play-dynamic.dll",(HANDLE)1,0)||GetLastError()!=ERROR_INVALID_PARAMETER)return 77;
    const DWORD unsupported[]={DONT_RESOLVE_DLL_REFERENCES,LOAD_LIBRARY_AS_DATAFILE,
        LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE,LOAD_LIBRARY_AS_IMAGE_RESOURCE,
        LOAD_LIBRARY_REQUIRE_SIGNED_TARGET,LOAD_IGNORE_CODE_AUTHZ_LEVEL,
        DWORD(0x00002000),DWORD(0x00004000),DWORD(0x00008000)};
    for(DWORD flags:unsupported) {
        SetLastError(0);
        if(LoadLibraryExW(L"play-dynamic.dll",nullptr,flags)||GetLastError()!=ERROR_NOT_SUPPORTED)return 78;
        SetLastError(0);
        if(LoadLibraryExA("play-dynamic.dll",nullptr,flags)||GetLastError()!=ERROR_NOT_SUPPORTED)return 83;
        if(GetModuleHandleW(L"play-dynamic.dll"))return 81;
    }
    SetLastError(0);
    if(LoadLibraryExW(L"play-dynamic.dll",nullptr,0x80000000)||GetLastError()!=ERROR_INVALID_PARAMETER)return 82;
    HMODULE dynamic=LoadLibraryW(L"play-dynamic.dll");
    if(!dynamic||LoadLibraryW(L"play-dynamic.dll")!=dynamic)return 55;
    loaderTracked=dynamic;
    if(!GetModuleHandleExW(0,L"play-dynamic.dll",&result)||result!=dynamic)return 56;
    // Two LoadLibrary calls and one GetModuleHandleEx acquire three refs.
    if(!FreeLibrary(dynamic)||!FreeLibrary(dynamic)||GetModuleHandleW(L"play-dynamic.dll")!=dynamic)return 57;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,L"play-dynamic.dll",&result)||result!=dynamic)return 58;
    auto check=(int(*)())GetProcAddress(dynamic,"dependency_check");
    if(!check||!check())return 59;
    if(!FreeLibrary(dynamic)||GetModuleHandleW(L"play-dynamic.dll"))return 60;
    if(loaderDetachDll!=1||loaderDetachTls!=1||loaderDetachBad)return 61;
    if(loaderNestedLoaded!=1||GetModuleHandleW(L"play-detach-target.dll"))return 84;
    MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(dynamic,&memory,sizeof memory)||memory.State!=MEM_FREE)return 62;
    // Static dependencies remain reachable from the executable after release.
    if(!FreeLibrary(GetModuleHandleW(L"play-dependency.dll"))||!dependency_check())return 63;
    loaderTracked=nullptr;
    dynamic=LoadLibraryW(L"play-dynamic.dll");
    if(!dynamic)return 64;
    check=(int(*)())GetProcAddress(dynamic,"dependency_check");
    if(!check||!check()||!FreeLibrary(dynamic)||GetModuleHandleW(L"play-dynamic.dll"))return 65;
    dynamic=LoadLibraryW(L"play-dynamic.dll");
    if(!dynamic)return 72;
    loaderTracked=dynamic;loaderDetachDll=loaderDetachTls=loaderDetachBad=loaderNestedLoaded=0;
    HANDLE worker=CreateThread(nullptr,0,loaderExitWorker,dynamic,0,nullptr);
    if(!worker||WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0)return 73;
    DWORD exitCode=0;
    if(!GetExitCodeThread(worker,&exitCode)||exitCode!=79||!CloseHandle(worker))return 74;
    if(GetModuleHandleW(L"play-dynamic.dll")||loaderDetachDll!=1||loaderDetachTls!=1||loaderDetachBad)return 75;
    if(loaderNestedLoaded!=1||GetModuleHandleW(L"play-detach-target.dll"))return 85;
    loaderTracked=nullptr;
    // Unloading releases the mapped image's TLS carrier for later loads. The
    // packaged pool bounds simultaneous TLS images, not lifetime load count.
    for(unsigned cycle=0;cycle<32;++cycle) {
        HMODULE repeated=LoadLibraryW(L"play-detach-target.dll");
        if(!repeated)return 111;
        auto repeatedCheck=(int(*)())GetProcAddress(repeated,"dependency_check");
        if(!repeatedCheck||!repeatedCheck()||!dependency_check())return 112;
        if(!FreeLibrary(repeated)||GetModuleHandleW(L"play-detach-target.dll"))return 113;
        MEMORY_BASIC_INFORMATION released{};
        if(!VirtualQuery(repeated,&released,sizeof released)||released.State!=MEM_FREE)return 114;
    }
    dynamic=LoadLibraryW(L"play-dynamic.dll");
    if(!dynamic||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,L"play-dynamic.dll",&result)||result!=dynamic)return 66;
    if(!FreeLibrary(dynamic)||GetModuleHandleW(L"play-dynamic.dll")!=dynamic)return 67;
    check=(int(*)())GetProcAddress(dynamic,"dependency_check");
    if(!check||!check())return 68;
    // Native DLL references remain owned by the OS and support release.
    result=nullptr;
    if(!GetModuleHandleExW(0,L"kernel32.dll",&result)||!result||!FreeLibrary(result))return 69;
    result=(HMODULE)1;SetLastError(0);
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,L"k32x-absent.dll",&result)||result||GetLastError()!=ERROR_MOD_NOT_FOUND)return 70;
    return 42;
}
#endif
