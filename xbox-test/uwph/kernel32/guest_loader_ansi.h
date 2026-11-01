#pragma once
// Included inside play_runtime after the wide module/load implementations.
// Wine kernelbase/loader.c uses file_name_AtoW for these four APIs. The
// logging UTF-8 codec must never decode guest ANSI/OEM module names.
static bool loaderNameA(LPCSTR name,std::wstring& result) {
    using Encoding=BOOL(WINAPI*)();
    auto encoding=reinterpret_cast<Encoding>(kernel_tail::native("AreFileApisANSI"));
    if(!encoding) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return false;}
    UINT codepage=encoding()?CP_ACP:CP_OEMCP;
    int length=MultiByteToWideChar(codepage,0,name,-1,nullptr,0);
    if(!length)return false;
    result.resize(length);
    return MultiByteToWideChar(codepage,0,name,-1,result.data(),length)!=0;
}
static HMODULE WINAPI moduleA(LPCSTR name) {
    if(!name)return moduleW(nullptr);
    std::wstring converted;
    if(!loaderNameA(name,converted)) {SetLastError(ERROR_MOD_NOT_FOUND);return nullptr;}
    return moduleW(converted.c_str());
}
static BOOL WINAPI moduleExA(DWORD flags,LPCSTR name,HMODULE* out) {
    if(!out) {SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    if(!name||(flags&GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS))
        return moduleExW(flags,reinterpret_cast<LPCWSTR>(name),out);
    std::wstring converted;
    if(!loaderNameA(name,converted)) {*out=nullptr;SetLastError(ERROR_MOD_NOT_FOUND);return FALSE;}
    return moduleExW(flags,converted.c_str(),out);
}
static HMODULE WINAPI loadExA(LPCSTR name,HANDLE file,DWORD flags) {
    if(!name)return loadExW(nullptr,file,flags);
    std::wstring converted;
    if(!loaderNameA(name,converted))return nullptr;
    return loadExW(converted.c_str(),file,flags);
}
static HMODULE WINAPI loadA(LPCSTR name) {return loadExA(name,nullptr,0);}
