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
    Memory mem; ThunkTable thunks; ComSpace com; Mock mock; Cpu cpu{mem, thunks};
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
    return failures ? 1 : 0;
}
