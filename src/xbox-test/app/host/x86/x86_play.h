// SPDX-License-Identifier: GPL-3.0-or-later
// Run a 32-bit (PE32) executable in the in-process x86 interpreter, with the
// guest OS surface in x86_guest.h serving its imports.
//
// This is the launcher's PE32 entry point. It is deliberately one function with
// a structured result: a guest that faults reports the fault kind, the guest
// EIP and the instruction bytes, and the imports it could not bind are listed by
// name, so a milestone subset is never mistaken for general Win32 support.
// Nothing here touches the host's address space with a guest pointer: the guest
// address space is the interpreter's own page table (x86_mem.h).
#pragma once
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <winsock2.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "x86_guest.h"
#include "x86_file_guard.h"
#if defined(_WIN32)
#include "../../../uwph/kernel32/route.h"
#endif
namespace x86 {
inline bool play_capture_nls_output(Host::NlsResponse& response, const void* output, size_t output_bytes,
                                    size_t required_bytes, bool output_null, uint32_t capacity) {
    if (!response.result || output_null || !capacity) return true;
    if (required_bytes > output_bytes || (required_bytes && !output)) return false;
    if (required_bytes) {
        const auto* bytes = static_cast<const uint8_t*>(output);
        response.output.assign(bytes, bytes + required_bytes);
    }
    return true;
}
#if defined(_WIN32)
inline void play_locale_call(Host::LocaleCall& q) {
    SYSTEMTIME st{}; if(q.has_system_time) memcpy(&st,q.system_time,sizeof(st));
    std::wstring fmt; if(q.has_format) fmt.assign(q.format.begin(),q.format.end());
    std::vector<wchar_t> out(q.buffer_chars>0?size_t(q.buffer_chars):(q.has_output_buffer?1u:0u));
    wchar_t locale_name[LOCALE_NAME_MAX_LENGTH]{};
    SetLastError(ERROR_SUCCESS);
    if(!LCIDToLocaleName(q.locale,locale_name,LOCALE_NAME_MAX_LENGTH,0)){q.error=GetLastError();q.succeeded=false;q.result=0;return;}
    SetLastError(ERROR_SUCCESS);
    wchar_t* output=q.has_output_buffer&&!out.empty()?out.data():nullptr;
    int n=q.date?GetDateFormatEx(locale_name,q.flags,q.has_system_time?&st:nullptr,q.has_format?fmt.c_str():nullptr,output,q.buffer_chars,nullptr)
                :GetTimeFormatEx(locale_name,q.flags,q.has_system_time?&st:nullptr,q.has_format?fmt.c_str():nullptr,output,q.buffer_chars);
    q.result=n; q.succeeded=n!=0; q.error=q.succeeded?0:GetLastError();
    if(q.succeeded&&!out.empty()){q.data.resize(size_t(n)*sizeof(wchar_t));memcpy(q.data.data(),out.data(),q.data.size());}
}
inline void play_socket_call(Host::SocketCall& q) {
    using StartupFn=int (WSAAPI*)(WORD,LPWSADATA);using CleanupFn=int (WSAAPI*)();using LastErrorFn=int (WSAAPI*)();
    static HMODULE module=LoadLibraryExW(L"ws2_32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto startup=module?reinterpret_cast<StartupFn>(GetProcAddress(module,"WSAStartup")):nullptr;
    auto cleanup=module?reinterpret_cast<CleanupFn>(GetProcAddress(module,"WSACleanup")):nullptr;
    auto last=module?reinterpret_cast<LastErrorFn>(GetProcAddress(module,"WSAGetLastError")):nullptr;
    if(q.kind==Host::SocketCall::Kind::Startup){if(!startup){q.result=10091;return;}WSADATA native{};q.result=startup(q.version,&native);if(q.result)return;
        q.data.assign(400,0);memcpy(q.data.data(),&native.wVersion,2);memcpy(q.data.data()+2,&native.wHighVersion,2);
        memcpy(q.data.data()+4,native.szDescription,std::min<size_t>(257,sizeof(native.szDescription)));memcpy(q.data.data()+261,native.szSystemStatus,std::min<size_t>(129,sizeof(native.szSystemStatus)));
        memcpy(q.data.data()+390,&native.iMaxSockets,2);memcpy(q.data.data()+392,&native.iMaxUdpDg,2);return;}
    if(q.kind==Host::SocketCall::Kind::Cleanup){q.result=cleanup?cleanup():-1;return;}q.result=last?last():10093;
}
inline void play_file_call(Host::FileCall& q) {
    auto failed=[&](){q.error=GetLastError();};
    auto native=[&](const char* name)->uintptr_t { static HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);return module?reinterpret_cast<uintptr_t>(GetProcAddress(module,name)):0; };
    switch(q.kind) {
    case Host::FileCall::Kind::CreateA: { HANDLE h=kernel32_core::create_file_a(q.path_a.c_str(),q.args[0],q.args[1],nullptr,q.args[2],q.args[3],(HANDLE)(uintptr_t)q.template_handle);q.value=(uintptr_t)h;q.succeeded=h!=INVALID_HANDLE_VALUE;failed();break; }
    case Host::FileCall::Kind::CreateW: { std::wstring p(q.path_w.begin(),q.path_w.end());HANDLE h=kernel32_core::create_file_w(p.c_str(),q.args[0],q.args[1],nullptr,q.args[2],q.args[3],(HANDLE)(uintptr_t)q.template_handle);q.value=(uintptr_t)h;q.succeeded=h!=INVALID_HANDLE_VALUE;failed();break; }
    case Host::FileCall::Kind::Close:q.succeeded=kernel32_core::close_handle((HANDLE)(uintptr_t)q.handle)!=FALSE;if(!q.succeeded)failed();break;
    case Host::FileCall::Kind::Read:{q.data.resize(q.args[0]);DWORD n=0;q.succeeded=kernel32_core::read_file((HANDLE)(uintptr_t)q.handle,q.data.data(),q.args[0],&n,nullptr)!=FALSE;q.args[1]=n;q.data.resize(n);if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::Write:{using Fn=BOOL(WINAPI*)(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","WriteFile");DWORD n=0;q.succeeded=fn&&fn((HANDLE)(uintptr_t)q.handle,q.data.data(),DWORD(q.data.size()),&n,nullptr)!=FALSE;q.args[0]=n;if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::Size:{DWORD high=0,low=kernel32_core::file_size((HANDLE)(uintptr_t)q.handle,q.args[0]?&high:nullptr);q.value=low;q.args[1]=high;q.error=GetLastError();q.succeeded=low!=INVALID_FILE_SIZE||q.error==ERROR_SUCCESS;break;}
    case Host::FileCall::Kind::Time:{FILETIME t[3]{};using Fn=BOOL(WINAPI*)(HANDLE,LPFILETIME,LPFILETIME,LPFILETIME);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","GetFileTime");q.succeeded=fn&&(fn((HANDLE)(uintptr_t)q.handle,q.args[0]?&t[0]:nullptr,q.args[1]?&t[1]:nullptr,q.args[2]?&t[2]:nullptr)!=FALSE);q.data.resize(sizeof(t));memcpy(q.data.data(),t,sizeof(t));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::AttributesA:{
        if(q.operation==Host::FileCall::Operation::CreateDirectoryA){SetLastError(ERROR_SUCCESS);q.succeeded=CreateDirectoryA(q.path_a.c_str(),nullptr)!=FALSE;if(!q.succeeded)failed();}
        else if(q.operation==Host::FileCall::Operation::DeleteA){SetLastError(ERROR_SUCCESS);q.succeeded=DeleteFileA(q.path_a.c_str())!=FALSE;if(!q.succeeded)failed();}
        else if(q.operation==Host::FileCall::Operation::DeleteW){std::wstring p(q.path_w.begin(),q.path_w.end());SetLastError(ERROR_SUCCESS);q.succeeded=DeleteFileW(p.c_str())!=FALSE;if(!q.succeeded)failed();}
        else if(q.args[1]){using Fn=BOOL(WINAPI*)(LPCSTR,GET_FILEEX_INFO_LEVELS,LPVOID);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","GetFileAttributesExA");WIN32_FILE_ATTRIBUTE_DATA d{};SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn(q.path_a.c_str(),GET_FILEEX_INFO_LEVELS(q.args[0]),&d);q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();}
        else {using Fn=DWORD(WINAPI*)(LPCSTR);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","GetFileAttributesA");SetLastError(ERROR_SUCCESS);q.value=fn?fn(q.path_a.c_str()):INVALID_FILE_ATTRIBUTES;q.succeeded=q.value!=INVALID_FILE_ATTRIBUTES;if(!q.succeeded)failed();}break;}
    case Host::FileCall::Kind::Seek:{using Fn=DWORD(WINAPI*)(HANDLE,LONG,PLONG,DWORD);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","SetFilePointer");LONG high=LONG(q.args[1]);SetLastError(ERROR_SUCCESS);DWORD low=fn?fn((HANDLE)(uintptr_t)q.handle,LONG(q.args[0]),q.args[3]?&high:nullptr,q.args[2]):INVALID_SET_FILE_POINTER;q.value=low;q.args[1]=DWORD(high);q.error=GetLastError();q.succeeded=low!=INVALID_SET_FILE_POINTER||q.error==ERROR_SUCCESS;break;}
    case Host::FileCall::Kind::SeekEx:{using Fn=BOOL(WINAPI*)(HANDLE,LARGE_INTEGER,PLARGE_INTEGER,DWORD);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","SetFilePointerEx");LARGE_INTEGER distance{},position{};distance.QuadPart=q.distance;SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn((HANDLE)(uintptr_t)q.handle,distance,q.args[0]?&position:nullptr,q.method)!=FALSE;q.error=fn?GetLastError():ERROR_PROC_NOT_FOUND;if(q.succeeded)q.position=position.QuadPart;break;}
    case Host::FileCall::Kind::End:q.succeeded=SetEndOfFile((HANDLE)(uintptr_t)q.handle)!=FALSE;if(!q.succeeded)failed();break;
    case Host::FileCall::Kind::TimeConvert:{
        if(q.operation==Host::FileCall::Operation::SystemToFileTime){SYSTEMTIME in{};FILETIME out{};memcpy(&in,q.data.data(),sizeof(in));SetLastError(ERROR_SUCCESS);q.succeeded=SystemTimeToFileTime(&in,&out)!=FALSE;if(q.succeeded){q.data.resize(sizeof(out));memcpy(q.data.data(),&out,sizeof(out));}}
        else {FILETIME in{};SYSTEMTIME out{};memcpy(&in,q.data.data(),8);q.succeeded=FileTimeToSystemTime(&in,&out)!=FALSE;q.data.resize(sizeof(out));memcpy(q.data.data(),&out,sizeof(out));}
        if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::TimeLocal:{
        if(q.operation==Host::FileCall::Operation::SystemToTzLocal){SYSTEMTIME in{},out{};TIME_ZONE_INFORMATION tz{};memcpy(&in,q.data.data(),sizeof(in));if(q.args[0])memcpy(&tz,q.data.data()+sizeof(in),sizeof(tz));SetLastError(ERROR_SUCCESS);q.succeeded=SystemTimeToTzSpecificLocalTime(q.args[0]?&tz:nullptr,&in,&out)!=FALSE;if(q.succeeded){q.data.resize(sizeof(out));memcpy(q.data.data(),&out,sizeof(out));}}
        else {FILETIME in{},out{};memcpy(&in,q.data.data(),8);q.succeeded=FileTimeToLocalFileTime(&in,&out)!=FALSE;q.data.resize(sizeof(out));memcpy(q.data.data(),&out,8);}
        if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::CreateDirectoryW:{using Fn=BOOL(WINAPI*)(LPCWSTR,LPSECURITY_ATTRIBUTES);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","CreateDirectoryW");std::wstring p(q.path_w.begin(),q.path_w.end());SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn(p.c_str(),nullptr);if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::RemoveDirectoryW:{using Fn=BOOL(WINAPI*)(LPCWSTR);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","RemoveDirectoryW");std::wstring p(q.path_w.begin(),q.path_w.end());SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn(p.c_str());if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::RemoveDirectoryA:{using Fn=BOOL(WINAPI*)(LPCSTR);auto fn=(Fn)native("RemoveDirectoryA");SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn(q.path_a.c_str());if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::GetCurrentDirectoryA:{using Fn=DWORD(WINAPI*)(DWORD,LPSTR);auto fn=(Fn)native("GetCurrentDirectoryA");q.data.resize(size_t(q.args[0]));q.value=fn?fn(q.args[0],q.data.empty()?nullptr:(LPSTR)q.data.data()):0;q.succeeded=q.value!=0;if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::GetCurrentDirectoryW:{using Fn=DWORD(WINAPI*)(DWORD,LPWSTR);auto fn=(Fn)native("GetCurrentDirectoryW");q.data.resize(size_t(q.args[0])*2);q.value=fn?fn(q.args[0],q.data.empty()?nullptr:(LPWSTR)q.data.data()):0;q.succeeded=q.value!=0;if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::SetCurrentDirectoryA:{using Fn=BOOL(WINAPI*)(LPCSTR);auto fn=(Fn)native("SetCurrentDirectoryA");SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn(q.path_a.c_str());if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::SetCurrentDirectoryW:{using Fn=BOOL(WINAPI*)(LPCWSTR);auto fn=(Fn)native("SetCurrentDirectoryW");std::wstring p(q.path_w.begin(),q.path_w.end());SetLastError(ERROR_SUCCESS);q.succeeded=fn&&fn(p.c_str());if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindFirstA:{using Fn=HANDLE(WINAPI*)(LPCSTR,LPWIN32_FIND_DATAA);SetLastError(ERROR_SUCCESS);auto fn=(Fn)native("FindFirstFileA");WIN32_FIND_DATAA d{};HANDLE h=fn?fn(q.path_a.c_str(),&d):INVALID_HANDLE_VALUE;q.value=(uintptr_t)h;q.succeeded=h!=INVALID_HANDLE_VALUE;q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindFirstW:{using Fn=HANDLE(WINAPI*)(LPCWSTR,LPWIN32_FIND_DATAW);SetLastError(ERROR_SUCCESS);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","FindFirstFileW");WIN32_FIND_DATAW d{};std::wstring p(q.path_w.begin(),q.path_w.end());HANDLE h=fn?fn(p.c_str(),&d):INVALID_HANDLE_VALUE;q.value=(uintptr_t)h;q.succeeded=h!=INVALID_HANDLE_VALUE;q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindFirstExA:{using Fn=HANDLE(WINAPI*)(LPCSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);SetLastError(ERROR_SUCCESS);auto fn=(Fn)native("FindFirstFileExA");WIN32_FIND_DATAA d{};HANDLE h=fn?fn(q.path_a.c_str(),FINDEX_INFO_LEVELS(q.args[0]),&d,FINDEX_SEARCH_OPS(q.args[1]),nullptr,q.args[2]):INVALID_HANDLE_VALUE;q.value=(uintptr_t)h;q.succeeded=h!=INVALID_HANDLE_VALUE;q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindFirstExW:{using Fn=HANDLE(WINAPI*)(LPCWSTR,FINDEX_INFO_LEVELS,LPVOID,FINDEX_SEARCH_OPS,LPVOID,DWORD);SetLastError(ERROR_SUCCESS);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","FindFirstFileExW");WIN32_FIND_DATAW d{};std::wstring p(q.path_w.begin(),q.path_w.end());HANDLE h=fn?fn(p.c_str(),FINDEX_INFO_LEVELS(q.args[0]),&d,FINDEX_SEARCH_OPS(q.args[1]),nullptr,q.args[2]):INVALID_HANDLE_VALUE;q.value=(uintptr_t)h;q.succeeded=h!=INVALID_HANDLE_VALUE;q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindNextA:{using Fn=BOOL(WINAPI*)(HANDLE,LPWIN32_FIND_DATAA);SetLastError(ERROR_SUCCESS);auto fn=(Fn)native("FindNextFileA");WIN32_FIND_DATAA d{};q.succeeded=fn&&fn((HANDLE)(uintptr_t)q.handle,&d);q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindNextW:{using Fn=BOOL(WINAPI*)(HANDLE,LPWIN32_FIND_DATAW);SetLastError(ERROR_SUCCESS);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","FindNextFileW");WIN32_FIND_DATAW d{};q.succeeded=fn&&fn((HANDLE)(uintptr_t)q.handle,&d);q.data.resize(sizeof(d));memcpy(q.data.data(),&d,sizeof(d));if(!q.succeeded)failed();break;}
    case Host::FileCall::Kind::FindClose:{using Fn=BOOL(WINAPI*)(HANDLE);auto fn=(Fn)kernel32_core::resolve_file("kernel32.dll","FindClose");q.succeeded=fn&&fn((HANDLE)(uintptr_t)q.handle);if(!q.succeeded)failed();break;}
    }
}
#endif
struct PlayResult {
    bool loaded = false;        // the PE32 image mapped and its imports were bound
    bool finished = false;      // the guest called ExitProcess (or its entry returned)
    uint32_t exit_code = 0;
    uint64_t instructions = 0;
    bool dbt_enabled = false;
    std::string dbt_reason;
    uint64_t translated_blocks = 0, interpreted_blocks = 0;
    std::string fault;          // empty when finished; else the Fault text
    std::string fault_kind;     // AccessViolation, Unsupported, UnresolvedImport, ...
    uint32_t fault_eip = 0;
    uint32_t base = 0, entry = 0;
    uint8_t peb_being_debugged = 0;
    uint32_t peb_nt_global_flag = 0;
    unsigned imports = 0, bound = 0, relocations = 0;
    std::vector<std::string> unresolved;   // "dll!symbol", in IAT order
    std::vector<std::tuple<std::string,std::string,gap_summary::Outcome>> gap_imports;
};
// `name` is the executable's file name as the guest sees it (GetModuleFileName,
// argv[0]); `cmdline` is the command line (quoted exe plus the option's
// arguments). `tee` receives guest stdout and stderr as it is written, so the
// launcher log shows it live. `dir` is the guest's working directory: the
// interpreter keeps no host current directory, so it is only reported.
// `on_loaded` runs once the image is mapped and its imports bound, before the
// entry point: the launcher reports progress from there, so a guest that runs
// for a long time is not mistaken for one that never started.
// `instruction_limit` is the interpreter's runaway-loop guard. Its default (the
// Cpu's 500 million) is a few seconds of interpreted work, which is right for a
// fixture and wrong for a game; the launcher passes kGuestRunCap instead.
// The cap stays finite on purpose: a guest that never returns still ends in an
// InstructionLimit fault the launcher can report, rather than spinning on the
// launch thread with no way out. A trillion instructions is hours of
// interpreted work, so no real session reaches it.
inline constexpr uint64_t kGuestRunCap = 1000ull * 1000 * 1000 * 1000;
#if defined(_WIN32)
inline void install_nls_host(Host& host) {
    host.nls_call = [](const Host::NlsRequest& request) -> Host::NlsResponse {
        Host::NlsResponse response{};
        if (request.output_capacity > 0x7fffffffu) { response.last_error = ERROR_INVALID_PARAMETER; return response; }
        switch (request.operation) {
        case Host::NlsOperation::IsValidCodePage:
            response.result = ::IsValidCodePage(request.code_page);
            break;
        case Host::NlsOperation::GetCPInfo: {
            CPINFO info{};
            response.result = ::GetCPInfo(request.code_page, &info);
            if (response.result) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(&info);
                response.output.assign(bytes, bytes + sizeof(info));
            }
            break;
        }
        case Host::NlsOperation::MultiByteToWideChar: {
            std::vector<wchar_t> output(request.output_null ? 0 : request.output_capacity);
            const char* input = request.input_null ? nullptr : reinterpret_cast<const char*>(request.input.data());
            response.result = uint32_t(::MultiByteToWideChar(request.code_page, request.flags, input, request.input_length,
                output.empty() ? nullptr : output.data(), int(request.output_capacity)));
            if (response.result && !request.output_null && request.output_capacity) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(output.data());
                response.output.assign(bytes, bytes + size_t(response.result) * sizeof(wchar_t));
            }
            break;
        }
        case Host::NlsOperation::WideCharToMultiByte: {
            static_assert(sizeof(wchar_t) == 2, "Windows wide characters are UTF-16 units");
            std::vector<char> output(request.output_null ? 0 : request.output_capacity);
            std::vector<wchar_t> input_storage(request.input.size() / 2);
            for (size_t i = 0; i < input_storage.size(); ++i)
                input_storage[i] = wchar_t(uint16_t(request.input[i * 2]) | uint16_t(request.input[i * 2 + 1]) << 8);
            const wchar_t* input = request.input_null ? nullptr : input_storage.data();
            const char* default_char = request.default_char.empty() ? nullptr : reinterpret_cast<const char*>(request.default_char.data());
            BOOL used_default = FALSE;
            response.result = uint32_t(::WideCharToMultiByte(request.code_page, request.flags, input, request.input_length,
                output.empty() ? nullptr : output.data(), int(request.output_capacity), default_char,
                request.wants_used_default ? &used_default : nullptr));
            response.used_default = used_default != FALSE;
            if (response.result && !request.output_null && request.output_capacity)
                response.output.assign(output.begin(), output.begin() + response.result);
            break;
        }
        case Host::NlsOperation::GetStringTypeA:
        case Host::NlsOperation::GetStringTypeW:
        case Host::NlsOperation::GetStringTypeExW: {
            static_assert(sizeof(wchar_t) == 2, "Windows wide characters are UTF-16 units");
            const bool ansi = request.operation == Host::NlsOperation::GetStringTypeA;
            std::vector<wchar_t> input(request.input.size() / 2);
            if (!ansi) for (size_t i = 0; i < input.size(); ++i)
                input[i] = wchar_t(uint16_t(request.input[i * 2]) | uint16_t(request.input[i * 2 + 1]) << 8);
            const int count = request.input_length == -1 ? int(ansi ? request.input.size() : input.size()) : request.input_length;
            if (count <= 0) { response.last_error = ERROR_INVALID_PARAMETER; return response; }
            std::vector<WORD> output(static_cast<size_t>(count));
            const char* input_a = request.input_null ? nullptr : reinterpret_cast<const char*>(request.input.data());
            const wchar_t* input_w = request.input_null ? nullptr : input.data();
            if (request.operation == Host::NlsOperation::GetStringTypeA)
                response.result = ::GetStringTypeA(request.locale, request.type, input_a, request.input_length, output.data());
            else if (request.operation == Host::NlsOperation::GetStringTypeW)
                response.result = ::GetStringTypeW(request.type, input_w, request.input_length, output.data());
            else response.result = ::GetStringTypeExW(request.locale, request.type, input_w, request.input_length, output.data());
            if (response.result) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(output.data());
                response.output.assign(bytes, bytes + output.size() * sizeof(WORD));
            }
            break;
        }
        case Host::NlsOperation::GetUserDefaultLocaleName: {
            using LocaleNameFn=BOOL(WINAPI*)(LPWSTR,int);
            static HMODULE base=LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            static HMODULE k32=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            FARPROC proc=base?GetProcAddress(base,"GetUserDefaultLocaleName"):nullptr;
            if(!proc&&k32)proc=GetProcAddress(k32,"GetUserDefaultLocaleName");
            if(!proc){response.last_error=ERROR_CALL_NOT_IMPLEMENTED;return response;}
            const uint32_t capacity=std::min(request.output_capacity,85u);
            std::vector<wchar_t> output(request.output_null?0:capacity);
            SetLastError(ERROR_SUCCESS);
            response.result=reinterpret_cast<LocaleNameFn>(proc)(output.empty()?nullptr:output.data(),int(capacity))?1u:0u;
            if(response.result&&!output.empty()) {
                size_t length=0;while(length<output.size()&&output[length])++length;
                if(length<output.size()) {
                    const auto* bytes=reinterpret_cast<const uint8_t*>(output.data());
                    response.output.assign(bytes,bytes+(length+1)*sizeof(wchar_t));
                } else response.result=0;
            }
            break;
        }
        case Host::NlsOperation::GetLocaleInfoA:
        case Host::NlsOperation::GetLocaleInfoW:
        case Host::NlsOperation::GetLocaleInfoEx: {
            static_assert(sizeof(wchar_t) == 2, "Windows wide characters are UTF-16 units");
            const bool ansi = request.operation == Host::NlsOperation::GetLocaleInfoA;
            std::vector<char> output_a(request.output_null ? 0 : request.output_capacity);
            std::vector<wchar_t> output_w(request.output_null ? 0 : request.output_capacity);
            std::vector<wchar_t> locale(request.locale_name.begin(), request.locale_name.end()); locale.push_back(0);
            if (request.operation == Host::NlsOperation::GetLocaleInfoEx) {
                using InfoExW=int(WINAPI*)(LPCWSTR,LCTYPE,LPWSTR,int);
                static HMODULE base=LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                static HMODULE k32=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                FARPROC proc=base?GetProcAddress(base,"GetLocaleInfoEx"):nullptr;
                if(!proc&&k32)proc=GetProcAddress(k32,"GetLocaleInfoEx");
                if(!proc){response.last_error=ERROR_CALL_NOT_IMPLEMENTED;return response;}
                response.result=uint32_t(reinterpret_cast<InfoExW>(proc)(request.locale_name_null?nullptr:locale.data(),request.type,
                    output_w.empty()?nullptr:output_w.data(),int(request.output_capacity)));
            } else {
                using InfoA=int(WINAPI*)(LCID,LCTYPE,LPSTR,int);
                using InfoW=int(WINAPI*)(LCID,LCTYPE,LPWSTR,int);
                static HMODULE base=LoadLibraryExW(L"kernelbase.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                static HMODULE k32=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
                FARPROC proc=base?GetProcAddress(base,ansi?"GetLocaleInfoA":"GetLocaleInfoW"):nullptr;
                if(!proc&&k32)proc=GetProcAddress(k32,ansi?"GetLocaleInfoA":"GetLocaleInfoW");
                if(!proc){response.last_error=ERROR_CALL_NOT_IMPLEMENTED;return response;}
                response.result=uint32_t(ansi?reinterpret_cast<InfoA>(proc)(request.locale,request.type,output_a.empty()?nullptr:output_a.data(),int(request.output_capacity)):
                    reinterpret_cast<InfoW>(proc)(request.locale,request.type,output_w.empty()?nullptr:output_w.data(),int(request.output_capacity)));
            }
            if(response.result&&!request.output_null) {
                const size_t unit=ansi?sizeof(char):sizeof(wchar_t);
                const size_t available=ansi?output_a.size():output_w.size()*sizeof(wchar_t);
                const void* output=ansi?static_cast<const void*>(output_a.data()):static_cast<const void*>(output_w.data());
                if(!play_capture_nls_output(response,output,available,size_t(response.result)*unit,false,request.output_capacity)) {
                    response.result=0;response.last_error=ERROR_INSUFFICIENT_BUFFER;
                }
            }
            break;
        }
        case Host::NlsOperation::LCMapStringW:
        case Host::NlsOperation::LCMapStringEx:
        case Host::NlsOperation::FoldStringW: {
            static_assert(sizeof(wchar_t) == 2, "Windows wide characters are UTF-16 units");
            std::vector<wchar_t> input(request.input.size() / 2), output(request.output_null ? 0 : request.output_capacity);
            for (size_t i = 0; i < input.size(); ++i)
                input[i] = wchar_t(uint16_t(request.input[i * 2]) | uint16_t(request.input[i * 2 + 1]) << 8);
            std::vector<wchar_t> locale(request.locale_name.begin(), request.locale_name.end()); locale.push_back(0);
            const wchar_t* input_ptr = request.input_null ? nullptr : input.data();
            const wchar_t* locale_ptr = request.locale_name_null ? nullptr : locale.data();
            if (request.operation == Host::NlsOperation::LCMapStringW) {
                // LCMapStringW sits outside the UWP SDK header partition (the console
                // build fails on ::LCMapStringW); resolve it from the system DLL at run
                // time and report ERROR_CALL_NOT_IMPLEMENTED if the export is absent.
                using MapW=int(WINAPI*)(LCID,DWORD,LPCWSTR,int,LPWSTR,int);
                static HMODULE base = LoadLibraryExW(L"kernelbase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                static HMODULE k32 = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                FARPROC proc = base ? GetProcAddress(base, "LCMapStringW") : nullptr;
                if (!proc && k32) proc = GetProcAddress(k32, "LCMapStringW");
                if (!proc) { response.last_error = ERROR_CALL_NOT_IMPLEMENTED; return response; }
                response.result = uint32_t(reinterpret_cast<MapW>(proc)(request.locale, request.flags, input_ptr, request.input_length,
                    output.empty() ? nullptr : output.data(), int(request.output_capacity)));
            }
            else if (request.operation == Host::NlsOperation::LCMapStringEx)
                response.result = uint32_t(::LCMapStringEx(locale_ptr, request.flags, input_ptr, request.input_length,
                    output.empty() ? nullptr : output.data(), int(request.output_capacity), nullptr, nullptr, 0));
            else response.result = uint32_t(::FoldStringW(request.flags, input_ptr, request.input_length,
                    output.empty() ? nullptr : output.data(), int(request.output_capacity)));
            if (response.result && !request.output_null && request.output_capacity) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(output.data());
                response.output.assign(bytes, bytes + size_t(response.result) * sizeof(wchar_t));
            }
            break;
        }
        case Host::NlsOperation::CompareStringW:
        case Host::NlsOperation::CompareStringEx: {
            static_assert(sizeof(wchar_t) == 2, "Windows wide characters are UTF-16 units");
            std::vector<wchar_t> input(request.input.size() / 2), input2(request.input2.size() / 2);
            for (size_t i = 0; i < input.size(); ++i) input[i] = wchar_t(uint16_t(request.input[i * 2]) | uint16_t(request.input[i * 2 + 1]) << 8);
            for (size_t i = 0; i < input2.size(); ++i) input2[i] = wchar_t(uint16_t(request.input2[i * 2]) | uint16_t(request.input2[i * 2 + 1]) << 8);
            std::vector<wchar_t> locale(request.locale_name.begin(), request.locale_name.end()); locale.push_back(0);
            const wchar_t* input_ptr = request.input_null ? nullptr : input.data();
            const wchar_t* input2_ptr = request.input2_null ? nullptr : input2.data();
            const wchar_t* locale_ptr = request.locale_name_null ? nullptr : locale.data();
            int result = request.operation == Host::NlsOperation::CompareStringW
                ? ::CompareStringW(request.locale, request.flags, input_ptr, request.input_length, input2_ptr, request.input2_length)
                : ::CompareStringEx(locale_ptr, request.flags, input_ptr, request.input_length, input2_ptr, request.input2_length, nullptr, nullptr, 0);
            response.result = uint32_t(result);
            break;
        }
        }
        response.last_error = ::GetLastError();
        return response;
    };
}
#endif
inline PlayResult run_image(const std::vector<uint8_t>& file, const std::string& name, const std::string& cmdline,
                            const std::function<void(const std::string&)>& tee = nullptr,
                            const std::function<void(const PlayResult&)>& on_loaded = nullptr,
                            uint64_t instruction_limit = 500ull * 1000 * 1000,
                            const DllSource& dll_source = nullptr,
                            bool trace_fault_context = false,
                            const std::function<void(const PlayResult&)>& on_imports = nullptr,
                            const DllLoadObserver& observe_dll = nullptr,
                            const std::function<void(Host&)>& configure_host = nullptr,
                            const std::function<bool(const char*, const std::u16string&)>& destructive_file_guard = nullptr,
                            bool dbt_requested = false) {
    PlayResult r;
    try {
        Memory mem; ThunkTable table; Host host; Registry reg;
        host.destructive_file_guard = destructive_file_guard;
#if defined(_WIN32)
        host.file_call = play_file_call;
        host.socket_call = play_socket_call;
        host.tick_count = []() { return uint64_t(::GetTickCount64()); };
        host.locale_call = play_locale_call;
#endif
        host.tee = tee;
        if (configure_host) configure_host(host);
#if defined(_WIN32)
        install_nls_host(host);
#endif
        Guest guest(mem, host, reg);
        guest.exe_path = name;
        guest.cmdline = cmdline.empty() ? name : cmdline;
        guest.dll_source = dll_source;
        guest.register_all();
        // The top image loads ONCE, through load_dll: it maps at the preferred
        // ImageBase when free (an exe's normal case) and only relocates when
        // something already holds that range. Loading it here as well would
        // take the preferred base with a throwaway copy and force the real
        // load to relocate — wrong for mingw CRTs that hard-code the preferred
        // base (e.g. __mingw_GetSectionForAddress reads headers at 0x400000).
        // Dependencies the registry cannot serve load as guest DLLs, tracked
        // in the process module table; their DllMain runs after init_process.
        std::map<std::string, Dll> dlls; std::vector<std::string> dll_order;
        Dll top = load_dll(mem, table, reg, file, dll_source, dlls, 0, &dll_order,
                           [&reg, &table](const std::string& dll, const std::string& sym) {
                               // Forwarded exports that name an OS-served module
                               // ("ntdll.memcpy"): the same thunk/data answer
                               // GetProcAddress gives on that pseudo-handle. The
                               // module part has no ".dll" — lowercase and suffix
                               // it for the registry; an ordinal forward ("dll.#n")
                               // answers 0 (no ordinal surface to guess against).
                               std::string l = dll, s = sym;
                               for (auto& ch : l) ch = char(std::tolower((unsigned char)ch));
                               for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
                               if (l.rfind(".dll") != l.size() - 4) l += ".dll";
                               if (s.rfind("#", 0) == 0) return 0u;
                               auto it = reg.map.find({l, s});
                               if (it != reg.map.end()) return table.add(it->second);
                               auto dt = reg.data.find({l, s});
                               if (dt != reg.data.end()) return dt->second;
                               return 0u;
                           },
                           {}, observe_dll);  // top image: unnamed (never re-entered by a cycle)
        dlls.emplace("__top__", std::move(top));  // not a named module; keeps the top image's exports reachable
        Image img = dlls["__top__"].image;
        guest.dlls = std::move(dlls);
        guest.dll_order = std::move(dll_order);
        for (const auto& kv : guest.dlls) {
            // entry_rva == 0 is "no entry point" (Bugbot 4559749424): `entry`
            // is base + RVA and never zero for a mapped image.
            if (kv.second.image.entry_rva) guest.dll_entries[kv.first] = kv.second.image.entry;
            guest.add_module(kv.first, kv.second.image.base);
        }
        r.loaded = true; r.base = img.base; r.entry = img.entry;
        r.imports = img.imports; r.bound = img.bound; r.relocations = img.relocations; r.unresolved = img.unresolved;
        for(const auto& module:guest.dlls) for(const auto& item:module.second.image.gap_imports) r.gap_imports.push_back(item);
        if(on_imports) on_imports(r);
        guest.image_base = img.base;
        guest.exe_tls = img.tls;  // each thread's TLS block comes from the exe's directory
        guest.add_module(name, img.base);
        map_thunk_page(mem, table);
        Cpu cpu(mem, table, dbt_requested);
        guest.invalidate_dbt = [&cpu](uint32_t base, uint32_t size) { cpu.invalidate_dbt_range(base, size); };
        const char* dbt_env = std::getenv("X86_DBT");
        const bool dbt_env_requested = dbt_env && dbt_env[0] == '1' && dbt_env[1] == '\0';
        r.dbt_enabled = cpu.dbt_enabled;
        r.dbt_reason = !mem.base() ? "Memory::base()==nullptr" : dbt_requested ? "LocalState\\x86dbt.txt" : dbt_env_requested ? "X86_DBT=1" : "not-requested";
        cpu.limit = instruction_limit;
        cpu.trace_fault_context = trace_fault_context;
        cpu.format_eip = [&guest, &img](uint32_t eip) {
            for (const auto& module : guest.modules) {
                if (module.first == "__top__") continue;  // loader sentinel; the exe is also registered under its own name
                uint32_t size = module.second == img.base ? img.size : 0;
                auto dll = guest.dlls.find(module.first);
                if (dll != guest.dlls.end()) size = dll->second.image.size;
                if (size && eip >= module.second && eip - module.second < size) {
                    char label[128];
                    snprintf(label, sizeof label, "%s+0x%08x", module.first.c_str(), eip - module.second);
                    return std::string(label);
                }
            }
            return std::string();
        };
        auto record_fault = [&](const Fault& fault) {
            r.fault = cpu.fault_text(fault); r.fault_eip = fault.eip;
            switch (fault.kind) {
            case FaultKind::AccessViolation: r.fault_kind = "AccessViolation"; break;
            case FaultKind::ExecuteViolation: r.fault_kind = "ExecuteViolation"; break;
            case FaultKind::DivideError: r.fault_kind = "DivideError"; break;
            case FaultKind::InvalidOpcode: r.fault_kind = "InvalidOpcode"; break;
            case FaultKind::Unsupported: r.fault_kind = "Unsupported"; break;
            case FaultKind::Breakpoint: r.fault_kind = "Breakpoint"; break;
            case FaultKind::InstructionLimit: r.fault_kind = "InstructionLimit"; break;
            case FaultKind::BadThunk: r.fault_kind = "BadThunk"; break;
            case FaultKind::UnresolvedImport: r.fault_kind = "UnresolvedImport"; break;
            }
        };
        init_process(mem, cpu, img);
        guest.tls_images.clear();
        for (const auto& module : guest.dll_order) {
            auto it = guest.dlls.find(module);
            if (it != guest.dlls.end() && it->second.image.tls.present)
                guest.tls_images.push_back(&it->second.image);
        }
        if (img.tls.present) guest.tls_images.push_back(&img);
        if (!guest.tls_images.empty()) {
            uint32_t slots = guest.heap.alloc(uint32_t(guest.tls_images.size() * sizeof(uint32_t)));
            for (size_t i = 0; i < guest.tls_images.size(); ++i) {
                Image& tls_image = *guest.tls_images[i];
                uint32_t raw = tls_image.tls.end - tls_image.tls.start;
                uint64_t total = uint64_t(raw) + tls_image.tls.zero_fill;
                if (total > 16u * 1024u * 1024u) throw std::runtime_error("PE32 TLS template exceeds 16 MiB");
                uint32_t block = guest.heap.alloc(uint32_t(total ? total : 1));
                for (uint32_t n = 0; n < raw; ++n) mem.w8(block + n, mem.r8(tls_image.tls.start + n));
                for (uint32_t n = raw; n < total; ++n) mem.w8(block + n, 0);
                mem.w32(slots + uint32_t(i * sizeof(uint32_t)), block);
                mem.w32(tls_image.tls.index_ptr, uint32_t(i));
            }
            mem.w32(TEB_ADDR + 0x2C, slots);
        }
        // DllMain(DLL_PROCESS_ATTACH) for every loaded guest DLL, in load
        // order — after the thunk page and TEB/PEB exist, before the entry.
        try { guest.run_dll_mains(cpu, 1); } catch (const Fault& f) {
            if (!cpu.exited) { record_fault(f); r.instructions = cpu.executed;
                r.translated_blocks = cpu.translated_blocks; r.interpreted_blocks = cpu.interpreted_blocks;
                r.peb_being_debugged=mem.r8(PEB_ADDR+2);r.peb_nt_global_flag=mem.r32(PEB_ADDR+0x68);return r; }
        }
        // TLS blocks are per loaded image. DLL_PROCESS_ATTACH runs first, then
        // each image's TLS callbacks, then the executable entry point.
        if (on_loaded) on_loaded(r);
        try {
            if (guest.tls_images.empty()) run_entry(cpu, img);
            else {
                for (Image* tls_image : guest.tls_images)
                    if (tls_image) // a DllMain may have unloaded it
                    for (uint32_t p = tls_image->tls.callbacks; p; p += 4) {
                        uint32_t fn = mem.r32(p);
                        if (!fn) break;
                        cpu.call(fn, {tls_image->base, 1, 0});
                    }
                uint32_t result = cpu.call(img.entry, {PEB_ADDR});
                if (!cpu.exited) { cpu.exited = true; cpu.exit_code = result; }
            }
            r.finished = cpu.exited;
            r.exit_code = cpu.exit_code;
        } catch (const Fault& fault) {
            record_fault(fault);
        }
        r.instructions = cpu.executed;
        r.translated_blocks = cpu.translated_blocks; r.interpreted_blocks = cpu.interpreted_blocks;
        r.peb_being_debugged=mem.r8(PEB_ADDR+2);r.peb_nt_global_flag=mem.r32(PEB_ADDR+0x68);
    } catch (const std::exception& e) {
        r.fault = std::string("loader: ") + e.what();
        r.fault_kind = "Loader";
    }
    return r;
}
#if defined(_WIN32)
// Reads the executable and runs it. `exe` is the full path, `dir` the guest's
// working directory (the launch option's, else the executable's own).
// `instruction_limit` is forwarded to run_image: a game run passes
// kGuestRunCap, a bounded one keeps the interpreter's default.
inline PlayResult run(const std::wstring& exe, const std::wstring& dir, const std::string& cmdline,
                      const std::function<void(const std::string&)>& tee = nullptr,
                      const std::function<void(const PlayResult&)>& on_loaded = nullptr,
                      uint64_t instruction_limit = 500ull * 1000 * 1000,
                      const DllSource& dll_source = nullptr,
                      bool trace_fault_context = false,
                      const std::function<void(const PlayResult&)>& on_imports = nullptr,
                      const DllLoadObserver& observe_dll = nullptr,
                      const std::function<void(Host&)>& configure_host = nullptr,
                      const std::function<bool(const char*, const std::u16string&)>& destructive_file_guard = nullptr,
                      bool dbt_requested = false) {
    PlayResult r;
    FILE* f = _wfopen(exe.c_str(), L"rb");
    if (!f) { r.fault = "cannot open the executable"; r.fault_kind = "Loader"; return r; }
    _fseeki64(f, 0, SEEK_END);
    long long length = _ftelli64(f);
    rewind(f);
    if (length < 0 || length > 256ll * 1024 * 1024) { fclose(f); r.fault = "executable size"; r.fault_kind = "Loader"; return r; }
    std::vector<uint8_t> file(static_cast<size_t>(length));
    size_t got = fread(file.data(), 1, file.size(), f);
    fclose(f);
    if (got != file.size()) { r.fault = "short read of the executable"; r.fault_kind = "Loader"; return r; }
    std::string name;
    for (size_t i = exe.find_last_of(L"\\/") + 1; i < exe.size(); ++i) name += char(exe[i]);
    std::wstring workingDirectory=dir;
    if(workingDirectory.empty()) { const auto slash=exe.find_last_of(L"\\/"); if(slash!=std::wstring::npos)workingDirectory=exe.substr(0,slash); }
    std::wstring previousDirectory;
    bool restoreDirectory = false;
    if (!workingDirectory.empty()) {
        DWORD needed = GetCurrentDirectoryW(0, nullptr);
        if (needed) { std::vector<wchar_t> previous(needed); DWORD n = GetCurrentDirectoryW(needed, previous.data());
            if (n && n < needed) { previousDirectory.assign(previous.data(), n); restoreDirectory = SetCurrentDirectoryW(workingDirectory.c_str()) != FALSE; } }
    }
    auto result = run_image(file, name, cmdline, tee, on_loaded, instruction_limit, dll_source, trace_fault_context, on_imports, observe_dll, configure_host, destructive_file_guard, dbt_requested);
    if (restoreDirectory) SetCurrentDirectoryW(previousDirectory.c_str());
    return result;
}
#endif
}
