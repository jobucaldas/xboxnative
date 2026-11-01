#pragma once
// Strict donor probe: never bind an import to a failure placeholder. Mapping
// success alone is insufficient: initialize with the runtime mapper, then check
// donor behavior before exposing its exports. Game entries remain uncalled.
namespace donor_pe {
static wxl::Log sink;
static unsigned missing;
static thread_local std::map<std::string,std::unique_ptr<wxl::Image>> images;
static uintptr_t resolve(const char* dll,const char* name,WORD ordinal);
inline uintptr_t exported(const char* dll,const char* name,WORD ordinal) {
    auto it=images.find(dll);if(it==images.end())return 0;
    auto& image=*it->second;
    if(auto p=mlp::asset_export(image,name,ordinal))return p;
    // The pinned version/shcore PEs contain kernel32/advapi32 forwarders.
    auto nt=(IMAGE_NT_HEADERS64*)(image.base+((IMAGE_DOS_HEADER*)image.base)->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!dir.Size||!image.valid(dir.VirtualAddress,dir.Size))return 0;
    auto e=(IMAGE_EXPORT_DIRECTORY*)(image.base+dir.VirtualAddress);
    auto names=(DWORD*)(image.base+e->AddressOfNames);
    auto ordinals=(WORD*)(image.base+e->AddressOfNameOrdinals);
    DWORD index=DWORD(ordinal)-e->Base;
    if(name) {index=e->NumberOfFunctions;for(DWORD i=0;i<e->NumberOfNames;i++) {
        auto n=image.str(names[i]);if(n&&!strcmp(n,name)){index=ordinals[i];break;}
    }}
    if(index>=e->NumberOfFunctions)return 0;
    DWORD rva=((DWORD*)(image.base+e->AddressOfFunctions))[index];
    if(rva<dir.VirtualAddress||rva-dir.VirtualAddress>=dir.Size)return 0;
    auto value=image.str(rva);if(!value)return 0;std::string text=value;
    auto dot=text.find_last_of('.');if(dot==std::string::npos)return 0;
    auto target=text.substr(0,dot)+".dll";auto symbol=text.substr(dot+1);
    return resolve(target.c_str(),symbol.c_str(),0);
}
static BOOL WINAPI disable_thread_calls(HMODULE) {
    // These donors have no static TLS or thread-attach work. They are owned by
    // this mapper, so the system loader cannot register their thread policy.
    return TRUE;
}
static std::map<std::string, HMODULE> modules;
static uintptr_t resolve(const char* dll, const char* name, WORD ordinal) {
    std::string key=dll; for(auto& c:key) if(c>='A'&&c<='Z') c+=32;
    uintptr_t p=0;
    if(key=="kernel32.dll"&&name&&!strcmp(name,"DisableThreadLibraryCalls")) p=(uintptr_t)&disable_thread_calls;
    else if(key=="user32.dll") p=user32_core::resolve(dll,name);
    else if(key=="kernel32.dll"&&name) p=kernel_tail::resolve(name);
    else {
        auto it=modules.find(key);
        if(it==modules.end()) {
            std::wstring wide(key.begin(),key.end());
            it=modules.emplace(key,LoadLibraryExW(wide.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)).first;
        }
        if(it->second) p=(uintptr_t)GetProcAddress(it->second,name?name:MAKEINTRESOURCEA(ordinal));
    }
    std::string text="DWPE IMPORT "+key+"!"+(name?name:std::string("#")+std::to_string(ordinal))+(p?" resolved":" MISSING");sink(text.c_str());
    if(!p) ++missing;
    return p;
}
static bool behavior(const std::string& label,wxl::Image& image,const std::wstring& path) {
    if(label=="hid.dll") {
        GUID guid{};auto call=(void(WINAPI*)(GUID*))image.symbol("HidD_GetHidGuid");
        if(!call)return false;call(&guid);
        const GUID expected={0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0,0x11,0x11,0,0,0x30}};
        return !memcmp(&guid,&expected,sizeof guid);
    }
    if(label=="setupapi.dll") {
        auto create=(HANDLE(WINAPI*)(const GUID*,HWND))image.symbol("SetupDiCreateDeviceInfoList");
        auto destroy=(BOOL(WINAPI*)(HANDLE))image.symbol("SetupDiDestroyDeviceInfoList");
        if(!create||!destroy)return false;HANDLE set=create(nullptr,nullptr);
        if(set==INVALID_HANDLE_VALUE)return false;return !!destroy(set);
    }
    if(label=="version.dll") {
        auto size=(DWORD(WINAPI*)(LPCWSTR,LPDWORD))image.symbol("GetFileVersionInfoSizeW");
        auto read=(BOOL(WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID))image.symbol("GetFileVersionInfoW");
        auto query=(BOOL(WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT))image.symbol("VerQueryValueW");
        if(!size||!read||!query)return false;DWORD ignored=0,n=size(path.c_str(),&ignored);
        if(!n||n>1024*1024)return false;std::vector<BYTE> bytes(n);
        if(!read(path.c_str(),0,n,bytes.data()))return false;
        DWORD* fixed=nullptr;UINT length=0;
        return query(bytes.data(),L"\\",(void**)&fixed,&length)&&length>=13*sizeof(DWORD)&&fixed&&fixed[0]==0xfeef04bd;
    }
    if(label=="shcore.dll") {
        auto parse=(LPWSTR*(WINAPI*)(LPCWSTR,int*))image.symbol("CommandLineToArgvW");
        if(!parse)return false;int count=0;auto args=parse(L"\"alpha beta\" tail",&count);
        bool ok=args&&count==2&&!wcscmp(args[0],L"alpha beta")&&!wcscmp(args[1],L"tail");
        if(args)LocalFree(args);return ok;
    }
    if(label=="avrt.dll") {
        auto call=(HANDLE(WINAPI*)(LPCWSTR,LPDWORD))image.symbol("AvSetMmThreadCharacteristicsW");
        if(!call)return false;DWORD index=0;SetLastError(0);
        return !call(nullptr,&index)&&GetLastError()==1550; // ERROR_INVALID_TASK_NAME
    }
    return false;
}
inline bool probe(wxl::Log log) {
    sink=log;images.clear();
    WCHAR file[2048];DWORD n=GetModuleFileNameW(nullptr,file,2048);
    if(!n||n>=2048)return false;
    std::wstring root=file;auto slash=root.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
    root.resize(slash+1);root+=L"donors\\";
    struct Import {const char* donor;const char* dll;const char* name;WORD ordinal;};
    static const Import imports[]={
#include "imports.h"
    };
    unsigned failures=0;
    for(auto name:{L"hid.dll",L"setupapi.dll",L"version.dll",L"shcore.dll",L"avrt.dll"}) {
        std::wstring path=root+name; std::string label(name,name+wcslen(name));
        missing=0;log(("DWPE BEGIN "+label+" strict imports before initialization").c_str());
        for(auto& i:imports) if(label==i.donor) resolve(i.dll,i.name,i.ordinal);
        unsigned preflightMissing=missing;
        std::unique_ptr<wxl::Image> image(new wxl::Image());
        bool ok=!preflightMissing&&wxl::load(*image,path.c_str(),log,{},resolve,false);
        bool tested=ok&&behavior(label,*image,path);
        if(tested) {
            images.emplace(label,std::move(image));
            if(label=="version.dll") {
                auto language=(DWORD(WINAPI*)(DWORD,LPWSTR,DWORD))exported("version.dll","VerLanguageNameW",0);
                WCHAR value[128]{};tested=language&&language(0x409,value,128)>0;
                if(!tested)images.erase(label);
            }
        }
        char text[256];snprintf(text,sizeof text,"DWPE RUN %s initialized=%u missing=%u behavior=%u",label.c_str(),ok,preflightMissing,tested);log(text);
        failures+=!tested;
    }
    return failures==0;
}
}
