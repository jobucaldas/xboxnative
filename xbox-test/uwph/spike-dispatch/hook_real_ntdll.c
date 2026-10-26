/*
 * uwph-spike-dispatch, part 2: try to hook the REAL prebuilt
 * /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/ntdll.dll's
 * __wine_unixlib_handle / __wine_unix_call_dispatcher exported globals
 * in-process and redirect RtlGetSystemTimePrecise's unix call to our own
 * handler table, instead of reimplementing the protocol from scratch
 * (dispatch_shim.c). This is the literal "load a copy of ntdll.dll" step
 * from the lane prompt. See LANE_NOTES.md for the result and why it does
 * or doesn't work.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef UINT64 unixlib_handle_t;
typedef NTSTATUS (*unixlib_entry_t)( void *args );

static int hook_called = 0;

static NTSTATUS WINAPI hook_system_time_precise( void *args )
{
    LONGLONG *ret = args;
    hook_called = 1;
    *ret = 0x7777777777777777LL; /* obviously-fake sentinel value */
    return 0;
}

/* Wine 10.0's real dlls/ntdll/unix/loader.c:unix_call_funcs[] order
 * (confirmed by reading the source in /workspace/ref/wine-10.0-src):
 * load_so_dll, unwind_builtin_dll, unixcall_wine_dbg_write,
 * unixcall_wine_server_call, unixcall_wine_server_fd_to_handle,
 * unixcall_wine_server_handle_to_fd, unixcall_wine_spawnvp,
 * system_time_precise -- i.e. system_time_precise is slot 7, not slot 0.
 * Fill the other 7 slots with a harmless stub in case anything unexpected
 * reaches them during the call. */
static NTSTATUS WINAPI hook_unused( void *args ) { (void)args; return 0; }
static const unixlib_entry_t hook_funcs[8] = {
    hook_unused, hook_unused, hook_unused, hook_unused,
    hook_unused, hook_unused, hook_unused, hook_system_time_precise,
};

typedef LONGLONG (WINAPI *RtlGetSystemTimePrecise_t)(void);

int main( void )
{
    HMODULE ntdll = GetModuleHandleA( "ntdll.dll" );
    printf( "ntdll.dll module handle = %p\n", (void*)ntdll );

    unixlib_handle_t *p_handle = (unixlib_handle_t *)GetProcAddress( ntdll, "__wine_unixlib_handle" );
    void **p_dispatcher = (void **)GetProcAddress( ntdll, "__wine_unix_call_dispatcher" );
    printf( "__wine_unixlib_handle addr=%p value_before=%#llx\n",
            (void*)p_handle, p_handle ? (unsigned long long)*p_handle : 0 );
    printf( "__wine_unix_call_dispatcher addr=%p value_before=%p\n",
            (void*)p_dispatcher, p_dispatcher ? *p_dispatcher : NULL );

    if (!p_handle || !p_dispatcher)
    {
        printf( "HOOK-RESULT: exports not found, cannot test\n" );
        return 1;
    }

    /* Try the direct, naive thing: just overwrite the handle (data slot) to
     * point at our own function table, leaving Wine's real dispatcher
     * trampoline in place -- the dispatcher is just `funcs[code](args)`
     * plus signal-frame bookkeeping, so if the handle is a plain opaque
     * UINT64 cast back to a function-pointer array (exactly as our own
     * shim_unix_call_dispatcher does it), this should "just work" for a
     * single flat call list IF the index/order matches what Wine's real
     * unix_call_funcs[] table expects for this build. We don't know that
     * order without reading the private enum in dlls/ntdll/unix/loader.c,
     * so this will almost certainly call the WRONG real handler at that
     * slot, or get the right slot and succeed, or crash -- any of those
     * outcomes is informative and is logged below. */
    unixlib_handle_t saved_handle = *p_handle;
    *p_handle = (unixlib_handle_t)(UINT_PTR)hook_funcs;

    RtlGetSystemTimePrecise_t fn =
        (RtlGetSystemTimePrecise_t)GetProcAddress( ntdll, "RtlGetSystemTimePrecise" );
    printf( "calling RtlGetSystemTimePrecise with handle redirected to our table (index 7 = our hook)...\n" );
    fflush(stdout);
    LONGLONG r = fn();
    printf( "RtlGetSystemTimePrecise returned %#llx, our_hook_called=%d\n",
            (unsigned long long)r, hook_called );

    /* restore, in case anything else in the process needs the real handle */
    *p_handle = saved_handle;

    if (hook_called && r == 0x7777777777777777LL)
        printf( "HOOK-RESULT: SUCCESS - our handler answered through the real ntdll.dll's dispatcher\n" );
    else
        printf( "HOOK-RESULT: our handler was NOT invoked even at the correct slot 7 -- the dispatcher does something incompatible with a naive data-slot handle swap\n" );

    return 0;
}
