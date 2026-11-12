/* SPDX-License-Identifier: GPL-3.0-or-later
 * x86 stage 3: an ordinary i686 C++ program (iostream, std::string,
 * std::vector, exceptions, std::thread + mutex + atomic) built against the
 * mingw C++ runtime, run through the interpreter. The same binary runs under
 * Wine (the cross-check): identical stdout and exit code. Exit 42 on success. */
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <thread>
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
    /* A real thread: the guest surface runs one thread per CPU, so the body
     * runs at the joining wait; the answer must be the same as on Windows. */
    std::atomic<int> acc{0};
    std::mutex m;
    std::thread t([&]{ for (int i = 1; i <= 10; ++i) { std::lock_guard<std::mutex> g(m); acc += i; } });
    t.join();
    std::cout << "acc=" << acc << "\n";
    return acc == 55 ? 42 : 1;
}
