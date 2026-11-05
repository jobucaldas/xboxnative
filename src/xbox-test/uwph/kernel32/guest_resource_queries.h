#pragma once
// Included in play_runtime after its mapped-module registry. A null module
// means the guest EXE, never the launcher hosting that EXE.
static HMODULE resourceModule(HMODULE module) {
    return module ? module : reinterpret_cast<HMODULE>(state->exe->image.base);
}
static HRSRC WINAPI kernelFindResourceW(HMODULE module,LPCWSTR name,LPCWSTR type) {
    return FindResourceW(resourceModule(module),name,type);
}
static LPCWSTR resourceWide(LPCSTR value,std::wstring& storage) {
    if(IS_INTRESOURCE(value)) return reinterpret_cast<LPCWSTR>(value);
    int size=MultiByteToWideChar(CP_ACP,0,value,-1,nullptr,0);
    if(!size) return nullptr;
    storage.resize(size);
    if(!MultiByteToWideChar(CP_ACP,0,value,-1,storage.data(),size))return nullptr;
    return storage.c_str();
}
static HRSRC WINAPI kernelFindResourceA(HMODULE module,LPCSTR name,LPCSTR type) {
    std::wstring nameStorage,typeStorage;
    auto wideName=resourceWide(name,nameStorage);
    if(!wideName && !IS_INTRESOURCE(name))return nullptr;
    auto wideType=resourceWide(type,typeStorage);
    if(!wideType && !IS_INTRESOURCE(type))return nullptr;
    return kernelFindResourceW(module,wideName,wideType);
}
static BOOL WINAPI kernelEnumResourceNamesA(HMODULE module,LPCSTR type,ENUMRESNAMEPROCA callback,LONG_PTR data) {
    // Wine 10 exports the desktop API but its newer UWP API-set forwarder is
    // a stub. Resolve this exact typed OS entry, retaining native enumeration
    // and callback semantics for native and mapped PE resources alike.
    using Enum=BOOL (WINAPI*)(HMODULE,LPCSTR,ENUMRESNAMEPROCA,LONG_PTR);
    static Enum enumerate=[]() -> Enum {
        HMODULE kernel=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto p=kernel ? GetProcAddress(kernel,"EnumResourceNamesA") : nullptr;
        return reinterpret_cast<Enum>(reinterpret_cast<uintptr_t>(p));
    }();
    if(!enumerate) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return FALSE;}
    return enumerate(resourceModule(module),type,callback,data);
}
static HGLOBAL WINAPI kernelLoadResource(HMODULE module,HRSRC resource) {
    return LoadResource(resourceModule(module),resource);
}
static DWORD WINAPI kernelSizeofResource(HMODULE module,HRSRC resource) {
    return SizeofResource(resourceModule(module),resource);
}
