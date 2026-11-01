#pragma once
// Included inside play_runtime; mapped images are absent from the OS registry.
static DWORD copyPath(const std::wstring& path,LPWSTR out,DWORD capacity) {
    if(!capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    size_t n=std::min(path.size(),size_t(capacity-1)); memcpy(out,path.data(),n*2); out[n]=0;
    if(path.size()>=capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return capacity; } SetLastError(ERROR_SUCCESS); return DWORD(n);
}
static DWORD WINAPI filenameW(HMODULE handle,LPWSTR out,DWORD size) {
    auto m=handle?find(handle):state->exe;
    if(m) return copyPath(m->path,out,size);
    using Fn=DWORD(WINAPI*)(HMODULE,LPWSTR,DWORD); auto fn=(Fn)kernel_tail::native("GetModuleFileNameW"); if(!fn) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return 0;}
    return fn(handle,out,size);
}
static DWORD WINAPI filenameA(HMODULE handle,LPSTR out,DWORD size) {
    auto m=handle?find(handle):state->exe;
    if(!m) {
        using Fn=DWORD(WINAPI*)(HMODULE,LPSTR,DWORD);
        auto fn=(Fn)kernel_tail::native("GetModuleFileNameA");
        if(!fn) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return 0;}
    return fn(handle,out,size);
    }
    if(!size) {SetLastError(ERROR_INSUFFICIENT_BUFFER);return 0;}
    // GetModuleFileNameA follows the process file API encoding. The log's
    // UTF-8 codec is unrelated to this guest ABI.
    using Encoding=BOOL(WINAPI*)();
    auto encoding=(Encoding)kernel_tail::native("AreFileApisANSI");
    if(!encoding) {SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return 0;}
    UINT codepage=encoding()?CP_ACP:CP_OEMCP;
    int bytes=WideCharToMultiByte(codepage,0,m->path.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(!bytes)return 0;
    std::string encoded(bytes,0);
    if(!WideCharToMultiByte(codepage,0,m->path.c_str(),-1,encoded.data(),bytes,nullptr,nullptr))return 0;
    DWORD length=static_cast<DWORD>(encoded.size()-1);
    DWORD take=std::min(size-1,length);
    memcpy(out,encoded.data(),take);out[take]=0;
    SetLastError(length>=size?ERROR_INSUFFICIENT_BUFFER:ERROR_SUCCESS);
    return length>=size?size:length;
}
