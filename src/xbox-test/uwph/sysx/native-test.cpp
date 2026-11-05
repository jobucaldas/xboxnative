#include "native.h"
#include <wininet.h>
#include <dbghelp.h>
#include <cstdio>
#include <vector>
#include <string>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
template<class T> T api(const char* dll, const char* symbol) {
    return reinterpret_cast<T>(sysx_native::resolve(dll, symbol));
}
extern "C" __declspec(dllexport) int sysx_exported_symbol() { return 42; }
static unsigned discovered = 0, unavailable = 0;
static void discovery_log(const char* message) {
    ++discovered;
    if (strstr(message, "available=0")) ++unavailable;
}
PVOID CALLBACK no_function_entry(HANDLE, DWORD64, ULONG64) { return nullptr; }
int main(int argc, char** argv) {
    sysx_native::discover(discovery_log);
    CHECK(discovered == 17 && unavailable == 0);
    CHECK(!sysx_native::resolve(nullptr, "SymInitialize"));
    CHECK(!sysx_native::resolve("dbghelp.dll", nullptr));
    CHECK(!sysx_native::resolve("untrusted.dll", "SymInitialize"));
    CHECK(!sysx_native::resolve("dbghelp.dll", "syminitialize"));
    CHECK(!sysx_native::resolve("dbghelp.dll", "SymEnumSymbols"));
    auto init = api<decltype(&SymInitialize)>("DBGHELP.DLL", "SymInitialize");
    auto load = api<decltype(&SymLoadModuleEx)>("dbghelp.dll", "SymLoadModuleEx");
    auto from = api<decltype(&SymFromAddr)>("dbghelp.dll", "SymFromAddr");
    auto callback = api<decltype(&SymRegisterFunctionEntryCallback64)>("dbghelp.dll", "SymRegisterFunctionEntryCallback64");
    CHECK(init && load && from && callback);
    HMODULE dbg = GetModuleHandleW(L"dbghelp.dll");
    CHECK(reinterpret_cast<FARPROC>(init) == GetProcAddress(dbg, "SymInitialize"));
    HANDLE process = GetCurrentProcess();
    CHECK(init(process, nullptr, FALSE));
    auto raw_callback = reinterpret_cast<decltype(&SymRegisterFunctionEntryCallback64)>(
        GetProcAddress(dbg, "SymRegisterFunctionEntryCallback64"));
    SetLastError(0xdead);
    BOOL callback_result = callback(process, no_function_entry, 0);
    DWORD callback_error = GetLastError();
    SetLastError(0xdead);
    CHECK(raw_callback(process, no_function_entry, 0) == callback_result);
    CHECK(GetLastError() == callback_error);
    // Wine 10 returns ERROR_CALL_NOT_IMPLEMENTED for this export. Preserve
    // that failure rather than claiming callback registration support.
    if (!callback_result) CHECK(callback_error == ERROR_CALL_NOT_IMPLEMENTED);
    char filename[MAX_PATH];
    CHECK(GetModuleFileNameA(nullptr, filename, MAX_PATH));
    DWORD64 base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    CHECK(load(process, nullptr, filename, nullptr, base, 0, nullptr, 0) == base);
    alignas(SYMBOL_INFO) unsigned char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    CHECK(from(process, reinterpret_cast<DWORD64>(&sysx_exported_symbol), &displacement, symbol));
    CHECK(strstr(symbol->Name, "sysx_exported_symbol"));
    CHECK(displacement == 0);
    CHECK(!from(process, 1, &displacement, symbol));
    // gbe_fork's exports are the exact OS functions and behave on a live session.
    for (const char* name : {"SymSetOptions", "SymCleanup", "SymGetSymFromAddr64", "SymGetModuleBase64",
                             "SymFunctionTableAccess64", "StackWalk64"})
        CHECK(reinterpret_cast<FARPROC>(sysx_native::resolve("dbghelp.dll", name)) == GetProcAddress(dbg, name));
    auto options = api<decltype(&SymSetOptions)>("dbghelp.dll", "SymSetOptions");
    auto module_base = api<decltype(&SymGetModuleBase64)>("dbghelp.dll", "SymGetModuleBase64");
    CHECK(options && module_base);
    DWORD previous = options(SYMOPT_UNDNAME);
    CHECK(options(previous) == SYMOPT_UNDNAME);
    CHECK(module_base(process, reinterpret_cast<DWORD64>(&sysx_exported_symbol)) == base);
    auto cleanup = api<decltype(&SymCleanup)>("dbghelp.dll", "SymCleanup");
    CHECK(cleanup && cleanup(process));

    auto crack = api<decltype(&InternetCrackUrlA)>("wininet.dll", "InternetCrackUrlA");
    auto open = api<decltype(&InternetOpenA)>("wininet.dll", "InternetOpenA");
    auto connect = api<decltype(&InternetConnectA)>("wininet.dll", "InternetConnectA");
    auto request = api<decltype(&HttpOpenRequestA)>("wininet.dll", "HttpOpenRequestA");
    auto send = api<decltype(&HttpSendRequestA)>("wininet.dll", "HttpSendRequestA");
    auto query = api<decltype(&HttpQueryInfoA)>("wininet.dll", "HttpQueryInfoA");
    auto close = api<decltype(&InternetCloseHandle)>("wininet.dll", "InternetCloseHandle");
    CHECK(crack && open && connect && request && send && query && close);
    URL_COMPONENTSA parts{};
    parts.dwStructSize = sizeof(parts);
    char host[128]{}, path[128]{};
    parts.lpszHostName = host; parts.dwHostNameLength = sizeof(host);
    parts.lpszUrlPath = path; parts.dwUrlPathLength = sizeof(path);
    CHECK(crack("https://example.invalid:8443/a/b", 0, 0, &parts));
    CHECK(parts.nScheme == INTERNET_SCHEME_HTTPS && parts.nPort == 8443);
    CHECK(!strcmp(host, "example.invalid") && !strcmp(path, "/a/b"));
    CHECK(!crack("not a URL", 0, 0, &parts));
    CHECK(argc == 2);
    HINTERNET session = open("sysx-regression", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    CHECK(session);
    HINTERNET connection = connect(session, "127.0.0.1", static_cast<INTERNET_PORT>(atoi(argv[1])),
        nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
    CHECK(connection);
    HINTERNET http = request(connection, "GET", "/sysx", nullptr, nullptr, nullptr,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    CHECK(http && send(http, nullptr, 0, nullptr, 0));
    DWORD status = 0, size = sizeof(status);
    CHECK(query(http, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr));
    CHECK(status == 200 && size == sizeof(status));
    char tiny[1]{}; size = sizeof(tiny);
    CHECK(!query(http, HTTP_QUERY_CONTENT_LENGTH, tiny, &size, nullptr));
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > sizeof(tiny));
    char header[32]{}; size = sizeof(header);
    CHECK(query(http, HTTP_QUERY_CONTENT_LENGTH, header, &size, nullptr));
    CHECK(!strcmp(header, "4"));
    CHECK(close(http) && close(connection) && close(session));
    CHECK(!close(nullptr));
    std::puts("SYSX NATIVE OFFDEVICE PASS: allowlist, real symbols, mapped export lookup, URL parsing, loopback HTTP status/headers, handle lifecycle");
}
