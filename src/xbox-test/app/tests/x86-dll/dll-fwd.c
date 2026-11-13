/* SPDX-License-Identifier: GPL-3.0-or-later */
/* x86 stage 3: forwarder DLL. Freestanding i686 PE32, no CRT. Its exports are
 * pure forwarders the linker records verbatim in the export table (the .def
 * is passed to ld directly, unreferenced, so ld emits forwarder entries):
 * one to msvcrt (lowercase), one to NTDLL in upper case (export matching is
 * case-insensitive on Windows), one to kernel32, an ordinal forward
 * ("ntdll.#123"), and one to a module that does not exist. The exe resolves
 * each through GetProcAddress — the same one-hop resolution the import
 * binder uses on an IAT slot — and calls the function forwards through the
 * returned guest address. */
int __stdcall DllMain(void *hinst, unsigned reason, void *reserved) {
    (void)hinst; (void)reserved;
    (void)reason;
    return 1;
}
