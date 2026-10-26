#include <windows.h>
// Force a DIR64 relocation and a writable data section. No CRT or TLS.
static const char * volatile prefix = "WXL-OK ";
static volatile DWORD counter;
DWORD wxl_entry(void) {
    char line[80]; unsigned i = 0;
    const char *p = prefix;
    while (*p) line[i++] = *p++;
    counter = GetCurrentProcessId() + (GetTickCount() & 7);
    DWORD n = counter; char digits[16]; unsigned j = 0;
    do { digits[j++] = '0' + n % 10; n /= 10; } while (n);
    while (j) line[i++] = digits[--j];
    line[i] = 0; OutputDebugStringA(line);
    return 42;
}
