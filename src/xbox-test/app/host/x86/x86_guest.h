// SPDX-License-Identifier: GPL-3.0-or-later
// Guest OS surface for the PE32 interpreter (milestone 1/2): the kernel32 and
// msvcrt calls a real i686 CRT startup and a console program make. Every guest
// pointer is bounds- and permission-checked through Memory, so a bad pointer is
// a Fault and never a host access. Buffers the guest hands us are copied in and
// out; no guest address is ever treated as a host pointer.
//
// Only the (dll, symbol) pairs registered here are served. Anything else stays a
// named UnresolvedImport Fault, so an unimplemented API is reported by name
// instead of being silently stubbed. That is deliberate: a milestone subset is
// never mistaken for general Win32 support.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include "x86_win32.h"
namespace x86 {
// Loaded guest DLLs, shared between the loader and the Guest surface. Kept in
// x86_pe.h's Dll form so GetProcAddress reads the same export tables the
// import binder filled.
struct Dll;

// ---- guest heap: first-fit free list over a lazily mapped guest arena ----
struct GuestHeap {
    Memory& mem;
    uint32_t base, mapped_end = 0, top = 0;
    struct Block { uint32_t addr, size; bool free; };
    std::vector<Block> blocks;
    GuestHeap(Memory& m, uint32_t at) : mem(m), base(at) {}
    void map_upto(uint32_t end) {
        if (end <= mapped_end) return;
        uint32_t a = mapped_end ? mapped_end : base;
        uint32_t want = ((end - a) + 0xffffu) & ~0xffffu;
        mem.map(a, want, PROT_R | PROT_W);
        mapped_end = a + want;
    }
    uint32_t alloc(uint32_t n) {
        if (!n) n = 1;
        n = (n + 7u) & ~7u;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (!blocks[i].free || blocks[i].size < n) continue;
            uint32_t a = blocks[i].addr, sz = blocks[i].size;
            if (sz >= n + 16) {
                blocks[i].size = n; blocks[i].free = false;
                blocks.insert(blocks.begin() + long(i) + 1, Block{a + n, sz - n, true});
            } else blocks[i].free = false;
            return a;
        }
        if (!top) top = base;
        map_upto(top + n);
        uint32_t a = top;
        blocks.push_back(Block{a, n, false});
        top = a + n;
        return a;
    }
    uint32_t size_of(uint32_t addr) const {
        for (const Block& b : blocks) if (b.addr == addr) return b.size;
        return 0;
    }
    void release(uint32_t addr) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].addr != addr) continue;
            blocks[i].free = true;
            for (size_t j = i + 1; j < blocks.size() && blocks[j].free; ++j) {  // coalesce forward
                blocks[i].size += blocks[j].size; blocks.erase(blocks.begin() + long(j)); --j;
            }
            if (i && blocks[i - 1].free) {  // coalesce backward
                blocks[i - 1].size += blocks[i].size; blocks.erase(blocks.begin() + long(i));
            }
            return;
        }
    }
    uint32_t realloc(uint32_t addr, uint32_t n) {
        uint32_t old = size_of(addr);
        uint32_t p = alloc(n);
        if (old) mem.write_bytes(p, mem.read_bytes(addr, old < n ? old : n).data(), old < n ? old : n);
        if (addr) release(addr);
        return p;
    }
};

