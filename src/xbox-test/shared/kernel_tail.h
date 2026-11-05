#pragma once
// Documented system loader only. Keep the desktop DLL name: kernelbase alone
// omits exports which are implemented by kernel32 on the Xbox OS.
#include <mutex>
#include <string>
namespace kernel_tail {
inline uintptr_t native(const char* name) {
    static HMODULE modules[]={
        LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32),
        LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32),
        LoadLibraryExW(L"api-ms-win-core-rtlsupport-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)};
    for(auto module:modules)if(module)if(auto p=GetProcAddress(module,name))return (uintptr_t)p;
    return 0;
}
inline LPVOID WINAPI alloc(LPVOID p,SIZE_T n,DWORD flags,DWORD protection) {
    if((protection&0xff)==PAGE_EXECUTE_READWRITE||(protection&0xff)==PAGE_EXECUTE_WRITECOPY){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    return VirtualAllocFromApp(p,n,flags,protection);
}
inline BOOL WINAPI protect(LPVOID p,SIZE_T n,DWORD protection,PDWORD old) {
    if((protection&0xff)==PAGE_EXECUTE_READWRITE||(protection&0xff)==PAGE_EXECUTE_WRITECOPY){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return VirtualProtectFromApp(p,n,protection,old);
}
struct ThreadEntry { DWORD size,usage,id,process; LONG priority,delta;DWORD flags; };
inline BOOL enumerate(HANDLE snapshot,ThreadEntry* entry,bool first) {
    if(entry->size<sizeof(ThreadEntry)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
    using Enumerate=BOOL (WINAPI*)(HANDLE,ThreadEntry*);
    auto call=(Enumerate)native(first?"Thread32First":"Thread32Next");
    if(!call){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
#ifdef _MSC_VER
    // Xbox's system Toolhelp implementation can raise for a closed snapshot.
    // Wine exposes a normal invalid-handle result. Do not change kernel policy.
    __try {return call(snapshot,entry);}
    __except(EXCEPTION_EXECUTE_HANDLER){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
#else
    return call(snapshot,entry);
#endif
}
inline BOOL WINAPI first(HANDLE h,ThreadEntry* entry){return enumerate(h,entry,true);}
inline BOOL WINAPI next(HANDLE h,ThreadEntry* entry){return enumerate(h,entry,false);}
// Xbox exports SetDllDirectoryW but rejects updates in AppContainer. Keep
// guest search state and apply it in the guest LoadLibrary wrappers instead.
struct Search {std::mutex mutex;std::wstring directory;bool configured=false;};
inline Search& search(){static Search value;return value;}
inline BOOL WINAPI set_directory(LPCWSTR path) {
    auto& value=search();std::lock_guard<std::mutex> lock(value.mutex);
    value.configured=path!=nullptr;value.directory=path?path:L"";return TRUE;
}
inline DWORD WINAPI get_directory(DWORD capacity,LPWSTR out) {
    auto& value=search();std::lock_guard<std::mutex> lock(value.mutex);
    DWORD n=DWORD(value.directory.size());
    if(n>=capacity){if(capacity&&out)*out=0;return n+1;}
    if(out)memcpy(out,value.directory.c_str(),(n+1)*sizeof(wchar_t));return n;
}
inline HMODULE WINAPI load_ex_w(LPCWSTR name,HANDLE file,DWORD flags) {
    using Load=HMODULE (WINAPI*)(LPCWSTR,HANDLE,DWORD);auto load=(Load)native("LoadLibraryExW");
    if(!load){SetLastError(ERROR_PROC_NOT_FOUND);return nullptr;}
    if(!name)return load(name,file,flags);
    std::wstring directory;bool configured;
    {auto& value=search();std::lock_guard<std::mutex> lock(value.mutex);directory=value.directory;configured=value.configured;}
    if(!flags&&configured&&!wcschr(name,L'\\')&&!wcschr(name,L'/')&&!wcschr(name,L':')) {
        // Already loaded modules and API-set contracts retain OS identity.
        using Module=HMODULE (WINAPI*)(LPCWSTR);auto module=(Module)native("GetModuleHandleW");
        if((module&&module(name))||!wcsncmp(name,L"api-",4)||!wcsncmp(name,L"ext-",4))return load(name,file,flags);
        // The packaged application's directory precedes SetDllDirectory.
        wchar_t path[32768]{};using Path=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD);
        auto getPath=(Path)native("GetModuleFileNameW");DWORD n=getPath?getPath(nullptr,path,32768):0;
        if(n&&n<32768) {
            std::wstring app=path;auto slash=app.find_last_of(L"\\/");
            if(slash!=std::wstring::npos){app.resize(slash+1);app+=name;if(auto result=load(app.c_str(),file,flags))return result;}
        }
        if(!directory.empty()) {auto candidate=directory+L"\\"+name;if(auto result=load(candidate.c_str(),file,flags))return result;}
        // Both empty and nonempty configured directories exclude current-dir
        // search. The documented loader still enforces package/signing policy.
        return load(name,file,LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    }
    return load(name,file,flags);
}
inline HMODULE WINAPI load_w(LPCWSTR name){return load_ex_w(name,nullptr,0);}
inline std::wstring wide(LPCSTR name) {
    if(!name)return {};int n=MultiByteToWideChar(CP_ACP,0,name,-1,nullptr,0);if(!n)return {};
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_ACP,0,name,-1,&out[0],n);return out;
}
inline HMODULE WINAPI load_ex_a(LPCSTR name,HANDLE file,DWORD flags){auto value=wide(name);return load_ex_w(name?value.c_str():nullptr,file,flags);}
inline HMODULE WINAPI load_a(LPCSTR name){return load_ex_a(name,nullptr,0);}
inline bool compare(DWORD actual,DWORD wanted,DWORD condition) {
    switch(condition){case VER_EQUAL:return actual==wanted;case VER_GREATER:return actual>wanted;case VER_GREATER_EQUAL:return actual>=wanted;case VER_LESS:return actual<wanted;case VER_LESS_EQUAL:return actual<=wanted;default:return false;}
}
inline BOOL WINAPI version(LPOSVERSIONINFOEXW wanted,DWORD types,DWORDLONG conditions) {
    using Query=BOOL (WINAPI*)(LPOSVERSIONINFOW);auto query=(Query)native("GetVersionExW");
    if(!query){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
    OSVERSIONINFOEXW actual{};actual.dwOSVersionInfoSize=sizeof(actual);
    if(!query((LPOSVERSIONINFOW)&actual))return FALSE;
    if(!types||!conditions){SetLastError(ERROR_BAD_ARGUMENTS);return FALSE;}
    const DWORD actuals[]={actual.dwMinorVersion,actual.dwMajorVersion,actual.dwBuildNumber,actual.dwPlatformId,actual.wServicePackMinor,actual.wServicePackMajor,actual.wSuiteMask,actual.wProductType};
    const DWORD wants[]={wanted->dwMinorVersion,wanted->dwMajorVersion,wanted->dwBuildNumber,wanted->dwPlatformId,wanted->wServicePackMinor,wanted->wServicePackMajor,wanted->wSuiteMask,wanted->wProductType};
    auto mismatch=[](){SetLastError(ERROR_OLD_WIN_VERSION);return FALSE;};
    // Match Wine's validation order: a product mismatch precedes even an
    // invalid suite condition. Error ordering is externally observable.
    for(unsigned i:{7u,6u,3u,2u})if(types&(1u<<i)) {
        DWORD c=DWORD(conditions>>(3*i))&7;
        if(i==6) {
            if(c!=VER_AND&&c!=VER_OR){SetLastError(ERROR_BAD_ARGUMENTS);return FALSE;}
            if(c==VER_AND?(actuals[i]&wants[i])!=wants[i]:wants[i]&&!(actuals[i]&wants[i]))return mismatch();
        } else if(!compare(actuals[i],wants[i],c))return mismatch();
    }
    // Major/minor/service-pack fields form one ordered comparison. A higher
    // field's direction constrains later conditions; zero locks the direction.
    DWORD relation=0;bool matched=true;
    for(unsigned i:{1u,0u,5u,4u})if(types&(1u<<i)) {
        DWORD c=DWORD(conditions>>(3*i))&7,effective;
        bool refine=(relation==VER_EQUAL&&c>=VER_EQUAL&&c<=VER_LESS_EQUAL)||
                    ((relation==VER_GREATER||relation==VER_GREATER_EQUAL)&&c>=VER_EQUAL&&c<=VER_GREATER_EQUAL)||
                    ((relation==VER_LESS||relation==VER_LESS_EQUAL)&&(c==VER_EQUAL||c==VER_LESS||c==VER_LESS_EQUAL));
        if(!relation||relation==VER_EQUAL&&refine)relation=c;
        effective=refine?c:relation&15;
        if(!refine&&!c)relation|=16;
        matched=compare(actuals[i],wants[i],effective);
        if(actuals[i]!=wants[i]||effective<VER_EQUAL||effective>VER_LESS_EQUAL)break;
    }
    if(!matched)SetLastError(ERROR_OLD_WIN_VERSION);return matched;
}
inline uintptr_t resolve(const char* name) {
    if(!strcmp(name,"VirtualAlloc"))return (uintptr_t)&alloc;
    if(!strcmp(name,"VirtualProtect"))return (uintptr_t)&protect;
    if(!strcmp(name,"Thread32First")&&native(name))return (uintptr_t)&first;
    if(!strcmp(name,"Thread32Next")&&native(name))return (uintptr_t)&next;
    if(!strcmp(name,"SetDllDirectoryW"))return (uintptr_t)&set_directory;
    if(!strcmp(name,"GetDllDirectoryW"))return (uintptr_t)&get_directory;
    if(!strcmp(name,"LoadLibraryExW"))return (uintptr_t)&load_ex_w;
    if(!strcmp(name,"LoadLibraryW"))return (uintptr_t)&load_w;
    if(!strcmp(name,"LoadLibraryExA"))return (uintptr_t)&load_ex_a;
    if(!strcmp(name,"LoadLibraryA"))return (uintptr_t)&load_a;
    if(!strcmp(name,"VerifyVersionInfoW"))return (uintptr_t)&version;
    return native(name);
}
}
