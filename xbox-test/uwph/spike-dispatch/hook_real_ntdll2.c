/*
 * uwph-spike-dispatch, part 3: same as hook_real_ntdll.c, but with the
 * handler tagged __attribute__((sysv_abi)) (both mingw-gcc and clang
 * support this specifically for Windows/Linux ABI interop). Wine's real
 * __wine_unix_call_dispatcher asm trampoline (dlls/ntdll/unix/
 * signal_x86_64.c) moves the args pointer from %r8 (3rd Windows-ABI
 * register) into %rdi (1st System-V-ABI register) before `callq
 * *(%r10,%rdx,8)` -- i.e. every unix_call_funcs[] entry is a System-V
 * function, because it's really calling into ntdll.so's own ELF-compiled
 * code. hook_real_ntdll.c's handler was an ordinary Windows-ABI function
 * and silently read garbage (whatever was left in %rcx, repurposed by the
 * trampoline to hold the syscall_frame pointer) instead of the real args
 * pointer. This version fixes that mismatch and checks whether the naive
 * handle-table swap then actually works end to end against the REAL
 * prebuilt ntdll.dll.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef UINT64 unixlib_handle_t;
typedef NTSTATUS (__attribute__((sysv_abi)) *unixlib_entry_t)( void *args );

static int hook_called = 0;

static NTSTATUS __attribute__((sysv_abi)) hook_system_time_precise( void *args )
{
    LONGLONG *ret = args;
    hook_called = 1;
    *ret = 0x7777777777777777LL;
    return 0;
}

static NTSTATUS __attribute__((sysv_abi)) hook_unused( void *args ) { (void)args; return 0; }

static const unixlib_entry_t hook_funcs[8] = {
    hook_unused, hook_unused, hook_unused, hook_unused,
    hook_unused, hook_unused, hook_unused, hook_system_time_precise,
};

typedef LONGLONG (WINAPI *RtlGetSystemTimePrecise_t)(void);

int main( void )
{
    HMODULE ntdll = GetModuleHandleA( "ntdll.dll" );
    unixlib_handle_t *p_handle = (unixlib_handle_t *)GetProcAddress( ntdll, "__wine_unixlib_handle" );
    if (!p_handle) { printf( "HOOK-RESULT: export not found\n" ); return 1; }

    unixlib_handle_t saved_handle = *p_handle;
    *p_handle = (unixlib_handle_t)(UINT_PTR)hook_funcs;

    RtlGetSystemTimePrecise_t fn =
        (RtlGetSystemTimePrecise_t)GetProcAddress( ntdll, "RtlGetSystemTimePrecise" );
    LONGLONG r = fn();
    printf( "RtlGetSystemTimePrecise returned %#llx, our_hook_called=%d\n",
            (unsigned long long)r, hook_called );

    *p_handle = saved_handle;

    if (hook_called && r == 0x7777777777777777LL)
        printf( "HOOK-RESULT: SUCCESS - sysv_abi-tagged pure-PE handler answered through the REAL ntdll.dll's own dispatcher\n" );
    else
        printf( "HOOK-RESULT: still not invoked correctly even with sysv_abi tag and correct slot\n" );

    return 0;
}
