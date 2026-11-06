#include <windows.h>
extern "C" {
#ifdef PLAY_STATIC_MISSING
__declspec(dllimport) void DeliberatelyMissingPlayImport();
#endif
#ifdef PLAY_STATIC_STEAM
__declspec(dllimport) void ForwardSteam();
#endif
thread_local int tls_value=73;
thread_local int tls_count=0;
DWORD _tls_index;
#include "../../uwph/kernel32/guest_thread_exit_test.h"
#include "../../uwph/kernel32/guest_loader_lifetime_test.h"
#include "../../uwph/kernel32/guest_loader_thread_test.h"
#include "../../uwph/kernel32/guest_loader_search_test.h"
extern BYTE tls_raw[8];
#pragma comment(linker,"/alternatename:tls_raw=tls_value")
static void NTAPI callback(void* image,DWORD reason,void*) {
#ifdef PLAY_DLL
    observeLoaderDetach(image,reason,false);
#else
    (void)image;
#endif
    observeThreadTlsDetach(reason);
    if(reason==DLL_PROCESS_ATTACH||reason==DLL_THREAD_ATTACH){
        ++tls_value;++tls_count;
#ifdef PLAY_DLL
        // A dependency callback may read its importer's TLS before that
        // importer's own callback. Both templates must already be installed.
        auto check=(int(*)())GetProcAddress(GetModuleHandleW(nullptr),"exe_storage_check");
        if(!check||!check())RaiseException(0xe0425354,EXCEPTION_NONCONTINUABLE,0,nullptr);
#endif
    }
}
PIMAGE_TLS_CALLBACK callbacks[]={callback,nullptr};
extern const IMAGE_TLS_DIRECTORY64 _tls_used={
    (ULONGLONG)tls_raw,(ULONGLONG)(tls_raw+4),(ULONGLONG)&_tls_index,
    (ULONGLONG)callbacks,4,IMAGE_SCN_ALIGN_4BYTES};
#ifdef PLAY_DLL
static int processAttached;
__declspec(dllexport) int dependency_check(){return tls_value==74&&tls_count==1&&processAttached;}
BOOL WINAPI guest_dll_entry(HINSTANCE image,DWORD reason,void*) {
    observeLoaderThread(image,reason);
    observeLoaderDetach(image,reason,true);
    observeThreadDllDetach(reason);
    if(reason==DLL_PROCESS_ATTACH){
#ifdef PLAY_STATIC_MISSING
        DeliberatelyMissingPlayImport(); // Must never reach DllMain.
#endif
#ifdef PLAY_REJECT_ATTACH
        return FALSE;
#endif
        if(tls_value!=74||tls_count!=1)return FALSE;processAttached=1;}
    return TRUE;
}
#else
__declspec(dllexport) int exe_storage_check(){return (tls_value==73&&tls_count==0)||(tls_value==74&&tls_count==1);}
__declspec(dllimport) int dependency_check();
__declspec(dllimport) int ForwardName();
static DWORD WINAPI child(void*){return tls_value==74&&tls_count==1&&dependency_check()?42:1;}
static bool contains(const wchar_t* s,const wchar_t* p){for(;*s;++s){auto a=s,b=p;while(*b&&*a==*b){++a;++b;}if(!*b)return true;}return false;}
static BOOL CALLBACK resourceName(HMODULE module,LPCSTR type,LPSTR name,LONG_PTR opaque) {
    auto count=(unsigned*)opaque;
    if(module!=GetModuleHandleW(nullptr)||type!=MAKEINTRESOURCEA(10)||name!=MAKEINTRESOURCEA(101))return FALSE;
    ++*count;return TRUE;
}
DWORD guest_exe_entry(){
    if(tls_value!=74||tls_count!=1||!dependency_check())return 1;
    HMODULE main=GetModuleHandleW(nullptr);wchar_t path[512];
    if(!GetModuleFileNameW(main,path,512)||!contains(path,L"play-guest.exe"))return 2;
    if(!GetModuleHandleW(L"play-dependency.dll"))return 3;
    HMODULE kernel=GetModuleHandleW(L"kernelbase.dll");
    using Init=BOOL(WINAPI*)(LPCRITICAL_SECTION,DWORD,DWORD);
    auto init=(Init)GetProcAddress(kernel,"InitializeCriticalSectionEx");
    if(!init||GetProcAddress(kernel,"DeliberatelyAbsentOptionalExport"))return 6;
    CRITICAL_SECTION section;if(!init(&section,0,0))return 7;DeleteCriticalSection(&section);
    if(contains(GetCommandLineW(),L"loader-lifetime"))return testLoaderLifetime();
    if(contains(GetCommandLineW(),L"loader-search"))return testLoaderSearch();
    if(contains(GetCommandLineW(),L"loader-thread"))return testLoaderThreadNotifications();
    if(contains(GetCommandLineW(),L"thread-exit")) {
        DWORD result=testExplicitThreadExit();
        if(result==42&&contains(GetCommandLineW(),L"thread-exit-main"))ExitThread(42);
        return result;
    }
    if(contains(GetCommandLineW(),L"resource-query")) {
        HRSRC resource=FindResourceA(nullptr,MAKEINTRESOURCEA(101),MAKEINTRESOURCEA(10));
        if(!resource)return 28;
        if(resource!=FindResourceA(main,MAKEINTRESOURCEA(101),MAKEINTRESOURCEA(10)))return 29;
        if(resource!=FindResourceW(nullptr,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10)))return 34;
        if(SizeofResource(nullptr,resource)!=4||SizeofResource(main,resource)!=4)return 30;
        HGLOBAL loaded=LoadResource(nullptr,resource);
        auto data=(WORD*)LockResource(loaded);
        if(!data||data[0]!=0x1234||data[1]!=0x5678)return 31;
        unsigned count=0;
        if(!EnumResourceNamesA(nullptr,MAKEINTRESOURCEA(10),resourceName,(LONG_PTR)&count)||count!=1)return 32;
        SetLastError(0);
        if(FindResourceA(nullptr,MAKEINTRESOURCEA(102),MAKEINTRESOURCEA(10))||GetLastError()!=ERROR_RESOURCE_NAME_NOT_FOUND)return 33;
        resource=FindResourceA(nullptr,"Alpha","DataType");
        if(!resource||SizeofResource(nullptr,resource)!=2)return 35;
        data=(WORD*)LockResource(LoadResource(nullptr,resource));
        if(!data||*data!=0xabcd)return 36;
        if(resource!=FindResourceW(main,L"Alpha",L"DataType"))return 38;
        return 42;
    }
    if(contains(GetCommandLineW(),L"loader-query")) {
        using Header=void*(WINAPI*)(void*,void**);
        auto header=(Header)GetProcAddress(kernel,"RtlPcToFileHeader");
        void* base=nullptr;
        if(!header||header((void*)&guest_exe_entry,&base)!=main||base!=main)return 19;
        using BaseName=DWORD(WINAPI*)(HANDLE,HMODULE,LPWSTR,DWORD);
        auto basename=(BaseName)GetProcAddress(kernel,"K32GetModuleBaseNameW");
        wchar_t name[64]{};
        if(!basename||basename(GetCurrentProcess(),main,name,64)!=14||!contains(name,L"play-guest.exe"))return 20;
        if(basename(GetCurrentProcess(),nullptr,name,64)!=14||!contains(name,L"play-guest.exe"))return 21;
        wchar_t shortName[5]={L'?',L'?',L'?',L'?',L'!'};
        if(basename(GetCurrentProcess(),main,shortName,3)!=3||shortName[0]!=L'p'||shortName[1]!=L'l'||shortName[2]!=L'a'||shortName[3]!=L'?'||shortName[4]!=L'!')return 22;
        if(basename(GetCurrentProcess(),main,shortName,0)||shortName[3]!=L'?')return 23;
        SetLastError(0); if(basename(nullptr,main,name,64)||GetLastError()!=ERROR_INVALID_HANDLE)return 24;
        void* native=(void*)GetProcAddress(kernel,"GetLastError");
        if(!header(native,&base)||base==main)return 25;
        base=main;if(header((void*)1,&base)||base)return 26;
        SetLastError(73); if(header((void*)&guest_exe_entry,&base)!=main||GetLastError()!=73)return 27;
        return 42;
    }
    if(contains(GetCommandLineW(),L"native-cxx")) {
        auto library=LoadLibraryW(L"play-cxx.dll");if(!library)return 31;
        auto test=(int(*)())GetProcAddress(library,"native_cxx_test");
        return test?test():32;
    }
    if(contains(GetCommandLineW(),L"shell-execute")) {
        // Every ShellExecute form, statically linked or looked up on the
        // native shell32 handle, is Play's refusal; nothing is launched.
        auto shell=LoadLibraryW(L"shell32.dll");if(!shell)return 60;
        using Execute=HINSTANCE(WINAPI*)(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT);
        using ExecuteA=HINSTANCE(WINAPI*)(HWND,LPCSTR,LPCSTR,LPCSTR,LPCSTR,INT);
        auto w=(Execute)GetProcAddress(shell,"ShellExecuteW");auto a=(ExecuteA)GetProcAddress(shell,"ShellExecuteA");
        auto ex=(BOOL(WINAPI*)(void*))GetProcAddress(shell,"ShellExecuteExW");
        if(!w||!a||!ex)return 61;
        SetLastError(0);if((ULONG_PTR)w(nullptr,L"open",L"steam://run/1",nullptr,nullptr,1)>32||GetLastError()!=ERROR_NOT_SUPPORTED)return 62;
        SetLastError(0);if((ULONG_PTR)a(nullptr,"open","play-guest.exe",nullptr,nullptr,1)>32||GetLastError()!=ERROR_NOT_SUPPORTED)return 63;
        struct {DWORD size,mask;HWND window;const void* verb,*file,*parameters,*directory;int show;HINSTANCE instance;void* idList;const void* className;HKEY classKey;DWORD hotKey;HANDLE icon,process;} info{};
        info.size=sizeof info;info.file=L"steam://run/1";info.process=(HANDLE)1;
        SetLastError(0);if(ex(&info)||GetLastError()!=ERROR_NOT_SUPPORTED||info.process||(ULONG_PTR)info.instance>32)return 64;
        return 42;
    }
    if(contains(GetCommandLineW(),L"native-msvcp")) {
        auto library=LoadLibraryW(L"play-msvcp.dll");if(!library)return 31;
        auto test=(int(*)())GetProcAddress(library,"native_msvcp_test");
        return test?test():32;
    }
    if(contains(GetCommandLineW(),L"native-vcrt")) {
        auto library=LoadLibraryW(L"play-vcrt.dll");if(!library)return 31;
        auto test=(int(*)())GetProcAddress(library,"native_vcrt_test");
        return test?test():32;
    }
    if(contains(GetCommandLineW(),L"reject-attach")) {
        SetLastError(0);
        if(LoadLibraryW(L"play-reject.dll"))return 16;
        DWORD firstError=GetLastError();
        SetLastError(0);
        if(LoadLibraryW(L"play-reject.dll"))return 17;
        if(firstError!=ERROR_DLL_INIT_FAILED||GetLastError()!=ERROR_DLL_INIT_FAILED)return 18;
        return 42;
    }
    if(contains(GetCommandLineW(),L"pending-")) {
        bool reject=contains(GetCommandLineW(),L"pending-reject-");
        bool ordinal=contains(GetCommandLineW(),L"-ordinal");
        auto forward=GetModuleHandleW(L"play-forward.dll");
        auto target=reject?L"play-pending-reject.dll":L"play-pending-target.dll";
        if(GetModuleHandleW(target))return 33;
        SetLastError(0);
        if(GetProcAddress(forward,reject?"MissingPendingReject":"MissingPending")||GetLastError()!=ERROR_PROC_NOT_FOUND)return 34;
        auto retained=GetModuleHandleW(target);if(!retained)return 35;
        LPCSTR symbol=reject?(ordinal?"PendingRejectOrdinal":"PendingRejectName"):(ordinal?"PendingOrdinal":"PendingName");
        SetLastError(0);
        auto pointer=GetProcAddress(forward,symbol);
        if(reject) {
            if(pointer||GetLastError()!=ERROR_PROC_NOT_FOUND)return 36;
            SetLastError(0);
            if(GetProcAddress(retained,ordinal?MAKEINTRESOURCEA(1):"dependency_check")||GetLastError()!=ERROR_PROC_NOT_FOUND)return 37;
            SetLastError(0);
            if(LoadLibraryW(target)||GetLastError()!=ERROR_DLL_INIT_FAILED)return 38;
        } else if(!pointer)return 39;
        // Never invoke the pending/rejected pointer: the host checks actual
        // TLS/DllMain lifecycle state even for accepted targets. A retained
        // failure must not poison lookups into an already initialized sibling.
        auto healthy=(int(*)())GetProcAddress(forward,"ForwardName");
        return healthy&&healthy()?42:40;
    }
    if(contains(GetCommandLineW(),L"reject-export-")) {
        auto forward=GetModuleHandleW(L"play-forward.dll");
        bool lazy=contains(GetCommandLineW(),L"reject-export-lazy");
        if(lazy) {
            // The forwarder maps its target now; failed attach must discard the
            // pointer computed before initializePending, without invoking it.
            if(GetModuleHandleW(L"play-reject.dll"))return 22;
        } else {
            SetLastError(0);
            if(LoadLibraryW(L"play-reject.dll")||GetLastError()!=ERROR_DLL_INIT_FAILED)return 23;
        }
        HMODULE owner=lazy?forward:GetModuleHandleW(L"play-reject.dll");
        if(!owner)return 24;
        LPCSTR symbol="dependency_check";
        if(contains(GetCommandLineW(),L"reject-export-ordinal"))symbol=MAKEINTRESOURCEA(1);
        if(contains(GetCommandLineW(),L"reject-export-source-forward"))symbol="RejectedForward";
        if(lazy||contains(GetCommandLineW(),L"reject-export-target-name")) {owner=forward;symbol="ForwardRejected";}
        if(contains(GetCommandLineW(),L"reject-export-target-ordinal")) {owner=forward;symbol="ForwardRejectedOrdinal";}
        SetLastError(0);
        if(GetProcAddress(owner,symbol)||GetLastError()!=ERROR_PROC_NOT_FOUND)return 25;
        // Failure retention is intentional diagnostic state, not an unload or
        // retry implementation. No rejected export is ever called by this test.
        if(!GetModuleHandleW(L"play-reject.dll"))return 26;
        SetLastError(0);
        if(LoadLibraryW(L"play-reject.dll")||GetLastError()!=ERROR_DLL_INIT_FAILED)return 27;
        return 42;
    }
    if(contains(GetCommandLineW(),L"reject-mapping")) {
        for(int i=0;i<2;++i) {
            SetLastError(0);
            if(LoadLibraryW(L"play-unresolved.dll") || GetLastError()!=ERROR_DLL_INIT_FAILED)return 20;
        }
        auto retained=GetModuleHandleW(L"play-unresolved.dll");
        if(retained && GetProcAddress(retained,"dependency_check"))return 21;
        return 42;
    }
#ifdef PLAY_STATIC_STEAM
    ForwardSteam(); // Static forwarded Steam imports must block before TLS.
#endif
    // No genuine Steam client exists: Steam calls fail as on Windows without
    // the files, and the guest carries on. Nothing answers in Steam's place.
    if(contains(GetCommandLineW(),L"steam-forward") &&
        GetProcAddress(GetModuleHandleW(L"play-forward.dll"),"ForwardSteam"))return 19;
    if(contains(GetCommandLineW(),L"steam-api") &&
        (LoadLibraryW(L"StEaM_ApI64") || GetLastError()!=ERROR_MOD_NOT_FOUND))return 40;
    // A steamclient64.dll in the game root is refused, never mapped.
    // PlayerPrefs-style registry use: the value written by "registry-write"
    // must be read back by a later, separate "registry-read" run.
    if(contains(GetCommandLineW(),L"registry-")) {
        HKEY key=nullptr;DWORD disposition=0;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Ved\\PlayTest",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&key,&disposition))return 50;
        DWORD value=0,size=sizeof value,type=0;
        if(contains(GetCommandLineW(),L"registry-write")) {
            value=0x5eed;
            if(RegSetValueExW(key,L"last_steam_id_h1",0,REG_DWORD,(const BYTE*)&value,sizeof value))return 51;
            value=0;
        } else if(disposition!=REG_OPENED_EXISTING_KEY)return 52;
        if(RegQueryValueExW(key,L"last_steam_id_h1",nullptr,&type,(BYTE*)&value,&size)||type!=REG_DWORD||value!=0x5eed)return 53;
        if(RegCloseKey(key))return 54;
        return 42;
    }
    if(contains(GetCommandLineW(),L"steam-client") &&
        (LoadLibraryW(L"STEAMCLIENT64.DLL") || GetLastError()!=ERROR_MOD_NOT_FOUND))return 41;
    // A root steam_api64.dll is an ordinary game DLL (owned stand-in here).
    if(contains(GetCommandLineW(),L"root-steam")) {
        auto api=LoadLibraryW(L"steam_api64");
        auto check=api?(int(*)())GetProcAddress(api,"dependency_check"):nullptr;
        if(!check || !check())return 43;
    }
    if(contains(GetCommandLineW(),L"wide-long")) {
        static wchar_t text[2050];for(int i=0;i<2047;++i)text[i]=L'\u00e7';
        text[2047]=0xd83d;text[2048]=0xde00;text[2049]=0;
        MessageBoxW(nullptr,text,L"Long Unicode",MB_OK);
    }
    if(contains(GetCommandLineW(),L"wide-dialog"))MessageBoxW(nullptr,L"Falha IL2CPP \u00e7\nprobe \"quoted\"",L"Erro \u00e7",MB_OK|MB_ICONERROR);
    if(contains(GetCommandLineW(),L"dialog"))MessageBoxA(nullptr,"Failed to initialize IL2CPP\nprobe \"quoted\"", "Fatal error",MB_OK|MB_ICONERROR);
#ifdef PLAY_STATIC_MISSING
    DeliberatelyMissingPlayImport(); // Must be rejected before TLS/DllMain/entry.
#endif
    if(contains(GetCommandLineW(),L"crash"))RaiseException(0xe0424242,EXCEPTION_NONCONTINUABLE,0,nullptr);
    if(contains(GetCommandLineW(),L"exit"))ExitProcess(37);
    if(contains(GetCommandLineW(),L"hang"))for(;;)Sleep(100);
    if(!ForwardName())return 11;
    HMODULE forward=GetModuleHandleW(L"play-forward.dll");
    auto byName=(int(*)())GetProcAddress(forward,"ForwardOrdinal");
    auto byOrdinal=(int(*)())GetProcAddress(forward,MAKEINTRESOURCEA(12));
    if(!byName||!byOrdinal||!byName()||!byOrdinal())return 12;
    if(GetProcAddress(forward,"Loop")||GetProcAddress(forward,"Absent"))return 13;
    if(GetModuleHandleW(L"play-lazy-target.dll"))return 14;
    auto lazy=(int(*)())GetProcAddress(forward,"ForwardDynamic");
    if(!lazy||!lazy()||!GetModuleHandleW(L"play-lazy-target.dll"))return 15;
    HMODULE dynamic=LoadLibraryW(L"play-dynamic");
    if(!dynamic)return 8;
    auto dynamicCheck=(int(*)())GetProcAddress(dynamic,"dependency_check");
    if(!dynamicCheck||!dynamicCheck())return 9;
    if(!FreeLibrary(dynamic))return 10;
    DWORD id=0,code=0;HANDLE h=CreateThread(nullptr,0,child,nullptr,0,&id);
    if(!h||WaitForSingleObject(h,10000)!=WAIT_OBJECT_0||!GetExitCodeThread(h,&code)||code!=42)return 4;
    CloseHandle(h);return tls_value==74&&tls_count==1?42:5;
}
#endif
}
