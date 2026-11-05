#pragma once
#include <windows.h>
#include <winstring.h>
#include <roapi.h>
#include <robuffer.h>
#ifdef _MSC_VER
#include <roerrorapi.h>
#else
// MinGW's runtimeobject import library has these SDK exports, but version 12
// omits roerrorapi.h. Keep the documented COM pointer and UINT32 signatures.
struct IRestrictedErrorInfo;
extern "C" __declspec(dllimport) HRESULT WINAPI GetRestrictedErrorInfo(IRestrictedErrorInfo**);
extern "C" __declspec(dllimport) HRESULT WINAPI RoSetErrorReportingFlags(UINT32);
#endif
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// HSTRING ownership and activation apartments belong to native Windows, like
// the guest's native threads. Only matching documented API-set contracts bind.
inline uintptr_t resolve_winrt(const char* dll,const char* name) {
    if(!dll||!name)return 0;
    if(!_stricmp(dll,"api-ms-win-core-winrt-string-l1-1-0.dll")) {
#define K32_STRING(symbol) if(!strcmp(name,#symbol))return reinterpret_cast<uintptr_t>(&::symbol)
        K32_STRING(WindowsCompareStringOrdinal);
        K32_STRING(WindowsCreateString);
        K32_STRING(WindowsCreateStringReference);
        K32_STRING(WindowsDeleteString);
        K32_STRING(WindowsDuplicateString);
        K32_STRING(WindowsGetStringRawBuffer);
#undef K32_STRING
    }
    if(!_stricmp(dll,"api-ms-win-core-winrt-l1-1-0.dll")) {
#define K32_ACTIVATION(symbol) if(!strcmp(name,#symbol))return reinterpret_cast<uintptr_t>(&::symbol)
        K32_ACTIVATION(RoInitialize);
        K32_ACTIVATION(RoUninitialize);
        K32_ACTIVATION(RoActivateInstance);
        K32_ACTIVATION(RoGetActivationFactory);
#undef K32_ACTIVATION
    }
    if(!_stricmp(dll,"api-ms-win-core-winrt-error-l1-1-0.dll")) {
        if(!strcmp(name,"GetRestrictedErrorInfo"))return reinterpret_cast<uintptr_t>(&::GetRestrictedErrorInfo);
        if(!strcmp(name,"RoSetErrorReportingFlags"))return reinterpret_cast<uintptr_t>(&::RoSetErrorReportingFlags);
    }
    if(!_stricmp(dll,"api-ms-win-core-winrt-robuffer-l1-1-0.dll")&&!strcmp(name,"RoGetBufferMarshaler"))
        return reinterpret_cast<uintptr_t>(&::RoGetBufferMarshaler);
    return 0;
}
}
