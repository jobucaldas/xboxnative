// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)
int main() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable th; Host host; Registry reg; Guest guest(mem, host, reg);
    guest.register_all(); Cpu cpu(mem, th); cpu.reg[ESP] = 0x1f000;
    auto it = reg.map.find({"kernel32.dll", "isvalidcodepage"});
    CHECK(it != reg.map.end());
    if (it != reg.map.end()) {
        CHECK(it->second.conv == Conv::Stdcall);
        CHECK(it->second.argc == 1);
        auto valid = [&](uint32_t cp) {
            mem.w32(cpu.reg[ESP] + 4, cp);
            return it->second.fn(cpu);
        };
        const uint32_t supported[] = {20127, 28591};
        for (uint32_t cp : supported) CHECK(valid(cp) == 1);
        CHECK(valid(932) == 0);
        CHECK(valid(65001) == 0);
        CHECK(valid(0) == 0);
        CHECK(valid(42) == 0);
        CHECK(valid(65002) == 0);
    }
    auto entry = [&](const char* name) -> Thunk& { return reg.map.at({"kernel32.dll", name}); };
    auto invoke = [&](const char* name, std::initializer_list<uint32_t> args) {
        uint32_t i = 0;
        for (uint32_t arg : args) mem.w32(cpu.reg[ESP] + 4 + i++ * 4, arg);
        return entry(name).fn(cpu);
    };
    uint32_t bytes = 0x1100, wide = 0x1200, out = 0x1300, used = 0x1400, default_char = 0x1500;
    mem.w8(bytes, 0x82); mem.w8(bytes + 1, 0xA0); mem.w8(default_char, '?');
    uint32_t seen_mb_flags = 0, seen_wc_flags = 0;
    bool saw_default_char = false, saw_used_default = false;
    host.nls_call = [&](const Host::NlsRequest& request) {
        if (request.operation == Host::NlsOperation::MultiByteToWideChar) seen_mb_flags = request.flags;
        if (request.operation == Host::NlsOperation::WideCharToMultiByte) {
            seen_wc_flags = request.flags; saw_default_char = request.default_char.size() == 1 && request.default_char[0] == '?';
            saw_used_default = request.wants_used_default;
        }
        Host::NlsResponse response{};
        if (request.operation == Host::NlsOperation::IsValidCodePage) {
            response.result = request.code_page == 932 || request.code_page == 65001;
            return response;
        }
        if (request.operation == Host::NlsOperation::GetCPInfo) {
            response.result = request.code_page == 932;
            if (response.result) {
                response.output.resize(20);
                uint32_t max_char_size = 2;
                std::memcpy(response.output.data(), &max_char_size, 4);
                response.output[4] = '?';
                response.output[6] = 0x81; response.output[7] = 0x9F;
                response.output[8] = 0xE0; response.output[9] = 0xFC;
            } else response.last_error = 87;
            return response;
        }
        if (request.operation == Host::NlsOperation::MultiByteToWideChar) {
            if (request.code_page != 932 || request.input.size() < 2 || request.input[0] != 0x82 || request.input[1] != 0xA0) {
                response.last_error = 1113; return response;
            }
            uint32_t count = request.input_length == -1 ? 2u : 1u;
            if (!request.output_capacity) { response.result = count; return response; }
            if (request.output_capacity < count) { response.last_error = 122; return response; }
            response.result = count;
            response.output = {0x42, 0x30};
            if (count == 2) response.output.insert(response.output.end(), {0, 0});
            return response;
        }
        if (request.operation == Host::NlsOperation::WideCharToMultiByte) {
            if (request.code_page != 932 || request.input.size() < 2 || request.input[0] != 0x42 || request.input[1] != 0x30) {
                response.last_error = 1113; return response;
            }
            uint32_t count = request.input_length == -1 ? 3u : 2u;
            if (!request.output_capacity) { response.result = count; return response; }
            if (request.output_capacity < count) { response.last_error = 122; return response; }
            response.result = count; response.output = {0x82, 0xA0};
            if (count == 3) response.output.push_back(0);
            return response;
        }
        response.last_error = 87;
        return response;
    };
    CHECK(invoke("isvalidcodepage", {932}) == 1);
    CHECK(invoke("getcpinfo", {932, out}) == 1);
    CHECK(mem.r32(out) == 2 && mem.r8(out + 6) == 0x81 && mem.r8(out + 9) == 0xFC);
    CHECK(invoke("multibytetowidechar", {932, 8, bytes, 2, 0, 0}) == 1);
    CHECK(invoke("multibytetowidechar", {932, 8, bytes, uint32_t(-1), wide, 2}) == 2);
    CHECK(mem.r(wide, 2) == 0x3042 && mem.r(wide + 2, 2) == 0);
    CHECK(seen_mb_flags == 8);
    CHECK(invoke("widechartomultibyte", {932, 0x400, wide, uint32_t(-1), 0, 0, default_char, used}) == 3);
    CHECK(invoke("widechartomultibyte", {932, 0x400, wide, 1, out, 1, default_char, used}) == 0);
    CHECK(guest.last_error() == 122);
    CHECK(invoke("widechartomultibyte", {932, 0x400, wide, 1, out, 2, default_char, used}) == 2);
    CHECK(mem.r8(out) == 0x82 && mem.r8(out + 1) == 0xA0 && mem.r32(used) == 0);
    CHECK(invoke("widechartomultibyte", {932, 0x400, wide, uint32_t(-1), out, 3, default_char, used}) == 3);
    CHECK(mem.r8(out + 2) == 0 && seen_wc_flags == 0x400 && saw_default_char && saw_used_default);
    mem.w8(bytes, 0x82); mem.w8(bytes + 1, 0x00);
    CHECK(invoke("multibytetowidechar", {932, 0, bytes, 2, wide, 2}) == 0);
    CHECK(guest.last_error() == 1113);
    CHECK(invoke("getcpinfo", {932, 0}) == 0);
    CHECK(guest.last_error() == 87);
    std::printf("x86-nls: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
