/*
 * uwph-spike-dispatch: does Wine's "unix call" boundary (the
 * __wine_unixlib_handle / __wine_unix_call_dispatcher pair that Wine's PE
 * ntdll.dll uses to reach its own Unix-side implementation) reduce to a
 * plain function-pointer/opcode protocol that a from-scratch, pure-PE
 * implementation can satisfy with zero ELF (.so) code behind it?
 *
 * This file is OUR OWN implementation of that protocol end to end: the
 * opcode table, the dispatcher trampoline, and two "unix call" handlers
 * (system_time_precise and dbg_write) -- using the exact symbol names and
 * function-pointer signature that Wine 10.0's real dlls/ntdll/unixlib.h
 * defines (see LANE_NOTES.md for the source citations). It does NOT load
 * or touch Wine's actual prebuilt ntdll.dll -- that's covered separately
 * by hook_real_ntdll.c/hook_real_ntdll2.c in this same directory, which
 * DO hook the real binary's exported globals successfully (see
 * LANE_NOTES.md item 3 for that result and its two extra caveats:
 * private per-build opcode ordering, and System-V vs Windows x64 calling
 * convention for the table entries).
 *
 * Every function below -- dispatcher, handlers, and the caller -- is
 * ordinary Win32 code (GetSystemTimeAsFileTime, WriteFile): nothing here
 * calls into Linux/libc/ELF. The CRT startup that wraps main() is the only
 * non-Win32 code in this binary, and it's an artifact of building a
 * runnable test harness with mingw, not part of the boundary under test.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif

typedef UINT64 unixlib_handle_t;
typedef NTSTATUS (*unixlib_entry_t)( void *args );

/* Same opcode order Wine 10.0 uses for ntdll's own unix_call_funcs[]
 * (dlls/ntdll/unix/loader.c), trimmed to the two leaf opcodes this spike
 * reimplements. Real Wine has 8 (load_so_dll, unwind_builtin_dll,
 * wine_dbg_write, wine_server_call, wine_server_fd_to_handle,
 * wine_server_handle_to_fd, wine_spawnvp, system_time_precise) -- the four
 * we are NOT reimplementing all need either wineserver IPC (AF_UNIX, a
 * separate process) or ELF .so loading, which is exactly the part this
 * spike found is NOT a bare function-pointer swap. See LANE_NOTES.md.
 */
enum shim_unix_funcs
{
    shim_system_time_precise,
    shim_dbg_write,
};

struct wine_dbg_write_params
{
    const char  *str;
    unsigned int len;
};

/* ---- the two "unix call" handlers: plain Win32, no ELF/.so ---- */

static NTSTATUS WINAPI shim_handle_system_time_precise( void *args )
{
    LONGLONG *ret = args;
    FILETIME ft;
    /* FILETIME is already "100ns intervals since 1601-01-01", the same
     * representation RtlGetSystemTimePrecise returns. */
    GetSystemTimeAsFileTime( &ft );
    *ret = ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI shim_handle_dbg_write( void *args )
{
    struct wine_dbg_write_params *p = args;
    DWORD written;
    WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), p->str, p->len, &written, NULL );
    return STATUS_SUCCESS;
}

static const unixlib_entry_t shim_unix_call_funcs[] =
{
    shim_handle_system_time_precise,
    shim_handle_dbg_write,
};

/* ---- the dispatch boundary itself: same names, same signature Wine's
 * real dlls/ntdll/unixlib.h declares for __wine_unixlib_handle and
 * __wine_unix_call_dispatcher ---- */

unixlib_handle_t __wine_unixlib_handle = (unixlib_handle_t)(UINT_PTR)shim_unix_call_funcs;

static NTSTATUS WINAPI shim_unix_call_dispatcher( unixlib_handle_t handle, unsigned int code, void *args )
{
    /* This is the part Wine's REAL dispatcher does NOT do this simply: its
     * x86_64 version (dlls/ntdll/unix/signal_x86_64.c) first spills every
     * callee-saved register and the XMM6-15 block into a "syscall_frame"
     * struct living at a fixed glibc-TLS offset (%gs:0x328,
     * amd64_thread_data()->syscall_frame) so a SIGSEGV/SIGBUS taken *inside*
     * the unix call can be unwound back into a Windows-shaped exception by
     * Wine's own signal handlers. That frame and those handlers are set up
     * by ntdll.so's ELF thread-init, not by anything exported from
     * ntdll.dll (PE) -- there is no pure-PE equivalent of it yet. Our
     * handlers below are leaf functions that cannot fault in a way that
     * needs unwinding, so the trampoline can be this trivial. See
     * LANE_NOTES.md item 2.
     */
    if (code >= sizeof(shim_unix_call_funcs) / sizeof(shim_unix_call_funcs[0]))
        return STATUS_INVALID_PARAMETER;
    return ((const unixlib_entry_t *)(UINT_PTR)handle)[code]( args );
}

NTSTATUS (WINAPI *__wine_unix_call_dispatcher)( unixlib_handle_t, unsigned int, void * ) = shim_unix_call_dispatcher;

/* Mirrors Wine's own WINE_UNIX_CALL macro (include/wine/unixlib.h). */
#define SHIM_UNIX_CALL(code,args) __wine_unix_call_dispatcher( __wine_unixlib_handle, (code), (args) )

/* ---- caller side: mirrors dlls/ntdll/time.c's RtlGetSystemTimePrecise ---- */

static LONGLONG shim_RtlGetSystemTimePrecise( void )
{
    LONGLONG ret;
    SHIM_UNIX_CALL( shim_system_time_precise, &ret );
    return ret;
}

static void shim_write_via_dispatch( const char *str )
{
    struct wine_dbg_write_params p = { str, (unsigned int)strlen( str ) };
    SHIM_UNIX_CALL( shim_dbg_write, &p );
}

int main( void )
{
    shim_write_via_dispatch( "uwph-spike-dispatch: calling RtlGetSystemTimePrecise through our own dispatcher...\n" );

    LONGLONG ft100ns = shim_RtlGetSystemTimePrecise();
    /* 1601-01-01 -> 1970-01-01 offset, in 100ns units (same constant ntdll
     * uses internally, e.g. dlls/ntdll/unix/sync.c ticks_from_time_t). */
    const LONGLONG EPOCH_DIFF_100NS = 116444736000000000LL;
    LONGLONG unix_100ns = ft100ns - EPOCH_DIFF_100NS;
    time_t shim_epoch = (time_t)(unix_100ns / 10000000LL);

    FILETIME host_ft;
    GetSystemTimeAsFileTime( &host_ft );
    LONGLONG host_100ns = (((LONGLONG)host_ft.dwHighDateTime << 32) | host_ft.dwLowDateTime) - EPOCH_DIFF_100NS;
    time_t host_epoch = (time_t)(host_100ns / 10000000LL);

    long long delta = (long long)host_epoch - (long long)shim_epoch;
    if (delta < 0) delta = -delta;

    printf( "SHIM-ANSWERED RtlGetSystemTimePrecise epoch=%lld host_epoch=%lld delta_s=%lld\n",
            (long long)shim_epoch, (long long)host_epoch, delta );

    if (delta > 5)
    {
        printf( "FAIL: delta exceeds 5s\n" );
        return 1;
    }
    printf( "PASS\n" );
    return 0;
}
