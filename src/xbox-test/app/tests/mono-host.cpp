// SPDX-License-Identifier: GPL-3.0-or-later
#include "../host/mono_host.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: mono-host <mono-prefix> <hello.exe>\n"); return 2; }
    if (!mono_host::managed_pe_file(argv[2])) {
        std::fprintf(stderr, "mono-host: mcs fixture was not detected as a managed PE\n"); return 1;
    }
    bool started = false;
    auto result = mono_host::run(argv[1], argv[2], {}, [&] { started = true; });
    if (!result.loaded || !result.executed || !started || result.exit_code != 23) {
        std::fprintf(stderr, "mono-host: loaded=%d executed=%d started=%d exit=%d error=%s\n",
                     result.loaded, result.executed, started, result.exit_code, result.error.c_str());
        return 1;
    }
    std::puts("MONO HOST PASS: embedded AnyCPU hello returned exit code 23");
    return 0;
}
