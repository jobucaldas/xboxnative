#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace kernel32_core {
// Standard handles and console attachments belong to the host process, just
// like native threads. Bind only these exact documented signatures. Console
// absence and AppContainer policy remain native failures, never dummy output.
inline uintptr_t resolve_console(const char* dll,const char* name) {
    if (!dll || !name) return 0;
    bool kernel=!_stricmp(dll,"kernel32.dll") || !_stricmp(dll,"kernelbase.dll");
    bool console=!_stricmp(dll,"api-ms-win-core-console-l1-1-0.dll") ||
                 !_stricmp(dll,"api-ms-win-core-console-l1-2-0.dll");
    if (!kernel && !console) return 0;
    // The console headers exclude App family declarations. Query explicit
    // typed OS exports rather than introducing disallowed static imports.
    HMODULE module=GetModuleHandleW(L"kernel32.dll");
    if (!module) return 0;
#define K32_CONSOLE(symbol,result,parameters) \
    if (!strcmp(name,#symbol)) { \
        using Function=result (WINAPI*)parameters; \
        static Function function=reinterpret_cast<Function>(reinterpret_cast<uintptr_t>(GetProcAddress(module,#symbol))); \
        return reinterpret_cast<uintptr_t>(function); \
    }
    K32_CONSOLE(AllocConsole,BOOL,());
    K32_CONSOLE(AttachConsole,BOOL,(DWORD));
    K32_CONSOLE(GetConsoleMode,BOOL,(HANDLE,LPDWORD));
    K32_CONSOLE(GetConsoleOutputCP,UINT,());
    K32_CONSOLE(GetStdHandle,HANDLE,(DWORD));
    K32_CONSOLE(SetStdHandle,BOOL,(DWORD,HANDLE));
    K32_CONSOLE(SetConsoleCtrlHandler,BOOL,(PHANDLER_ROUTINE,BOOL));
    K32_CONSOLE(ReadConsoleW,BOOL,(HANDLE,LPVOID,DWORD,LPDWORD,PCONSOLE_READCONSOLE_CONTROL));
    K32_CONSOLE(WriteConsoleW,BOOL,(HANDLE,const void*,DWORD,LPDWORD,LPVOID));
#undef K32_CONSOLE
    return 0;
}
}
