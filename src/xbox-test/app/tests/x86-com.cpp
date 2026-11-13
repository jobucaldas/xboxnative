// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device tests for the guest COM thunk layer (host/x86/x86com.h): a mock
// host COM object is proxied into guest memory, and i686 machine-code programs
// call its vtable slots. Proves: vtable stubs the guest can read and call,
// stdcall argument pop per slot, marshalling with checked guest pointers, a
// struct translator (the marshaller rewrites a pointer-bearing struct), a
// guest callback invoked from a host method, AddRef/Release refcounts with
// proxy destruction, fault-by-name for an unimplemented slot, and a stale
// proxy faulting after Release. Deterministic, console-free, no Wine.
#include <cstdio>
#include "../host/x86/x86_pe.h"
#include "../host/x86/x86com.h"
using namespace x86;
static int failures = 0;
#define EXPECT(c, ...) do { if (!(c)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// The mock host object: records calls, hands back values.
struct Mock {
    unsigned host_refs = 1;      // the reference the space holds
    unsigned calls = 0;
    uint32_t last_value = 0;
    unsigned released = 0;
    void addref() { ++host_refs; }
    void release() { ++released; }
};

// A tiny flat machine like x86-core.cpp's: code/data/stack pages + thunk page.
struct Machine {
    Memory mem; ThunkTable thunks; Mock mock; ComSpace com; Cpu cpu{mem, thunks};
    static constexpr uint32_t CODE = 0x10000, DATA = 0x20000, STACK = 0x30000;
    Machine() {
        thunks.base = 0x7FFC0000;  // below the COM regions, same page mechanics
        map_thunk_page(mem, thunks);
        mem.map(CODE, 4096, PROT_R | PROT_W);  // rewritten between programs; X added before each run
        mem.map(DATA, 0x10000, PROT_R | PROT_W);
        mem.map(STACK, 4096, PROT_R | PROT_W);
    }
    void write_code(uint32_t addr, const std::vector<uint8_t>& c) {
        mem.write_bytes(addr, c.data(), uint32_t(c.size()));
    }
    // Run from CODE: make it executable, run to the hlt sentinel (an
    // Unsupported fault naming the hlt is the stop), then take X away again.
    bool run_to_hlt(std::string& fault) {
        mem.protect(Machine::CODE, 4096, PROT_R | PROT_X);
        cpu.eip = Machine::CODE; cpu.reg[ESP] = Machine::STACK + 4096 - 64; cpu.exited = false; cpu.depth = 0;
        bool reached = false;
        try { cpu.run(0); } catch (const Fault& f) {
            fault = f.what();
            reached = f.kind == FaultKind::Unsupported;  // the hlt: programs here end in one
        }
        mem.protect(Machine::CODE, 4096, PROT_R | PROT_W);
        return reached;
    }
};

static std::vector<uint8_t> imm(uint32_t v) { return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; }

// A tiny machine-code emitter (used by the sentinel-regression scope below).
struct Asm {
    std::vector<uint8_t> c;
    void push_imm(uint32_t v) { c.push_back(0x68); auto i = imm(v); c.insert(c.end(), i.begin(), i.end()); }
    void mov_eax_imm(uint32_t v) { c.push_back(0xB8); auto i = imm(v); c.insert(c.end(), i.begin(), i.end()); }
    void mov_ecx_eax() { c.push_back(0x8B); c.push_back(0x08); }
    void call_slot(unsigned slot) {
        uint32_t off = slot * 4;
        if (off < 0x80) { c.push_back(0xFF); c.push_back(0x51); c.push_back(uint8_t(off)); }
        else { c.push_back(0xFF); c.push_back(0x91); auto i = imm(off); c.insert(c.end(), i.begin(), i.end()); }
    }
    void mov_ebx_eax() { c.insert(c.end(), {0x89, 0xC3}); }
    void hlt() { c.push_back(0xF4); }
    void com_call(uint32_t proxy, unsigned slot, std::vector<uint32_t> args) {
        for (auto it = args.rbegin(); it != args.rend(); ++it) push_imm(*it);
        push_imm(proxy); mov_eax_imm(proxy); mov_ecx_eax(); call_slot(slot);
    }
};

