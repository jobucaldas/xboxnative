#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
inline int WINAPI date_format(LCID locale, DWORD flags, const SYSTEMTIME* time,
        LPCWSTR format, LPWSTR output, int capacity) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!LCIDToLocaleName(locale, name, LOCALE_NAME_MAX_LENGTH, 0)) return 0;
    return GetDateFormatEx(name, flags & ~LOCALE_USE_CP_ACP, time, format, output, capacity, nullptr);
}
inline int WINAPI time_format(LCID locale, DWORD flags, const SYSTEMTIME* time,
        LPCWSTR format, LPWSTR output, int capacity) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!LCIDToLocaleName(locale, name, LOCALE_NAME_MAX_LENGTH, 0)) return 0;
    return GetTimeFormatEx(name, flags & ~LOCALE_USE_CP_ACP, time, format, output, capacity);
}
inline int WINAPI map_string(LCID locale, DWORD flags, LPCWSTR source, int length,
        LPWSTR output, int capacity) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!LCIDToLocaleName(locale, name, LOCALE_NAME_MAX_LENGTH, 0)) return 0;
    return LCMapStringEx(name, flags & ~LOCALE_USE_CP_ACP, source, length, output, capacity, nullptr, nullptr, 0);
}
inline UINT WINAPI oem_cp() {
    CPINFOEXW info{};
    if (!GetCPInfoExW(CP_OEMCP, 0, &info)) return 0;
    return info.CodePage;
}
inline LCID WINAPI user_lcid() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) return 0;
    return LocaleNameToLCID(name, 0);
}
inline BOOL WINAPI valid_locale(LCID locale, DWORD flags) {
    // Preserve legacy installed/supported validation rather than inferring
    // installation status solely from numeric LCID conversion.
    using Query = BOOL (WINAPI*)(LCID, DWORD);
    static Query query = []() -> Query {
        HMODULE module = LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto p = module ? GetProcAddress(module, "IsValidLocale") : nullptr;
        return reinterpret_cast<Query>(reinterpret_cast<uintptr_t>(p));
    }();
    if (!query) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
    return query(locale, flags);
}

// Use the OS locale database and civil-time rules. Do not hardcode the
// console's language, UTC offset or an artificial performance frequency.
inline uintptr_t resolve_nls_time(const char* dll, const char* name) {
    if (!dll || !name) return 0;
    if (_stricmp(dll, "kernel32.dll") && _stricmp(dll, "kernelbase.dll")) return 0;
#define K32_NLS_TIME(symbol) if (!strcmp(name, #symbol)) return reinterpret_cast<uintptr_t>(&::symbol)
    K32_NLS_TIME(FileTimeToSystemTime);
    K32_NLS_TIME(SystemTimeToFileTime);
    K32_NLS_TIME(GetLocalTime);
    K32_NLS_TIME(GetSystemTime);
    K32_NLS_TIME(GetSystemTimeAsFileTime);
    K32_NLS_TIME(GetTimeZoneInformation);
    K32_NLS_TIME(GetDynamicTimeZoneInformation);
    K32_NLS_TIME(GetTimeZoneInformationForYear);
    K32_NLS_TIME(SystemTimeToTzSpecificLocalTime);
    K32_NLS_TIME(QueryPerformanceCounter);
    K32_NLS_TIME(QueryPerformanceFrequency);
    if (!strcmp(name, "GetDateFormatW")) return reinterpret_cast<uintptr_t>(&date_format);
    if (!strcmp(name, "GetTimeFormatW")) return reinterpret_cast<uintptr_t>(&time_format);
    K32_NLS_TIME(CompareStringEx);
    K32_NLS_TIME(CompareStringW);
    K32_NLS_TIME(GetACP);
    if (!strcmp(name, "GetOEMCP")) return reinterpret_cast<uintptr_t>(&oem_cp);
    K32_NLS_TIME(GetCPInfo);
    K32_NLS_TIME(IsValidCodePage);
    if (!strcmp(name, "IsValidLocale")) return reinterpret_cast<uintptr_t>(&valid_locale);
    K32_NLS_TIME(GetLocaleInfoW);
    K32_NLS_TIME(GetStringTypeW);
    K32_NLS_TIME(GetThreadLocale);
    if (!strcmp(name, "GetUserDefaultLCID")) return reinterpret_cast<uintptr_t>(&user_lcid);
    K32_NLS_TIME(GetUserDefaultLocaleName);
    if (!strcmp(name,"EnumSystemLocalesW")) {
        using Enum=BOOL (WINAPI*)(LOCALE_ENUMPROCW,DWORD);
        static Enum enumerate=[]() -> Enum {
            HMODULE module=LoadLibraryExW(L"kernel32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto p=module ? GetProcAddress(module,"EnumSystemLocalesW") : nullptr;
            return reinterpret_cast<Enum>(reinterpret_cast<uintptr_t>(p));
        }();
        return reinterpret_cast<uintptr_t>(enumerate);
    }
    K32_NLS_TIME(LCMapStringEx);
    if (!strcmp(name, "LCMapStringW")) return reinterpret_cast<uintptr_t>(&map_string);
    K32_NLS_TIME(MultiByteToWideChar);
    K32_NLS_TIME(WideCharToMultiByte);
#undef K32_NLS_TIME
    return 0;
}
}