// ---- the guest process surface ----
struct Guest {
    Memory& mem; Host& host; Registry& reg;
    GuestHeap heap;
    std::string exe_path, cmdline;
    std::map<std::string, uint32_t> modules;   // lowercase module name -> guest base
    std::vector<uint32_t> atexit_fns;
    std::vector<uint8_t> iob;                  // 3 FILE structs, guest-visible
    uint32_t iob_addr = 0x7FFD0000;
    uint32_t initenv = 0, commode = 0, fmode = 0, mb_cur_max = 0, errno_ptr = 0;
    uint32_t startupinfo = 0, cmdline_ptr = 0, env_ptr = 0, module_name_ptr = 0;
    uint32_t image_base = 0;
    // The exe's implicit TLS directory (the runner copies it from its Image, so
    // each new thread gets its own block) and the TEB of the running thread,
    // mirroring Cpu.fs_base for host helpers that have no Cpu.
    Image::Tls exe_tls;
    uint32_t cur_teb = TEB_ADDR;
    // EncodePointer/DecodePointer (and the System variants): the security
    // cookie; VC/UCRT startup encodes every function pointer it stores and
    // decodes it on use, so the pair must be a real inverse or a decoded
    // pointer is garbage. The key is per-process ("random-looking" to the
    // guest) and the encoding is XOR, so decode(encode(p)) == p and
    // encode(NULL) == decode(NULL) == NULL hold exactly as the documented
    // pointer == NULL exactly as the documented invariant requires (a plain
    // XOR answers the cookie for NULL and breaks every "encoded pointer not
    // yet set == 0" check the CRT startup makes). A conditional XOR is still
    // a real inverse pair (Bugbot 4559749432).
    uint32_t pointer_cookie = 0x3FD2A71Eu ^ uint32_t(reinterpret_cast<uintptr_t>(this));
    uint32_t encode_ptr(uint32_t p) const { return p ? p ^ pointer_cookie : 0; }
    unsigned heap_serial = 0;
    std::map<uint32_t, uint32_t> heap_handles;  // handle -> arena base
    std::map<uint32_t, uint32_t> virtual_alloc; // guest addr -> size
    uint32_t virtual_top = 0x60000000u;  // above the DLL relocation arena (0x38000000-0x60000000)
    unsigned next_tls = 0, thread_serial = 0;
    std::map<uint32_t, uint32_t> sections;  // handle -> size in bytes
    uint32_t section_serial = 0, section_top = 0x64000000u;
    // Thread bodies CreateThread recorded (handle, start, param); each body
    // runs at the guest's first blocking wait (see the CreateThread comment).
    // The handle rides along so the body's exit code can be recorded under the
    // handle GetExitCodeThread is called with.
    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> pending_threads;
    // ExitThread ends only the calling thread (MSDN: "Ends the calling thread"),
    // so a body that calls it is unwound to run_pending_threads by a ThreadExit
    // exception and the guest code after the call never runs. Outside a body
    // (the main thread) ExitThread only records its code: the process keeps
    // going (Bugbot 4559267305). ExitProcess/do_exit ends the process.
    struct ThreadExit {};
    bool in_thread_body = false;
    uint32_t thread_exit_code = 0;
    std::map<uint32_t, uint32_t> thread_exit_codes;  // handle -> exit code
    std::map<uint32_t, uint32_t> thread_ids;         // handle -> thread identifier
    // Guest DLLs loaded by the process (lowercase module name -> Dll) and the
    // order they were mapped in (DllMain PROCESS_DETACH runs in reverse).
    // `dll_source` supplies DLL bytes by module name; empty = not found.
    std::map<std::string, Dll> dlls;
    std::vector<std::string> dll_order;
    DllSource dll_source;
    // DllMain absolute entry addresses by module name, run by the Cpu after
    // the thunk page is mapped; the loader records them, run_dll_mains
    // dispatches. (Image::entry is already absolute: base + entry RVA.)
    std::map<std::string, uint32_t> dll_entries;
    // The heap arena starts at 0x08000000: above the exe (0x400000) and its
    // stack, below the 0x10000000 base mingw/MSVC give every DLL by default —
    // a DLL at its preferred base and later heap growth must never fight over
    // the same pages (mem.map throws on overlap; Bugbot 4559749442). The
    // 128 MiB headroom is the heap's before it could reach the first DLL.
    Guest(Memory& m, Host& h, Registry& r) : mem(m), host(h), reg(r), heap(m, 0x08000000u) {}
    void register_all();
    // Module names are case-insensitive on Windows, and GetModuleHandleA
    // lowercases its lookup, so every insert goes through here: a mixed-case
    // executable name ("Proteus.exe") must still be found by the guest.
    void add_module(std::string name, uint32_t base) {
        for (auto& ch : name) ch = char(std::tolower((unsigned char)ch));
        modules[name] = base;
    }
    // --- guest-memory helpers ---
    std::string str(uint32_t p) const { return p ? mem.read_cstr(p) : std::string(); }
    std::u16string wstr(uint32_t p) const {
        std::u16string s;
        if (p) for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; s.push_back(w); }
        return s;
    }
    uint32_t put_str(const std::string& s) {
        uint32_t p = heap.alloc(uint32_t(s.size() + 1));
        mem.write_bytes(p, s.c_str(), uint32_t(s.size() + 1));
        return p;
    }
    uint32_t put_wstr(const std::u16string& s) {
        uint32_t p = heap.alloc(uint32_t((s.size() + 1) * 2));
        for (size_t i = 0; i < s.size(); ++i) { mem.w8(p + uint32_t(i * 2), uint8_t(s[i])); mem.w8(p + uint32_t(i * 2 + 1), uint8_t(s[i] >> 8)); }
        mem.w8(p + uint32_t(s.size() * 2), 0); mem.w8(p + uint32_t(s.size() * 2 + 1), 0);
        return p;
    }
    void copy_out(uint32_t dst, const void* src, uint32_t n) { if (dst && n) mem.write_bytes(dst, src, n); }
    void copy_in(void* dst, uint32_t src, uint32_t n) { if (src && n) { auto b = mem.read_bytes(src, n); std::memcpy(dst, b.data(), n); } }
    uint32_t last_error() const { return mem.r32(cur_teb + 0x34); }
    void set_last_error(uint32_t v) { mem.w32(cur_teb + 0x34, v); }
    // A FILE* is one of our three iob entries; anything else is a bad handle.
    // The index is the C stream number: 0 stdin, 1 stdout, 2 stderr.
    int file_index(uint32_t f) const {
        if (f >= iob_addr && f < iob_addr + 3 * 32 && (f - iob_addr) % 32 == 0) return int((f - iob_addr) / 32);
        return -1;
    }
    uint32_t file_ptr(int i) const { return iob_addr + uint32_t(i) * 32; }
    // `which` is a C stream number (0/1/2), the same one file_index returns, so
    // stderr reaches host.err on both the FILE* and the WriteFile path.
    void emit(int which, const std::string& s) {
        (which == 2 ? host.err : host.out) += s;
        if (host.tee) host.tee(s);
    }
    void write_file(int which, const std::string& s) { emit(which, s); }
    // printf-family core. `next` yields the next 32-bit argument slot: the guest
    // stack for printf/fprintf/sprintf, or a walk over the guest va_list for the
    // v* forms (on i386 a va_list is a char* to the first vararg). The i386
    // calling convention aligns every argument to 4 bytes only: a double is two
    // consecutive slots wherever they fall, with no padding before it (mingw's
    // va_arg and msvcrt's own printf both read it that way; measured, not
    // assumed). A `long long`/`__int64` is likewise two slots, low dword first.
    struct FmtArgs { std::function<uint32_t()> next; };
    std::string format(FmtArgs a, const std::string& fmt);
    // Copy a formatted result into guest memory. A negative `cap` means
    // unbounded (sprintf); 0 writes nothing (snprintf with a zero size). With
    // append_nul the result is NUL-terminated inside cap. Returns the full
    // length the format would have produced, as C99 snprintf does.
    int store(const std::string& s, uint32_t out_ptr, int cap, bool append_nul);
    std::string format_stack(Cpu& c, const std::string& fmt, unsigned first_arg) {
        unsigned ai = first_arg;
        return format({[&c, &ai]() { return c.arg(ai++); }}, fmt);
    }
    std::string format_va(const std::string& fmt, uint32_t& ap) {
        return format({[this, &ap]() { uint32_t v = mem.r32(ap); ap += 4; return v; }}, fmt);
    }
    // sprintf/_snprintf/snprintf take their varargs on the stack, not a va_list:
    // only the v* forms walk a pointer.
    int format_stack_to(Cpu& c, uint32_t fmt_ptr, unsigned first_arg, uint32_t out_ptr, int cap, bool append_nul) {
        return store(format_stack(c, str(fmt_ptr), first_arg), out_ptr, cap, append_nul);
    }
    int format_va_to(uint32_t fmt_ptr, uint32_t argp, uint32_t out_ptr, int cap, bool append_nul) {
        return store(format_va(str(fmt_ptr), argp), out_ptr, cap, append_nul);
    }
    // SYSTEMTIME is eight WORDs (year, month, dayOfWeek, day, hour, minute,
    // second, milliseconds), 16 bytes total, so every field is a 2-byte store.
    void fill_system_time(uint32_t p) {
        if (!p) return;
        static const uint16_t f[8] = {2026, 11, 0, 9, 12, 0, 0, 0};
        for (unsigned i = 0; i < 8; ++i) mem.w(p + i * 2, f[i], 2);
    }
    void run_atexit(Cpu& c);
    void do_exit(Cpu& c, uint32_t code);
    // Runs every thread body CreateThread recorded, in creation order, before
    // the blocking wait (or yield) that triggered it proceeds. Each body runs on
    // its own TEB (a page of its own, fs_base pointing at it), so the SEH chain,
    // stack bounds, thread id, LastError and TlsSlots are that thread's, as
    // Windows gives every thread its own TEB. The exe's implicit TLS is per
    // thread too: the TEB's ThreadLocalStoragePointer (fs:[0x2C]) addresses a
    // block copied from the image's TLS template, so __declspec(thread) data is
    // separate per thread (MSDN "Using Thread Local Storage"). TLS callbacks run
    // with DLL_THREAD_ATTACH before the body and DLL_THREAD_DETACH after it, on
    // the thread's own TEB; the TEB and block are freed when the thread ends.
    // The main thread's TEB is restored afterwards.
    static constexpr uint64_t kThreadBudget = 100ull * 1000 * 1000;  // instructions per thread body run
    static constexpr uint32_t kThreadRegionBase = 0x7E000000u;      // thread TEBs: above the DLL and section arenas
    struct ThreadTeb { uint32_t base, size; };
    ThreadTeb map_thread_teb(Cpu& c, uint32_t tid) {
        uint32_t tls_bytes = exe_tls.present ? 16 + (exe_tls.end - exe_tls.start) + exe_tls.zero_fill : 0;
        uint32_t size = Memory::PAGE + (tls_bytes + Memory::PAGE - 1) / Memory::PAGE * Memory::PAGE;
        uint32_t base = kThreadRegionBase;
        while (!c.mem.free_range(base, size)) {
            base += 0x10000u;
            if (base >= 0x7FF00000u) throw std::runtime_error("no address space for a thread TEB");
        }
        c.mem.map(base, size, PROT_R | PROT_W);
        uint32_t main = cur_teb;
        c.mem.w32(base + 0x00, 0xFFFFFFFFu);                    // ExceptionList: empty chain
        c.mem.w32(base + 0x04, c.mem.r32(main + 0x04));         // StackBase: the process stack
        c.mem.w32(base + 0x08, c.mem.r32(main + 0x08));         // StackLimit
        c.mem.w32(base + 0x18, base);                           // Self
        c.mem.w32(base + 0x20, c.mem.r32(main + 0x20));         // ClientId.UniqueProcess
        c.mem.w32(base + 0x24, tid);                            // ClientId.UniqueThread
        c.mem.w32(base + 0x30, c.mem.r32(main + 0x30));         // ProcessEnvironmentBlock
        // LastError (0x34) and TlsSlots (0xE10..) start zeroed: this thread's own.
        if (exe_tls.present) {
            uint32_t slot = base + Memory::PAGE, block = slot + 16;
            uint32_t raw = exe_tls.end - exe_tls.start;
            c.mem.w32(slot, block);                             // ThreadLocalStoragePointer[index 0]
            c.mem.w32(base + 0x2C, slot);
            for (uint32_t i = 0; i < raw; ++i) c.mem.w8(block + i, c.mem.r8(exe_tls.start + i));  // template copy; the rest is zero
        }
        return ThreadTeb{base, size};
    }
    // The exe's TLS callbacks for `reason` (DLL_THREAD_ATTACH 2 / DLL_THREAD_DETACH 3),
    // called as (image base, reason, NULL) like the loader does.
    void tls_callbacks_call(Cpu& c, uint32_t reason) {
        if (!exe_tls.present) return;
        for (uint32_t p = exe_tls.callbacks; p; p += 4) {
            uint32_t fn = c.mem.r32(p);
            if (!fn) break;
            c.call(fn, {image_base, reason, 0});
        }
    }
    // The worker's context lives exactly as long as this object: the worker's TEB
    // is the current thread, its in-body flag holds and its instruction budget
    // applies. The destructor restores the main thread's state, so every exit
    // path (normal return, ExitThread unwind, host exception) restores it once and
    // no worker TEB is ever left active.
    struct ThreadScope {
        Guest& g; Cpu& c;
        uint32_t main_teb, main_fs; uint64_t main_limit; bool main_in_body;
        ThreadScope(Guest& gg, Cpu& cc, uint32_t teb, uint64_t budget)
            : g(gg), c(cc), main_teb(gg.cur_teb), main_fs(cc.fs_base), main_limit(cc.limit), main_in_body(gg.in_thread_body) {
            g.cur_teb = teb; c.fs_base = teb; g.in_thread_body = true;
            // Each body gets `budget` instructions per run. The interpreter is
            // single-threaded, so a body that never returns (a worker that waits on
            // the main thread) must fail the run by name instead of hanging the host.
            if (c.executed + budget < c.limit) c.limit = c.executed + budget;
        }
        ~ThreadScope() { g.cur_teb = main_teb; c.fs_base = main_fs; c.limit = main_limit; g.in_thread_body = main_in_body; }
    };
    // Order per thread: DLL_THREAD_ATTACH callbacks, the body, DLL_THREAD_DETACH
    // callbacks (all on the thread's own TEB), then the TEB and TLS block are
    // unmapped, and the scope restores the main thread. ExitThread in the body
    // ends the thread; ExitThread in a detach callback ends only that callback:
    // the thread is already exiting and keeps its body's exit code.
    void run_pending_threads(Cpu& c) {
        while (!pending_threads.empty()) {
            auto t = pending_threads.front(); pending_threads.erase(pending_threads.begin());
            auto id = thread_ids.find(std::get<0>(t));
            ThreadTeb th = map_thread_teb(c, id != thread_ids.end() ? id->second : 0);
            ThreadScope scope(*this, c, th.base, kThreadBudget);
            uint32_t saved_regs[8]; std::memcpy(saved_regs, c.reg, sizeof saved_regs);
            uint32_t saved_eip = c.eip, code = 0;
            try {
                tls_callbacks_call(c, 2);  // DLL_THREAD_ATTACH
                code = c.call(std::get<1>(t), {std::get<2>(t)});
            }
            catch (const ThreadExit&) {
                // Drop the body's frames: restore the scheduler's registers so
                // the wait that started the body returns to its caller. The
                // body's exit code is what ExitThread was given (MSDN ExitThread).
                std::memcpy(c.reg, saved_regs, sizeof saved_regs); c.eip = saved_eip;
                code = thread_exit_code;
            }
            catch (...) {
                if (scope.main_limit > c.limit && c.executed > c.limit) {
                    emit(2, "x86: thread body did not return within its instruction budget\n");
                    throw Fault(FaultKind::InstructionLimit, c.eip, std::get<1>(t), "thread body exceeded its instruction budget");
                }
                throw;
            }
            // The exit code is the body's return value (EAX) or the ExitThread code:
            // a returned body is finished too, so GetExitCodeThread answers it, not
            // STILL_ACTIVE (MSDN GetExitCodeThread). Recorded under the HANDLE.
            thread_exit_codes[std::get<0>(t)] = code;
            // DLL_THREAD_DETACH on the thread's own TEB, before its memory goes.
            std::memcpy(saved_regs, c.reg, sizeof saved_regs); saved_eip = c.eip;
            try { tls_callbacks_call(c, 3); }
            catch (const ThreadExit&) { std::memcpy(c.reg, saved_regs, sizeof saved_regs); c.eip = saved_eip; }
            c.mem.unmap(th.base, th.size);
        }
    }
    // Guest DLL loading (stage 3). `run_dll_mains` runs each loaded DLL's
    // DllMain(DLL_PROCESS_ATTACH) in load order, like the Windows loader, and
    // is called by the runner after init_process. A DLL without DllMain is
    // skipped; a DllMain that faults propagates (never silently ignored).
    void run_dll_mains(Cpu& c, uint32_t reason) {
        (void)reason;  // only DLL_PROCESS_ATTACH (1) is dispatched today
        for (const auto& name : dll_order) {
            if (attached[name]) continue;
            attached[name] = true;
            auto e = dll_entries.find(name);
            if (e == dll_entries.end()) continue;
            uint32_t base = dlls[name].image.base;
            c.call(e->second, {base, static_cast<uint32_t>(1 /*DLL_PROCESS_ATTACH*/), 0});
        }
    }
    // LoadLibrary core: maps `file` as a guest DLL (or bumps the refcount of a
    // loaded one), binds imports recursively, records DllMain, and returns the
    // guest base, or 0 with ERROR_MOD_NOT_FOUND / ERROR_BAD_EXE_FORMAT. DllMain
    // runs here when the CPU is live (a LoadLibrary from guest code); the
    // loader path passes cpu == nullptr and defers to run_dll_mains.
    // Modules the OS surface itself serves (kernel32, msvcrt, ntdll) have no
    // image: LoadLibrary answers a pseudo-handle and GetProcAddress on it
    // binds the same host thunk the import binder would have, as Windows
    // answers for its own modules.
    std::map<std::string, uint32_t> os_modules;  // lowercase dll -> pseudo base
    static constexpr uint32_t kOsModuleBase = 0x68000000u, kOsModuleStride = 0x10000u;
    // kernelbase exports nothing here (the CRT probes it and falls back when
    // the lookup misses), but LoadLibrary must succeed, as on Windows.
    bool registry_serves(const std::string& dll) const {
        if (dll == "kernelbase.dll") return true;
        for (auto& kv : reg.map) if (kv.first.first == dll) return true;
        return false;
    }
    // Modules every process has from its start, served without an image.
    bool os_served(const std::string& dll) const {
        return dll == "kernel32.dll" || dll == "ntdll.dll" || dll == "msvcrt.dll" || registry_serves(dll);
    }
    uint32_t os_module_handle(const std::string& dll) {
        auto it = os_modules.find(dll);
        if (it != os_modules.end()) return it->second;
        uint32_t h = kOsModuleBase + uint32_t(os_modules.size()) * kOsModuleStride;
        os_modules[dll] = h;
        add_module(dll, h);  // GetModuleHandleA("ntdll.dll") answers it too
        return h;
    }
    // The GetModuleHandleA/W/ExW lookup (MSDN GetModuleHandleA): case-insensitive;
    // a name without an extension gets ".dll" appended, and a trailing "." means
    // the name has no extension. It answers a loaded image, the main module, or
    // an OS-served module (kernel32/ntdll/msvcrt/kernelbase are in the process
    // from its start, so they answer without LoadLibrary, with the same stable
    // handle LoadLibrary returns). A miss is NULL with ERROR_MOD_NOT_FOUND.
    // The one name rule GetModuleHandle and LoadLibrary share (module_key).
    static std::string module_key(std::string n) {
        size_t sep = n.find_last_of("\\/");  // a path names its file (host and guest modules alike)
        if (sep != std::string::npos) n = n.substr(sep + 1);
        for (auto& ch : n) ch = char(std::tolower((unsigned char)ch));
        if (!n.empty() && n.back() == '.') n.pop_back();
        else if (n.find('.') == std::string::npos) n += ".dll";
        return n;
    }
    uint32_t module_by_name(const std::string& name) {
        std::string n = module_key(name);
        auto it = modules.find(n);
        if (it != modules.end()) return it->second;
        auto d = dlls.find(n);
        if (d != dlls.end()) return d->second.image.base;
        if (os_served(n)) return os_module_handle(n);
        set_last_error(126);  // ERROR_MOD_NOT_FOUND
        return 0;
    }
    // Resolves an export of an OS-served module (kernel32/msvcrt/ntdll) that a
    // guest DLL's forwarding export names ("ntdll.memcpy"): a host thunk, or a
    // registered data export's address. The forwarder's module part carries no
    // ".dll" (Windows forward strings are "ntdll.memcpy", "NTDLL.memcpy"), so
    // the name is lowercased and suffixed before the registry lookup — the
    // registry's keys are lowercase full module names. An ordinal forward
    // ("ntdll.#123") is answered 0: the surface registers no ordinals, and
    // guessing one would bind a different function silently. Returns 0 when
    // the surface has none (the IAT slot stays an unresolved stub that faults
    // by name); resolution stays one hop, so no chain can recurse here.
    uint32_t os_export_addr(ThunkTable& table, const std::string& dll, const std::string& sym) {
        std::string l = dll; for (auto& ch : l) ch = char(std::tolower((unsigned char)ch));
        std::string s = sym; for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
        if (l.rfind(".dll") != l.size() - 4) l += ".dll";
        if (s.rfind("#", 0) == 0) return 0;  // ordinal forward: no ordinal surface
        auto it = reg.map.find({l, s});
        if (it != reg.map.end()) return table.add(it->second);
        auto dt = reg.data.find({l, s});
        if (dt != reg.data.end()) return dt->second;
        return 0;
    }
    uint32_t load_guest_dll(Cpu* cpu, const std::string& requested, ThunkTable& table) {
        std::string lower = module_key(requested);
        auto it = dlls.find(lower);
        if (it != dlls.end()) { it->second.refs++; return it->second.image.base; }
        if (os_served(lower)) return os_module_handle(lower);
        if (!dll_source) { set_last_error(126); return 0; }  // ERROR_MOD_NOT_FOUND
        std::vector<uint8_t> bytes = dll_source(lower);
        if (bytes.empty()) { set_last_error(126); return 0; }
        try {
            load_dll(mem, table, reg, bytes, dll_source, dlls, 0, &dll_order,
                     [this, &table](const std::string& dll, const std::string& sym) {
                         return os_export_addr(table, dll, sym);
                     },
                     lower);
        } catch (const std::exception&) {
            // A malformed image (garbage file, PE64) is a failed load, not a
            // crash: LoadLibrary answers 0 with ERROR_BAD_EXE_FORMAT (193),
            // as LoadLibraryA documents (Bugbot 4559267311).
            set_last_error(193);
            return 0;
        }
        // load_dll reserved and completed the slot in `dlls` itself (cycles
        // reuse it): read the base from the table instead of copying the Dll.
        uint32_t base = dlls[lower].image.base;
        dll_order.push_back(lower);
        add_module(lower, base);  // GetModuleHandleW must find LoadLibrary'd DLLs
        // AddressOfEntryPoint == 0 means no entry point (a resource-only or
        // data DLL): `entry` is absolute (base + RVA) and always nonzero, so
        // the decision tests the RVA (Bugbot 4559749424).
        if (dlls[lower].image.entry_rva) dll_entries[lower] = dlls[lower].image.entry;  // absolute
        // DllMain(DLL_PROCESS_ATTACH) for every module this call newly mapped,
        // dependencies first (load_dll pushed them onto dll_order in that
        // order), then the requested module — as the Windows loader attaches
        // each module as it maps it (Bugbot 4559267301).
        if (cpu) run_new_dll_mains(*cpu);
        return base;
    }
    // Runs DllMain(DLL_PROCESS_ATTACH) for every module of `dll_order` that has
    // an entry and has not been attached yet, in load order (dependencies
    // first). `attached` marks the dispatch so a later LoadLibrary of a
    // dependency does not re-attach it (a module attaches exactly once per
    // process, as the Windows loader guarantees).
    std::map<std::string, bool> attached;
    void run_new_dll_mains(Cpu& c) {
        for (const auto& name : dll_order) {
            if (attached[name]) continue;
            attached[name] = true;
            auto e = dll_entries.find(name);
            if (e == dll_entries.end()) continue;
            uint32_t base = dlls[name].image.base;
            c.call(e->second, {base, static_cast<uint32_t>(1 /*DLL_PROCESS_ATTACH*/), 0});
        }
    }
    // GetProcAddress core: `name_ptr` is a string pointer or an ordinal in the
    // MAKEINTRESOURCE form (a value below 65536 is an ordinal, as Windows
    // answers). Returns the guest address, or 0 with ERROR_PROC_NOT_FOUND.
    uint32_t get_proc_address(uint32_t module, uint32_t name_ptr, ThunkTable& table) {
        for (auto& kv : dlls) {
            if (kv.second.image.base != module) continue;
            uint32_t a = 0;
            if (name_ptr < 0x10000u) {  // ordinal form: the export address table by index
                // An ordinal's EAT RVA can be a forwarder string (inside the
                // export directory range): resolve it like a name does, never
                // rebase ASCII (Bugbot 4559267308).
                auto fo = kv.second.image.forwarded_by_ord.find(name_ptr);
                if (fo != kv.second.image.forwarded_by_ord.end()) {
                    size_t dot = fo->second.find('.');
                    if (dot != std::string::npos) {
                        std::string target = forward_module_key(fo->second.substr(0, dot)), tsym = fo->second.substr(dot + 1);
                        auto m = dlls.find(target);
                        if (m != dlls.end()) a = dll_export_addr(m->second, tsym, dlls,
                                                [this, &table](const std::string& dll, const std::string& s) {
                                                    return os_export_addr(table, dll, s);
                                                });
                        else a = os_export_addr(table, target, tsym);
                    }
                } else for (auto& o : kv.second.image.ordinals)
                    if (o.first == name_ptr) { a = kv.second.image.base + o.second; break; }
            } else {
                std::string sym = str(name_ptr);
                for (auto& ch : sym) ch = char(std::tolower((unsigned char)ch));
                a = dll_export_addr(kv.second, sym, dlls,
                                    [this, &table](const std::string& dll, const std::string& s) {
                                        return os_export_addr(table, dll, s);
                                    });
            }
            if (!a) set_last_error(127);  // ERROR_PROC_NOT_FOUND
            return a;
        }
        // An OS-served module: the export is a host thunk, added on demand —
        // the same stub the import binder installs for it. A data export
        // (msvcrt's __initenv and friends) answers the variable's address.
        for (auto& kv : os_modules) {
            if (kv.second != module) continue;
            if (name_ptr >= 0x10000u) {
                std::string sym = Registry::key_sym(str(name_ptr));
                auto it = reg.map.find({kv.first, sym});
                if (it != reg.map.end()) return table.add(it->second);
                auto dt = reg.data.find({kv.first, sym});
                if (dt != reg.data.end()) return dt->second;
                // Also answer the undecorated stdcall name for a decorated
                // registry entry (a game may ask either form).
                std::string dec = sym + "@";
                for (auto& e : reg.map)
                    if (e.first.first == kv.first && e.first.second.rfind(dec, 0) == 0)
                        return table.add(e.second);
            }
            set_last_error(127);  // ERROR_PROC_NOT_FOUND
            return 0;
        }
        set_last_error(126);  // ERROR_MOD_NOT_FOUND: not a module we track
        return 0;
    }
};