int main() {
    // ---- setup: one interface, 5 slots ----
    // slot 3: SetValue(value) - one stack arg beyond this; records and returns 7
    // slot 4: FillStruct(ptr)  - writes {a=0x11,b=0x22} through the guest pointer (struct translator shape)
    // slot 5: RunCallback(fn)  - host calls the guest stdcall callback with 42, returns its result
    // slot 6: GetValue(ptrOut) - writes 0xABCD through the out pointer
    // slot 7: left unimplemented -> must fault by name
    Machine m;
    // The marshallers need the Mock; ComSpace is not templated, so the test
    // keeps one Mock and points a static at it.
    static Mock* g_mock = &m.mock;
    m.com.interface_("IMock", 8);
    // The code page stays writable in this harness: the hlt-stop sentinel is
    // detected as an Unsupported fault either way, and W^X page flipping is
    // already covered by x86-core's own tests.
    m.com.method("IMock", 3, 2, [](Proxy& p, Cpu& c) -> uint32_t {
        ++g_mock->calls; g_mock->last_value = c.arg(1); (void)p; return 7; });
    m.com.method("IMock", 4, 2, [](Proxy&, Cpu& c) -> uint32_t {
        uint32_t p = c.arg(1);
        c.mem.w32(p + 0, 0x11); c.mem.w32(p + 4, 0x22);   // the "translated" struct
        return 0; });
    m.com.method("IMock", 5, 2, [](Proxy&, Cpu& c) -> uint32_t {
        uint32_t fn = c.arg(1);
        return c.call(fn, {42});                            // host -> guest callback
    });
    m.com.method("IMock", 6, 2, [](Proxy&, Cpu& c) -> uint32_t {
        c.mem.w32(c.arg(1), 0xABCD); return 0; });

    uint32_t proxy = m.com.create(m.cpu, "IMock", &m.mock, [&] { m.mock.addref(); }, [&] { m.mock.release(); });
    EXPECT(proxy != 0, "proxy created");
    EXPECT(m.mem.r32(proxy) != 0, "proxy block points at its vtable");
    uint32_t vtable = m.mem.r32(proxy);
    // The guest can read every vtable slot and they are distinct stub addresses.
    for (unsigned s = 1; s < 8; ++s) EXPECT(m.mem.r32(vtable + s * 4) != m.mem.r32(vtable + (s - 1) * 4), "slot %u stub distinct", s);
    EXPECT(m.mock.host_refs == 2, "host object got its proxy reference (refs=%u)", m.mock.host_refs);

    // ---- guest program 1: call slot 3 (SetValue) with 0x1234, then slot 6 (GetValue) ----
    // esp-relative call through the vtable: mov eax,[proxy]; mov ecx,[eax]; push 0x1234; push eax; call [ecx+12]
    // stdcall pops its 2 dwords; then GetValue: push out_ptr; push eax; call [ecx+24]
    uint32_t out_ptr = Machine::DATA + 0x100;
    std::vector<uint8_t> code;
    auto push_imm = [](std::vector<uint8_t>& c, uint32_t v) { c.push_back(0x68); auto i = imm(v); c.insert(c.end(), i.begin(), i.end()); };
    push_imm(code, 0x1234);                       // push value
    push_imm(code, proxy);                        // push this
    code.push_back(0xB8); auto pv = imm(proxy); code.insert(code.end(), pv.begin(), pv.end());   // mov eax, proxy
    code.push_back(0x8B); code.insert(code.end(), {0x08});                                       // mov ecx,[eax]
    code.push_back(0xFF); code.insert(code.end(), {0x51, 12});                                   // call [ecx+12] (slot 3)
    code.insert(code.end(), {0x89, 0xC3});                                                       // mov ebx, eax (save hr)
    push_imm(code, out_ptr);                      // push out
    push_imm(code, proxy);                        // push this
    code.push_back(0xB8); code.insert(code.end(), pv.begin(), pv.end());                         // mov eax, proxy
    code.push_back(0x8B); code.insert(code.end(), {0x08});
    code.push_back(0xFF); code.insert(code.end(), {0x51, 24});                                   // call [ecx+24] (slot 6)
    code.push_back(0xF4);                   // hlt -> stop sentinel
    m.write_code(Machine::CODE, code);
    std::string fault;
    uint32_t esp0 = Machine::STACK + 4096 - 64;
    bool ran = m.run_to_hlt(fault);
    EXPECT(ran, "guest run faulted: %s", fault.c_str());
    if (ran) {
        EXPECT(m.cpu.reg[EBX] == 7, "slot 3 returned 7 (got %u)", m.cpu.reg[EBX]);
        EXPECT(m.mock.calls == 1 && m.mock.last_value == 0x1234, "marshaller saw the guest argument");
        EXPECT(m.mem.r32(out_ptr) == 0xABCD, "slot 6 wrote through the out pointer");
        EXPECT(m.cpu.reg[ESP] == esp0, "stdcall args popped by the stubs (esp delta %d)", int(m.cpu.reg[ESP]) - int(esp0));
    }

    // ---- guest program 2: slot 4 fills a struct through a guest pointer ----
    uint32_t st = Machine::DATA + 0x200;
    std::vector<uint8_t> code2;
    push_imm(code2, st); push_imm(code2, proxy);
    code2.push_back(0xB8); code2.insert(code2.end(), pv.begin(), pv.end());
    code2.push_back(0x8B); code2.insert(code2.end(), {0x08});
    code2.push_back(0xFF); code2.insert(code2.end(), {0x51, 16});   // call [ecx+16] (slot 4)
    code2.push_back(0xF4);
    m.write_code(Machine::CODE, code2);
    ran = m.run_to_hlt(fault);
    EXPECT(ran, "struct slot run faulted: %s", fault.c_str());
    if (ran) EXPECT(m.mem.r32(st) == 0x11 && m.mem.r32(st + 4) == 0x22, "struct translator wrote the guest struct");

    // ---- guest program 3: slot 5 calls a guest stdcall callback (host -> guest -> host nesting) ----
    // callback at CODE+0x200: return arg*2 (stdcall, ret 4)
    std::vector<uint8_t> cb{0x8B, 0x44, 0x24, 0x04, 0x01, 0xC0, 0xC2, 0x04, 0x00};  // mov eax,[esp+4]; add eax,eax; ret 4
    m.write_code(Machine::CODE + 0x200, cb);
    std::vector<uint8_t> code3;
    push_imm(code3, Machine::CODE + 0x200); push_imm(code3, proxy);
    code3.push_back(0xB8); code3.insert(code3.end(), pv.begin(), pv.end());
    code3.push_back(0x8B); code3.insert(code3.end(), {0x08});
    code3.push_back(0xFF); code3.insert(code3.end(), {0x51, 20});   // call [ecx+20] (slot 5)
    code3.push_back(0xF4);
    m.write_code(Machine::CODE, code3);
    ran = m.run_to_hlt(fault);
    EXPECT(ran, "callback slot run faulted: %s", fault.c_str());
    if (ran) EXPECT(m.cpu.reg[EAX] == 84, "host method called the guest callback and returned its 84 (got %u)", m.cpu.reg[EAX]);

    // ---- slot 7 is unimplemented: calling it faults naming the interface and slot ----
    std::vector<uint8_t> code4;
    push_imm(code4, 0); push_imm(code4, proxy);
    code4.push_back(0xB8); code4.insert(code4.end(), pv.begin(), pv.end());
    code4.push_back(0x8B); code4.insert(code4.end(), {0x08});
    code4.push_back(0xFF); code4.insert(code4.end(), {0x51, 28});   // call [ecx+28] (slot 7)
    code4.push_back(0xF4);
    m.write_code(Machine::CODE, code4);
    fault.clear();
    bool named = false;
    try { m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_X); m.cpu.eip = Machine::CODE;
          m.cpu.reg[ESP] = Machine::STACK + 4096 - 64; m.cpu.exited = false; m.cpu.depth = 0; m.cpu.run(0); }
    catch (const Fault& f) { named = f.kind == FaultKind::UnresolvedImport && std::string(f.what()).find("IMock slot 7") != std::string::npos; }
    m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_W);
    EXPECT(named, "unimplemented slot faults by name (got: %s)", fault.c_str());

    // ---- AddRef/Release through the vtable; a stale proxy faults ----
    std::vector<uint8_t> code5;
    // eax = proxy is loaded once; ecx = vtable stays valid across the calls, so
    // the later calls go through [ecx+8] without re-reading [eax]. Each call
    // pushes its own stdcall argument (the stub pops it).
    code5.push_back(0xB8); code5.insert(code5.end(), pv.begin(), pv.end());  // mov eax, proxy
    code5.push_back(0x8B); code5.insert(code5.end(), {0x08});                // mov ecx,[eax]
    auto push_proxy = [&] { code5.push_back(0x68); code5.insert(code5.end(), pv.begin(), pv.end()); };  // push proxy (imm32)
    push_proxy();
    code5.push_back(0xFF); code5.insert(code5.end(), {0x51, 4});     // call [ecx+4] (AddRef)
    code5.insert(code5.end(), {0x89, 0xC7});                                 // mov edi, eax
    push_proxy();
    code5.push_back(0xFF); code5.insert(code5.end(), {0x51, 8});     // call [ecx+8] (Release)
    code5.insert(code5.end(), {0x89, 0xC6});                                 // mov esi, eax
    push_proxy();
    code5.push_back(0xFF); code5.insert(code5.end(), {0x51, 8});     // Release again -> destroys the proxy
    code5.push_back(0xF4);
    m.write_code(Machine::CODE, code5);
    ran = m.run_to_hlt(fault);
    EXPECT(ran, "refcount run faulted: %s", fault.c_str());
    if (ran) {
        EXPECT(m.cpu.reg[EDI] == 2, "AddRef returned 2 (1 initial + 1; got %u)", m.cpu.reg[EDI]);
        EXPECT(m.cpu.reg[ESI] == 1, "first Release returned 1 (got %u)", m.cpu.reg[ESI]);
        EXPECT(m.mock.released == 1, "proxy destroyed at zero: host object released once");
        EXPECT(m.com.live() == 0, "identity map is empty after the last Release");
        // A call through the stale pointer is a named fault, never a host crash.
        std::vector<uint8_t> code6;
        push_imm(code6, proxy);
        code6.push_back(0xB8); code6.insert(code6.end(), pv.begin(), pv.end());
        code6.push_back(0x8B); code6.insert(code6.end(), {0x08});
        code6.push_back(0xFF); code6.insert(code6.end(), {0x51, 4});
        code6.push_back(0xF4);
        m.write_code(Machine::CODE, code6);
        bool stale = false;
        try { m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_X); m.cpu.eip = Machine::CODE;
              m.cpu.reg[ESP] = Machine::STACK + 4096 - 64; m.cpu.exited = false; m.cpu.depth = 0; m.cpu.run(0); }
        catch (const Fault& f) { stale = f.kind == FaultKind::AccessViolation && std::string(f.what()).find("released or foreign proxy") != std::string::npos; }
        m.mem.protect(Machine::CODE, 4096, PROT_R | PROT_W);
        EXPECT(stale, "stale proxy call faults by name");
    }

    printf(failures ? "X86-COM FAIL: %d failure(s)\n" : "X86-COM PASS: vtable stubs, stdcall pops, struct translator, guest callback, refcounts, fault-by-name, stale-proxy fault\n", failures);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }

    // ---- teardown: the engine releases every live proxy's host reference exactly once ----
    {
        struct Counting {
            unsigned refs = 0, releases = 0;
            void addref() { ++refs; }
            void release() { ++releases; if (refs) --refs; }
        };
        // Both hosts declared BEFORE the ComSpace members: reverse-declaration
        // order keeps them alive through com2's destructor, whether teardown
        // runs explicitly (destroy_all below) or in the destructor itself.
        Counting c, c2;
        // A fresh engine per scope: its destructor is the teardown under test.
        Memory mem2; ThunkTable thunks2; ComSpace com2; Cpu cpu2{mem2, thunks2};
        thunks2.base = 0x7FFA0000;
        map_thunk_page(mem2, thunks2);
        mem2.map(0x40000, 4096, PROT_R | PROT_W);
        com2.interface_("IMock", 8);
        uint32_t p1 = com2.create(cpu2, "IMock", &c, [&] { c.addref(); }, [&] { c.release(); });
        // The second create of the same host object is the identity map's
        // dedupe path: same proxy, one more guest ref, no second host addref.
        uint32_t p2 = com2.create(cpu2, "IMock", &c, [&] { c.addref(); }, [&] { c.release(); });
        // A distinct host object gets its own proxy even on the same interface.
        uint32_t p3 = com2.create(cpu2, "IMock", &c2, [&] { c2.addref(); }, [&] { c2.release(); });
        EXPECT(p1 && p2 && p3 && p1 == p2 && p2 != p3, "dedupe: same host+iface returns the same proxy, a new host gets a new one");
        EXPECT(c.refs == 1 && c2.refs == 1, "one host ref per proxy (c=%u c2=%u)", c.refs, c2.refs);
        // The guest AddRefs the deduped proxy, then the engine is torn down with
        // both live: each host object must reach zero exactly once.
        com2.by_guest(p2).guest_refs += 1;   // what a guest AddRef leaves behind
        com2.destroy_all();
        EXPECT(c.refs == 0 && c.releases == 1, "host object fully released on teardown (refs=%u releases=%u)", c.refs, c.releases);
        EXPECT(c2.refs == 0 && c2.releases == 1, "second host object released once (refs=%u releases=%u)", c2.refs, c2.releases);
        EXPECT(com2.live() == 0, "identity map empty after teardown");
        // A second destroy_all is a no-op (idempotent): no double release.
        com2.destroy_all();
        EXPECT(c.releases == 1 && c2.releases == 1, "teardown is idempotent");
        // Any later use of a proxy faults loudly instead of double-releasing.
        bool loud = false;
        try { com2.by_guest(p1); }
        catch (const Fault& f) { loud = f.kind == FaultKind::AccessViolation && std::string(f.what()).find("torn down") != std::string::npos; }
        EXPECT(loud, "use after teardown faults by name");
        bool loud_release = false;
        try { com2.guest_release(com2.by_guest(p1)); }
        catch (const Fault& f) { loud_release = f.kind == FaultKind::AccessViolation; }
        EXPECT(loud_release, "Release after teardown faults instead of double-releasing");
        // And create after teardown refuses rather than building on a dead space.
        bool refused = false;
        try { com2.create(cpu2, "IMock", &c, [&] { c.addref(); }, [&] { c.release(); }); }
        catch (const std::exception&) { refused = true; }
        EXPECT(refused, "create after teardown is refused");
    }

    // ---- churn: 100000 create/destroy cycles over two interfaces ----
    // Freed proxy blocks are recycled, so the block window stays at a small
    // bound and a live proxy is never corrupted by a later create.
    {
        struct Counting {
            unsigned refs = 0, releases = 0;
            void addref() { ++refs; }
            void release() { ++releases; if (refs) --refs; }
        };
        // Hosts for the exhaustion leg live in unique_ptrs owned by a vector
        // declared BEFORE the ComSpace members: reverse-declaration order means
        // com2's teardown (releasing every live proxy's host ref) runs while
        // every Counting is still alive. The lambdas capture the stable
        // unique_ptr element, never `hosts.back()` — a vector reallocates as
        // it grows, which would leave earlier captures dangling.
        std::vector<std::unique_ptr<Counting>> exhaust_hosts;
        // hostA/hostB also outlive com2: same reverse-declaration rule.
        Counting hostA, hostB;
        Memory mem2; ThunkTable thunks2; ComSpace com2; Cpu cpu2{mem2, thunks2};
        thunks2.base = 0x7FF90000;
        map_thunk_page(mem2, thunks2);
        com2.interface_("IMock", 8);
        com2.interface_("IChurn", 5);
        com2.method("IChurn", 3, 2, [](Proxy&, Cpu& c) -> uint32_t { return c.arg(1); });
        uint32_t liveA = com2.create(cpu2, "IChurn", &hostA, [&] { hostA.addref(); }, [&] { hostA.release(); });
        uint32_t liveB = com2.create(cpu2, "IMock", &hostB, [&] { hostB.addref(); }, [&] { hostB.release(); });
        unsigned live_before = com2.live();
        for (unsigned i = 0; i < 100000; ++i) {
            Counting tmp;
            uint32_t pa = com2.create(cpu2, "IChurn", &tmp, [&] { tmp.addref(); }, [&] { tmp.release(); });
            uint32_t pb = com2.create(cpu2, "IMock", &tmp, [&] { tmp.addref(); }, [&] { tmp.release(); });
            com2.guest_release(com2.by_guest(pa));
            com2.guest_release(com2.by_guest(pb));
        }
        // The two live proxies are intact and the map holds exactly them.
        EXPECT(com2.by_guest(liveA).host == &hostA && com2.by_guest(liveB).host == &hostB,
               "churn never corrupts a live proxy");
        EXPECT(com2.live() == live_before, "live count stable across churn");
        EXPECT(com2.by_guest(liveA).guest_refs == 1 && com2.by_guest(liveB).guest_refs == 1,
               "live proxy refcounts survived the churn");
        // Exhausting the window with live proxies faults by name, no overwrite.
        // (Hosts stay alive and distinct: a freed object's address would be
        // reused by the allocator and hit the dedupe path instead.)
        bool exhausted = false;
        try {
            for (unsigned i = 0; i < 300000; ++i) {
                exhaust_hosts.push_back(std::make_unique<Counting>());
                com2.create(cpu2, "IChurn", exhaust_hosts.back().get(),
                            [h = exhaust_hosts.back().get()] { h->addref(); },
                            [h = exhaust_hosts.back().get()] { h->release(); });
            }
        } catch (const Fault& f) { exhausted = std::string(f.what()).find("proxy block window exhausted") != std::string::npos; }
        EXPECT(exhausted, "exhausting the block window with live proxies faults by name");
        // The live proxies survived the exhaustion attempt untouched.
        EXPECT(com2.by_guest(liveA).host == &hostA && com2.by_guest(liveB).host == &hostB,
               "live proxies intact after exhaustion fault");
        // Scope end: com2 is destroyed BEFORE exhaust_hosts and hostA/hostB
        // (all declared before it), so teardown releases into live objects —
        // under ASan this whole block is the proof.
    }

    // ---- proxy created inside a host->guest callback: the sentinel must hold ----
    // The first create of an interface adds vtable stubs to the thunk table.
    // If that happens while a host->guest call is in flight (a guest callback
    // that instantiates an interface and immediately QIs it), the new stubs
    // land just past the in-flight return address — with a size-derived
    // sentinel the next stub WOULD sit on that return address and the nested
    // run would treat the stub as callback completion. Mapping the thunk page
    // pins the sentinel to the page's last slot, so the dispatch always runs.
    {
        static constexpr uint32_t CODE2 = 0x40000, CODE3 = 0x41000, DATA2 = 0x50000, STACK2 = 0x60000;
        // Host declared before the ComSpace members (reverse-declaration rule);
        // its proxies carry no host_release, but the ordering must hold anyway.
        struct H {} late_host;
        static void* s_late_host = &late_host;
        Memory mem2; ThunkTable thunks2; ComSpace com2; Cpu cpu2{mem2, thunks2};
        thunks2.base = 0x7FF80000;
        map_thunk_page(mem2, thunks2);   // pins the sentinel BEFORE any stub exists
        mem2.map(CODE2, 4096, PROT_R | PROT_W);
        mem2.map(CODE3, 4096, PROT_R | PROT_W);
        mem2.map(DATA2, 4096, PROT_R | PROT_W);
        mem2.map(STACK2, 4096, PROT_R | PROT_W);
        com2.interface_("ILate", 4);
        static ComSpace* s_com = &com2; static Cpu* s_cpu = &cpu2;
        // ITrigger slot 3: the host marshaller creates the FIRST ILate proxy
        // (stub-building mid-call), then calls guest code that QIs it through
        // its brand-new vtable. The QI must dispatch (E_NOINTERFACE, out=0).
        com2.interface_("ITrigger", 4);
        com2.method("ITrigger", 3, 2, [](Proxy&, Cpu& c) -> uint32_t {
            uint32_t late = s_com->create(*s_cpu, "ILate", s_late_host, nullptr, nullptr);
            std::vector<uint8_t> q{0xA1};                            // mov eax,[disp32] (late)
            auto la = imm(late); q.insert(q.end(), la.begin(), la.end());
            q.insert(q.end(), {0x89, 0xC1});                         // mov ecx,eax (the vtable)
            q.push_back(0x68);                                       // push out
            auto o = imm(DATA2); q.insert(q.end(), o.begin(), o.end());
            q.push_back(0x68); q.insert(q.end(), {0, 0, 0, 0});      // push riid (null)
            q.push_back(0x68); auto pa = imm(late); q.insert(q.end(), pa.begin(), pa.end());
            q.push_back(0xFF); q.push_back(0x51); q.push_back(0);  // call [ecx+0] (QI)
            q.push_back(0xC3);                                     // ret
            s_cpu->mem.protect(CODE3, 4096, PROT_R | PROT_W);      // the page is X-only during the run
            s_cpu->mem.write_bytes(CODE3, q.data(), uint32_t(q.size()));
            s_cpu->mem.protect(CODE3, 4096, PROT_R | PROT_X);
            uint32_t hr = c.call(CODE3, {});
            s_cpu->mem.protect(CODE3, 4096, PROT_R | PROT_W);
            return (hr == 0x80004002u && s_cpu->mem.r32(DATA2) == 0) ? 0x1234 : 0xBADu;
        });
        uint32_t trigger = com2.create(cpu2, "ITrigger", &late_host, nullptr, nullptr);
        // Guest program: push 0 arg; push trigger; call [ecx+12] (slot 3); mov ebx,eax; hlt.
        Asm a4;
        a4.com_call(trigger, 3, {0});
        a4.mov_ebx_eax(); a4.hlt();
        mem2.protect(CODE2, 4096, PROT_R | PROT_W);
        mem2.write_bytes(CODE2, a4.c.data(), uint32_t(a4.c.size()));
        std::string late_fault;
        mem2.protect(CODE2, 4096, PROT_R | PROT_X);
        cpu2.eip = CODE2; cpu2.reg[ESP] = STACK2 + 4096 - 64; cpu2.exited = false; cpu2.depth = 0;
        bool late_ran = false;
        try { cpu2.run(0); late_ran = true; }
        catch (const Fault& f) {
            late_fault = f.what();
            late_ran = f.kind == FaultKind::Unsupported;   // the hlt stop sentinel
        }
        mem2.protect(CODE2, 4096, PROT_R | PROT_W);
        EXPECT(late_ran, "sentinel-regression run faulted: %s", late_fault.c_str());

        if (late_ran)
            EXPECT(cpu2.reg[EBX] == 0x1234, "QI on a proxy created mid-callback dispatched (got 0x%x)", cpu2.reg[EBX]);
    }

    // ---- temporary interface name: the table owns the name, not the caller ----
    // create/interface_ keep pointers into their own storage; a caller passing
    // a temporary std::string::c_str() that dies right after must still leave
    // a working proxy (dedupe, dispatch, fault names all use the interned copy).
    {
        Memory mem2; ThunkTable thunks2; ComSpace com2; Cpu cpu2{mem2, thunks2};
        thunks2.base = 0x7FF70000;
        map_thunk_page(mem2, thunks2);
        com2.interface_("ITemp", 4);
        com2.method("ITemp", 3, 2, [](Proxy&, Cpu& c) -> uint32_t { return c.arg(1) + 1; });
        uint32_t temp_proxy = 0;
        {
            std::string temp = "ITemp";      // dies at the end of this block
            temp_proxy = com2.create(cpu2, temp.c_str(), &temp_proxy, nullptr, nullptr);
        }
        EXPECT(temp_proxy != 0, "proxy created with a temporary name");
        // Dedupe with a DIFFERENT temporary holding the same characters must
        // find the interned proxy (no second host ref, no second proxy).
        struct H {} h2; (void)h2;
        {
            std::string again = "ITemp";
            uint32_t p2 = com2.create(cpu2, again.c_str(), &temp_proxy, nullptr, nullptr);
            EXPECT(p2 == temp_proxy, "dedupe works after the original name string died");
        }
        // Dispatch through the proxy still works (the stub lambda's name copy).
        // slot 3: push 41; push proxy; call [ecx+12] -> returns 42.
        Asm a5;
        a5.com_call(temp_proxy, 3, {41});
        a5.mov_ebx_eax(); a5.hlt();
        mem2.map(0x40000, 4096, PROT_R | PROT_W);
        mem2.map(0x50000, 4096, PROT_R | PROT_W);   // the stack for this scope
        mem2.write_bytes(0x40000, a5.c.data(), uint32_t(a5.c.size()));
        mem2.protect(0x40000, 4096, PROT_R | PROT_X);
        cpu2.eip = 0x40000; cpu2.reg[ESP] = 0x50000 + 4096 - 64; cpu2.exited = false; cpu2.depth = 0;
        std::string name_fault;
        bool name_ran = false;
        try { cpu2.run(0); name_ran = true; }
        catch (const Fault& f) { name_fault = f.what(); name_ran = f.kind == FaultKind::Unsupported; }
        mem2.protect(0x40000, 4096, PROT_R | PROT_W);
        EXPECT(name_ran && cpu2.reg[EBX] == 42, "dispatch after the name string died (ran=%d ebx=%u fault=%s)",
               name_ran, cpu2.reg[EBX], name_fault.c_str());
    }

    // ---- a ThunkTable::add failure mid-build leaves the interface retryable ----
    // The vtable build adds every slot's thunk BEFORE marking the interface
    // built; if add() throws partway (page full), the interface must look
    // unbuilt (vtable==0) so the next create retries from scratch instead of
    // handing out a vtable with missing slots.
    {
        Memory mem2; ThunkTable thunks2; ComSpace com2; Cpu cpu2{mem2, thunks2};
        thunks2.base = 0x7FF60000;
        map_thunk_page(mem2, thunks2);
        // Shrink the visible capacity so the build runs out of slots mid-way:
        // an 8-slot interface needs 8 entries; set capacity to a value the
        // pre-existing entries + a few of ours can overflow.
        thunks2.capacity = 4;   // 4 stub slots before "x86 thunk page full"
        com2.interface_("IPartial", 8);
        struct H {} h; (void)h;
        bool threw = false;
        try { com2.create(cpu2, "IPartial", &h, nullptr, nullptr); }
        catch (const std::exception& e) { threw = std::string(e.what()).find("thunk page full") != std::string::npos; }
        EXPECT(threw, "vtable build that overflows the thunk page throws");
        // The interface must NOT be marked built: no vtable reserved, and the
        // window did not advance past the failed attempt.
        EXPECT(com2.by_name("IPartial").vtable == 0, "failed build leaves the interface unbuilt");
        // Retry with room: same space, capacity raised — the build succeeds
        // from scratch and the proxy works.
        thunks2.capacity = 64;
        uint32_t p = com2.create(cpu2, "IPartial", &h, nullptr, nullptr);
        EXPECT(p != 0, "retry after a failed build succeeds");
        EXPECT(com2.by_name("IPartial").vtable != 0, "retry builds the vtable");
        // Every slot of the built vtable is a distinct valid stub address.
        uint32_t vt = com2.by_name("IPartial").vtable;
        for (unsigned s = 0; s < 8; ++s)
            EXPECT(cpu2.thunks.at(cpu2.mem.r32(vt + s * 4)) != nullptr, "slot %u stub valid after retry", s);
        // A failed build must also roll the TABLE back: capacity is fixed, so
        // leaked partial stubs would shrink the page for every later build.
        // Rebuild the same scenario with a different interface: several failed
        // builds in a row, then a successful one — all within the original
        // capacity (4 + 8 slots, never raised again).
        {
            Memory mem3; ThunkTable thunks3; ComSpace com3; Cpu cpu3{mem3, thunks3};
            thunks3.base = 0x7FF50000;
            map_thunk_page(mem3, thunks3);
            thunks3.capacity = 8;    // 4 seeds + only 4 of the 8 slots: every build dies halfway
            for (unsigned i = 0; i < 4; ++i) thunks3.add(Thunk{"seed", "s", Conv::Cdecl, 0, [](Cpu&) -> uint32_t { return 0; }});
            com3.interface_("IRoll", 8);
            struct R {} r; (void)r;
            unsigned failures = 0;
            for (unsigned attempt = 0; attempt < 5; ++attempt) {
                try { com3.create(cpu3, "IRoll", &r, nullptr, nullptr); }
                catch (const std::exception& e) {
                    if (std::string(e.what()).find("thunk page full") != std::string::npos) ++failures;
                    else throw;
                }
                // Each failed build left no debris: the table is back to 4.
                EXPECT(cpu3.thunks.entries.size() == 4, "failed build %u leaves no stubs in the table", attempt);
            }
            EXPECT(failures == 5, "all 5 attempts failed with a full page (%u)", failures);
            // Free the seed slots: the successful build must fit at the ORIGINAL
            // capacity (8, never raised) — impossible if any failed build had
            // leaked its partial stubs (5 leaks of 4 slots each would not).
            thunks3.entries.resize(0);
            uint32_t p2 = com3.create(cpu3, "IRoll", &r, nullptr, nullptr);
            EXPECT(p2 != 0, "successful build after 5 failed ones fits original capacity");
        }
    }

    printf(failures ? "X86-COM FAIL: %d failure(s) after teardown checks\n" : "X86-COM PASS: vtable stubs, stdcall pops, struct translator, guest callback, refcounts, fault-by-name, stale-proxy fault, teardown releases host refs once\n", failures);
    return failures ? 1 : 0;
}
