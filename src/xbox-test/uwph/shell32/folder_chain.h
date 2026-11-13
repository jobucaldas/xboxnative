// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The guest's folder fallback chain, shared by the shell32 folder route
// (special_folder.h) and the Steam cloud roots (app/launch.h cloud_roots). Order:
// the system known folder first (SHGetKnownFolderPath, the call the guest already
// uses), then the environment variable or package-folder steps of the chain, in the
// order cloud_roots has always tried them. The first answer wins; labels name the
// step for the cloud log. Nothing here is a LocalState redirect.
// The providers are parameters, so the off-device test injects failing ones; the app
// supplies package folders through packageHook (winrt lives only in launch.h).
#include <windows.h>
#include <objbase.h>
#include <functional>
#include <string>
#include <vector>
namespace folder_chain {
enum class Package { Roaming, LocalCache, Local };
// Returns false when the package folder cannot be read (an exception in the app).
using PackageHook=bool(*)(Package,std::wstring&);
inline PackageHook& packageHook(){ static PackageHook hook=nullptr; return hook; }
struct Providers {
    // SHGetKnownFolderPath(id, flags): true with a non-null path on success.
    std::function<bool(const GUID&,DWORD,std::wstring&)> known;
    // Environment variable value; empty when unset or not shorter than MAX_PATH.
    std::function<std::wstring(const wchar_t*)> env;
    // Package folder base path (see packageHook).
    std::function<bool(Package,std::wstring&)> package;
};
struct Step {
    enum Kind { Env, Pkg } kind;
    const wchar_t* name;    // Env: variable name
    Package package;        // Pkg: which package folder
    const wchar_t* suffix;  // appended to the base path (may be empty)
    const char* what;       // label for the log
};
using Chain=std::vector<Step>;
struct Found { std::wstring path; std::string what; bool known=false; };
inline const Chain& roamingChain(){
    static const Chain chain{
        {Step::Env,L"APPDATA",Package::Roaming,L"","the APPDATA environment variable"},
        {Step::Pkg,nullptr,Package::Roaming,L"","RoamingFolder"},
        {Step::Pkg,nullptr,Package::LocalCache,L"\\Roaming","LocalCache\\Roaming"},
        {Step::Pkg,nullptr,Package::Local,L"\\..\\RoamingState","LocalFolder\\..\\RoamingState"},
    };
    return chain;
}
inline const Chain& localChain(){
    static const Chain chain{{Step::Pkg,nullptr,Package::Local,L"","LocalFolder"}};
    return chain;
}
inline const Chain& documentsChain(){
    static const Chain chain{{Step::Env,L"USERPROFILE",Package::Roaming,L"\\Documents","USERPROFILE\\Documents"}};
    return chain;
}
inline const Chain& desktopChain(){
    static const Chain chain{{Step::Env,L"USERPROFILE",Package::Roaming,L"\\Desktop","USERPROFILE\\Desktop"}};
    return chain;
}
inline const Chain& programDataChain(){
    static const Chain chain{{Step::Env,L"ALLUSERSPROFILE",Package::Roaming,L"","the ALLUSERSPROFILE environment variable"}};
    return chain;
}
// Known folder first, then the chain. A known answer is returned even when empty, as
// cloud_roots always stored it; a fallback answer must be non-empty.
inline Found resolve(const Providers& p,const GUID& id,DWORD flags,const Chain& chain){
    std::wstring path;
    if(p.known&&p.known(id,flags,path)) return {path,"",true};
    for(const Step& step:chain){
        std::wstring base;
        if(step.kind==Step::Env){
            base=p.env?p.env(step.name):std::wstring();
            if(base.empty()) continue;
        } else if(!p.package||!p.package(step.package,base)) continue;
        std::wstring candidate=base+step.suffix;
        if(candidate.empty()) continue;
        return {candidate,step.what,false};
    }
    return {};
}
// The production providers: the system known folder (given), the process environment
// and the app's package hook.
inline Providers systemProviders(HRESULT(WINAPI* known)(const GUID&,DWORD,HANDLE,PWSTR*)){
    Providers p;
    p.known=[known](const GUID& id,DWORD flags,std::wstring& out){
        PWSTR text=nullptr;
        bool ok=known&&SUCCEEDED(known(id,flags,nullptr,&text))&&text;
        if(ok) out=text;
        if(text) CoTaskMemFree(text);
        return ok;
    };
    p.env=[](const wchar_t* name){
        wchar_t buf[MAX_PATH]{};
        DWORD n=GetEnvironmentVariableW(name,buf,MAX_PATH);
        return n&&n<MAX_PATH?std::wstring(buf):std::wstring();
    };
    p.package=[](Package kind,std::wstring& out){
        PackageHook hook=packageHook();
        return hook&&hook(kind,out);
    };
    return p;
}
}
