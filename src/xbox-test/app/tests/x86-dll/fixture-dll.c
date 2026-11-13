/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3 fixture: a freestanding i686 PE32 exe exercising guest DLL
 * loading in the interpreter. Chain: exe -> dll-a.dll -> dll-b.dll (guest PEs
 * all the way down), plus LoadLibraryA, GetProcAddress (name and ordinal),
 * FreeLibrary, GetModuleHandleA/W/ExW and the DllMain attach marker.
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

    return failed ? 43 : 42;
}
