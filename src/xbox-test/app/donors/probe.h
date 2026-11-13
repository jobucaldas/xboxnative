#pragma once
#include <mutex>
#include <d3dcompiler.h>
// d3d11shader.h declares the IID with DEFINE_GUID (extern declaration without
// INITGUID); the console build links no uuid.lib for it, so give the one IID
// its definition here under a local name (initguid.h would also define every
// other included GUID, duplicating any TU that did it first).
#include <initguid.h>
static const GUID IID_ID3D11ShaderReflectionLocal =
    {0x8d536ca1,0x0cca,0x4956,{0xa8,0x37,0x78,0x69,0x63,0x75,0x55,0x84}};
// Strict donor probe: never bind an import to a failure placeholder. Mapping
// success alone is insufficient: initialize with the runtime mapper, then check
// donor behavior before exposing its exports. Game entries remain uncalled.
namespace donor_pe {
static wxl::Log sink;
static unsigned missing;
// Executable guests may retain donor addresses after launcher supervision
// ends. Publish immutable successful donors across threads and retain their
// mappings until process exit, just like play_runtime::State.
static auto& images=*new std::map<std::string,std::unique_ptr<wxl::Image>>;
static std::mutex probeMutex;
static bool ready=false;
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
// A guest's by-name load (the D3D10 shim's d3dcompiler_47) is served the
// already-probed image instead of a second mapping: the behavior gate owns
// publication, so an unprobed or withheld donor answers nothing.
inline HMODULE module(const wchar_t* name) {
    if(!ready||!name)return nullptr;
    std::string key;for(auto p=name;*p;++p)key+=char(towlower(*p));
    auto it=images.find(key);return it==images.end()?nullptr:(HMODULE)it->second->base;
}
// GetProcAddress on a donor image (play_runtime::proc): the image is mapped
// outside play_runtime's own module list, so the handle is matched against the
// probed set and the export answered through the same published route.
inline uintptr_t from_handle(HMODULE handle,const char* name,WORD ordinal) {
    for(auto& image:images) if(image.second->base==(BYTE*)handle) return exported(image.first.c_str(),name,ordinal);
    return 0;
}
static BOOL WINAPI disable_thread_calls(HMODULE) {
    // These donors have no static TLS or thread-attach work. They are owned by
    // this mapper, so the system loader cannot register their thread policy.
    return TRUE;
}
static std::map<std::string, HMODULE> modules;
// comctl32 v6's InitCommonControls registers nothing (its classes register
// when comctl32 loads); dinput8 calls it only before its configuration dialog.
static void WINAPI init_common_controls() {}
// Wine donors delay-load user32/ole32/... through winecrt0's __delayLoadHelper2,
// which calls kernel32!ResolveDelayLoadedAPI. The native API would load host
// DLLs and pass guest record handles (HMONITOR, HWND) to host user32, so donors
// resolve delay imports through the same router as their static imports (user32
// goes to the record core). The IAT is left unpatched (this donor build puts it
// in read-only .idata); each call returns the same cached address.
struct DelayDescriptor { DWORD attributes,name,module,iat,names,bound,unload,timestamp; };
using DelaySystemHook=FARPROC(WINAPI*)(LPCSTR,LPCSTR);
static PVOID WINAPI resolve_delay_loaded(PVOID base,const DelayDescriptor* desc,PVOID,DelaySystemHook systemHook,
                                         IMAGE_THUNK_DATA64* thunk,ULONG) {
    static std::mutex mutex;static std::map<const void*,uintptr_t> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if(auto it=cache.find(thunk);it!=cache.end())return (PVOID)it->second;
    const wxl::Image* owner=nullptr;
    for(auto& entry:images) if(entry.second->base==(BYTE*)base) owner=entry.second.get();
    auto inside=[&](const void* p,size_t n){return owner&&(const BYTE*)p>=owner->base&&owner->valid(size_t((const BYTE*)p-owner->base),n);};
    if(!inside(desc,sizeof *desc)||!inside(thunk,8)||desc->attributes!=1||
       !owner->valid(desc->iat,8)||!owner->valid(desc->names,8))return nullptr;
    auto iat=(IMAGE_THUNK_DATA64*)(owner->base+desc->iat);
    if(thunk<iat||(thunk-iat)>=4096)return nullptr;
    size_t id=size_t(thunk-iat);
    if(!owner->valid(desc->names+id*8,8))return nullptr;
    uint64_t entry=((const uint64_t*)(owner->base+desc->names))[id];
    auto dll=owner->str(desc->name);if(!dll)return nullptr;
    const char* function=nullptr;WORD ordinal=0;
    if(entry&IMAGE_ORDINAL_FLAG64)ordinal=WORD(entry);
    else if(entry<owner->size&&owner->valid(size_t(entry),3))function=owner->str(size_t(entry)+2);
    if(!function&&!ordinal)return nullptr;
    uintptr_t p=resolve(dll,function,ordinal);
    // As Windows: an unresolvable delay import goes to the system failure hook.
    if(!p) return systemHook?(PVOID)systemHook(dll,function?function:MAKEINTRESOURCEA(ordinal)):nullptr;
    cache.emplace(thunk,p);return (PVOID)p;
}
static uintptr_t resolve(const char* dll, const char* name, WORD ordinal) {
    std::string key=dll; for(auto& c:key) if(c>='A'&&c<='Z') c+=32;
    uintptr_t p=0;
    if(key=="kernel32.dll"&&name&&!strcmp(name,"DisableThreadLibraryCalls")) p=(uintptr_t)&disable_thread_calls;
    else if(key=="kernel32.dll"&&name&&!strcmp(name,"ResolveDelayLoadedAPI")) p=(uintptr_t)&resolve_delay_loaded;
    else if(key=="user32.dll") p=user32_core::resolve(dll,name);
    else if(key=="kernel32.dll"&&name) p=kernel_tail::resolve(name);
    else if(key=="comctl32.dll"&&name&&!strcmp(name,"InitCommonControls")) p=(uintptr_t)&init_common_controls;
    else if(images.count(key)) p=exported(key.c_str(),name,ordinal);   // an earlier donor (dinput8 -> hid, setupapi)
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
static BOOL WINAPI count_device(const void*,void* count) {++*(unsigned*)count;return TRUE;}
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
    if(label=="dinput8.dll") {
        // Create DirectInput 8 and enumerate attached devices, as games do
        // first. Keyboard and mouse come from the record core; joysticks from
        // the hid/setupapi donors (none on a console: pads use XInput).
        using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,const GUID&,void**,IUnknown*);
        auto create=(Create)image.symbol("DirectInput8Create");if(!create)return false;
        static const GUID input8W={0xbf798031,0x483a,0x4da2,{0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0x00}};   // IID_IDirectInput8W
        void* input=nullptr;
        if(FAILED(create(GetModuleHandleW(nullptr),0x0800,input8W,&input,nullptr))||!input)return false;
        struct Table {void* query;void* addRef;ULONG(WINAPI*release)(void*);void* createDevice;
                      HRESULT(WINAPI*enumDevices)(void*,DWORD,BOOL(WINAPI*)(const void*,void*),void*,DWORD);};
        auto table=*(Table**)input;unsigned devices=0;
        HRESULT hr=table->enumDevices(input,0/*DI8DEVCLASS_ALL*/,count_device,&devices,1/*DIEDFL_ATTACHEDONLY*/);
        table->release(input);
        char text[96];snprintf(text,sizeof text,"DWPE dinput8 EnumDevices hr=0x%08lx attached=%u",(unsigned long)hr,devices);sink(text);
        return hr==S_OK;
    }
    if(label=="avrt.dll") {
        auto call=(HANDLE(WINAPI*)(LPCWSTR,LPDWORD))image.symbol("AvSetMmThreadCharacteristicsW");
        if(!call)return false;DWORD index=0;SetLastError(0);
        return !call(nullptr,&index)&&GetLastError()==1550; // ERROR_INVALID_TASK_NAME
    }
    if(label=="d3dcompiler_47.dll") {
        // Compile a real vertex shader and reflect it, as xgameFinal.exe does
        // at load: D3DCompile through the static vkd3d front end, D3DReflect
        // for the shader-description view. Disassemble is not exercised here.
        // Wine's d3dcompiler_blob vtable order matches the SDK ID3DBlob, so the
        // gate reads it through that interface exactly as a game does. GetDesc
        // writes a full D3D11_SHADER_DESC (Wine's is a partial stub but still
        // touches the whole struct): the buffer must be the real struct size.
        using Compile=decltype(&D3DCompile);
        using Reflect=decltype(&D3DReflect);
        auto compile=(Compile)image.symbol("D3DCompile");
        auto reflect=(Reflect)image.symbol("D3DReflect");
        if(!compile||!reflect)return false;
        static const char source[]=
            "float4 main(float4 pos:SV_Position):SV_Position{return pos;}\n";
        ID3DBlob* code=nullptr;ID3DBlob* errors=nullptr;
        HRESULT hr=compile(source,sizeof source-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,&errors);
        if(errors) {
            char text[160];snprintf(text,sizeof text,"DWPE d3dcompiler errors: %.130s",(char*)errors->GetBufferPointer());sink(text);
            errors->Release();
        }
        if(FAILED(hr)||!code)return false;
        bool ok=false;
        void* table=nullptr;
        HRESULT rhr=reflect(code->GetBufferPointer(),code->GetBufferSize(),IID_ID3D11ShaderReflectionLocal,&table);
        if(SUCCEEDED(rhr)&&table) {
            auto reflection=(ID3D11ShaderReflection*)table;
            D3D11_SHADER_DESC desc{};
            HRESULT dhr=reflection->GetDesc(&desc);
            ok=SUCCEEDED(dhr)&&desc.InputParameters>=1;
            if(!ok){char t2[128];snprintf(t2,sizeof t2,"DWPE d3dcompiler GetDesc hr=0x%08lx parameters=%u version=%#x",(unsigned long)dhr,desc.InputParameters,desc.Version);sink(t2);}
            reflection->Release();
        } else {
            char t2[96];snprintf(t2,sizeof t2,"DWPE d3dcompiler reflect hr=0x%08lx table=%p",(unsigned long)rhr,table);sink(t2);
        }
        code->Release();
        char text[96];snprintf(text,sizeof text,"DWPE d3dcompiler D3DCompile hr=0x%08lx reflect=%u",(unsigned long)hr,unsigned(ok));sink(text);
        return ok;
    }
    return false;
}
inline bool probe(wxl::Log log) {
    std::lock_guard<std::mutex> lock(probeMutex);
    if(ready) return true; // Never unload/reinitialize published executable code.
    sink=log;images.clear();
    WCHAR file[2048];DWORD n=GetModuleFileNameW(nullptr,file,2048);
    if(!n||n>=2048)return false;
    std::wstring root=file;auto slash=root.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
    root.resize(slash+1);root+=L"donors\\";
    struct Import {const char* donor;const char* dll;const char* name;WORD ordinal;};
    static const Import imports[]={
#include "donor_imports.generated.h"
    };
    unsigned failures=0;
    // dinput8 is last: its hid/setupapi imports resolve to those donors.
    for(auto name:{L"hid.dll",L"setupapi.dll",L"version.dll",L"shcore.dll",L"avrt.dll",L"d3dcompiler_47.dll",L"dinput8.dll"}) {
        std::wstring path=root+name; std::string label(name,name+wcslen(name));
        missing=0;log(("DWPE BEGIN "+label+" strict imports before initialization").c_str());
        for(auto& i:imports) if(label==i.donor) resolve(i.dll,i.name,i.ordinal);
        unsigned preflightMissing=missing;
        std::unique_ptr<wxl::Image> image(new wxl::Image());
        bool ok=!preflightMissing&&wxl::load(*image,path.c_str(),log,{},resolve,false,wxl::LoadConfig::inert_wine);
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
        // dinput8 serves only games that import it: when its gate fails it is
        // withheld (those games fail on the unresolved import, as before);
        // d3dcompiler_47 likewise: only xgameFinal.exe imports it statically and
        // the d3d10 shim loads it by name. The other donors stay a gate for
        // every launch.
        if((label=="dinput8.dll"||label=="d3dcompiler_47.dll")&&!tested) {
            log(("DWPE "+label+" withheld; games importing it stop at that import").c_str());continue;
        }
        failures+=!tested;
    }
    ready=failures==0;
    if(!ready) images.clear(); // Partial results are not executable routes.
    return ready;
}
}
