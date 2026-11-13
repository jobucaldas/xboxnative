/* SPDX-License-Identifier: GPL-3.0-or-later
 * x86 stage 3: an ordinary i686 C++ program (iostream, std::string,
 * std::vector, exceptions, std::mutex + atomic) built against the mingw C++
 * runtime, run through the interpreter. The same binary runs under Wine (the
 * cross-check): identical stdout and exit code. Exit 42 on success.
 * std::thread is deliberately absent: libstdc++'s thread support imports
 * toolchain-specific symbols (_M_start_thread and a thread-proc trampoline in
 * libgcc/mcfgthread/winpthread) that vary per mingw build, so it belongs to a
 * later stage, not to the runtime pieces a normal game links. */
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <atomic>
int main() {
    std::string s = "hello from i686 C++";
    std::cout << s << " len=" << s.size() << "\n";
    std::vector<int> v{3,1,2};
    int sum = 0; for (int x : v) sum += x;
    try { if (sum != 6) throw std::runtime_error("bad sum"); } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n"; return 1; }
    std::cout << "sum ok\n";
    /* Mutex + atomic without a thread: the same synchronization primitives
     * (SRWLock/Interlocked paths) the thread body would use, answer-identical
     * to Wine. */
    std::atomic<int> acc{0};
    std::mutex m;
    { std::lock_guard<std::mutex> g(m); for (int i = 1; i <= 10; ++i) acc += i; }
    std::cout << "acc=" << acc << "\n";
    return acc == 55 ? 42 : 1;
}
