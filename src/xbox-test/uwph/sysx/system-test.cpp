// Off-device regression for the launcher's existing System32 routes. This
// verifies operations, not merely export presence; it is not Xbox proof.
#include <windows.h>
#include <wincrypt.h>
#include <objbase.h>
#include <oleauto.h>
#include <roapi.h>
#include <winstring.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "../../app/host/mlp_routes.h"
};
template<class T> T api(const char* dll, const char* name) {
    bool system = false;
    for (const auto& route : routes)
        if (!_stricmp(dll, route.dll)) system = !strcmp(route.kind, "system");
    CHECK(system);
    std::wstring wide(dll, dll + strlen(dll));
    HMODULE module = LoadLibraryExW(wide.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module);
    auto function = reinterpret_cast<T>(GetProcAddress(module, name));
    CHECK(function);
    return function;
}
#define API(dll, name) auto name = api<decltype(&::name)>(dll, #name)
static void crypto() {
    API("advapi32.dll", CryptAcquireContextW);
    API("advapi32.dll", CryptCreateHash);
    API("advapi32.dll", CryptHashData);
    API("advapi32.dll", CryptGetHashParam);
    API("advapi32.dll", CryptDestroyHash);
    API("advapi32.dll", CryptGenRandom);
    API("advapi32.dll", CryptReleaseContext);
    HCRYPTPROV provider = 0;
    CHECK(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT));
    HCRYPTHASH hash = 0;
    CHECK(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash));
    CHECK(CryptHashData(hash, reinterpret_cast<const BYTE*>("abc"), 3, 0));
    const BYTE expected[] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,
        0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    BYTE digest[32]{};
    DWORD bytes = 1;
    CHECK(!CryptGetHashParam(hash, HP_HASHVAL, digest, &bytes, 0));
    CHECK(GetLastError() == ERROR_MORE_DATA && bytes == sizeof(digest));
    CHECK(CryptGetHashParam(hash, HP_HASHVAL, digest, &bytes, 0));
    CHECK(bytes == sizeof(digest) && !memcmp(digest, expected, sizeof(digest)));
    BYTE random[32]{};
    CHECK(CryptGenRandom(provider, sizeof(random), random));
    CHECK(CryptDestroyHash(hash));
    CHECK(!CryptHashData(hash, nullptr, 0, 0));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(CryptReleaseContext(provider, 0));
    std::puts("PASS system crypto: SHA256 known vector, sizing, random API, destroyed hash");
}
static void registry_and_token() {
    API("advapi32.dll", RegCreateKeyExW);
    API("advapi32.dll", RegSetValueExW);
    API("advapi32.dll", RegQueryValueExW);
    API("advapi32.dll", RegOpenKeyExW);
    API("advapi32.dll", RegCloseKey);
    API("advapi32.dll", OpenProcessToken);
    API("advapi32.dll", GetTokenInformation);
    API("advapi32.dll", GetSidSubAuthority);
    wchar_t path[128];
    swprintf(path, 128, L"Software\\sysx-regression-%lu", GetCurrentProcessId());
    HKEY key = nullptr;
    DWORD disposition = 0;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr,
        REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &key, &disposition) == ERROR_SUCCESS);
    CHECK(disposition == REG_CREATED_NEW_KEY);
    DWORD value = 0x12345678;
    CHECK(RegSetValueExW(key, L"value", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS);
    CHECK(RegCloseKey(key) == ERROR_SUCCESS);
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS);
    DWORD type = 0, bytes = 0;
    CHECK(RegQueryValueExW(key, L"value", nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS);
    CHECK(type == REG_DWORD && bytes == sizeof(value));
    DWORD actual = 0;
    CHECK(RegQueryValueExW(key, L"value", nullptr, &type,
        reinterpret_cast<BYTE*>(&actual), &bytes) == ERROR_SUCCESS);
    CHECK(actual == value);
    CHECK(RegQueryValueExW(key, L"missing", nullptr, nullptr, nullptr, &bytes) == ERROR_FILE_NOT_FOUND);
    CHECK(RegCloseKey(key) == ERROR_SUCCESS);
    // Cleanup uses an OS API outside the game's import set, never a new route.
    CHECK(RegDeleteKeyW(HKEY_CURRENT_USER, path) == ERROR_SUCCESS);
    HANDLE token = nullptr;
    CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    bytes = 0;
    CHECK(!GetTokenInformation(token, TokenUser, nullptr, 0, &bytes));
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes >= sizeof(TOKEN_USER));
    std::vector<BYTE> storage(bytes);
    CHECK(GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes));
    auto user = reinterpret_cast<TOKEN_USER*>(storage.data());
    CHECK(IsValidSid(user->User.Sid));
    CHECK(*GetSidSubAuthorityCount(user->User.Sid) > 0);
    CHECK(GetSidSubAuthority(user->User.Sid, 0));
    CHECK(CloseHandle(token));
    std::puts("PASS system registry/token: persisted typed value, missing value, sizing, real SID");
}
static void com_and_strings() {
    API("ole32.dll", CoInitializeEx);
    API("ole32.dll", CoUninitialize);
    API("ole32.dll", CoTaskMemAlloc);
    API("ole32.dll", CoTaskMemFree);
    API("ole32.dll", CoCreateGuid);
    API("ole32.dll", StringFromGUID2);
    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK);
    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_FALSE);
    CHECK(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == RPC_E_CHANGED_MODE);
    void* memory = CoTaskMemAlloc(64);
    CHECK(memory);
    memset(memory, 0xa5, 64);
    CoTaskMemFree(memory);
    GUID guid{};
    CHECK(CoCreateGuid(&guid) == S_OK);
    wchar_t formatted[39]{};
    CHECK(StringFromGUID2(guid, formatted, 38) == 0);
    CHECK(StringFromGUID2(guid, formatted, 39) == 39);
    CHECK(formatted[0] == L'{' && formatted[37] == L'}');
    // Megabonk imports these OLEAUT32 APIs by ordinal.
    auto alloc = api<decltype(&::SysAllocStringLen)>("oleaut32.dll", MAKEINTRESOURCEA(4));
    auto length = api<decltype(&::SysStringLen)>("oleaut32.dll", MAKEINTRESOURCEA(7));
    auto free = api<decltype(&::SysFreeString)>("oleaut32.dll", MAKEINTRESOURCEA(6));
    const wchar_t source[] = {L'a', 0, L'b'};
    BSTR bstr = alloc(source, 3);
    CHECK(bstr && length(bstr) == 3 && !memcmp(bstr, source, sizeof(source)));
    free(bstr);
    API("api-ms-win-core-winrt-string-l1-1-0.dll", WindowsCreateString);
    API("api-ms-win-core-winrt-string-l1-1-0.dll", WindowsCreateStringReference);
    API("api-ms-win-core-winrt-string-l1-1-0.dll", WindowsDuplicateString);
    API("api-ms-win-core-winrt-string-l1-1-0.dll", WindowsGetStringRawBuffer);
    API("api-ms-win-core-winrt-string-l1-1-0.dll", WindowsCompareStringOrdinal);
    API("api-ms-win-core-winrt-string-l1-1-0.dll", WindowsDeleteString);
    HSTRING string = nullptr, duplicate = nullptr;
    CHECK(WindowsCreateString(source, 3, &string) == S_OK);
    CHECK(WindowsDuplicateString(string, &duplicate) == S_OK);
    CHECK(WindowsDeleteString(string) == S_OK);
    UINT32 size = 0;
    auto raw = WindowsGetStringRawBuffer(duplicate, &size);
    CHECK(size == 3 && !memcmp(raw, source, sizeof(source)) && raw[3] == 0);
    INT32 comparison = 0;
    CHECK(WindowsCompareStringOrdinal(nullptr, duplicate, &comparison) == S_OK && comparison < 0);
    CHECK(WindowsDeleteString(duplicate) == S_OK);
    HSTRING_HEADER header{};
    HSTRING reference = nullptr;
    const wchar_t terminated[] = {L'a', 0, L'b', 0};
    CHECK(WindowsCreateStringReference(terminated, 3, &header, &reference) == S_OK);
    raw = WindowsGetStringRawBuffer(reference, &size);
    CHECK(raw == terminated && size == 3);
    CHECK(WindowsDuplicateString(reference, &duplicate) == S_OK);
    CHECK(WindowsDeleteString(reference) == S_OK);
    raw = WindowsGetStringRawBuffer(duplicate, &size);
    CHECK(size == 3 && !memcmp(raw, terminated, sizeof(terminated)));
    CHECK(WindowsDeleteString(duplicate) == S_OK);
    API("api-ms-win-core-winrt-l1-1-0.dll", RoInitialize);
    API("api-ms-win-core-winrt-l1-1-0.dll", RoUninitialize);
    CHECK(RoInitialize(RO_INIT_MULTITHREADED) == S_FALSE);
    CHECK(RoInitialize(RO_INIT_SINGLETHREADED) == RPC_E_CHANGED_MODE);
    RoUninitialize();
    CoUninitialize();
    CoUninitialize();
    std::puts("PASS system COM/WinRT: apartment balancing, memory, GUID, ordinal BSTR, embedded NUL and string lifetime");
}
int main() {
    crypto();
    registry_and_token();
    com_and_strings();
    std::puts("SYSX SYSTEM OFFDEVICE PASS (not Xbox/AppContainer proof)");
}
