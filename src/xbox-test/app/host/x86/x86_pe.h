// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 -> guest address space: maps a relocated image (via the existing, bounds
// checked wxl::Pe32 mapper) at its ImageBase, applies per-section protections,
// binds the IAT to host thunk stubs from an explicit allowlist, and builds the
// minimal TEB32/PEB32 + stack a CRT-free entry point needs. Imports outside the
// allowlist are bound to stubs that Fault when *called* (never silently stubbed).
#pragma once
#include <map>
#include "../wxl_pe32.h"
#include "x86_cpu.h"
namespace x86 {
// Guest layout (all fixed, outside every ImageBase a normal exe uses).
constexpr uint32_t STACK_TOP = 0x00300000, STACK_SIZE = 0x00100000;
constexpr uint32_t TEB_ADDR = 0x7FFDE000, PEB_ADDR = 0x7FFDF000;
struct Registry {  // (lowercase dll, lowercase symbol) -> thunk definition
    // Symbol keys are stored lowercase: Windows' loader matches export names
    // case-insensitively, and a guest forward string ("kernel32.Sleep") is
    // lowercased by read_exports before the lookup. The thunk keeps the
    // canonical SDK casing for fault labels ("kernel32!Sleep").
    std::map<std::pair<std::string, std::string>, Thunk> map;
    // Data imports: (lowercase dll, lowercase symbol) -> guest address of the variable.
    // A data import's IAT slot holds the variable's guest address, not a thunk
    // (msvcrt's __initenv/__mb_cur_max/_iob are imported as data by mingw's
    // CRT startup; reading them is a plain memory load).
    std::map<std::pair<std::string, std::string>, uint32_t> data;
    static std::string key_sym(const std::string& name) {
        std::string l = name; for (auto& ch : l) ch = char(std::tolower((unsigned char)ch));
        return l;
    }
    void add(const std::string& dll, const std::string& name, Conv conv, unsigned argc, std::function<uint32_t(Cpu&)> fn) {
        map[{dll, key_sym(name)}] = Thunk{dll, name, conv, argc, std::move(fn)};
    }
    void add_data(const std::string& dll, const std::string& name, uint32_t addr) {
        data[{dll, key_sym(name)}] = addr;
    }
};
struct Image {
    uint32_t base = 0, size = 0, entry = 0;
    unsigned relocations = 0, imports = 0, bound = 0;
    std::vector<std::string> unresolved;
    // Implicit TLS (IMAGE_DIRECTORY_ENTRY_TLS): the fields are absolute VAs as
    // they stand after relocation, since they are read from the mapped image.
    struct Tls {
        bool present = false;
        uint32_t start = 0, end = 0, index_ptr = 0, callbacks = 0, zero_fill = 0;
    } tls;
    bool is_dll = false;
    // Export table of a mapped image (guest addresses): name/ordinal -> RVA.
    // Filled from the PE's export directory so GetProcAddress can serve guest
    // modules without a second copy of the file.
    std::map<std::string, uint32_t> exports;          // lowercase name -> RVA
    std::map<std::string, std::string> forwarded;     // lowercase name -> "dll.symbol"
    std::vector<std::pair<uint32_t, uint32_t>> ordinals;  // ordinal -> RVA
    std::map<uint32_t, std::string> forwarded_by_ord;     // ordinal -> "dll.symbol"
    uint32_t ordinal_base = 0;
    // TLS callbacks (absolute guest addresses), run with DLL_PROCESS_ATTACH
    // before the entry point, as the Windows loader does.
    std::vector<uint32_t> tls_callbacks;
};
// stdcall argument bytes of an unresolved import are unknown; the stub faults before returning.
inline uint32_t u32(const std::vector<uint8_t>& b, size_t p) { return wxl::Pe32::read(b, p); }
inline uint32_t u32(const std::vector<uint8_t>& b, size_t p, unsigned n) { return wxl::Pe32::read(b, p, n); }
// NUL-terminated string at offset `p` of a flat PE image (RVA == index).
inline std::string pe_str(const std::vector<uint8_t>& b, size_t p) {
    if (p >= b.size()) throw std::runtime_error("PE32 string bounds");
    size_t e = p; while (e < b.size() && b[e]) ++e;
    if (e >= b.size()) throw std::runtime_error("PE32 unterminated string");
    return std::string(b.begin() + p, b.begin() + e);
}
// Reads the export directory of a mapped image. `bytes` is the flat image
// (RVA == index); every export address is an RVA, rebased by the caller.
inline void read_exports(Image& img, const std::vector<uint8_t>& bytes) {
    size_t nt = u32(bytes, 0x3c), opt = nt + 24;
    uint32_t exp_rva = u32(bytes, opt + 96 + 0);  // data directory 0: export table
    if (!exp_rva) return;
    img.ordinal_base = u32(bytes, exp_rva + 16);
    uint32_t n_names = u32(bytes, exp_rva + 24), n_funcs = u32(bytes, exp_rva + 20);
    uint32_t names_rva = u32(bytes, exp_rva + 32), funcs_rva = u32(bytes, exp_rva + 28), ords_rva = u32(bytes, exp_rva + 36);
    for (uint32_t i = 0; i < n_names; ++i) {
        uint32_t name_rva = u32(bytes, names_rva + i * 4);
        if (!name_rva) continue;
        std::string name = pe_str(bytes, name_rva);
        // Import lookups lowercase the symbol, so exports are keyed the same
        // way (Windows' own loader matches export names case-insensitively).
        for (auto& ch : name) if (ch >= 'A' && ch <= 'Z') ch += 32;
        uint32_t idx = u32(bytes, ords_rva + i * 2, 2);
        uint32_t fn_rva = u32(bytes, funcs_rva + idx * 4);
        // A forwarded export (RVA inside the export directory) is recorded as-is:
        // the caller resolves the "dll.symbol" string form, as Windows answers.
        if (fn_rva >= exp_rva && fn_rva < exp_rva + 0x10000 && n_funcs) {
            std::string fwd = pe_str(bytes, fn_rva);
            for (auto& ch : fwd) if (ch >= 'A' && ch <= 'Z') ch += 32;
            img.exports[name] = 0;  // marker: forwarded; the name string is kept below
            img.forwarded[name] = fwd;
        } else img.exports[name] = fn_rva;
    }
    for (uint32_t i = 0; i < n_funcs; ++i) {
        uint32_t fn_rva = u32(bytes, funcs_rva + i * 4);
        // A forwarder RVA (inside the export directory) is kept as its string:
        // ordinal lookups must resolve it like name lookups do, never rebase an
        // RVA that is really ASCII text (Bugbot 4559267308).
        if (fn_rva >= exp_rva && fn_rva < exp_rva + 0x10000 && n_funcs)
            img.forwarded_by_ord[img.ordinal_base + i] = pe_str(bytes, fn_rva);
        img.ordinals.push_back({img.ordinal_base + i, fn_rva});
    }
}
inline Image load_pe32(Memory& mem, ThunkTable& table, const Registry& reg, const std::vector<uint8_t>& file, uint32_t load_base = 0) {
    if (file.size() < 0x40) throw std::runtime_error("PE32 truncated");
    size_t nt = u32(file, 0x3c);
    uint32_t preferred = u32(file, nt + 24 + 28);
    wxl::Pe32 pe; pe.map(file, load_base ? load_base : preferred);
    Image img; img.base = pe.logical; img.size = uint32_t(pe.bytes.size()); img.entry = pe.logical + pe.entry; img.relocations = pe.relocations;
    // Sections: headers R; each section gets its own page protections.
    mem.map(img.base, img.size, PROT_R | PROT_W);
    mem.write_bytes(img.base, pe.bytes.data(), img.size);
    // Bind imports while the whole image is still writable.
    uint32_t imp = u32(pe.bytes, nt + 24 + 96 + 8);
    if (imp) for (size_t d = imp;; d += 20) {
        uint32_t name = u32(pe.bytes, d + 12);
        if (!name) break;
        std::string dll = pe.string(name); for (auto& c : dll) if (c >= 'A' && c <= 'Z') c += 32;
        size_t src = u32(pe.bytes, d), dst = u32(pe.bytes, d + 16); if (!src) src = dst;
        for (unsigned j = 0;; ++j) {
            uint32_t t = u32(pe.bytes, src + size_t(j) * 4); if (!t) break;
            std::string sym = (t & 0x80000000u) ? "#" + std::to_string(t & 65535) : pe.string(t + 2);
            ++img.imports;
            // The registry keys symbols lowercase (Windows matches export
            // names case-insensitively); the label keeps the import's casing.
            auto dit = reg.data.find({dll, Registry::key_sym(sym)});
            if (dit != reg.data.end()) { mem.w32(img.base + dst + j * 4, dit->second); ++img.bound; continue; }
            auto it = reg.map.find({dll, Registry::key_sym(sym)}); uint32_t stub;
            if (it != reg.map.end()) { stub = table.add(it->second); ++img.bound; }
            else {
                img.unresolved.push_back(dll + "!" + sym);
                std::string label = dll + "!" + sym;
                stub = table.add(Thunk{dll, sym, Conv::Stdcall, 0, [label](Cpu& c) -> uint32_t {
                    throw Fault(FaultKind::UnresolvedImport, c.eip, 0, "call to unresolved import " + label); }});
            }
            mem.w32(img.base + dst + j * 4, stub);
        }
    }
    // Implicit TLS (data directory 9). Parsed after relocation: the directory's
    // fields are absolute VAs and the callbacks array is a VA list, so both must
    // be read from the relocated image. A malformed directory is an error, not a
    // silent skip (a game that ships one expects its TLS to exist).
    {
        uint32_t tls_rva = u32(pe.bytes, nt + 24 + 96 + 9 * 8), tls_sz = u32(pe.bytes, nt + 24 + 96 + 9 * 8 + 4);
        if (tls_rva || tls_sz) {
            if (tls_sz < 24) throw std::runtime_error("PE32 TLS directory shorter than IMAGE_TLS_DIRECTORY32");
            img.tls.present = true;
            img.tls.start = u32(pe.bytes, tls_rva + 0);
            img.tls.end = u32(pe.bytes, tls_rva + 4);
            img.tls.index_ptr = u32(pe.bytes, tls_rva + 8);
            img.tls.callbacks = u32(pe.bytes, tls_rva + 12);
            img.tls.zero_fill = u32(pe.bytes, tls_rva + 16);
            if (img.tls.end < img.tls.start) throw std::runtime_error("PE32 TLS raw data range inverted");
            if (!img.tls.index_ptr) throw std::runtime_error("PE32 TLS directory lacks AddressOfIndex");
        }
    }
    // Section protections (headers stay read-only). Image pages are
    // copy-on-write: Windows maps image sections copy-on-write, so mingw's
    // runtime pseudo-relocator memcpy into .text (and .rdata) promotes the
    // page instead of faulting. PROT_COW gates the promotion; the
    // interpreter's own W^X code pages never carry it.
    size_t opt = nt + 24, ns = wxl::Pe32::read(pe.bytes, nt + 6, 2), secs = opt + wxl::Pe32::read(pe.bytes, nt + 20, 2);
    mem.protect(img.base, img.size, PROT_R | PROT_COW);  // headers and any gap between sections
    for (size_t i = 0; i < ns; ++i) {
        size_t s = secs + 40 * i; uint32_t va = u32(pe.bytes, s + 12), vs = std::max(u32(pe.bytes, s + 8), u32(pe.bytes, s + 16)), ch = u32(pe.bytes, s + 36);
        uint8_t p = PROT_R | PROT_COW | ((ch & 0x80000000u) ? PROT_W : 0) | ((ch & 0x20000000u) ? PROT_X : 0);
        mem.protect(img.base + va, vs, p);
    }
    // TLS directory (data dir 9): the callback list a static CRT (mingw's
    // mcfgthread) initializes its globals from. The pointer fields inside the
    // directory are absolute VAs (relocated with the image), so each becomes
    // an RVA by subtracting the load base; the callback list is a
    // NUL-terminated array. The index variable is left alone (one thread, so
    // index 0 is never used).
    uint32_t tls_rva = u32(pe.bytes, opt + 96 + 9 * 8);
    if (tls_rva) {
        uint32_t cb_va = u32(pe.bytes, tls_rva + 12);
        if (cb_va >= img.base && cb_va < img.base + img.size)  // a null/odd list is skipped
            for (uint32_t i = 0; i < 64; ++i) {
                uint32_t fn = u32(pe.bytes, cb_va - img.base + i * 4);
                if (!fn) break;
                if (fn < img.base || fn >= img.base + img.size) break;  // malformed list
                img.tls_callbacks.push_back(fn);
            }
    }
    return img;
}
// DLL search: the host supplies the bytes for a guest LoadLibrary by module
// file name. Returning an empty vector reports "module not found" to the
// guest (ERROR_MOD_NOT_FOUND), matching Windows' loader.
using DllSource = std::function<std::vector<uint8_t>(const std::string&)>;
// One loaded DLL: the mapped image plus the LoadLibrary refcount. A real
// unload (refcount to zero -> unmap) is not implemented; FreeLibrary says so.
struct Dll {
    Image image;
    unsigned refs = 1;
    std::string host_name;  // as the guest asked for it, for GetProcAddress diagnostics
};
// Normalizes a forward string's module part for a module-table lookup: Windows
// forward strings carry no extension ("ntdll.memcpy", "MSVCP140.some_fn") while
// module keys are full file names ("ntdll.dll"), and both match
// case-insensitively (pe_str + read_exports already lowercase both parts).
inline std::string forward_module_key(const std::string& mod) {
    std::string l = mod;
    if (l.rfind(".dll") != l.size() - 4) l += ".dll";
    return l;
}
// Resolves `sym` (already lowercase) in a loaded DLL's export table to a guest
// address: an RVA in the image, a forwarded export resolved through the module
// table (one hop, as Windows answers a forward with the target's address), or 0.
// `os_resolve` answers a forward whose target is an OS-served module (e.g.
// "ntdll.memcpy": not in `loaded`); it returns the guest address or 0.
inline uint32_t dll_export_addr(const Dll& dep, const std::string& sym, const std::map<std::string, Dll>& loaded,
                                const std::function<uint32_t(const std::string&, const std::string&)>& os_resolve = nullptr) {
    auto it = dep.image.exports.find(sym);
    if (it == dep.image.exports.end()) return 0;
    auto fwd = dep.image.forwarded.find(sym);
    if (fwd != dep.image.forwarded.end()) {  // "dll.symbol": resolve one hop
        size_t dot = fwd->second.find('.');
        if (dot == std::string::npos) return 0;
        std::string target = forward_module_key(fwd->second.substr(0, dot)), tsym = fwd->second.substr(dot + 1);
        auto m = loaded.find(target);
        if (m != loaded.end()) return dll_export_addr(m->second, tsym, loaded, os_resolve);
        return os_resolve ? os_resolve(target, tsym) : 0;
    }
    return dep.image.base + it->second;  // RVA in the mapped image
}
// Loads a guest PE32 DLL: map at ImageBase, relocations, exports read for
// GetProcAddress, imports bound recursively — a dependency in the registry
// binds to host thunks, one served by `source` loads as another guest DLL
// (depth-capped), anything else stays an UnresolvedImport fault stub. DllMain
// is NOT called here: the caller runs it with the CPU once the thunk page is
// mapped, so a DllMain that itself calls LoadLibrary still works.
// `loaded` is the process module table (lowercase name -> Dll); a dependency
// already loaded is reused and its refcount bumped, as Windows' loader does.
inline Dll load_dll(Memory& mem, ThunkTable& table, const Registry& reg, const std::vector<uint8_t>& file,
                    const DllSource& source, std::map<std::string, Dll>& loaded, unsigned depth = 0,
                    std::vector<std::string>* order = nullptr,
                    const std::function<uint32_t(const std::string&, const std::string&)>& os_resolve = nullptr,
                    const std::string& module_name = {}) {
    if (depth > 16) throw std::runtime_error("DLL dependencies nested too deep");
    // Load at the ImageBase when it is free; otherwise relocate like Windows'
    // loader does for a DLL whose base is taken. Candidate bases stay in the
    // 0x10000000 arena a normal exe never uses (wxl::Pe32 relocates there too).
    size_t nt0 = u32(file, 0x3c);
    uint32_t preferred = u32(file, nt0 + 24 + 28);
    uint32_t load_base = preferred;
    if (!mem.free_range(preferred, uint32_t(file.size()))) {
        uint32_t candidate = 0x10000000u;
        while (candidate < 0xF0000000u && !mem.free_range(candidate, uint32_t(file.size()))) candidate += 0x1000000u;
        if (candidate >= 0xF0000000u) throw std::runtime_error("DLL load: no free base for relocation");
        load_base = candidate;
    }
    // Windows' loader puts a module in the load table BEFORE walking its
    // imports: a cycle (A imports B imports A) must resolve to the
    // in-progress mapping, not map A a second time. Reserve the slot here
    // with the mapped base and complete it after the import walk; the
    // DllMain pass runs only once, for the module the caller asked for.
    Dll* self = nullptr;
    if (!module_name.empty()) {
        // A name collision (same module loaded twice concurrently) cannot
        // happen through the recursive path: the caller checks `loaded` first.
        self = &loaded[module_name];
        if (self->image.base) return *self;  // already mapped: reuse, as the loader does
        self->image.base = load_base;
        self->refs = 1;
    }
    try {
    Image img = load_pe32(mem, table, reg, file, load_base);
    img.is_dll = true;
    // Export and import RVAs index the mapped image, not the raw file, so
    // both walks below read the flat image load_pe32 produced (relocated,
    // RVA == index), copied back out of guest memory.
    std::vector<uint8_t> flat = mem.read_bytes(img.base, img.size);
    read_exports(img, flat);
    if (self) {  // publish the export tables now: a dependency loading later in
        // this walk (a cycle's back edge) must resolve against them, as Windows
        // resolves against the in-progress module's real export table.
        self->image.exports = img.exports;
        self->image.forwarded = img.forwarded;
        self->image.forwarded_by_ord = img.forwarded_by_ord;
        self->image.ordinals = img.ordinals;
        self->image.ordinal_base = img.ordinal_base;
    }
    // load_pe32 bound registry imports and gave every other one a fault stub.
    // Walk the import directory of the flat image again and rebind the slots
    // a now-loaded guest DLL serves. Only entries whose whole DLL is a guest
    // module are candidates: a DLL partly in the registry keeps its registry
    // bindings (rebinding from the guest image would shadow host thunks).
    size_t nt = u32(flat, 0x3c), opt = nt + 24;
    uint32_t imp = u32(flat, opt + 96 + 8);
    if (imp) for (size_t d = imp;; d += 20) {
        uint32_t name = u32(flat, d + 12);
        if (!name) break;
        std::string dll = pe_str(flat, name);
        for (auto& c : dll) if (c >= 'A' && c <= 'Z') c += 32;
        auto known = loaded.find(dll);
        if (known == loaded.end()) {
            std::vector<uint8_t> bytes = source ? source(dll) : std::vector<uint8_t>{};
            if (bytes.empty()) continue;  // stays unresolved; the stub faults by name
            Dll dep = load_dll(mem, table, reg, bytes, source, loaded, depth + 1, order, os_resolve, dll);
            known = loaded.find(dll);  // load_dll reserved the slot (cycles reuse it)
            known->second.host_name = dll;
            if (order) order->push_back(dll);  // load order: DllMain runs in reverse at detach
        } else known->second.refs++;
        // Rebind this dependency's IAT: ordinal or name -> export guest address.
        // load_pe32 already applied section protections, so the IAT pages are
        // read-only now; they go writable for the rebind and back afterwards
        // (the IAT is data a guest must not write, as Windows maps it).
        size_t src = u32(flat, d), dst = u32(flat, d + 16); if (!src) src = dst;
        for (unsigned j = 0;; ++j) {
            uint32_t t = u32(flat, src + size_t(j) * 4); if (!t) break;
            uint32_t addr = 0;
            std::string label = dll + "!" + (t & 0x80000000u ? "#" + std::to_string(t & 65535) : pe_str(flat, t + 2));
            if (t & 0x80000000u) {
                uint32_t ord = t & 65535;
                // An ordinal's EAT RVA can be a forwarder string (it lives in
                // the export directory range): resolve it like a name, never
                // add the image base to ASCII (Bugbot 4559267308).
                auto fo = known->second.image.forwarded_by_ord.find(ord);
                if (fo != known->second.image.forwarded_by_ord.end()) {
                    size_t dot = fo->second.find('.');
                    if (dot != std::string::npos) {
                        std::string target = forward_module_key(fo->second.substr(0, dot)), tsym = fo->second.substr(dot + 1);
                        auto m = loaded.find(target);
                        if (m != loaded.end()) addr = dll_export_addr(m->second, tsym, loaded, os_resolve);
                        else if (os_resolve) addr = os_resolve(target, tsym);
                    }
                } else for (auto& o : known->second.image.ordinals) if (o.first == ord) { addr = known->second.image.base + o.second; break; }
            } else {
                std::string sym = pe_str(flat, t + 2);
                for (auto& ch : sym) if (ch >= 'A' && ch <= 'Z') ch += 32;
                addr = dll_export_addr(known->second, sym, loaded, os_resolve);
            }
            if (addr) {
                uint32_t slot = img.base + dst + j * 4;
                mem.protect(slot & ~4095u, 4096, PROT_R | PROT_W);
                mem.w32(slot, addr); ++img.bound;
                mem.protect(slot & ~4095u, 4096, PROT_R);
                // The fault stub from load_pe32 is replaced: drop the entry
                // from the unresolved list so the runner reports only real gaps.
                auto u = std::find(img.unresolved.begin(), img.unresolved.end(), label);
                if (u != img.unresolved.end()) img.unresolved.erase(u);
            }
        }
    }
    if (self) {  // the reserved slot is completed in place: cycles keep the base
        self->image = std::move(img);
        self->host_name = module_name;
        return *self;
    }
    Dll out; out.image = std::move(img);
    return out;
    } catch (...) {
        // The reservation dies with the load: a failed load must not leave a
        // half-mapped module in the table (a later load would reuse its base).
        if (self) loaded.erase(module_name);
        throw;
    }
}
// Thunk page (read+execute, never fetched through the interpreter), stack, TEB32, PEB32.
// Mapping the page fixes the table's capacity: the process-return sentinel is
// the LAST slot of the mapped page from then on and never moves, so stubs added
// while a host->guest call is in flight (COM proxy creation inside a callback,
// GetProcAddress) can never land on the address that call is waiting to return
// to. Stubs beyond the capacity are refused by ThunkTable::add.
inline void map_thunk_page(Memory& mem, ThunkTable& table) {
    uint32_t len = (table.span() + 4095) & ~4095u;
    if (!table.capacity) table.capacity = len / table.stride - 1;  // last slot: the sentinel
    mem.map(table.base, len, PROT_R | PROT_W);
    std::vector<uint8_t> int3(len, 0xCC); mem.write_bytes(table.base, int3.data(), len);
    mem.protect(table.base, len, PROT_R | PROT_X);
}
inline void init_process(Memory& mem, Cpu& cpu, const Image& img, uint32_t pid = 0x1000, uint32_t tid = 0x1004) {
    mem.map(STACK_TOP - STACK_SIZE, STACK_SIZE, PROT_R | PROT_W);
    mem.map(TEB_ADDR, 4096, PROT_R | PROT_W); mem.map(PEB_ADDR, 4096, PROT_R | PROT_W);
    mem.w32(TEB_ADDR + 0x00, 0xFFFFFFFFu);        // ExceptionList (empty chain)
    mem.w32(TEB_ADDR + 0x04, STACK_TOP);          // StackBase
    mem.w32(TEB_ADDR + 0x08, STACK_TOP - STACK_SIZE);  // StackLimit
    mem.w32(TEB_ADDR + 0x18, TEB_ADDR);           // Self
    mem.w32(TEB_ADDR + 0x20, pid); mem.w32(TEB_ADDR + 0x24, tid);
    mem.w32(TEB_ADDR + 0x30, PEB_ADDR);           // ProcessEnvironmentBlock
    mem.w32(PEB_ADDR + 0x08, img.base);           // ImageBaseAddress
    mem.w32(PEB_ADDR + 0xA4, 10); mem.w32(PEB_ADDR + 0xA8, 0);  // OSMajorVersion/OSMinorVersion
    cpu.fs_base = TEB_ADDR; cpu.reg[ESP] = STACK_TOP - 16; cpu.eip = img.entry;
}
// Implicit TLS for the main thread: allocates the module's TLS block (raw data
// copy, then SizeOfZeroFill zero bytes), points TEB32.ThreadLocalStoragePointer
// (0x2C) at a one-slot array and TEB32.TlsSlots stay for the explicit TlsAlloc
// surface, writes the block address into the image's index variable, and runs
// every TLS callback with DLL_PROCESS_ATTACH (the main thread's ReasonCode is
// also DLL_THREAD_ATTACH-less: Windows calls DLL_THREAD_ATTACH only for threads
// created after the callback registration). Returns the index (0).
// Callbacks are called like the loader does: (handle, reason, reserved) with
// handle = image base, reason = DLL_PROCESS_ATTACH (1), reserved = NULL. Their
// return value is ignored and a callback that faults propagates: a game's TLS
// initializer failing must not look like a clean start.
// Implicit TLS for the main thread, the way the Windows loader sets it up: the
// per-thread block is a fresh allocation, not the image template — the raw data
// ([StartAddressOfRawData, EndAddressOfRawData) in the mapped image) is copied
// there, SizeOfZeroFill bytes are appended zeroed, TEB32.ThreadLocalStoragePointer
// (fs:[0x2C]) points at a one-slot array holding the block address, and the image's
// index variable gets slot 0. Then every TLS callback in the 0-terminated
// AddressOfCallBacks array runs with DLL_PROCESS_ATTACH (the initial thread gets no
// DLL_THREAD_ATTACH). Callback return values are ignored; a callback that faults
// propagates, so a game's TLS initializer failing never looks like a clean start.
// Returns the index (0).
// The TLS region is anchored to end just below the TEB and grows downward with
// the template size (the free gap between the harness's iob page at 0x7FFD0000
// and the TEB is 52 KiB; a template that does not fit is a load error).
inline uint32_t init_tls(Memory& mem, Cpu& cpu, const Image& img, std::vector<uint32_t>* called = nullptr) {
    if (!img.tls.present) return 0;
    uint32_t raw = img.tls.end - img.tls.start;
    // The template lives in the mapped image; reading it is bounds-checked.
    for (uint32_t a = img.tls.start; raw && a < img.tls.end; a += Memory::PAGE)
        if (!mem.mapped(a)) throw std::runtime_error("PE32 TLS raw data outside the mapped image");
    uint32_t total = raw + img.tls.zero_fill;
    // Slot array at +0, the block at 16.
    uint32_t pages = (16 + total + Memory::PAGE - 1) / Memory::PAGE;
    uint32_t slot_addr = TEB_ADDR - pages * Memory::PAGE;
    mem.map(slot_addr, pages * Memory::PAGE, PROT_R | PROT_W);
    uint32_t block = slot_addr + 16;
    mem.w32(slot_addr, block);              // ThreadLocalStoragePointer[0]
    mem.w32(TEB_ADDR + 0x2C, slot_addr);
    for (uint32_t i = 0; i < raw; ++i) mem.w8(block + i, mem.r8(img.tls.start + i));
    for (uint32_t i = raw; i < total; ++i) mem.w8(block + i, 0);  // SizeOfZeroFill
    // The index variable: implicit TLS is slot 0 for the first module.
    mem.w32(img.tls.index_ptr, 0);
    // TLS callbacks, 0-terminated array of VAs, called (handle, reason, reserved).
    for (uint32_t p = img.tls.callbacks; p; p += 4) {
        uint32_t fn = mem.r32(p);
        if (!fn) break;
        if (called) called->push_back(fn);
        cpu.call(fn, {img.base, 1, 0});     // DLL_PROCESS_ATTACH
    }
    return 0;
}
// Runs the entry point like BaseProcessStart: entry(PEB) and an implicit ExitProcess(eax).
inline uint32_t run_entry(Cpu& cpu, const Image& img) {
    init_tls(cpu.mem, cpu, img);  // implicit TLS block + callbacks before the entry point
    uint32_t r = cpu.call(img.entry, {PEB_ADDR});
    if (!cpu.exited) { cpu.exited = true; cpu.exit_code = r; }
    return cpu.exit_code;
}
}
