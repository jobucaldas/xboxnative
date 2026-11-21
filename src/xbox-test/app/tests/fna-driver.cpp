// SPDX-License-Identifier: GPL-3.0-or-later
// fna_driver.h: a managed launch pins FNA3D to D3D11 only when no driver was
// chosen, leaves the process environment as it found it, and stays correct when
// launches overlap. Built for POSIX and, under mingw, for Win32 (run in Wine),
// where it exercises Get/SetEnvironmentVariableW.
#include "../host/fna_driver.h"
#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#include <unistd.h>
#endif
static std::atomic<int> checks{0}, failures{0}; // the overlap cases check from a second thread
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

// Independent of the header: what the process environment really holds.
static void put(const char* value) {
#ifdef _WIN32
    std::wstring v = value ? std::wstring(value, value + std::string(value).size()) : std::wstring();
    SetEnvironmentVariableW(L"FNA3D_FORCE_DRIVER", value ? v.c_str() : nullptr);
#else
    if (value) setenv("FNA3D_FORCE_DRIVER", value, 1); else unsetenv("FNA3D_FORCE_DRIVER");
#endif
}
static bool get(std::string& out) {
#ifdef _WIN32
    wchar_t buf[2048];
    SetLastError(0);
    DWORD n = GetEnvironmentVariableW(L"FNA3D_FORCE_DRIVER", buf, 2048);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return false;
    out.assign(buf, buf + n);
    return true;
#else
    const char* v = std::getenv("FNA3D_FORCE_DRIVER");
    if (!v) return false;
    out = v;
    return true;
#endif
}
static bool is(const char* want) { std::string v; return get(v) && v == want; }
static bool unset() { std::string v; return !get(v); }
static void nap() {
#ifdef _WIN32
    Sleep(1);
#else
    usleep(1000);
#endif
}
static bool wait_for(const std::atomic<int>& flag) {
    for (int i = 0; i < 10000; ++i) { if (flag.load()) return true; nap(); }
    return false;
}
struct Worker {
    std::function<void()> fn;
#ifdef _WIN32
    HANDLE h = nullptr;
    static DWORD WINAPI run(LPVOID p) { (*static_cast<std::function<void()>*>(p))(); return 0; }
    explicit Worker(std::function<void()> f) : fn(std::move(f)) { h = CreateThread(nullptr, 0, run, &fn, 0, nullptr); }
    void join() { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
#else
    std::thread t;
    explicit Worker(std::function<void()> f) : fn(std::move(f)), t(fn) {}
    void join() { t.join(); }
#endif
};

int main() {
    // (a) unset -> D3D11 during the scope, unset again after.
    put(nullptr);
    {
        fna_driver::Scope pin;
        CHECK(is("D3D11"));
        CHECK(fna_driver::current() == "D3D11");
    }
    CHECK(unset());

    // (b) empty -> D3D11 during the scope, restored as empty (not deleted).
    put("");
    {
        fna_driver::Scope pin;
        CHECK(is("D3D11"));
    }
    CHECK(is(""));

    // (c) explicit override, short and long (> the 256-wchar first read), is never touched.
    for (const std::string& value : {std::string("OpenGL"), std::string(600, 'x')}) {
        put(value.c_str());
        {
            fna_driver::Scope pin;
            CHECK(is(value.c_str()));
            CHECK(fna_driver::current() == value);
        }
        CHECK(is(value.c_str()));
    }

    // Nested on one thread: only the outermost scope restores.
    put(nullptr);
    {
        fna_driver::Scope outer;
        {
            fna_driver::Scope inner;
            CHECK(is("D3D11"));
        }
        CHECK(is("D3D11"));
    }
    CHECK(unset());

    // release_all (the SEH path, where destructors never run) restores at once,
    // and the destructor that follows is a no-op.
    put(nullptr);
    {
        fna_driver::Scope pin;
        CHECK(is("D3D11"));
        fna_driver::release_all();
        CHECK(unset());
    }
    CHECK(unset());

    // Overlapping launches on two threads: the second never reads the temporary
    // D3D11 as an override, and the pin outlives whichever finishes first.
    put(nullptr);
    {
        std::atomic<int> inside{0}, go{0}, done{0};
        fna_driver::Scope first;
        Worker second([&] {
            fna_driver::Scope pin;
            inside = 1;
            wait_for(go);
            CHECK(is("D3D11"));
            done = 1;
        });
        CHECK(wait_for(inside));
        // The first launch winds down while the second is still running.
        fna_driver::release_all();
        CHECK(is("D3D11"));
        go = 1;
        CHECK(wait_for(done));
        second.join();
    }
    CHECK(unset());

    // Overlap with an explicit override: both leave it alone.
    put("OpenGL");
    {
        std::atomic<int> inside{0}, go{0};
        fna_driver::Scope first;
        Worker second([&] {
            fna_driver::Scope pin;
            inside = 1;
            wait_for(go);
            CHECK(is("OpenGL"));
        });
        CHECK(wait_for(inside));
        fna_driver::release_all();
        CHECK(is("OpenGL"));
        go = 1;
        second.join();
    }
    CHECK(is("OpenGL"));

    put(nullptr);
    std::printf("fna-driver: %d checks, %d failures\n", checks.load(), failures.load());
    return failures.load() ? 1 : 0;
}
