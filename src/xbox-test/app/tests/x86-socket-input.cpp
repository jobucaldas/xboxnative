// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest Winsock ordinal and XInput ordinal contract test.
#include <cstdio>
#include <initializer_list>
#include "../host/x86/x86_guest.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

int main() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000;
    bool xinput_called = false; host.xinput_call = [&](Host::XInputCall& q) { if (q.kind == Host::XInputCall::Kind::GetCapabilitiesEx) { q.result = 0; q.output.assign(32, 0xab); return; } xinput_called = q.kind == Host::XInputCall::Kind::GetState && q.index == 1; q.result = 1167; };
    guest.register_all(); Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = guest.cur_teb;
    struct Route { unsigned ordinal; const char* name; unsigned argc; } routes[] = {
        {1,"accept",3}, {101,"WSAAsyncSelect",4}, {11,"inet_addr",1}, {12,"inet_ntoa",1}, {13,"listen",2},
        {15,"ntohs",1}, {16,"recv",4}, {17,"recvfrom",6}, {19,"send",4}, {2,"bind",3}, {20,"sendto",6},
        {22,"shutdown",2}, {23,"socket",3}, {3,"closesocket",1}, {4,"connect",3}, {51,"gethostbyaddr",3},
        {52,"gethostbyname",1}, {9,"htons",1},
    };
    for (const auto& route : routes) {
        auto it = reg.map.find({"ws2_32.dll", Registry::key_sym(route.name)}); CHECK(it != reg.map.end());
        if (it != reg.map.end()) CHECK(it->second.argc == route.argc && it->second.conv == Conv::Stdcall);
        auto ordinal = reg.ordinals.find({"ws2_32.dll", uint16_t(route.ordinal)});
        CHECK(ordinal != reg.ordinals.end() && ordinal->second == Registry::key_sym(route.name));
    }
    auto call = [&](const char* dll, const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({dll, Registry::key_sym(name)}).fn(cpu);
    };
    CHECK(call("ws2_32.dll", "htons", {0x1234}) == 0x3412); CHECK(call("ws2_32.dll", "ntohs", {0x3412}) == 0x1234);
    CHECK(call("ws2_32.dll", "socket", {2, 1, 6}) == uint32_t(-1) && guest.last_error() == 10045);
    CHECK(call("ws2_32.dll", "inet_ntoa", {0}) == 0 && guest.last_error() == 10045);
    auto xinput = reg.ordinals.at({"xinput1_3.dll", 2}); CHECK(xinput == Registry::key_sym("XInputGetState"));
    CHECK(reg.ordinals.count({"xinput1_3.dll", 3}) == 1 && reg.ordinals.count({"xinput1_3.dll", 4}) == 1 && reg.ordinals.count({"xinput1_3.dll", 5}) == 1 && reg.ordinals.count({"xinput1_3.dll", 7}) == 1 && reg.ordinals.count({"xinput1_3.dll", 8}) == 1);
    CHECK(reg.ordinals.count({"xinput9_1_0.dll", 3}) == 0 && reg.ordinals.count({"xinput9_1_0.dll", 4}) == 0 && reg.ordinals.count({"xinput9_1_0.dll", 5}) == 0 && reg.ordinals.count({"xinput9_1_0.dll", 7}) == 0 && reg.ordinals.count({"xinput9_1_0.dll", 8}) == 0);
    mem.w32(cpu.reg[ESP] + 4, 1); mem.w32(cpu.reg[ESP] + 8, 0x4000); CHECK(reg.map.at({"xinput1_3.dll", xinput}).fn(cpu) == 1167 && xinput_called);
    CHECK(reg.ordinals.at({"xinput1_4.dll", 108}) == Registry::key_sym("XInputGetCapabilitiesEx"));
    CHECK(call("xinput1_4.dll", "XInputGetCapabilitiesEx", {0, 1, 0, 0x4000}) == 0 && mem.read_bytes(0x4000, 32) == std::vector<uint8_t>(32, 0xab));
    std::printf("x86-socket-input: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
