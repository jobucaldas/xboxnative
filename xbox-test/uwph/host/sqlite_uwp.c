/* SQLite 3.54.1 amalgamation built for the UWP/AppContainer host. The SDK headers hide three Win32 calls that
   SQLite's win32 VFS names (AreFileApisANSI, CreateFileW, HeapValidate) under WINAPI_FAMILY_APP; map them onto the
   sanctioned UWP APIs (CreateFile2) before including the unmodified amalgamation. */
#include <windows.h>
static BOOL WINAPI uwp_AreFileApisANSI(VOID) { return FALSE; }
static BOOL WINAPI uwp_HeapValidate(HANDLE h, DWORD f, LPCVOID m) { (void)h; (void)f; (void)m; return TRUE; }
static HANDLE WINAPI uwp_CreateFileW(LPCWSTR n, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    CREATEFILE2_EXTENDED_PARAMETERS p; ZeroMemory(&p, sizeof p); p.dwSize = sizeof p;
    p.dwFileAttributes = flags & 0xFFFF; p.dwFileFlags = flags & 0xFFF00000; p.dwSecurityQosFlags = flags & 0x000F0000; p.lpSecurityAttributes = sa; p.hTemplateFile = tmpl;
    return CreateFile2(n, access, share, disp, &p);
}
#define AreFileApisANSI uwp_AreFileApisANSI
#define HeapValidate uwp_HeapValidate
#define CreateFileW uwp_CreateFileW
#include "sqlite3.c"