// ---- number formatting shared by the printf family ----
namespace guest_fmt {
inline std::string pad(const std::string& body, int width, bool left, char fill) {
    if (width <= int(body.size())) return body;
    std::string p(size_t(width - int(body.size())), fill);
    return left ? body + p : p + body;
}
inline std::string int_to(uint64_t v, int base, bool upper, bool neg) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.push_back(d[v % uint64_t(base)]); v /= uint64_t(base); } while (v);
    if (neg) s.push_back('-');
    return std::string(s.rbegin(), s.rend());
}
inline std::string uint_to(uint64_t v, int base, bool upper) {
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    do { s.push_back(d[v % uint64_t(base)]); v /= uint64_t(base); } while (v);
    return std::string(s.rbegin(), s.rend());
}
}

inline std::string Guest::format(FmtArgs a, const std::string& fmt) {
    auto& next = a.next;
    std::string out;
    size_t i = 0;
    while (i < fmt.size()) {
        char ch = fmt[i++];
        if (ch != '%') { out.push_back(ch); continue; }
        if (i < fmt.size() && fmt[i] == '%') { out.push_back('%'); ++i; continue; }
        bool left = false, plus = false, space = false, alt = false, zero = false;
        for (;;) {
            if (i >= fmt.size()) break;
            char f = fmt[i];
            if (f == '-') left = true; else if (f == '+') plus = true; else if (f == ' ') space = true;
            else if (f == '#') alt = true; else if (f == '0') zero = true; else break;
            ++i;
        }
        int width = 0;
        if (i < fmt.size() && fmt[i] == '*') { width = int(next()); if (width < 0) { left = true; width = -width; } ++i; }
        else while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') width = width * 10 + (fmt[i++] - '0');
        int prec = -1;
        if (i < fmt.size() && fmt[i] == '.') {
            ++i; prec = 0;
            if (i < fmt.size() && fmt[i] == '*') { prec = int(next()); ++i; if (prec < 0) prec = -1; }
            else while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') prec = prec * 10 + (fmt[i++] - '0');
        }
        enum { LEN_NONE, LEN_H, LEN_HH, LEN_L, LEN_LL, LEN_LDBL, LEN_Z, LEN_I64 } len = LEN_NONE;
        if (i < fmt.size()) {
            char l = fmt[i];
            if (l == 'h') { ++i; if (i < fmt.size() && fmt[i] == 'h') { ++i; len = LEN_HH; } else len = LEN_H; }
            else if (l == 'l') { ++i; if (i < fmt.size() && fmt[i] == 'l') { ++i; len = LEN_LL; } else len = LEN_L; }
            else if (l == 'L') { ++i; len = LEN_LDBL; }
            else if (l == 'z') { ++i; len = LEN_Z; }
            else if (l == 'I') {
                ++i;
                if (i + 1 < fmt.size() && fmt[i] == '6' && fmt[i + 1] == '4') { i += 2; len = LEN_I64; }
                else if (i + 1 < fmt.size() && fmt[i] == '3' && fmt[i + 1] == '2') { i += 2; len = LEN_NONE; }
                else if (i < fmt.size() && fmt[i] == 'd') { len = LEN_Z; }  // %Id
            }
        }
        if (i >= fmt.size()) break;
        char conv = fmt[i++];
        switch (conv) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            // On Win32 `long` is 32 bits, so %l is one slot; %ll and %I64 are
            // two (low dword first). %z is size_t, 32 bits on i386.
            uint64_t v = next();
            bool wide = len == LEN_LL || len == LEN_I64;
            if (wide) v |= uint64_t(next()) << 32;
            else v &= 0xffffffffu;
            if (len == LEN_H) v &= 0xffffu;
            if (len == LEN_HH) v &= 0xffu;
            bool neg = false; std::string body;
            if (conv == 'd' || conv == 'i') {
                int64_t s = wide ? int64_t(v) : int64_t(int32_t(uint32_t(v)));
                if (len == LEN_H) s = int16_t(v);
                if (len == LEN_HH) s = int8_t(v);
                neg = s < 0;
                body = guest_fmt::uint_to(neg ? uint64_t(0) - uint64_t(s) : uint64_t(s), 10, false);
            } else body = guest_fmt::uint_to(v, conv == 'o' ? 8 : conv == 'u' ? 10 : 16, conv == 'X');
            if (prec >= 0 && int(body.size()) < prec) body = std::string(size_t(prec - int(body.size())), '0') + body;
            if (alt) {
                if (conv == 'x') body = "0x" + body;
                else if (conv == 'X') body = "0X" + body;
                else if (conv == 'o' && (body.empty() || body[0] != '0')) body = "0" + body;
            }
            if (neg) body = "-" + body;
            else if (plus && (conv == 'd' || conv == 'i')) body = "+" + body;
            else if (space && (conv == 'd' || conv == 'i')) body = " " + body;
            out += guest_fmt::pad(body, width, left, zero && !left && prec < 0 ? '0' : ' ');
            break; }
        case 'c': { char v = char(next()); out += guest_fmt::pad(std::string(1, v), width, left, ' '); break; }
        case 's': {
            uint32_t p = next(); std::string s = p ? str(p) : std::string("(null)");
            if (prec >= 0 && int(s.size()) > prec) s.resize(size_t(prec));
            out += guest_fmt::pad(s, width, left, ' ');
            break; }
        case 'p': {
            uint32_t p = next();
            std::string body = "00000000" + guest_fmt::uint_to(p, 16, false);
            out += "0x" + body.substr(body.size() - 8);
            break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            // A double vararg occupies two 32-bit stack slots (little-endian: low
            // dword first) with no alignment padding on i386. long double is 12
            // bytes on i386, but no CRT printf path in a game passes one without
            // an explicit cast, and MSVC's %Lf is not used by the fixtures; treat
            // it as double and say so.
            double v;
            uint32_t lo = next(), hi = next(); uint64_t bits = uint64_t(hi) << 32 | lo; std::memcpy(&v, &bits, 8);
            char spec[32]; int k = 0;
            spec[k++] = '%';
            if (left) spec[k++] = '-';
            if (plus) spec[k++] = '+';
            if (space) spec[k++] = ' ';
            if (alt) spec[k++] = '#';
            if (zero) spec[k++] = '0';
            if (width) k += std::snprintf(spec + k, sizeof spec - size_t(k), "%d", width);
            if (prec >= 0) k += std::snprintf(spec + k, sizeof spec - size_t(k), ".%d", prec);
            spec[k++] = conv; if (conv == 'F') spec[k - 1] = 'f'; if (conv == 'E') spec[k - 1] = 'e'; if (conv == 'G') spec[k - 1] = 'g';
            spec[k] = 0;
            char buf[512];
            int n = std::snprintf(buf, sizeof buf, spec, v);
            if (n < 0) n = 0;
            if (size_t(n) >= sizeof buf) n = int(sizeof buf) - 1;
            out.append(buf, size_t(n));
            break; }
        case 'n': { uint32_t p = next(); if (p) mem.w32(p, uint32_t(out.size())); break; }
        default: out.push_back('%'); out.push_back(conv); break;
        }
    }
    return out;
}

inline int Guest::store(const std::string& s, uint32_t out_ptr, int cap, bool append_nul) {
    int n = int(s.size());
    if (cap != 0) {
        // cap < 0 is an unbounded buffer: every byte fits, plus the terminator.
        int room = cap < 0 ? n : (append_nul ? cap - 1 : cap);
        if (room < 0) room = 0;
        int put = n < room ? n : room;
        if (put > 0) mem.write_bytes(out_ptr, s.data(), uint32_t(put));
        if (append_nul) mem.w8(out_ptr + uint32_t(put), 0);
    }
    return n;
}

inline void Guest::run_atexit(Cpu& c) {
    while (!atexit_fns.empty()) {
        uint32_t fn = atexit_fns.back(); atexit_fns.pop_back();
        if (fn) c.call(fn, {});
    }
}
inline void Guest::do_exit(Cpu& c, uint32_t code) {
    try { run_atexit(c); } catch (...) { c.exited = true; c.exit_code = code; throw; }
    c.exited = true; c.exit_code = code;
}

