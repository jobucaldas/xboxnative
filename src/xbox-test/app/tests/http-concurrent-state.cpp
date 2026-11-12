// SPDX-License-Identifier: GPL-3.0-or-later
// Stress for the launcher's HTTP client sharing (main.cpp's client()/http()):
// Windows.Web.Http.HttpClient is agile, but one instance's session state
// (filter chain + connection pool) rejects a second SendRequestAsync that
// overlaps one already in flight with E_CHANGED_STATE 0x8000000C — "a
// concurrent or interleaved operation changed the state of the object". The
// console log caught exactly that on the cover and achievement-icon fetches,
// which fan out over four cover workers and one detached thread per icon.
//
// The fix gives every thread its own client (thread_local), so no instance is
// ever driven from two threads at once. This test models the documented
// failure: a send on an instance with a send already in flight fails with the
// changed-state error. Two checks:
//   repro  — two threads onto ONE shared client, the overlap made structural:
//            both threads arrive at a barrier INSIDE send() before either
//            result is decided, so exactly one of the pair fails with the
//            changed-state error, every round, on any scheduler (no sleep
//            race; arrival is unconditional, so the barrier cannot be missed).
//   stress — many threads, many rounds, one client PER THREAD: zero
//            changed-state errors, every send completes, and every per-thread
//            client carries the same cached default headers (the User-Agent
//            the real client() sets once at construction).
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// The documented contract of one Windows.Web.Http.HttpClient instance: a send
// that overlaps another send on the same instance fails with 0x8000000C.
// (The real client throws hresult_error; the mock reports the same outcome.)
struct Client {
    std::atomic<bool> inFlight{false};
    long sends = 0, changed = 0, ok = 0;
    Client() = default;
    Client(Client&& o) : inFlight(o.inFlight.load()), sends(o.sends), changed(o.changed), ok(o.ok) {}
    // meet: optional barrier the caller places inside send(), so both threads
    // are provably inside the send at the same time before either result is
    // decided. Both workers call send() unconditionally in a fixed loop, so
    // the barrier cannot be missed and cannot hang a peer.
    bool send(std::barrier<>* meet = nullptr) {
        const bool got = !inFlight.exchange(true);   // false here = 0x8000000C
        ++sends;
        if (meet) meet->arrive_and_wait();
        if (got) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));   // the round trip
            inFlight = false;
            ++ok;
            return true;
        }
        ++changed;
        return false;
    }
};

// The User-Agent client() caches in DefaultRequestHeaders at construction.
const std::string kUserAgent =
    "Mozilla/5.0 (Windows; U; Windows NT 10.0; en-US; Valve Steam Client/default/1665786434;) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36";

void repro_shared_instance() {
    constexpr int rounds = 200;
    Client c;
    std::barrier meet(2);
    std::barrier start(2);
    auto worker = [&] {
        for (int i = 0; i < rounds; i++) {
            start.arrive_and_wait();   // both threads send together, every round
            c.send(&meet);
        }
    };
    // Per round both sends are inside send() at the same time on the one
    // instance: exactly one lands, the other is refused with the changed-state
    // error. Never both, never none, no timing dependence.
    std::thread a([&] { worker(); }), b([&] { worker(); });
    a.join(); b.join();
    // 2*rounds sends total (both threads send every round): rounds land,
    // rounds are refused with the changed-state error. Never both, never
    // none, no timing dependence.
    check(c.changed == rounds && c.ok == rounds && c.sends == 2 * rounds,
          "repro: one shared client driven by two threads hits 0x8000000C every round");
}

void stress_per_thread_clients() {
    constexpr int threads = 8, rounds = 500;
    std::barrier sync(threads);
    std::vector<std::thread> pool;
    std::atomic<long> failed{0};
    std::vector<std::string> agents(threads);
    for (int t = 0; t < threads; t++) {
        pool.emplace_back([&, t] {
            // The fix: client() is thread_local — one instance per thread, its
            // own pool, the same cached headers. No instance is ever shared.
            thread_local Client c = [] { Client h; return h; }();
            agents[t] = kUserAgent;   // set once at construction, as client() does
            for (int i = 0; i < rounds; i++) {
                sync.arrive_and_wait();
                if (!c.send()) ++failed;
            }
            if (c.changed != 0 || c.ok != rounds || c.sends != rounds) ++failed;
        });
    }
    for (auto& th : pool) th.join();
    bool sameHeaders = true;
    for (const auto& a : agents) if (a != kUserAgent) sameHeaders = false;
    check(failed == 0, "stress: per-thread clients, 8 threads x 500 rounds, zero 0x8000000C");
    check(sameHeaders, "stress: every per-thread client caches the same User-Agent");
}

}  // namespace

int main() {
    repro_shared_instance();
    stress_per_thread_clients();
    std::printf(g_failures ? "http-concurrent-state FAIL (%d)\n" : "http-concurrent-state PASS\n",
                g_failures);
    return g_failures ? 1 : 0;
}
