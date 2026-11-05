// Registry PlayerPrefs probe. CRT-free PE, mapped by the wxl mapper. Calls exactly the
// nine advapi32 Reg* imports UnityPlayer.dll uses, on HKCU\Software\Ved\Megabonk, with
// the value shapes Unity PlayerPrefs writes (REG_DWORD ints, REG_BINARY strings).
#include <windows.h>
#ifdef REG_DIRECT  // off-device self-test: same calls against the regb builtins (see reg_builtin_selftest.cpp)
LONG WINAPI regb_RegCreateKeyW(HKEY, LPCWSTR, PHKEY);
LONG WINAPI regb_RegCreateKeyExW(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM, const SECURITY_ATTRIBUTES*, PHKEY, LPDWORD);
LONG WINAPI regb_RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD);
LONG WINAPI regb_RegSetValueExW(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD);
LONG WINAPI regb_RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
LONG WINAPI regb_RegQueryValueExW(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
LONG WINAPI regb_RegDeleteValueA(HKEY, LPCSTR);
LONG WINAPI regb_RegOpenKeyExW(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
LONG WINAPI regb_RegCloseKey(HKEY);
void regb_dbg(const char*);
#define OutputDebugStringA regb_dbg
#define RegCreateKeyW regb_RegCreateKeyW
#define RegCreateKeyExW regb_RegCreateKeyExW
#define RegSetValueExA regb_RegSetValueExA
#define RegSetValueExW regb_RegSetValueExW
#define RegQueryValueExA regb_RegQueryValueExA
#define RegQueryValueExW regb_RegQueryValueExW
#define RegDeleteValueA regb_RegDeleteValueA
#define RegOpenKeyExW regb_RegOpenKeyExW
#define RegCloseKey regb_RegCloseKey
#endif
// A pointer in writable data forces a DIR64 relocation, as the mapper requires.
static const char* volatile prefix = "REG ";
static char line[256]; static unsigned n; static int calls, failures;
static void put(const char* s) { while (*s && n < 240) line[n++] = *s++; }
static void num(unsigned long v) { char d[24]; unsigned j = 0; do { d[j++] = '0' + v % 10; v /= 10; } while (v); while (j) line[n++] = d[--j]; }
static void begin(const char* api, const char* what) { n = 0; put(prefix); put(api); put(" "); put(what); put(" -> "); }
static void end(LONG rc, LONG want, const char* extra) {
    num((unsigned long)rc); put(rc == want ? " OK" : " FAIL(want "); if (rc != want) { num((unsigned long)want); put(")"); }
    if (extra) { put(" "); put(extra); }
    line[n] = 0; OutputDebugStringA(line); calls++; if (rc != want) failures++;
}
static int same(const void* a, const void* b, unsigned len) { const BYTE* x = a; const BYTE* y = b; while (len--) if (*x++ != *y++) return 0; return 1; }
static void check(const char* api, const char* what, int ok) {
    n = 0; put("REG "); put(api); put(" "); put(what); put(ok ? " -> data OK" : " -> data MISMATCH"); line[n] = 0;
    OutputDebugStringA(line); calls++; if (!ok) failures++;
}
static void dline(const char* what, LONG rc) { n = 0; put("REGDIAG "); put(what); put(" -> "); num((unsigned long)rc); line[n] = 0; OutputDebugStringA(line); }
static const WCHAR kKey[] = L"Software\\Ved\\Megabonk";
// Which access is refused? Diagnostics only; never counted as Reg* calls.
static void diag(void) {
    HKEY h = 0; DWORD d;
    h = 0; dline("RegOpenKeyExW HKCU\\ KEY_READ", RegOpenKeyExW(HKEY_CURRENT_USER, L"", 0, KEY_READ, &h)); if (h) RegCloseKey(h);
    h = 0; dline("RegOpenKeyExW HKCU\\Software KEY_READ", RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &h)); if (h) RegCloseKey(h);
    h = 0; dline("RegCreateKeyExW HKCU\\Software\\Ved\\Megabonk KEY_READ", RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, 0, 0, KEY_READ, 0, &h, &d)); if (h) RegCloseKey(h);
    h = 0; dline("RegCreateKeyExW HKCU\\Software\\Ved KEY_WRITE", RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Ved", 0, 0, 0, KEY_WRITE, 0, &h, &d)); if (h) RegCloseKey(h);
    h = 0; dline("RegCreateKeyExW HKCU\\Ved KEY_ALL_ACCESS", RegCreateKeyExW(HKEY_CURRENT_USER, L"Ved", 0, 0, 0, KEY_ALL_ACCESS, 0, &h, &d)); if (h) RegCloseKey(h);
    h = 0; dline("RegCreateKeyW HKCU\\Software\\Ved\\Megabonk", RegCreateKeyW(HKEY_CURRENT_USER, kKey, &h)); if (h) RegCloseKey(h);
}
DWORD reg_entry(void) {
    HKEY k = 0, sub = 0, ro = 0, none = 0; DWORD disp = 0, type, size; LONG rc; BYTE buf[64];
    rc = RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, 0, 0, KEY_READ | KEY_WRITE, 0, &k, &disp);
    begin("RegCreateKeyExW", "HKCU\\Software\\Ved\\Megabonk"); end(rc, 0, disp == REG_CREATED_NEW_KEY ? "created" : "existing");
    if (rc) { diag(); OutputDebugStringA("REG-FAILED cannot create key"); return 1; }
    rc = RegCreateKeyW(HKEY_CURRENT_USER, L"Software\\Ved\\Megabonk\\Prefs", &sub);
    begin("RegCreateKeyW", "HKCU\\Software\\Ved\\Megabonk\\Prefs"); end(rc, 0, 0);
    if (sub) { rc = RegCloseKey(sub); begin("RegCloseKey", "Prefs"); end(rc, 0, 0); }

    DWORD vol = 7;
    rc = RegSetValueExW(k, L"Volume_h1234", 0, REG_DWORD, (const BYTE*)&vol, 4); begin("RegSetValueExW", "Volume_h1234 REG_DWORD=7"); end(rc, 0, 0);
    static const WCHAR ws[] = L"seed-\x00e9";
    rc = RegSetValueExW(k, L"Name_h5678", 0, REG_SZ, (const BYTE*)ws, sizeof ws); begin("RegSetValueExW", "Name_h5678 REG_SZ"); end(rc, 0, 0);
    static const BYTE bin[8] = { 'M','e','g','a',0,'b','o','n' };
    rc = RegSetValueExA(k, "Blob_h9abc", 0, REG_BINARY, bin, sizeof bin); begin("RegSetValueExA", "Blob_h9abc REG_BINARY[8]"); end(rc, 0, 0);
    rc = RegSetValueExA(k, "Ansi_h0def", 0, REG_SZ, (const BYTE*)"hello", 6); begin("RegSetValueExA", "Ansi_h0def REG_SZ=hello"); end(rc, 0, 0);
    rc = RegCloseKey(k); begin("RegCloseKey", "Megabonk (after write)"); end(rc, 0, 0); k = 0;

    rc = RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ | KEY_WRITE, &ro); begin("RegOpenKeyExW", "HKCU\\Software\\Ved\\Megabonk"); end(rc, 0, 0);
    if (rc) { OutputDebugStringA("REG-FAILED cannot reopen key"); return 1; }

    size = 0; type = 0; rc = RegQueryValueExW(ro, L"Volume_h1234", 0, &type, 0, &size);
    begin("RegQueryValueExW", "Volume_h1234 size-only"); end(rc, 0, size == 4 && type == REG_DWORD ? "type=REG_DWORD size=4" : "BAD-TYPE-OR-SIZE");
    if (!(size == 4 && type == REG_DWORD)) failures++;
    size = sizeof buf; type = 0; *(DWORD*)buf = 0; rc = RegQueryValueExW(ro, L"Volume_h1234", 0, &type, buf, &size);
    begin("RegQueryValueExW", "Volume_h1234 read"); end(rc, 0, 0); check("RegQueryValueExW", "Volume_h1234 == 7", *(DWORD*)buf == 7 && size == 4);
    size = sizeof buf; type = 0; rc = RegQueryValueExW(ro, L"Name_h5678", 0, &type, buf, &size);
    begin("RegQueryValueExW", "Name_h5678 read"); end(rc, 0, 0); check("RegQueryValueExW", "Name_h5678 == seed-\\u00e9", type == REG_SZ && size == sizeof ws && same(buf, ws, size));
    size = 3; type = 0; rc = RegQueryValueExW(ro, L"Name_h5678", 0, &type, buf, &size);
    begin("RegQueryValueExW", "Name_h5678 small buffer"); end(rc, ERROR_MORE_DATA, size == sizeof ws ? "needed=14" : "BAD-NEEDED");
    if (size != sizeof ws) failures++;
    size = sizeof buf; type = 0; rc = RegQueryValueExA(ro, "Blob_h9abc", 0, &type, buf, &size);
    begin("RegQueryValueExA", "Blob_h9abc read"); end(rc, 0, 0); check("RegQueryValueExA", "Blob_h9abc == 8 bytes", type == REG_BINARY && size == 8 && same(buf, bin, 8));
    size = sizeof buf; type = 0; rc = RegQueryValueExA(ro, "Ansi_h0def", 0, &type, buf, &size);
    begin("RegQueryValueExA", "Ansi_h0def read"); end(rc, 0, 0); check("RegQueryValueExA", "Ansi_h0def == hello", type == REG_SZ && size == 6 && same(buf, "hello", 6));
    size = sizeof buf; rc = RegQueryValueExA(ro, "Missing_h0000", 0, &type, buf, &size);
    begin("RegQueryValueExA", "Missing_h0000"); end(rc, ERROR_FILE_NOT_FOUND, 0);

    rc = RegDeleteValueA(ro, "Ansi_h0def"); begin("RegDeleteValueA", "Ansi_h0def"); end(rc, 0, 0);
    size = sizeof buf; rc = RegQueryValueExA(ro, "Ansi_h0def", 0, &type, buf, &size);
    begin("RegQueryValueExA", "Ansi_h0def after delete"); end(rc, ERROR_FILE_NOT_FOUND, 0);
    rc = RegDeleteValueA(ro, "Ansi_h0def"); begin("RegDeleteValueA", "Ansi_h0def again"); end(rc, ERROR_FILE_NOT_FOUND, 0);
    rc = RegCloseKey(ro); begin("RegCloseKey", "Megabonk (after read)"); end(rc, 0, 0);
    rc = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Ved\\NoSuchGame", 0, KEY_READ, &none); begin("RegOpenKeyExW", "HKCU\\Software\\Ved\\NoSuchGame"); end(rc, ERROR_FILE_NOT_FOUND, 0);

    n = 0; put(failures ? "REG-FAILED calls=" : "REG-OK calls="); num(calls); put(" failures="); num(failures); line[n] = 0; OutputDebugStringA(line);
    return failures ? 1 : 42;
}