// ---- registration ----
inline void Guest::register_all() {
    reg.map.clear();
    register_win32(reg, host);  // the stage-1 allowlist stays the base surface
    // Fixed guest structures: three FILE structs for stdin/stdout/stderr.
    mem.map(iob_addr, 4096, PROT_R | PROT_W);
    iob.assign(3 * 32, 0);
    mem.write_bytes(iob_addr, iob.data(), uint32_t(iob.size()));
    for (int i = 0; i < 3; ++i) mem.w32(iob_addr + uint32_t(i * 32), 0xFFFFFFFFu);  // _file = -1
    initenv = heap.alloc(16); commode = heap.alloc(16); fmode = heap.alloc(16);
    mb_cur_max = heap.alloc(16); errno_ptr = heap.alloc(16);
    mem.w32(commode, 0); mem.w32(fmode, 0); mem.w32(mb_cur_max, 1); mem.w32(errno_ptr, 0);
    startupinfo = heap.alloc(68); mem.write_bytes(startupinfo, std::vector<uint8_t>(68, 0).data(), 68);
    mem.w32(startupinfo, 68);  // cb
    cmdline_ptr = put_str(cmdline.empty() ? exe_path : cmdline);
    env_ptr = heap.alloc(4); mem.w32(env_ptr, 0);
    module_name_ptr = put_str(exe_path);
    // Data imports (mingw's win32-threaded CRT startup references msvcrt's
    // variables directly instead of through the __p__ accessors): the IAT slot
    // holds the variable's guest address. __initenv starts NULL and the CRT
    // startup writes it through __getmainargs' env out-param; __mb_cur_max is 1.
    reg.data.clear();
    reg.add_data("msvcrt.dll", "__initenv", initenv);
    reg.add_data("msvcrt.dll", "__winitenv", initenv);
    reg.add_data("msvcrt.dll", "__mb_cur_max", mb_cur_max);
    reg.add_data("msvcrt.dll", "_iob", iob_addr);
    // __iob_func is a FUNCTION (FILE* __iob_func(void), stdio.h): its IAT slot
    // must hold a callable thunk, not the table address — a data binding makes
    // a call through it jump into the iob bytes. It returns the table pointer.
    reg.add("msvcrt.dll", "__iob_func", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return iob_addr; });
    auto C = [](const char* n) { return n; };
    (void)C;

    // kernel32: process, error, memory, sync, TLS
    reg.add("kernel32.dll", "GetLastError", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return last_error(); });
    reg.add("kernel32.dll", "SetLastError", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { set_last_error(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetCurrentProcess", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "GetCurrentProcessId", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x1000; });
    reg.add("kernel32.dll", "GetCurrentThread", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0xFFFFFFFEu; });
    // The thread id lives in the running thread's TEB (ClientId.UniqueThread).
    reg.add("kernel32.dll", "GetCurrentThreadId", Conv::Stdcall, 0, [](Cpu& c) -> uint32_t { return c.mem.r32(c.fs_base + 0x24); });
    reg.add("kernel32.dll", "GetCommandLineA", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return cmdline_ptr; });
    reg.add("kernel32.dll", "GetCommandLineW", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return put_wstr(u"x"); });
    reg.add("kernel32.dll", "GetStartupInfoA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto b = mem.read_bytes(startupinfo, 68); copy_out(c.arg(0), b.data(), 68); return 0; });
    // GetStartupInfoW (processthreadsapi.h): the same 68-byte block. Its string
    // members are NULL here, so the ANSI and wide layouts agree byte for byte.
    reg.add("kernel32.dll", "GetStartupInfoW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto b = mem.read_bytes(startupinfo, 68); copy_out(c.arg(0), b.data(), 68); return 0; });
    reg.add("kernel32.dll", "GetEnvironmentStrings", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return env_ptr; });
    reg.add("kernel32.dll", "GetEnvironmentStringsW", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return put_wstr(u"\0"); });
    reg.add("kernel32.dll", "FreeEnvironmentStringsA", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "FreeEnvironmentStringsW", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "GetModuleHandleA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        std::string n = str(c.arg(0)); if (n.empty()) return image_base;
        return module_by_name(n); });
    // FreeLibrary: decrements the refcount of a module this surface loaded or
    // tracks, and never unmaps: a real unload (refcount to zero -> unmap +
    // DLL_PROCESS_DETACH) is not implemented, and the coverage matrix says so.
    // An unknown handle fails with ERROR_MOD_NOT_FOUND, as Windows answers.
    reg.add("kernel32.dll", "FreeLibrary", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t h = c.arg(0);
        if (h == image_base) return 1;
        for (auto& kv : dlls) if (kv.second.image.base == h) {
            if (kv.second.refs > 1) kv.second.refs--;
            return 1;
        }
        for (const auto& m : modules) if (m.second == h) return 1;
        set_last_error(126);  // ERROR_MOD_NOT_FOUND
        return 0; });
    // IsDBCSLeadByteEx: the code pages this surface answers (1252, 437, 65001)
    // are all single-byte or UTF-8, so no byte is a lead byte.
    reg.add("kernel32.dll", "IsDBCSLeadByteEx", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "IsDBCSLeadByte", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    // (hModule, lpFilename, nSize): arg(0) is the module handle, not the buffer.
    reg.add("kernel32.dll", "GetModuleFileNameA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t buf = c.arg(1), n = c.arg(2); if (!buf || !n) return 0;
        uint32_t put = uint32_t(exe_path.size()) < n ? uint32_t(exe_path.size()) : n - 1;
        mem.write_bytes(buf, exe_path.data(), put); mem.w8(buf + put, 0); return put; });
    reg.add("kernel32.dll", "GetModuleFileNameW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t buf = c.arg(1), n = c.arg(2); if (!buf || !n) return 0;
        std::u16string w(exe_path.begin(), exe_path.end());
        uint32_t put = uint32_t(w.size()) < n ? uint32_t(w.size()) : n - 1;
        for (uint32_t i = 0; i < put; ++i) { mem.w8(buf + i * 2, uint8_t(w[i])); mem.w8(buf + i * 2 + 1, uint8_t(w[i] >> 8)); }
        mem.w8(buf + put * 2, 0); mem.w8(buf + put * 2 + 1, 0); return put; });
    reg.add("kernel32.dll", "ExitProcess", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("kernel32.dll", "TerminateProcess", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(1)); return 1; });
    // Yield points of the cooperative scheduler: a game that polls with Sleep
    // (`while (!flag) Sleep(1)`) waits on a worker that only runs here, so each
    // of these runs the pending bodies before it returns (Bugbot 4561794148).
    reg.add("kernel32.dll", "Sleep", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("kernel32.dll", "SleepEx", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });  // 0: not an alertable wakeup
    // Threads: the surface runs one guest thread per CPU, so a thread body
    // cannot run inside CreateThread — mcfgthread's creator->thread handshake
    // (__MCF_event_await_change on the new thread's `ready` flag) spins forever
    // there. Instead the (start, param) pair is recorded and each body runs at
    // the guest's first blocking wait (keyed event / single object), with the
    // TEB TLS slots and SEH chain saved and restored around it: a real thread
    // has its own, and the synchronous model shares one TEB.
    reg.add("kernel32.dll", "CreateThread", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t {
        uint32_t handle = 0x60000000u + ++thread_serial;
        // lpThreadId receives the thread identifier (MSDN CreateThread; a DWORD
        // that identifies the thread while it lives). It must be nonzero and
        // unique among threads, and GetThreadId answers the same value.
        uint32_t id = 0x2000u + 4u * thread_serial;
        thread_ids[handle] = id;
        pending_threads.push_back({handle, c.arg(2), c.arg(3)});
        if (c.arg(5)) mem.w32(c.arg(5), id);
        return handle; });
    // ExitThread ends only the calling thread (winbase.h: "Ends the calling
    // thread"): a body that calls it is unwound to run_pending_threads (the
    // ThreadExit exception) and the process keeps going (Bugbot 4559267305) —
    // do_exit here would end the whole process.
    reg.add("kernel32.dll", "ExitThread", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        thread_exit_code = c.arg(0);
        if (in_thread_body) throw ThreadExit{};
        return thread_exit_code; });
    // SwitchToThread: nonzero when another thread ran (MSDN), i.e. bodies were pending.
    reg.add("kernel32.dll", "SwitchToThread", Conv::Stdcall, 0, [this](Cpu& c) -> uint32_t {
        bool ran = !pending_threads.empty(); run_pending_threads(c); return ran ? 1u : 0u; });
    // The blocking-wait surface a win32-threaded CRT startup uses (mingw 13's
    // winpthreads/mcfgthread peers): waits run the pending thread bodies first,
    // then succeed, as the keyed-event thunks below answer.
    reg.add("kernel32.dll", "WaitForSingleObject", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });  // WAIT_OBJECT_0
    reg.add("kernel32.dll", "WaitForSingleObjectEx", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    // WaitForMultipleObjects(Ex): the surface's handles are all thread or event
    // stand-ins, so the wait succeeds with the first object (WAIT_OBJECT_0).
    reg.add("kernel32.dll", "WaitForMultipleObjects", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("kernel32.dll", "WaitForMultipleObjectsEx", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("kernel32.dll", "GetExitCodeThread", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        // A poll is a yield point: pending bodies run before the code is read.
        run_pending_threads(c);
        // Thread bodies run to completion before any wait returns, so the exit
        // code is the body's own: its return value or its ExitThread code, both
        // recorded by run_pending_threads (STILL_ACTIVE is 259, the documented
        // answer for a running thread, used only for a handle with no body run).
        if (c.arg(1)) mem.w32(c.arg(1), thread_exit_codes.count(c.arg(0)) ? thread_exit_codes[c.arg(0)] : 259u);
        return 1; });
    reg.add("kernel32.dll", "ResumeThread", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });  // was not suspended
    // The identifier of a thread this surface created. Any other handle (the
    // GetCurrentThread pseudo handle) answers the running thread's own id.
    reg.add("kernel32.dll", "GetThreadId", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        auto it = thread_ids.find(c.arg(0));
        return it != thread_ids.end() ? it->second : c.mem.r32(c.fs_base + 0x24); });
    reg.add("kernel32.dll", "DuplicateHandle", Conv::Stdcall, 7, [this](Cpu& c) -> uint32_t {
        // One process, no real handle table: the duplicate is the source value.
        if (c.arg(3)) mem.w32(c.arg(3), c.arg(1));
        return 1; });
    // Condition variables: single-threaded surface — init zero-fills, wakes are
    // no-ops, and a sleep runs pending thread bodies then succeeds.
    reg.add("kernel32.dll", "InitializeConditionVariable", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (c.arg(0)) mem.w32(c.arg(0), 0);
        return 1; });
    reg.add("kernel32.dll", "WakeConditionVariable", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "WakeAllConditionVariable", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "SleepConditionVariableCS", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 1; });  // nonzero: woken, not timed out
    // GetTimeZoneInformation: UTC surface — bias 0, no standard/daylight names,
    // TIME_ZONE_ID_UNKNOWN (0), as a system set to UTC answers.
    reg.add("kernel32.dll", "GetTimeZoneInformation", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) mem.write_bytes(p, std::vector<uint8_t>(172, 0).data(), 172);
        return 0; });
    reg.add("kernel32.dll", "GetCurrentProcessorNumberEx", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w(p, 0, 2); mem.w(p + 2, 0, 2); } return 0; });
    // Thread affinity/priority: one CPU, normal priority, affinity mask 1.
    // BOOL GetProcessAffinityMask(HANDLE, PDWORD, PDWORD): process and system
    // masks are the 2nd/3rd parameters (winbase.h), not the handle.
    reg.add("kernel32.dll", "GetProcessAffinityMask", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        if (c.arg(1)) { mem.w32(c.arg(1), 1); }
        if (c.arg(2)) { mem.w32(c.arg(2), 1); }
        return 1; });
    reg.add("kernel32.dll", "SetThreadAffinityMask", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return c.arg(1) ? 1u : 0u; });
    reg.add("kernel32.dll", "GetThreadGroupAffinity", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(1); if (p) { mem.w32(p, 1); mem.w32(p + 4, 0); mem.w(p + 8, 0, 2); } return 1; });
    reg.add("kernel32.dll", "GetThreadPriority", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "SetThreadPriority", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    // EncodePointer/DecodePointer (and the System variants): VC/UCRT startup
    // encodes every function pointer it stores and decodes it on use, so the
    // pair must be a real inverse or a decoded pointer is garbage. The key is
    // per-process ("random-looking" to the guest) and the encoding is XOR, so
    // decode(encode(p)) == p and encode(NULL) == decode(NULL) == NULL hold
    // exactly as the documented invariants require (Bugbot 4559749432).
    reg.add("kernel32.dll", "EncodePointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    reg.add("kernel32.dll", "DecodePointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    reg.add("kernel32.dll", "EncodeSystemPointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    reg.add("kernel32.dll", "DecodeSystemPointer", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { return encode_ptr(c.arg(0)); });
    // FormatMessage: no message tables; a caller must handle the failure, as
    // on Windows when no message is found.
    reg.add("kernel32.dll", "FormatMessageA", Conv::Stdcall, 7, [this](Cpu&) -> uint32_t { set_last_error(15100); return 0; });
    reg.add("kernel32.dll", "FormatMessageW", Conv::Stdcall, 7, [this](Cpu&) -> uint32_t { set_last_error(15100); return 0; });
    // File info / module enumeration used by C++ runtimes' diagnostics paths:
    // answer "not available" the way the API does on a real failure.
    reg.add("kernel32.dll", "GetFileInformationByHandle", Conv::Stdcall, 2, [this](Cpu&) -> uint32_t { set_last_error(6); return 0; });
    reg.add("kernel32.dll", "K32EnumProcessModules", Conv::Stdcall, 4, [this](Cpu&) -> uint32_t { set_last_error(6); return 0; });
    reg.add("kernel32.dll", "GetLogicalProcessorInformationEx", Conv::Stdcall, 3, [this](Cpu&) -> uint32_t { set_last_error(122); return 0; });
    // BOOL GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE *phModule)
    // (libsrc/winbase.h): three parameters — flags, name, out handle. The
    // documented flag contract (Bugbot 4559749437): NULL name means the main
    // module (the exe's base); GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS (0x4)
    // treats lpModuleName as an address and answers the module containing it;
    // UNCHANGED_REFCOUNT (0x2) and PIN (0x1) only affect refcounting, which
    // this surface's FreeLibrary never unmaps anyway. A lookup that misses
    // writes NULL and fails, as Windows does.
    reg.add("kernel32.dll", "GetModuleHandleExW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t flags = c.arg(0), p = c.arg(1), out = c.arg(2);
        uint32_t base = 0;
        if (flags & 4) {  // FROM_ADDRESS: find the module containing the address
            if (p >= image_base && p < image_base + 0x1000000u) base = image_base;
            else for (auto& kv : dlls) {
                if (p >= kv.second.image.base && p < kv.second.image.base + kv.second.image.size) { base = kv.second.image.base; break; }
            }
        } else if (!p) {  // NULL name: the main module
            base = image_base;
        } else {
            std::string n;
            for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
            base = module_by_name(n);
        }
        if (out) mem.w32(out, base);
        if (!base) set_last_error(126);  // ERROR_MOD_NOT_FOUND, as Windows answers
        return base ? 1 : 0;
    });
    // Time/clock queries the C++ runtime's once-init and this_thread use.
    reg.add("kernel32.dll", "QueryUnbiasedInterruptTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 0x00200000u); mem.w32(p + 4, 0); } return 1; });
    // RaiseFailFastException: a fatal runtime error (fail-fast); end the run
    // with the crash exit code instead of continuing with broken state.
    reg.add("kernel32.dll", "RaiseFailFastException", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { do_exit(c, 0x0DEAD10Cu); return 0; });
    // Console output: WriteConsoleW is the wide path WriteFile takes for a
    // console handle; the UTF-16 buffer is narrowed (the surface's output is
    // ASCII, as the module names it serves are).
    reg.add("kernel32.dll", "WriteConsoleW", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        uint32_t handle = c.arg(0), buf = c.arg(1), n = c.arg(2), written = c.arg(3);
        if (handle != Host::H_STDOUT && handle != Host::H_STDERR) { set_last_error(6); return 0; }
        std::string s;
        for (uint32_t i = 0; i < n; ++i) s.push_back(char(mem.r(buf + i * 2, 2) & 0xff));
        emit(handle == Host::H_STDOUT ? 1 : 2, s);
        if (written) mem.w32(written, n);
        return 1; });
    reg.add("kernel32.dll", "SetUnhandledExceptionFilter", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "UnhandledExceptionFilter", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "RaiseException", Conv::Stdcall, 4, [](Cpu& c) -> uint32_t {
        throw Fault(FaultKind::Unsupported, c.eip, c.arg(0), "RaiseException (guest SEH not implemented)"); });
    // GetVersion packs major in the low byte and minor in the next: 10.0.19045.
    reg.add("kernel32.dll", "GetVersion", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x4A65000Au; });
    reg.add("kernel32.dll", "GetVersionExA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        uint32_t cb = mem.r32(p); if (cb < 148) { set_last_error(122); return 0; }
        mem.w32(p + 4, 10); mem.w32(p + 8, 0); mem.w32(p + 12, 19045); mem.w32(p + 16, 2);
        // OSVERSIONINFOA: the five dwords, then szCSDVersion[128] at offset 20,
        // so the struct is 148 bytes. OSVERSIONINFOEXA appends wServicePackMajor,
        // wServicePackMinor, wSuiteMask, wProductType and wReserved (156 bytes).
        // Nothing is written past the size the caller declared.
        std::string cs = "Service Pack 0";
        mem.write_bytes(p + 20, cs.c_str(), uint32_t(cs.size() + 1));
        if (cb >= 156) {
            mem.w(p + 148, 0, 2);  // wServicePackMajor
            mem.w(p + 150, 0, 2);  // wServicePackMinor
            mem.w(p + 152, 0, 2);  // wSuiteMask
            mem.w(p + 154, 1, 2);  // wProductType: VER_NT_WORKSTATION
            mem.w8(p + 155, 0);    // wReserved
        }
        return 1; });
    reg.add("kernel32.dll", "IsProcessorFeaturePresent", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        return c.arg(0) == 10 ? 1u : 0u; });  // PF_XMMI64_INSTRUCTIONS_AVAILABLE (SSE2)
    reg.add("kernel32.dll", "GetACP", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 1252; });
    reg.add("kernel32.dll", "GetOEMCP", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 437; });
    reg.add("kernel32.dll", "GetCPInfo", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), p = c.arg(1); if (!p) return 0;
        mem.w32(p, cp == 65001 ? 65001u : 0u); mem.w32(p + 4, 1); mem.w8(p + 8, '?'); mem.w8(p + 9, 0);
        for (int i = 10; i < 20; ++i) mem.w8(p + uint32_t(i), 0);
        return 1; });
    reg.add("kernel32.dll", "MultiByteToWideChar", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t {
        uint32_t cp = c.arg(0), src = c.arg(2), n = c.arg(3), dst = c.arg(4), cap = c.arg(5);
        (void)cp; std::string s = n == 0xFFFFFFFFu ? str(src) : std::string();
        if (n != 0xFFFFFFFFu) { auto b = mem.read_bytes(src, n); s.assign(b.begin(), b.end()); }
        if (!dst || !cap) return uint32_t(s.size() + 1);
        uint32_t put = uint32_t(s.size()) < cap ? uint32_t(s.size()) : cap - 1;
        for (uint32_t i = 0; i < put; ++i) { mem.w8(dst + i * 2, uint8_t(s[i])); mem.w8(dst + i * 2 + 1, 0); }
        if (put < cap) { mem.w8(dst + put * 2, 0); mem.w8(dst + put * 2 + 1, 0); }
        return put; });
    reg.add("kernel32.dll", "WideCharToMultiByte", Conv::Stdcall, 8, [this](Cpu& c) -> uint32_t {
        uint32_t src = c.arg(2), n = c.arg(3), dst = c.arg(4), cap = c.arg(5);
        std::string s;
        if (n == 0xFFFFFFFFu) { for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(i * 2 + src, 2)); if (!w) break; s.push_back(char(w & 0xff)); } }
        else for (uint32_t i = 0; i < n; ++i) s.push_back(char(mem.r(src + i * 2, 2) & 0xff));
        if (!dst || !cap) return uint32_t(s.size() + 1);
        uint32_t put = uint32_t(s.size()) < cap ? uint32_t(s.size()) : cap - 1;
        mem.write_bytes(dst, s.data(), put); if (put < cap) mem.w8(dst + put, 0);
        return put; });
    // Heap
    reg.add("kernel32.dll", "GetProcessHeap", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0x50000000u; });
    reg.add("kernel32.dll", "HeapCreate", Conv::Stdcall, 3, [this](Cpu&) -> uint32_t {
        uint32_t h = 0x50000001u + heap_serial++; heap_handles[h] = 0; return h; });
    reg.add("kernel32.dll", "HeapDestroy", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap_handles.erase(c.arg(0)); return 1; });
    reg.add("kernel32.dll", "HeapAlloc", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); if (!n) n = 1;
        uint32_t p = heap.alloc(n);
        if (c.arg(1) & 8) { auto z = std::vector<uint8_t>(n, 0); mem.write_bytes(p, z.data(), n); }  // HEAP_ZERO_MEMORY
        return p; });
    reg.add("kernel32.dll", "HeapFree", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { heap.release(c.arg(2)); return 1; });
    reg.add("kernel32.dll", "HeapReAlloc", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t { return heap.realloc(c.arg(2), c.arg(3)); });
    reg.add("kernel32.dll", "HeapSize", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t { return heap.size_of(c.arg(2)); });
    reg.add("kernel32.dll", "LocalAlloc", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(1)); });
    reg.add("kernel32.dll", "LocalFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GlobalAlloc", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(1)); });
    reg.add("kernel32.dll", "GlobalFree", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    // Virtual memory
    reg.add("kernel32.dll", "VirtualAlloc", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t want = (c.arg(1) + 0xfffu) & ~0xfffu; if (!want) want = 0x1000;
        uint32_t p = virtual_top; virtual_top += want;
        mem.map(p, want, PROT_R | PROT_W); virtual_alloc[p] = want;
        if (c.arg(3) & 0x40) { auto z = std::vector<uint8_t>(want, 0); mem.write_bytes(p, z.data(), want); }
        return p; });
    reg.add("kernel32.dll", "VirtualFree", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        auto it = virtual_alloc.find(c.arg(0)); if (it == virtual_alloc.end()) return 0;
        virtual_alloc.erase(it); return 1; });
    reg.add("kernel32.dll", "VirtualProtect", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        uint32_t a = c.arg(0) & ~0xfffu, n = (c.arg(1) + 0xfffu) & ~0xfffu, prot = c.arg(2), old = c.arg(3);
        // Page-protection flags (winnt.h): 0x02 READONLY, 0x04 READWRITE,
        // 0x08 WRITECOPY, 0x10 EXECUTE, 0x20 EXECUTE_READ, 0x40
        // EXECUTE_READWRITE, 0x80 EXECUTE_WRITECOPY. mingw's
        // _mark_section_writable passes PAGE_EXECUTE_READWRITE (0x40): a
        // check for only 0x04/0x20 drops X and the return into .text faults.
        uint8_t p = PROT_R;
        if (prot & 0x04 || prot & 0x08 || prot & 0x40 || prot & 0x80) p |= PROT_W;
        if (prot & 0x10 || prot & 0x20 || prot & 0x40 || prot & 0x80) p |= PROT_X;
        if (!n) n = 0x1000;
        try { mem.protect(a, n, p); } catch (...) { set_last_error(487); return 0; }
        if (old) mem.w32(old, 0x04);
        return 1; });
    reg.add("kernel32.dll", "VirtualQuery", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t a = c.arg(0), p = c.arg(1); if (!p) return 0;
        uint32_t base = a & ~0xfffu, size = 0x1000;
        // The real per-page protection: callers (mingw's pseudo-relocator)
        // skip their own VirtualProtect when the answer says the page is
        // already writable, so a blanket READWRITE answer makes their next
        // write fault.
        uint8_t pr = mem.mapped(a) ? mem.protect_of(a) : 0;
        uint32_t win = 0x04;  // PAGE_READWRITE is the sane default
        if (!mem.mapped(a)) win = 0;  // free: State MEM_FREE below
        else if (!(pr & PROT_W)) win = (pr & PROT_X) ? 0x20 /*PAGE_EXECUTE_READ*/ : 0x02 /*PAGE_READONLY*/;
        else if (pr & PROT_X) win = 0x40 /*PAGE_EXECUTE_READWRITE*/;
        // AllocationProtect (mbi+8) must NOT read as already-fully-writable:
        // mingw's _mark_section_writable skips its VirtualProtect when this
        // field says EXECUTE_READWRITE, then its pseudo-reloc memcpy faults.
        // Real images allocate as writecopy (0x80); report that.
        // MEMORY_BASIC_INFORMATION32: BaseAddress(0) AllocationBase(4)
        // AllocationProtect(8) RegionSize(12) State(16) Protect(20) Type(24).
        mem.w32(p + 0, base); mem.w32(p + 4, base); mem.w32(p + 8, win ? 0x80u : 0u);
        mem.w32(p + 12, size);
        mem.w32(p + 16, mem.mapped(a) ? 0x1000u : 0x10000u);  // MEM_COMMIT / MEM_FREE
        mem.w32(p + 20, win);                                  // Protect: the real protection
        mem.w32(p + 24, 0x20000);                              // Type: MEM_PRIVATE
        return 28; });
    // Critical sections: one guest thread per CPU, so they are no-ops.
    for (const char* n : {"InitializeCriticalSection", "EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection"})
        reg.add("kernel32.dll", n, Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("kernel32.dll", "InitializeCriticalSectionAndSpinCount", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "TryEnterCriticalSection", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "InitializeCriticalSectionEx", Conv::Stdcall, 3, [](Cpu&) -> uint32_t { return 1; });
    // OpenEventA (synchapi.h): this surface creates no named objects, so no name
    // exists: NULL with ERROR_FILE_NOT_FOUND, as Windows answers for a missing
    // name. Registered so GetProcAddress finds the export (a guest's probe for
    // it read the missing export as a failure and took its failure path).
    reg.add("kernel32.dll", "OpenEventA", Conv::Stdcall, 3, [this](Cpu&) -> uint32_t { set_last_error(2); return 0; });
    // TLS
    reg.add("kernel32.dll", "TlsAlloc", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t {
        if (next_tls >= 64) return 0xFFFFFFFFu;
        return next_tls++; });
    reg.add("kernel32.dll", "TlsFree", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "TlsGetValue", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i >= 64) { set_last_error(87); return 0; }
        return mem.r32(c.fs_base + 0xE10 + i * 4); });
    reg.add("kernel32.dll", "TlsSetValue", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t i = c.arg(0); if (i >= 64) { set_last_error(87); return 0; }
        mem.w32(c.fs_base + 0xE10 + i * 4, c.arg(1)); return 1; });
    // Interlocked
    reg.add("kernel32.dll", "InterlockedIncrement", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = mem.r32(c.arg(0)) + 1; mem.w32(c.arg(0), v); return v; });
    reg.add("kernel32.dll", "InterlockedDecrement", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = mem.r32(c.arg(0)) - 1; mem.w32(c.arg(0), v); return v; });
    reg.add("kernel32.dll", "InterlockedExchange", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); mem.w32(c.arg(0), c.arg(1)); return old; });
    reg.add("kernel32.dll", "InterlockedCompareExchange", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); if (old == c.arg(2)) mem.w32(c.arg(0), c.arg(1)); return old; });
    reg.add("kernel32.dll", "InterlockedExchangeAdd", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t old = mem.r32(c.arg(0)); mem.w32(c.arg(0), old + c.arg(1)); return old; });
    // Time
    reg.add("kernel32.dll", "GetTickCount", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 12345; });
    reg.add("kernel32.dll", "GetTickCount64", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 12345; });
    reg.add("kernel32.dll", "QueryPerformanceCounter", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 1000); mem.w32(p + 4, 0); } return 1; });
    reg.add("kernel32.dll", "QueryPerformanceFrequency", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 1000000); mem.w32(p + 4, 0); } return 1; });
    reg.add("kernel32.dll", "GetSystemTimeAsFileTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (p) { mem.w32(p, 0x80000000u); mem.w32(p + 4, 0x01D00000u); } return 0; });
    reg.add("kernel32.dll", "GetSystemTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        fill_system_time(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetLocalTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        fill_system_time(c.arg(0)); return 0; });
    reg.add("kernel32.dll", "GetSystemInfo", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); if (!p) return 0;
        mem.w32(p + 0, 0); mem.w32(p + 4, 4096); mem.w32(p + 8, 0x10000); mem.w32(p + 12, 0x7FFE0000u);
        mem.w32(p + 16, 1); mem.w32(p + 20, 1); mem.w32(p + 24, 0); mem.w32(p + 28, 0); mem.w32(p + 32, 9);
        return 0; });
    // Console output handles: stdout/stderr are the Host sinks.
    reg.add("kernel32.dll", "GetStdHandle", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        switch (int32_t(c.arg(0))) { case -10: return Host::H_STDIN; case -11: return Host::H_STDOUT; case -12: return Host::H_STDERR; }
        return 0xFFFFFFFFu; });
    reg.add("kernel32.dll", "WriteFile", Conv::Stdcall, 5, [this](Cpu& c) -> uint32_t {
        uint32_t handle = c.arg(0), buf = c.arg(1), n = c.arg(2), written = c.arg(3);
        if (handle != Host::H_STDOUT && handle != Host::H_STDERR) { set_last_error(6); return 0; }  // ERROR_INVALID_HANDLE
        if (n > (1u << 24)) { set_last_error(87); return 0; }
        auto bytes = mem.read_bytes(buf, n);
        emit(handle == Host::H_STDOUT ? 1 : 2, std::string(bytes.begin(), bytes.end()));
        if (written) mem.w32(written, n);
        return 1; });
    reg.add("kernel32.dll", "GetFileType", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t {
        return (c.arg(0) == Host::H_STDOUT || c.arg(0) == Host::H_STDERR) ? 2u : 0u; });  // FILE_TYPE_CHAR
    reg.add("kernel32.dll", "SetConsoleCtrlHandler", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "FlushFileBuffers", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "CloseHandle", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("kernel32.dll", "SetHandleCount", Conv::Stdcall, 1, [](Cpu& c) -> uint32_t { return c.arg(0); });

    // d3dx9_36 math: D3DX uses row-major matrices and row-vector transforms.
    auto fp = [](uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; };
    auto fb = [](float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; };
    auto rd = [this, fp](uint32_t p, unsigned n) { std::vector<float> v(n); for (unsigned i = 0; i < n; ++i) v[i] = fp(mem.r32(p + 4 * i)); return v; };
    auto wr = [this, fb](uint32_t p, const float* v, unsigned n) { for (unsigned i = 0; i < n; ++i) mem.w32(p + 4 * i, fb(v[i])); };
    auto matmul = [](const float* a, const float* b, float* o) { for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { o[i*4+j] = 0; for (int k = 0; k < 4; ++k) o[i*4+j] += a[i*4+k] * b[k*4+j]; } };
    auto mat = [this, fp, fb, rd, wr, matmul](const char* name, unsigned argc, auto build) {
        reg.add("d3dx9_36.dll", name, Conv::Stdcall, argc, [this, fp, fb, rd, wr, matmul, build](Cpu& c) -> uint32_t {
            uint32_t out = c.arg(0); float m[16]; build(c, m, rd, fp, matmul); wr(out, m, 16); return out;
        });
    };
    auto ident = [](float* m) { std::fill(m, m + 16, 0.0f); m[0] = m[5] = m[10] = m[15] = 1; };
    mat("D3DXMatrixLookAtLH", 4, [ident](Cpu& c, float* m, auto rd, auto fp, auto) { auto e=rd(c.arg(1),3), a=rd(c.arg(2),3), u=rd(c.arg(3),3); float z[3],x[3],y[3]; auto norm=[](float* v){float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(int i=0;i<3;++i)v[i]/=n;}; for(int i=0;i<3;++i)z[i]=a[i]-e[i]; norm(z); x[0]=u[1]*z[2]-u[2]*z[1];x[1]=u[2]*z[0]-u[0]*z[2];x[2]=u[0]*z[1]-u[1]*z[0];norm(x); y[0]=z[1]*x[2]-z[2]*x[1];y[1]=z[2]*x[0]-z[0]*x[2];y[2]=z[0]*x[1]-z[1]*x[0]; ident(m); for(int i=0;i<3;++i){m[4*i]=x[i];m[4*i+1]=y[i];m[4*i+2]=z[i];} m[12]=-(x[0]*e[0]+x[1]*e[1]+x[2]*e[2]);m[13]=-(y[0]*e[0]+y[1]*e[1]+y[2]*e[2]);m[14]=-(z[0]*e[0]+z[1]*e[1]+z[2]*e[2]); (void)fp; });
    mat("D3DXMatrixMultiply", 3, [](Cpu& c,float* o,auto rd,auto,auto mul){auto a=rd(c.arg(1),16),b=rd(c.arg(2),16);mul(a.data(),b.data(),o);});
    mat("D3DXMatrixPerspectiveFovLH", 5, [ident](Cpu& c,float* m,auto,auto fp,auto){float y=1/std::tan(fp(c.arg(1))/2),a=fp(c.arg(2)),n=fp(c.arg(3)),f=fp(c.arg(4));std::fill(m,m+16,0.0f);m[0]=y/a;m[5]=y;m[10]=f/(f-n);m[11]=1;m[14]=-n*f/(f-n);(void)ident;});
    auto rotation = [mat](const char* name, unsigned argc, int axis) { mat(name, argc, [axis](Cpu& c,float* m,auto,auto fp,auto){float a=fp(c.arg(1)),s=std::sin(a),v=std::cos(a);std::fill(m,m+16,0.0f);m[15]=1;if(axis==0){m[0]=1;m[5]=m[10]=v;m[6]=s;m[9]=-s;}else if(axis==1){m[5]=1;m[0]=m[10]=v;m[2]=-s;m[8]=s;}else{m[10]=1;m[0]=m[5]=v;m[1]=s;m[4]=-s;}}); };
    rotation("D3DXMatrixRotationX",2,0); rotation("D3DXMatrixRotationY",2,1); rotation("D3DXMatrixRotationZ",2,2);
    mat("D3DXMatrixRotationYawPitchRoll", 4, [](Cpu& c,float* m,auto,auto fp,auto){float y=fp(c.arg(1)),p=fp(c.arg(2)),r=fp(c.arg(3)),sy=sin(y),cy=cos(y),sp=sin(p),cp=cos(p),sr=sin(r),cr=cos(r);float v[16]={cy*cr+sy*sp*sr,sr*cp,-sy*cr+cy*sp*sr,0,-cy*sr+sy*sp*cr,cr*cp,sr*sy+cy*sp*cr,0,sy*cp,-sp,cy*cp,0,0,0,0,1};std::copy(v,v+16,m);});
    mat("D3DXMatrixScaling", 4, [ident](Cpu& c,float* m,auto,auto fp,auto){ident(m);m[0]=fp(c.arg(1));m[5]=fp(c.arg(2));m[10]=fp(c.arg(3));});
    mat("D3DXMatrixTranslation", 4, [ident](Cpu& c,float* m,auto,auto fp,auto){ident(m);m[12]=fp(c.arg(1));m[13]=fp(c.arg(2));m[14]=fp(c.arg(3));});
    auto vec = [this, fp, fb, rd, wr](const char* name,unsigned argc,auto fn){reg.add("d3dx9_36.dll",name,Conv::Stdcall,argc,[this,fp,fb,rd,wr,fn](Cpu& c)->uint32_t{uint32_t out=c.arg(0);fn(c,out,rd,fp,wr);return out;});};
    vec("D3DXVec3Normalize",2,[](Cpu& c,uint32_t o,auto rd,auto,auto wr){auto v=rd(c.arg(1),3);float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(float& x:v)x=n?x/n:0;wr(o,v.data(),3);});
    auto transform = [vec](const char* name,unsigned argc,bool coord,bool array){vec(name,argc,[coord,array](Cpu& c,uint32_t o,auto rd,auto fp,auto wr){uint32_t in=c.arg(array?2:1),mp=c.arg(array?4:2),count=array?c.arg(5):1,os=array?c.arg(1):12,is=array?c.arg(3):12;auto m=rd(mp,16);for(uint32_t n=0;n<count;++n){auto v=rd(in+n*is,3);float q[4]={0,0,0,0};for(int j=0;j<4;++j)q[j]=v[0]*m[j]+v[1]*m[4+j]+v[2]*m[8+j]+m[12+j];if(coord){float d=q[3];for(int j=0;j<3;++j)q[j]/=d;wr(o+n*os,q,3);}else wr(o+n*os,q,4);(void)fp;}});};
    transform("D3DXVec3Transform",3,false,false); transform("D3DXVec3TransformArray",6,false,true);
    transform("D3DXVec3TransformCoord",3,true,false); transform("D3DXVec3TransformCoordArray",6,true,true);
    // Shlwapi contracts: filename lookup returns a pointer into the input; remove-extension is void; remove-file-spec returns nonzero only when something was removed. See https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathfindfilenamea, https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremoveextensiona, and https://learn.microsoft.com/en-us/windows/win32/api/shlwapi/nf-shlwapi-pathremovefilespeca.
    reg.add("shlwapi.dll","PathFindFileNameA",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::string s=str(p);size_t i=s.find_last_of("\\\\/:");return p+(i==std::string::npos?0:uint32_t(i+1));});
    reg.add("shlwapi.dll","PathRemoveExtensionA",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::string s=str(p);size_t start=s.find_last_of("\\\\/:"),dot=s.find_last_of('.');if(dot!=std::string::npos&&(start==std::string::npos||dot>start))mem.w8(p+uint32_t(dot),0);return 0;});
    reg.add("shlwapi.dll","PathRemoveFileSpecA",Conv::Stdcall,1,[this](Cpu& c)->uint32_t{uint32_t p=c.arg(0);if(!p)return 0;std::string s=str(p);auto sep=[](char x){return x=='\\'||x=='/';};size_t n=s.size();bool root=n==1&&sep(s[0]);root|=n==3&&s[1]==':'&&sep(s[2]);if(n>=2&&sep(s[0])&&sep(s[1])){size_t a=s.find_first_not_of("\\\\/",2),b=a==std::string::npos?a:s.find_first_of("\\\\/",a),d=b==std::string::npos?b:s.find_first_not_of("\\\\/",b),e=d==std::string::npos?d:s.find_first_of("\\\\/",d);root|=a!=std::string::npos&&b!=std::string::npos&&d!=std::string::npos&&(e==std::string::npos||e==n-1);}if(root)return 0;if(n&&sep(s[n-1])){mem.w8(p+uint32_t(n-1),0);return 1;}size_t i=s.find_last_of("\\\\/");if(i==std::string::npos)return 0;if(i==0)mem.w8(p+1,0);else if(i==2&&s[1]==':')mem.w8(p+3,0);else mem.w8(p+uint32_t(i),0);return 1;});

    // msvcrt: CRT startup, heap, strings, stdio
    reg.add("msvcrt.dll", "_lock", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_unlock", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "__lc_codepage", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return mb_cur_max; });
    reg.add("msvcrt.dll", "wcslen", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t n = 0; while (mem.r(c.arg(0) + n * 2, 2)) ++n; return n; });
    reg.add("msvcrt.dll", "strerror", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        static const char* msgs[] = {"No error", "Operation not permitted", "No such file or directory", "No such process"};
        uint32_t e = c.arg(0); return put_str(e < 4 ? msgs[e] : "Unknown error"); });
    reg.add("msvcrt.dll", "localeconv", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t {
        // struct lconv: 10 char* fields then 8 chars; all but the decimal point are "".
        uint32_t p = heap.alloc(48); std::vector<uint8_t> z(48, 0); mem.write_bytes(p, z.data(), 48);
        uint32_t empty = put_str(""), dot = put_str(".");
        for (int i = 0; i < 10; ++i) mem.w32(p + uint32_t(i * 4), i == 0 ? dot : empty);
        return p; });
    // setlocale(category, locale): the only locale this surface implements is
    // "C"/"" (the process default), so a query or a request for it answers
    // "C" and anything else answers NULL, as msvcrt does for an unavailable
    // locale. mingw's CRT calls it from its startup path.
    reg.add("msvcrt.dll", "setlocale", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t name = c.arg(1);
        if (name) {
            std::string want = str(name);
            if (!want.empty() && want != "C" && want != "POSIX") return 0;
        }
        return put_str("C"); });
    // Stage 3: guest DLL loading. `table` is captured so a LoadLibrary from
    // guest code can add thunks for the new module's imports.
    reg.add("kernel32.dll", "GetProcAddress", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        uint32_t module = c.arg(0), name = c.arg(1);
        if (!module || !name) { set_last_error(126); return 0; }
        return get_proc_address(module, name, c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryA", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        return load_guest_dll(&c, str(c.arg(0)), c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        // UTF-16 in, UTF-8 out: the module names this surface serves are ASCII.
        uint32_t p = c.arg(0); std::string n;
        for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
        return load_guest_dll(&c, n, c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryExA", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        return load_guest_dll(&c, str(c.arg(0)), c.thunks); });
    reg.add("kernel32.dll", "LoadLibraryExW", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0); std::string n;
        for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(p + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
        return load_guest_dll(&c, n, c.thunks); });
    reg.add("kernel32.dll", "GetModuleHandleW", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (!c.arg(0)) return image_base;
        std::string n;
        for (uint32_t i = 0;; ++i) { uint16_t w = uint16_t(mem.r(c.arg(0) + i * 2, 2)); if (!w) break; n.push_back(char(w & 0xff)); }
        return module_by_name(n); });
    reg.add("msvcrt.dll", "__set_app_type", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "__setusermatherr", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_amsg_exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "__p___initenv", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return initenv; });
    reg.add("msvcrt.dll", "__p__commode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return commode; });
    reg.add("msvcrt.dll", "__p__fmode", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return fmode; });
    reg.add("msvcrt.dll", "__p___mb_cur_max", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return mb_cur_max; });
    reg.add("msvcrt.dll", "_errno", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return errno_ptr; });
    reg.add("msvcrt.dll", "__p__iob", Conv::Cdecl, 0, [this](Cpu&) -> uint32_t { return iob_addr; });
    // CRT startup: __getmainargs fills _argc/_argv/_environ and the startupinfo
    // pointer. argv[0] is the exe path, as on Windows.
    reg.add("msvcrt.dll", "__getmainargs", Conv::Cdecl, 5, [this](Cpu& c) -> uint32_t {
        uint32_t argc_p = c.arg(0), argv_p = c.arg(1), env_p = c.arg(2), si_p = c.arg(4);
        if (!argc_p || !argv_p) return 0xFFFFFFFFu;
        uint32_t name = put_str(exe_path);
        uint32_t argv = heap.alloc(8); mem.w32(argv, name); mem.w32(argv + 4, 0);
        mem.w32(argc_p, 1); mem.w32(argv_p, argv); if (env_p) mem.w32(env_p, env_ptr);
        if (si_p) mem.w32(si_p, startupinfo);
        return 0; });
    reg.add("msvcrt.dll", "__wgetmainargs", Conv::Cdecl, 5, [this](Cpu& c) -> uint32_t {
        uint32_t argc_p = c.arg(0), argv_p = c.arg(1), env_p = c.arg(2), si_p = c.arg(4);
        if (!argc_p || !argv_p) return 0xFFFFFFFFu;
        uint32_t name = put_wstr(std::u16string(exe_path.begin(), exe_path.end()));
        uint32_t argv = heap.alloc(8); mem.w32(argv, name); mem.w32(argv + 4, 0);
        mem.w32(argc_p, 1); mem.w32(argv_p, argv); if (env_p) mem.w32(env_p, env_ptr);
        if (si_p) mem.w32(si_p, startupinfo);
        return 0; });
    reg.add("msvcrt.dll", "_get_osfhandle", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0);
        return fd == 0 ? Host::H_STDIN : fd == 1 ? Host::H_STDOUT : fd == 2 ? Host::H_STDERR : 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_set_fmode", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { mem.w32(fmode, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_initterm", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0), end = c.arg(1);
        for (; p < end; p += 4) { uint32_t fn = c.mem.r32(p); if (fn) c.call(fn, {}); }
        return 0; });
    reg.add("msvcrt.dll", "_initterm_e", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t {
        uint32_t p = c.arg(0), end = c.arg(1);
        for (; p < end; p += 4) { uint32_t fn = c.mem.r32(p); if (fn) { uint32_t r = c.call(fn, {}); if (r) return r; } }
        return 0; });
    reg.add("msvcrt.dll", "atexit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { atexit_fns.push_back(c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_onexit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { atexit_fns.push_back(c.arg(0)); return c.arg(0); });
    reg.add("msvcrt.dll", "_cexit", Conv::Cdecl, 0, [this](Cpu& c) -> uint32_t { run_atexit(c); return 0; });
    reg.add("msvcrt.dll", "exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { do_exit(c, c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "_exit", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { c.exited = true; c.exit_code = c.arg(0); return 0; });
    reg.add("msvcrt.dll", "abort", Conv::Cdecl, 0, [this](Cpu& c) -> uint32_t { c.exited = true; c.exit_code = 3; return 0; });
    reg.add("msvcrt.dll", "signal", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "setvbuf", Conv::Cdecl, 4, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "fflush", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    // heap
    reg.add("msvcrt.dll", "malloc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("msvcrt.dll", "calloc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(0), sz = c.arg(1); uint64_t total = uint64_t(n) * sz;
        if (total > 0x10000000ull) return 0;
        uint32_t p = heap.alloc(uint32_t(total));
        if (total) { auto z = std::vector<uint8_t>(size_t(total), 0); mem.write_bytes(p, z.data(), uint32_t(total)); }
        return p; });
    reg.add("msvcrt.dll", "free", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    reg.add("msvcrt.dll", "realloc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t { return heap.realloc(c.arg(0), c.arg(1)); });
    reg.add("msvcrt.dll", "_malloc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return heap.alloc(c.arg(0)); });
    reg.add("msvcrt.dll", "_free", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { heap.release(c.arg(0)); return 0; });
    // rotate intrinsics the MSVC CRT exports (games call them via _lrotl)
    reg.add("msvcrt.dll", "_lrotl", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t { uint32_t v = c.arg(0), n = c.arg(1) & 31; return n ? (v << n) | (v >> (32 - n)) : v; });
    reg.add("msvcrt.dll", "_lrotr", Conv::Cdecl, 2, [](Cpu& c) -> uint32_t { uint32_t v = c.arg(0), n = c.arg(1) & 31; return n ? (v >> n) | (v << (32 - n)) : v; });
    // strings/memory
    reg.add("msvcrt.dll", "strlen", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(str(c.arg(0)).size()); });
    reg.add("msvcrt.dll", "strnlen", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); return uint32_t(s.size()) < c.arg(1) ? uint32_t(s.size()) : c.arg(1); });
    reg.add("msvcrt.dll", "strcmp", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "strncmp", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); uint32_t n = c.arg(2);
        int r = a.compare(0, n, b, 0, n); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "strcpy", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); mem.write_bytes(c.arg(0), s.c_str(), uint32_t(s.size() + 1)); return c.arg(0); });
    reg.add("msvcrt.dll", "strncpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); uint32_t n = c.arg(2);
        std::vector<uint8_t> b(n, 0); for (uint32_t i = 0; i < n && i < s.size(); ++i) b[i] = uint8_t(s[i]);
        mem.write_bytes(c.arg(0), b.data(), n); return c.arg(0); });
    reg.add("msvcrt.dll", "strcat", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); a += b;
        mem.write_bytes(c.arg(0), a.c_str(), uint32_t(a.size() + 1)); return c.arg(0); });
    // strchr(s, 0) returns the terminator, so the search covers the NUL that
    // read_cstr drops.
    reg.add("msvcrt.dll", "strchr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char ch = char(c.arg(1));
        size_t p = ch ? s.find(ch) : s.size();
        return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "strrchr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char ch = char(c.arg(1));
        size_t p = ch ? s.rfind(ch) : s.size();
        return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "strstr", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)), n = str(c.arg(1));
        size_t p = s.find(n); return p == std::string::npos ? 0u : c.arg(0) + uint32_t(p); });
    reg.add("msvcrt.dll", "memcpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) mem.write_bytes(d, mem.read_bytes(s, n).data(), n);
        return d; });
    reg.add("msvcrt.dll", "memmove", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) { auto b = mem.read_bytes(s, n); mem.write_bytes(d, b.data(), n); } return d; });
    reg.add("msvcrt.dll", "memset", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), n = c.arg(2); uint8_t v = uint8_t(c.arg(1));
        std::vector<uint8_t> b(n, v); if (n) mem.write_bytes(d, b.data(), n); return d; });
    reg.add("msvcrt.dll", "memcmp", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); auto a = mem.read_bytes(c.arg(0), n), b = mem.read_bytes(c.arg(1), n);
        int r = n ? std::memcmp(a.data(), b.data(), n) : 0; return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "memchr", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t n = c.arg(2); auto b = mem.read_bytes(c.arg(0), n); uint8_t v = uint8_t(c.arg(1));
        for (uint32_t i = 0; i < n; ++i) if (b[i] == v) return c.arg(0) + i;
        return 0; });
    reg.add("msvcrt.dll", "atoi", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(std::atoi(str(c.arg(0)).c_str())); });
    reg.add("msvcrt.dll", "atol", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return uint32_t(std::atol(str(c.arg(0)).c_str())); });
    reg.add("msvcrt.dll", "toupper", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t { int v = int(c.arg(0)); return uint32_t(v >= 'a' && v <= 'z' ? v - 32 : v); });
    reg.add("msvcrt.dll", "tolower", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t { int v = int(c.arg(0)); return uint32_t(v >= 'A' && v <= 'Z' ? v + 32 : v); });
    reg.add("msvcrt.dll", "_stricmp", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1));
        for (auto& ch : a) ch = char(tolower((unsigned char)ch));
        for (auto& ch : b) ch = char(tolower((unsigned char)ch));
        int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    // stdio
    reg.add("msvcrt.dll", "printf", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        std::string s = format_stack(c, str(c.arg(0)), 1); emit(1, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "vprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t ap = c.arg(1);
        std::string s = format_va(str(c.arg(0)), ap); emit(1, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "fprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(0)); if (fi < 0) return 0xFFFFFFFFu;
        std::string s = format_stack(c, str(c.arg(1)), 2); emit(fi, s); return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "vfprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(0)); if (fi < 0) return 0xFFFFFFFFu;
        uint32_t ap = c.arg(2);  // an i386 va_list is a pointer to the first vararg
        std::string s = format_va(str(c.arg(1)), ap); emit(fi, s); return uint32_t(s.size()); });
    // The non-v forms take their varargs on the stack (first one at arg 2 or 3);
    // only the v* forms receive a va_list pointer.
    reg.add("msvcrt.dll", "sprintf", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(1), 2, c.arg(0), -1, true)); });
    reg.add("msvcrt.dll", "_snprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(2), 3, c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "snprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_stack_to(c, c.arg(2), 3, c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "vsprintf", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_va_to(c.arg(1), c.arg(2), c.arg(0), -1, true)); });
    reg.add("msvcrt.dll", "_vsnprintf", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        return uint32_t(format_va_to(c.arg(2), c.arg(3), c.arg(0), int(c.arg(1)), true)); });
    reg.add("msvcrt.dll", "puts", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); emit(1, s + "\n"); return uint32_t(s.size() + 1); });
    reg.add("msvcrt.dll", "fputs", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, str(c.arg(0))); return 0; });
    reg.add("msvcrt.dll", "putchar", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        emit(1, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "fputc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "fwrite", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(3)); if (fi < 0) return 0;
        uint32_t sz = c.arg(1), n = c.arg(2), total = sz * n;
        if (total) { auto b = mem.read_bytes(c.arg(0), total); emit(fi, std::string(b.begin(), b.end())); }
        return n; });
    reg.add("msvcrt.dll", "fread", Conv::Cdecl, 4, [this](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "ferror", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "feof", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("msvcrt.dll", "_fileno", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { int fi = file_index(c.arg(0)); return fi < 0 ? 0xFFFFFFFFu : uint32_t(fi); });
    reg.add("msvcrt.dll", "_isatty", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 1; });
    reg.add("msvcrt.dll", "setbuf", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "_setmode", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0; });
    // The remaining FILE*-based stdio the C++ streams layer calls on the three
    // standard streams. There is no filesystem: fopen answers NULL (the C++
    // fixture never opens files), and the position/character calls on the
    // standard streams behave like their unbuffered counterparts above.
    reg.add("msvcrt.dll", "fopen", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(2); return 0; });  // ENOENT
    reg.add("msvcrt.dll", "_wfopen", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(2); return 0; });
    reg.add("msvcrt.dll", "fclose", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { return file_index(c.arg(0)) < 0 ? 0xFFFFFFFFu : 0u; });
    reg.add("msvcrt.dll", "getc", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t { (void)c; return 0xFFFFFFFFu; });  // EOF: no input
    reg.add("msvcrt.dll", "putc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        emit(fi, std::string(1, char(c.arg(0)))); return c.arg(0) & 0xff; });
    reg.add("msvcrt.dll", "getwc", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "putwc", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        int fi = file_index(c.arg(1)); if (fi < 0) return 0xFFFFFFFFu;
        uint16_t w = uint16_t(c.arg(0)); emit(fi, std::string(1, char(w & 0xff))); return w; });
    reg.add("msvcrt.dll", "ungetc", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "ungetwc", Conv::Cdecl, 2, [](Cpu&) -> uint32_t { return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "fgetpos", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        // fpos_t on i386 msvcrt is a 64-bit value; a console stream has no position.
        if (c.arg(1)) { mem.w32(c.arg(1), 0); mem.w32(c.arg(1) + 4, 0); } return 0; });
    reg.add("msvcrt.dll", "fsetpos", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });  // EBADF
    reg.add("msvcrt.dll", "_lseeki64", Conv::Cdecl, 4, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_read", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t { (void)c; return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_write", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0), buf = c.arg(1), n = c.arg(2);
        if (fd != 1 && fd != 2) { set_last_error(9); return 0xFFFFFFFFu; }
        if (n) { auto b = mem.read_bytes(buf, n); emit(int(fd), std::string(b.begin(), b.end())); }
        return n; });
    reg.add("msvcrt.dll", "_fdopen", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint32_t fd = c.arg(0); return fd <= 2 ? file_ptr(int(fd)) : 0u; });
    // File stat/length on a handle we do not serve: fail like the real API.
    // All three names the mingw line imports: _fstati64 (nix), _fstat64
    // (Debian mingw 13) and _fstat (the 32-bit form CI's runner hit).
    reg.add("msvcrt.dll", "_fstati64", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_fstat64", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_fstat", Conv::Cdecl, 2, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    reg.add("msvcrt.dll", "_filelengthi64", Conv::Cdecl, 1, [this](Cpu&) -> uint32_t { set_last_error(9); return 0xFFFFFFFFu; });
    // wide ctype / collation: the "C" locale answers ASCII-only tables.
    reg.add("msvcrt.dll", "iswctype", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        uint16_t w = uint16_t(c.arg(0)); uint32_t t = c.arg(1);
        // _SPACE=0x1 _PRINT=0x2 _LOWER=0x4 _UPPER=0x8 _DIGIT=0x10 _ALPHA=0x100
        int r = 0;
        if (w == ' ' || (w >= 9 && w <= 13)) r |= 0x1;
        if (w >= 0x20 && w < 0x7f) r |= 0x2;
        if (w >= 'a' && w <= 'z') r |= 0x4 | 0x100;
        if (w >= 'A' && w <= 'Z') r |= 0x8 | 0x100;
        if (w >= '0' && w <= '9') r |= 0x10 | 0x100;
        return uint32_t(t & 0x1ff ? (r & t ? 1 : 0) : 0); });
    reg.add("msvcrt.dll", "towlower", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t {
        uint16_t w = uint16_t(c.arg(0)); return uint32_t(w >= 'A' && w <= 'Z' ? w + 32 : w); });
    reg.add("msvcrt.dll", "towupper", Conv::Cdecl, 1, [](Cpu& c) -> uint32_t {
        uint16_t w = uint16_t(c.arg(0)); return uint32_t(w >= 'a' && w <= 'z' ? w - 32 : w); });
    // strcoll/wcscoll/strxfrm/wcsxfrm: the "C" locale collates by strcmp order.
    reg.add("msvcrt.dll", "strcoll", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        std::string a = str(c.arg(0)), b = str(c.arg(1)); int r = a.compare(b); return uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0); });
    reg.add("msvcrt.dll", "wcscoll", Conv::Cdecl, 2, [this](Cpu& c) -> uint32_t {
        auto wa = wstr(c.arg(0)), wb = wstr(c.arg(1)); int r = wa < wb ? -1 : wb < wa ? 1 : 0; return uint32_t(r); });
    reg.add("msvcrt.dll", "strxfrm", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(1)); uint32_t n = c.arg(2);
        if (c.arg(0) && n) { uint32_t put = uint32_t(s.size()) < n ? uint32_t(s.size()) : n - 1;
            mem.write_bytes(c.arg(0), s.data(), put); mem.w8(c.arg(0) + put, 0); }
        return uint32_t(s.size()); });
    reg.add("msvcrt.dll", "wcsxfrm", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        auto w = wstr(c.arg(1)); uint32_t n = c.arg(2);
        if (c.arg(0) && n) { uint32_t put = uint32_t(w.size()) < n ? uint32_t(w.size()) : n - 1;
            for (uint32_t i = 0; i < put; ++i) { mem.w8(c.arg(0) + i * 2, uint8_t(w[i])); mem.w8(c.arg(0) + i * 2 + 1, uint8_t(w[i] >> 8)); }
            mem.w8(c.arg(0) + put * 2, 0); mem.w8(c.arg(0) + put * 2 + 1, 0); }
        return uint32_t(w.size()); });
    reg.add("msvcrt.dll", "strftime", Conv::Cdecl, 4, [this](Cpu& c) -> uint32_t {
        // size_t strftime(char *dest, size_t maxsize, const char *format,
        // const struct tm *tm) — dest/maxsize first (time.h), format third.
        // A real (deterministic) formatter: reads the guest struct tm
        // (int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday,
        // tm_yday, tm_isdst — the layout time.h has had since 1989) and
        // renders the common specifiers. With the old arg order (format read
        // from the destination) the output was garbage, which the fixture's
        // exact comparison discriminates.
        uint32_t out = c.arg(0), cap = c.arg(1);
        std::string fmt = str(c.arg(2));
        uint32_t tm_p = c.arg(3);
        static const char* wday_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char* mon_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int sec = 0, minute = 0, hour = 0, mday = 1, mon = 0, year = 1970, wday = 4, yday = 0;
        if (tm_p) {
            sec = int32_t(mem.r32(tm_p)); minute = int32_t(mem.r32(tm_p + 4));
            hour = int32_t(mem.r32(tm_p + 8)); mday = int32_t(mem.r32(tm_p + 12));
            mon = int32_t(mem.r32(tm_p + 16)); year = int32_t(mem.r32(tm_p + 20));
            wday = int32_t(mem.r32(tm_p + 24)); yday = int32_t(mem.r32(tm_p + 28));
        }
        std::string r;
        char num[12];
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%') { r.push_back(fmt[i]); continue; }
            if (++i >= fmt.size()) break;
            switch (fmt[i]) {
            case 'Y': snprintf(num, sizeof num, "%d", year + 1900); r += num; break;
            case 'm': snprintf(num, sizeof num, "%02d", mon + 1); r += num; break;
            case 'd': snprintf(num, sizeof num, "%02d", mday); r += num; break;
            case 'H': snprintf(num, sizeof num, "%02d", hour); r += num; break;
            case 'M': snprintf(num, sizeof num, "%02d", minute); r += num; break;
            case 'S': snprintf(num, sizeof num, "%02d", sec); r += num; break;
            case 'a': if (wday >= 0 && wday < 7) r += wday_names[wday]; break;
            case 'b': case 'h': if (mon >= 0 && mon < 12) r += mon_names[mon]; break;
            case 'j': snprintf(num, sizeof num, "%03d", yday + 1); r += num; break;
            case '%': r.push_back('%'); break;
            default: r.push_back('%'); r.push_back(fmt[i]); break;  // unknown: keep both, like a failed conversion
            }
        }
        if (out && cap && r.size() < cap) {
            mem.write_bytes(out, r.data(), uint32_t(r.size()));
            mem.w8(out + uint32_t(r.size()), 0);
            return uint32_t(r.size());
        }
        if (out && cap) mem.w8(out, 0);  // too big: empty string, as the CRT answers
        return 0; });
    // size_t wcsftime(wchar_t *dest, size_t maxsize, const wchar_t *format,
    // const struct tm *tm) — the wide form; a failed conversion answers 0,
    // which the CRT treats as "no time string" (a legal answer).
    reg.add("msvcrt.dll", "wcsftime", Conv::Cdecl, 4, [this](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "strtoul", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        std::string s = str(c.arg(0)); char* e = nullptr;
        unsigned long v = std::strtoul(s.c_str(), &e, int(c.arg(2)));
        if (c.arg(1)) mem.w32(c.arg(1), c.arg(0) + uint32_t(e ? e - s.c_str() : 0));
        return uint32_t(v); });
    reg.add("msvcrt.dll", "getenv", Conv::Cdecl, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "clock", Conv::Cdecl, 0, [](Cpu&) -> uint32_t { return 0; });
    reg.add("msvcrt.dll", "time", Conv::Cdecl, 1, [this](Cpu& c) -> uint32_t {
        uint32_t v = 1760000000u; if (c.arg(0)) mem.w32(c.arg(0), v); return v; });
    reg.add("msvcrt.dll", "qsort", Conv::Cdecl, 4, [](Cpu& c) -> uint32_t {
        uint32_t base = c.arg(0), n = c.arg(1), size = c.arg(2), cmp = c.arg(3);
        if (size == 0 || size > 4096 || uint64_t(n) * size > (1u << 26)) throw Fault(FaultKind::AccessViolation, c.eip, base, "qsort: bad size");
        auto less = [&](uint32_t i, uint32_t j) { return int32_t(c.call(cmp, {base + i * size, base + j * size})) < 0; };
        auto swap = [&](uint32_t i, uint32_t j) { auto a = c.mem.read_bytes(base + i * size, size), b = c.mem.read_bytes(base + j * size, size);
            c.mem.write_bytes(base + i * size, b.data(), size); c.mem.write_bytes(base + j * size, a.data(), size); };
        auto sift = [&](uint32_t root, uint32_t end) {
            for (;;) { uint32_t child = 2 * root + 1; if (child >= end) return;
                if (child + 1 < end && less(child, child + 1)) ++child;
                if (!less(root, child)) return;
                swap(root, child); root = child; } };
        if (n > 1) { for (uint32_t i = n / 2; i-- > 0;) sift(i, n); for (uint32_t e = n - 1; e > 0; --e) { swap(0, e); sift(0, e); } }
        return 0; });

    // ntdll: the primitives a fully static mingw C++ runtime leans on.
    // Keyed events are NT's futex: a single-threaded surface acquires and
    // releases without contention, so both waits answer "woke" and both
    // releases succeed. Everything here matches what mcfgthread's static
    // build and the CRT's once-init call at startup and exit.
    // The mem family: real ntdll.dll exports memcpy/memmove/memset/memchr
    // (msvcrt forwards to it there), so a guest forwarder naming
    // "ntdll.memcpy" resolves to the same behaviour as msvcrt's.
    reg.add("ntdll.dll", "memcpy", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) mem.write_bytes(d, mem.read_bytes(s, n).data(), n);
        return d; });
    reg.add("ntdll.dll", "memmove", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), s = c.arg(1), n = c.arg(2);
        if (n) { auto b = mem.read_bytes(s, n); mem.write_bytes(d, b.data(), n); } return d; });
    reg.add("ntdll.dll", "memset", Conv::Cdecl, 3, [this](Cpu& c) -> uint32_t {
        uint32_t d = c.arg(0), n = c.arg(2); uint8_t v = uint8_t(c.arg(1));
        std::vector<uint8_t> b(n, v); if (n) mem.write_bytes(d, b.data(), n); return d; });
    reg.add("ntdll.dll", "NtWaitForKeyedEvent", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("ntdll.dll", "NtReleaseKeyedEvent", Conv::Stdcall, 4, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });
    reg.add("ntdll.dll", "NtWaitForSingleObject", Conv::Stdcall, 3, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });  // STATUS_WAIT_0
    reg.add("ntdll.dll", "NtDelayExecution", Conv::Stdcall, 2, [this](Cpu& c) -> uint32_t {
        run_pending_threads(c); return 0; });  // STATUS_SUCCESS: a delay is a yield point
    reg.add("ntdll.dll", "NtClose", Conv::Stdcall, 1, [](Cpu&) -> uint32_t { return 0; });
    reg.add("ntdll.dll", "NtDuplicateObject", Conv::Stdcall, 7, [](Cpu&) -> uint32_t { return 0; });
    // Section objects: NtCreateSection/NtMapViewOfSection back a shared
    // memory block (mcfgthread's globals arena). One process, so a section is
    // just a zero-filled guest mapping; the handle records its size.
    // (Section mapping itself backs the loader's memory module path; this
    // surface maps images itself, so an unmapped-section answer keeps callers
    // off that path.)
    // images itself, so an unmapped section answer keeps callers off that path.
    // NtCreateSection(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER,
    // ULONG, ULONG, HANDLE) — seven parameters (winnt.h/winternl.h); the size
    // pointer is the 4th.
    reg.add("ntdll.dll", "NtCreateSection", Conv::Stdcall, 7, [this](Cpu& c) -> uint32_t {
        uint32_t out = c.arg(0), size_p = c.arg(3);
        uint64_t size = size_p ? (uint64_t(mem.r32(size_p + 4)) << 32) | mem.r32(size_p) : 0x10000u;
        if (!size || size > 0x10000000ull) return 0xC0000043u;  // invalid page protection/size
        if (!out) return 0xC000000Du;  // invalid parameter
        uint32_t h = 0x61000000u + ++section_serial;
        sections[h] = uint32_t(size);
        mem.w32(out, h);
        return 0; });
    reg.add("ntdll.dll", "NtMapViewOfSection", Conv::Stdcall, 10, [this](Cpu& c) -> uint32_t {
        auto s = sections.find(c.arg(0));
        if (s == sections.end()) return 0xC0000008u;  // invalid handle
        uint32_t base_out = c.arg(2), view_size_out = c.arg(6);
        uint32_t size = (s->second + 0xfffu) & ~0xfffu;
        if (section_top + size < section_top) return 0xC0000017u;  // no memory
        uint32_t a = section_top; section_top += size;
        mem.map(a, size, PROT_R | PROT_W);
        auto z = std::vector<uint8_t>(size, 0); mem.write_bytes(a, z.data(), size);
        if (base_out) mem.w32(base_out, a);
        if (view_size_out) mem.w32(view_size_out, s->second);
        return 0; });
    reg.add("ntdll.dll", "NtUnmapViewOfSection", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { return 0; });
    reg.add("ntdll.dll", "NtQuerySystemTime", Conv::Stdcall, 1, [this](Cpu& c) -> uint32_t {
        if (c.arg(0)) { mem.w32(c.arg(0), 0x00A8B800u); mem.w32(c.arg(0) + 4, 0x01DC0000u); } return 0; });
    reg.add("ntdll.dll", "NtQueryInformationFile", Conv::Stdcall, 5, [](Cpu&) -> uint32_t { return 0xC0000008u; });  // invalid handle
    reg.add("ntdll.dll", "NtRaiseHardError", Conv::Stdcall, 6, [this](Cpu& c) -> uint32_t { do_exit(c, 3); return 0; });
    reg.add("ntdll.dll", "RtlDllShutdownInProgress", Conv::Stdcall, 0, [](Cpu&) -> uint32_t { return 0; });

    // kernel32/ntdll bridge: the CRT startup asks for the named-object
    // directory handle; a fake handle keeps the caller off the real path.
    reg.add("kernel32.dll", "BaseGetNamedObjectDirectory", Conv::Stdcall, 0, [this](Cpu&) -> uint32_t { return 0x60000100u; });
}

}
