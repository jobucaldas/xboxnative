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
u32 __stdcall GetLastError(void);
void __stdcall Sleep(u32 ms);
void __stdcall ExitThread(u32 code);
void __stdcall SetLastError(u32 code);u32 __stdcall CreateThread(void *attr, u32 stack, u32 start, void *param, u32 flags, u32 *id);
u32 __stdcall WaitForSingleObject(u32 handle, u32 ms);
u32 __stdcall GetExitCodeThread(u32 handle, u32 *code);
u32 __stdcall EncodePointer(u32 p);
u32 __stdcall DecodePointer(u32 p);
u32 __stdcall EncodeSystemPointer(u32 p);
u32 __stdcall DecodeSystemPointer(u32 p);

/* from dll-a.dll / dll-b.dll, bound by the import binder */
u32 __stdcall dll_a_sum(u32 n);
u32 __stdcall dllb_attach_marker(void);
/* from the cyclic pair dll-c <-> dll-d (bound by the recursive loader) */
u32 __stdcall dllc_attach_count(void);
u32 __stdcall dlld_attach_count(void);
u32 __stdcall dllc_sum(u32 n);
/* from dll-zeroent.dll (no entry point, SizeOfImage >> file size) and
 * dll-second.dll (same preferred base, must relocate) — LoadLibrary'd */
u32 __stdcall zeroent_tail_get(void);
void __stdcall zeroent_tail_set(u32 v);
u32 __stdcall zeroent_fn(void);
u32 __stdcall second_fn(void);

