// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device interpreter benchmark. Usage: x86-bench <PE32 benchmark DLL>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "../host/x86/x86_pe.h"
using namespace x86;
extern "C" uint32_t bench_integer();
extern "C" uint32_t bench_memcpy_memset();
extern "C" uint32_t bench_float_sse2();
extern "C" uint32_t bench_calls_recursion();
extern "C" uint32_t bench_sprite_blit();
struct Kernel { const char* name; const char* symbol; uint32_t (*native)(); };
static const Kernel kernels[] = {
    {"integer", "bench_integer", bench_integer}, {"memcpy+memset", "bench_memcpy_memset", bench_memcpy_memset},
    {"float+SSE2", "bench_float_sse2", bench_float_sse2}, {"calls+recursion", "bench_calls_recursion", bench_calls_recursion},
    {"sprite-blit", "bench_sprite_blit", bench_sprite_blit}
};
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open benchmark DLL");
    return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: x86-bench <benchmark.dll>\n"); return 2; }
    try {
        Memory mem; ThunkTable table; map_thunk_page(mem, table);
        Registry reg; std::map<std::string, Dll> loaded;
        Dll image = load_dll(mem, table, reg, slurp(argv[1]), {}, loaded, 0, nullptr, nullptr, "x86-bench.dll");
        mem.map(0x20000000, 0x20000, PROT_R | PROT_W);
        Cpu cpu(mem, table); cpu.reg[ESP] = 0x2001fff0;
        std::printf("kernel,guest_instructions,guest_instructions_per_second,guest_ms,native_ms,interpreter_over_native\n");
        for (const auto& k : kernels) {
            auto it = image.image.exports.find(k.symbol);
            if (it == image.image.exports.end()) throw std::runtime_error(std::string("missing export: ") + k.symbol);
            uint32_t fn = image.image.base + it->second;
            std::array<double, 5> guest_ms{}, native_ms{};
            uint64_t instructions = 0;
            for (size_t run = 0; run < guest_ms.size(); ++run) {
                auto g0 = std::chrono::steady_clock::now();
                uint64_t before = cpu.executed;
                uint32_t guest_result = cpu.call(fn, {});
                auto g1 = std::chrono::steady_clock::now();
                auto n0 = std::chrono::steady_clock::now();
                uint32_t native_result = k.native();
                auto n1 = std::chrono::steady_clock::now();
                if (guest_result != native_result) {
                    char msg[128]; std::snprintf(msg, sizeof msg, "%s result differs: guest=0x%08x native=0x%08x", k.name, guest_result, native_result);
                    throw std::runtime_error(msg);
                }
                uint64_t run_instructions = cpu.executed - before;
                if (run && run_instructions != instructions) throw std::runtime_error(std::string(k.name) + " instruction count changed");
                instructions = run_instructions;
                guest_ms[run] = std::chrono::duration<double, std::milli>(g1 - g0).count();
                native_ms[run] = std::chrono::duration<double, std::milli>(n1 - n0).count();
            }
            std::sort(guest_ms.begin(), guest_ms.end()); std::sort(native_ms.begin(), native_ms.end());
            double guest_median = guest_ms[guest_ms.size() / 2], native_median = native_ms[native_ms.size() / 2];
            double ips = guest_median > 0 ? double(instructions) / (guest_median / 1000.0) : 0;
            double ratio = native_median > 0 ? guest_median / native_median : 0;
            std::printf("%s,%llu,%.0f,%.3f,%.3f,%.1fx\n", k.name,
                static_cast<unsigned long long>(instructions), ips, guest_median, native_median, ratio);
        }
    } catch (const std::exception& e) { std::fprintf(stderr, "x86-bench: FAIL: %s\n", e.what()); return 1; }
    return 0;
}
