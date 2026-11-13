/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3 fixture: a freestanding i686 PE32 exe exercising guest DLL
 * loading in the interpreter. Chain: exe -> dll-a.dll -> dll-b.dll (guest PEs
 * all the way down), plus LoadLibraryA, GetProcAddress (name and ordinal),
 * FreeLibrary, GetModuleHandleA/W/ExW, forwarded exports to OS modules
 * (msvcrt.memcpy, NTDLL.memcpy, kernel32.Sleep, an ordinal forward and a
 * missing module) and the DllMain attach marker.
 * Freestanding: the only host imports are the kernel32 APIs under test, so
 * the fixture bounds exactly the surface it proves. Exit 42 on success. */
typedef unsigned int u32;
typedef unsigned short u16;

u32 __stdcall LoadLibraryA(const char *name);
u32 __stdcall GetProcAddress(u32 module, const char *name);
u32 __stdcall FreeLibrary(u32 module);
u32 __stdcall GetModuleHandleA(const char *name);
u32 __stdcall GetModuleHandleW(const u16 *name);
/* Real Windows prototype (winbase.h): 3 parameters — flags, name, out. */
u32 __stdcall GetModuleHandleExW(u32 flags, const u16 *name, u32 *out);

/* from dll-a.dll / dll-b.dll, bound by the import binder */
u32 __stdcall dll_a_sum(u32 n);
u32 __stdcall dllb_attach_marker(void);

static u32 failed = 0;
static void ok(u32 cond) { if (!cond) ++failed; }

int fixture_main(void) {
    /* 1. The import binder bound the guest-to-guest imports. */
    ok(dll_a_sum(3) == 3 * 0xB00B5u + 6);
    /* 2. DllMain(DLL_PROCESS_ATTACH) ran in dll-b before the exe's entry. */
    ok(dllb_attach_marker() == 1);

    /* 3. GetModuleHandleA of a loaded guest DLL returns its base. */
    u32 hb = GetModuleHandleA("dll-b.dll");
    ok(hb != 0);
    /* 4-5. GetProcAddress by name, then call through the pointer. */
    u32 pval = GetProcAddress(hb, "dllb_value");
    ok(pval != 0);
    u32 (__stdcall *val)(void) = (u32 (__stdcall *)(void))pval;
    ok(val() == 0xB00B5u);
    /* 6. GetProcAddress by ordinal (MAKEINTRESOURCEA form): dllb_value is
     * the second named export in the linker's table. */
    u32 pord = GetProcAddress(hb, (const char *)2);
    ok(pord != 0);
    /* 7. A missing export answers 0. */
    ok(GetProcAddress(hb, "no_such_export") == 0);
    /* 8. GetModuleHandleW of a loaded DLL (wide name). */
    const u16 wname[] = {'d','l','l','-','a','.','d','l','l',0};
    ok(GetModuleHandleW(wname) != 0);

    /* 9. LoadLibraryA of a loaded module returns the same base. */
    u32 h2 = LoadLibraryA("dll-b.dll");
    ok(h2 == hb);
    /* 10. FreeLibrary succeeds and the module stays usable (no real unmap). */
    ok(FreeLibrary(h2) != 0);
    ok(GetProcAddress(hb, "dllb_value") != 0);
    /* 11. An unknown module fails with 0. */
    ok(LoadLibraryA("no-such-dll.dll") == 0);
    /* 12. GetModuleHandleA(NULL) is the exe's own image base. */
    ok(GetModuleHandleA(0) != 0);

    /* 13. GetModuleHandleExW with the real 3-parameter prototype (dwFlags,
     * lpModuleName, phModule): finds a loaded DLL and writes its base. With
     * only 2 parameters declared the out pointer would be read from the wrong
     * stack slot (garbage or a fault), so this discriminates the arity. */
    {
        const u16 wname2[] = {'d','l','l','-','b','.','d','l','l',0};
        u32 h3 = 0;
        ok(GetModuleHandleExW(0, wname2, &h3) != 0);
        ok(h3 == hb);
    }
    /* 14. LoadLibraryA of a DLL the loader has NOT seen yet registers it in
     * the module table: a later GetModuleHandleW must find it. (The loader
     * bug this discriminates: load_guest_dll returned the base without
     * add_module, so GetModuleHandle missed it.) */
    {
        u32 hfresh = LoadLibraryA("dll-a.dll");
        ok(hfresh != 0);
        const u16 wname3[] = {'d','l','l','-','a','.','d','l','l',0};
        ok(GetModuleHandleW(wname3) == hfresh);
    }

    /* 15-19. Forwarded exports to OS modules. fwd.dll's exports are pure
     * forward strings (msvcrt.memcpy, NTDLL.memcpy, kernel32.Sleep,
     * "ntdll.#123", nosuchmod.nosuchsym) built by the real linker. GetProcAddress
     * must resolve them one hop through the registry — the same path the
     * import binder uses — and the resolved addresses must be callable.
     * With the pre-fix resolver (no ".dll" suffix, no case fold) every one of
     * these answered 0. */
    {
        u32 hfwd = LoadLibraryA("fwd.dll");
        ok(hfwd != 0);
        /* 15. msvcrt.memcpy: call through the forward and check the bytes. */
        void *(*fwmemcpy)(void *, const void *, u32) =
            (void *(*)(void *, const void *, u32))GetProcAddress(hfwd, "fwd_memcpy");
        ok(fwmemcpy != 0);
        if (fwmemcpy) {
            char dst[8];
            static const char src[8] = {'f','w','d','!','m','e','m','1'};
            fwmemcpy(dst, src, sizeof dst);
            ok(dst[0]=='f' && dst[1]=='w' && dst[2]=='d' && dst[3]=='!' &&
               dst[4]=='m' && dst[5]=='e' && dst[6]=='m' && dst[7]=='1');
        }
        /* 16. NTDLL.memcpy: upper-case module part must match case-insensitively
         * (the same thunk the lowercase forward got — one stub per registry
         * entry? No: add() appends, so the two GetProcAddress calls each add;
         * equality of the resolved addresses is what Windows answers too, but
         * the surface hands out distinct stubs, so compare behaviour instead:
         * call it and check the same copy works). */
        void *(*fwmix)(void *, const void *, u32) =
            (void *(*)(void *, const void *, u32))GetProcAddress(hfwd, "fwd_mixed");
        ok(fwmix != 0);
        if (fwmix) {
            char dst[8];
            static const char src[8] = {'N','T','D','L','L','!','m','m'};
            fwmix(dst, src, sizeof dst);
            ok(dst[0]=='N' && dst[1]=='T' && dst[2]=='D' && dst[3]=='L' &&
               dst[4]=='L' && dst[5]=='!' && dst[6]=='m' && dst[7]=='m');
        }
        /* 17. kernel32.Sleep: resolves to the Sleep thunk and is callable.
         * The pointer is stdcall (the thunk pops its 1 argument); a cdecl
         * pointer here leaves ESP off by 4 and derails the entry. */
        void (__stdcall *fwsleep)(u32) = (void (__stdcall *)(u32))GetProcAddress(hfwd, "fwd_sleep");
        ok(fwsleep != 0);
        if (fwsleep) fwsleep(0);
        /* 18. Ordinal forward ("ntdll.#123"): the surface registers no
         * ordinals, so resolution answers 0 cleanly — not a guessed thunk. */
        ok(GetProcAddress(hfwd, "fwd_ord") == 0);
        /* 19. Forward to a module that does not exist: 0, no hang, no crash. */
        ok(GetProcAddress(hfwd, "fwd_ghost") == 0);
    }

    return failed ? 43 : 42;
}