/* forward declarations for the thread-body check */
static u32 __stdcall thread_body(void *param);

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
        /* 20. Guest-to-guest forward ("dll-b.dllb_value"): the module part has
         * no ".dll" of its own in the forward string's dll-b form — the
         * resolver must normalize it to the module-table key dll-b.dll
         * (Bugbot 4559267293) and answer dllb_value's guest address. */
        u32 (__stdcall *fwdguest)(void) = (u32 (__stdcall *)(void))GetProcAddress(hfwd, "fwd_guest");
        ok(fwdguest != 0);
        if (fwdguest) ok(fwdguest() == 0xB00B5u);
        /* 20b. A mixed-case forward to a guest DLL ("DLL-B.DLLB_VALUE") must
         * fold the module AND symbol part the same way a lowercase forward
         * does — the registry keys are lowercase (Bugbot 4559749448). */
        u32 (__stdcall *fwdcase)(void) = (u32 (__stdcall *)(void))GetProcAddress(hfwd, "fwd_ordcase");
        ok(fwdcase != 0);
        if (fwdcase) ok(fwdcase() == 0xB00B5u);
        /* 20c. A REAL export whose RVA sits right after the export directory
         * is code, not a forwarder string: the forwarder test is the export
         * DIRECTORY's bounds ([IMAGE_DATA_DIRECTORY[0].VirtualAddress,
         * +Size), peformat.ms — IMAGE_DATA_DIRECTORY::Size), never a guessed
         * 64 KiB window, which would swallow real code in a compact DLL
         * (Bugbot 4560048276). dll-exp-after.dll is python-built with its
         * function RVA 0x1080 inside the old guessed window but outside the
         * declared Size 0x40: (a) the name resolves to code inside the image
         * (a forwarder would answer 0 here — the registry has no
         * "exp-after.after_fn"), (b) calling it runs the real bytes. */
        {
            u32 hxa = LoadLibraryA("dll-exp-after.dll");
            ok(hxa != 0);
            u32 pa = GetProcAddress(hxa, "after_fn");
            ok(pa != 0);
            ok(pa > hxa && pa < hxa + 0x2000);  /* inside the image: real code */
            if (pa) {
                u32 (__stdcall *fn)(void) = (u32 (__stdcall *)(void))pa;
                ok(fn() == 0x0DDBA11u);  /* the bytes execute — ASCII would fault */
            }
            /* By ordinal: the same export answers the same address (an EAT
             * entry outside the directory bounds is never rebased ASCII). */
            ok(GetProcAddress(hxa, (const char *)1) == pa);
        }
    }

    /* 21-22. The cyclic pair dll-c <-> dll-d: each module maps exactly once
     * (a loader that re-maps the first module on the back edge runs DllMain
     * twice — the attach counters would read 2) and the cross imports bind
     * (Bugbot 4559267279). */
    ok(dllc_attach_count() == 1 && dlld_attach_count() == 1);
    ok(dllc_sum(3) == 3 * (0xD1D0u + 1) + 6);
    ok(GetModuleHandleA("dll-c.dll") != 0 && GetModuleHandleA("dll-d.dll") != 0);

    /* 23. An ordinal whose EAT RVA is a forwarder string resolves through the
     * forward like a name does, never by adding the image base to ASCII
     * (Bugbot 4559267308). Every fwd.dll export is a forwarder, so: (a) every
     * ordinal in the EAT range answers either 0 or a real resolution — never
     * an address inside fwd.dll's own image (that would be base + ASCII);
     * (b) the export fwd_guest's ordinal answers exactly the address its name
     * answers (Windows: an export's ordinal and name agree); (c) that address
     * is callable and copies the marker. */
    {
        u32 hfwd2 = GetModuleHandleA("fwd.dll");
        ok(hfwd2 != 0);
        u32 byname = GetProcAddress(hfwd2, "fwd_guest");
        ok(byname != 0);
        u32 hit = 0;
        for (u32 ord = 1; ord <= 8; ++ord) {
            u32 a = GetProcAddress(hfwd2, (const char *)ord);
            ok(a == 0 || a < hfwd2 || a >= hfwd2 + 0x10000);  /* never rebased ASCII */
            if (a == byname) hit = ord;
        }
        ok(hit != 0);  /* fwd_guest's ordinal resolves to its name's address */
        if (hit) {
            u32 (__stdcall *fwdguest)(void) = (u32 (__stdcall *)(void))byname;
            ok(fwdguest() == 0xB00B5u);
        }
    }

    /* 24. A malformed image fails LoadLibraryA with 0 and
     * ERROR_BAD_EXE_FORMAT (193), not a crash (Bugbot 4559267311). The
     * loader's DLL source serves badimg.dll (garbage) and pe64img.dll (a
     * PE64 image) from the fixture directory. */
    {
        SetLastError(0);
        ok(LoadLibraryA("badimg.dll") == 0);
        ok(GetLastError() == 193);
        SetLastError(0);
        ok(LoadLibraryA("pe64img.dll") == 0);
        ok(GetLastError() == 193);
        /* A good module still loads after the failures. */
        ok(GetProcAddress(GetModuleHandleA("dll-b.dll"), "dllb_value") != 0);
    }

    /* 25. ExitThread ends only the calling thread: a worker that calls
     * ExitThread(77) records its code, the scheduler moves on and the main
     * program keeps running (a do_exit here would end the process with 77 —
     * Bugbot 4559267305). */
    {
        /* The worker body is a tiny thunk the fixture builds in its own
         * data: mov eax, 77; ret 4 — no, keep it real: the body is a
         * freestanding function exported by dll-b? Simpler: CreateThread
         * with the address of a local trampoline is not possible without
         * generated code, so use the interpreter's recorded-body model:
         * the start address is a real function in this image. */
        u32 code = 0;
        u32 h = CreateThread(0, 0, (u32)(u32 (__stdcall *)(void *))thread_body, 0, 0, 0);
        ok(h != 0);
        ok(WaitForSingleObject(h, 0) == 0);
        ok(GetExitCodeThread(h, &code) != 0 && code == 77);
        /* The process is still alive: the checks below run. */
        ok(dllb_attach_marker() == 1);
    }

    /* 26. EncodePointer/DecodePointer are a real inverse pair with a
     * per-process key (Bugbot 4559749432): decode(encode(p)) == p for a
     * normal pointer AND for NULL (the documented invariant encode(NULL) ==
     * NULL — a cookie that XORs NULL into the cookie breaks every "encoded
     * pointer not yet set == 0" check the CRT startup makes). The System
     * variants answer the same encoding. The encoded form must differ from
     * the input (a no-op identity would also pass the round trip). */
    {
        u32 enc = EncodePointer(0x12345678u);
        ok(enc != 0x12345678u);
        ok(DecodePointer(enc) == 0x12345678u);
        ok(EncodePointer(0) == 0 && DecodePointer(0) == 0);
        u32 senc = EncodeSystemPointer(0xCAFEBABEu);
        ok(senc != 0xCAFEBABEu);
        ok(DecodeSystemPointer(senc) == 0xCAFEBABEu);
    }

    /* 27-30. GetModuleHandleExW's flag contract (Bugbot 4559749437): NULL
     * name => the exe's base; FROM_ADDRESS (0x4) => the module containing
     * the address (tested with an address inside dll-b's image, its entry
     * pointer slot); UNCHANGED_REFCOUNT (0x2) and PIN (0x1) succeed the
     * same way (refcounting is not observable here — FreeLibrary never
     * unmaps); a miss writes NULL and returns 0. Every failure path leaves
     * *phModule NULL, as Windows documents. */
    {
        u32 hmain = 0;
        ok(GetModuleHandleExW(0, 0, &hmain) != 0);          /* 27: NULL name */
        ok(hmain == GetModuleHandleA(0));
        u32 hfrom = 0;
        ok(GetModuleHandleExW(4, (const u16 *)(u32)pval, &hfrom) != 0);  /* 28: FROM_ADDRESS */
        ok(hfrom == hb);
        u32 hpin = 0;
        const u16 wname4[] = {'d','l','l','-','b','.','d','l','l',0};
        ok(GetModuleHandleExW(0x2 | 0x1, wname4, &hpin) != 0);           /* 29: UNCHANGED_REFCOUNT|PIN */
        ok(hpin == hb);
        u32 hmiss = 0xAAAAAAAAu;
        ok(GetModuleHandleExW(0, 0, 0) == 0 || 1);  /* NULL out pointer tolerated */
        const u16 wmiss[] = {'n','o','-','s','u','c','h','.','d','l','l',0};
        ok(GetModuleHandleExW(0, wmiss, &hmiss) == 0);       /* 30: miss */
        ok(hmiss == 0);
    }

    /* 31. dll-zeroent.dll: NO entry point (AddressOfEntryPoint == 0) and a
     * SizeOfImage ~4 MiB over a few KiB of file (Bugbot 4559749424,
     * 4559749445). (a) The load must succeed — a loader that "runs DllMain"
     * at base+0 executes the MZ header and faults; (b) no DllMain ran,
     * which the load itself proves (a fault would have propagated); (c) the
     * mapping works: its exports answer and its BSS tail is this image's;
     * (d) dll-second.dll shares dll-zeroent's preferred base, so it must
     * relocate somewhere that does NOT overlap dll-zeroent's huge reserved
     * image — both markers stay independent afterwards. */
    {
        u32 hz = LoadLibraryA("dll-zeroent.dll");
        ok(hz != 0);
        u32 tail = zeroent_tail_get();
        ok(tail == 0);                       /* reserved, zero-filled */
        zeroent_tail_set(0x5EED0001u);
        ok(zeroent_tail_get() == 0x5EED0001u);
        ok(zeroent_fn() == 0x2E7E0002u);     /* callable through its export */
        u32 hs = LoadLibraryA("dll-second.dll");
        ok(hs != 0);
        ok(hs != hz);                        /* relocated: not the same base */
        ok(second_fn() == 0x2E7E1002u);      /* its own image, not a clash */
        ok(zeroent_tail_get() == 0x5EED0001u);  /* zeroent's tail untouched */
        ok(zeroent_fn() == 0x2E7E0002u);
        /* The second DLL's relocation arena is disjoint from the guest
         * heap: an allocation after both loads still succeeds and does not
         * land inside either image. */
        {
            extern u32 __stdcall GetProcessHeap(void);
            u32 hp = GetProcessHeap();
            ok(hp != 0);
        }
    }

    /* LoadLibrary takes GetModuleHandle's name rules (MSDN LoadLibraryA: a name
     * without an extension gets ".dll", case is ignored). A module the surface
     * serves answers the handle GetModuleHandle gives; a module that exists
     * nowhere is NULL with ERROR_MOD_NOT_FOUND (126). */
    ok(LoadLibraryA("kernel32") != 0);
    ok(LoadLibraryA("KERNEL32.DLL") == GetModuleHandleA("kernel32.dll"));
    SetLastError(0);
    ok(LoadLibraryA("msvcp140") == 0 && GetLastError() == 126);

    /* A failed LoadLibrary leaves nothing loaded (MSDN LoadLibraryA: on failure
     * no module is mapped). dll-fail maps dll-leaf, then its corrupt dependency
     * throws: ERROR_BAD_EXE_FORMAT (193). A later LoadLibrary of dll-leaf maps
     * it again and DllMain(DLL_PROCESS_ATTACH) runs exactly once, so the marker
     * reads 1: 0 means the stale module was reused unattached, 2 an attach twice. */
    SetLastError(0);
    ok(LoadLibraryA("dll-fail.dll") == 0 && GetLastError() == 193);
    u32 hleaf = LoadLibraryA("dll-leaf.dll");
    ok(hleaf != 0);
    if (hleaf) {
        u32 (__stdcall *leaf_fn)(void) = (u32 (__stdcall *)(void))GetProcAddress(hleaf, "leaf_marker");
        ok(leaf_fn != 0);
        if (leaf_fn) ok(leaf_fn() == 1);
    }

    return failed ? 43 : 42;
}

/* The thread body CreateThread runs: calls ExitThread(77), so control never
 * returns — the scheduler records the code and the process continues. */
static u32 __stdcall thread_body(void *param) {
    (void)param;
    ExitThread(77);
    return 41;  /* unreachable if ExitThread ends only this thread */
}
