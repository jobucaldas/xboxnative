// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device PE32 static-TLS integration fixture: the executable and a
// preloaded DLL each own a TLS template; two cooperative guest workers touch
// both copies and the DLL receives thread attach/detach notifications.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../host/x86/x86_play.h"
using namespace x86;

static std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: x86-thread-tls <exe> <dll-dir>\n"); return 2; }
    const std::string dir = argv[2];
    DllSource source = [dir](const std::string& name) {
        std::ifstream f(dir + "/" + name, std::ios::binary);
        if (!f) return std::vector<uint8_t>{};
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
    };
    PlayResult result = run_image(slurp(argv[1]), "fixture-thread-tls.exe", "fixture-thread-tls.exe",
                                  [](const std::string& text) { std::printf("%s", text.c_str()); },
                                  nullptr, 500ull * 1000 * 1000, source);
    if (!result.fault.empty()) {
        std::printf("x86-thread-tls: fault kind=%s %s\n", result.fault_kind.c_str(), result.fault.c_str());
        return 1;
    }
    std::printf("x86-thread-tls: loaded=%d finished=%d exit=%u\n",
                result.loaded ? 1 : 0, result.finished ? 1 : 0, result.exit_code);
    return result.finished && result.exit_code == 42 ? 0 : 1;
}
