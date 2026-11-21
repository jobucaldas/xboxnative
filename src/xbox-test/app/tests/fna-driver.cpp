// SPDX-License-Identifier: GPL-3.0-or-later
// fna_driver.h: a managed launch pins FNA3D to D3D11 only when no driver was
// chosen, and leaves the process environment as it found it.
#include "../host/fna_driver.h"
#include <cstdio>
#include <cstdlib>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static const char* get() { return std::getenv("FNA3D_FORCE_DRIVER"); }
int main() {
    unsetenv("FNA3D_FORCE_DRIVER");
    {
        fna_driver::ForceD3D11 pin;
        CHECK(pin.pinned());
        CHECK(get() && std::string(get()) == "D3D11");
        CHECK(fna_driver::current() == "D3D11");
    }
    CHECK(get() == nullptr); // unset again, nothing leaks into the next launch

    setenv("FNA3D_FORCE_DRIVER", "", 1);
    {
        fna_driver::ForceD3D11 pin;
        CHECK(pin.pinned());
        CHECK(get() && std::string(get()) == "D3D11");
    }
    CHECK(get() && std::string(get()).empty()); // restored as empty

    setenv("FNA3D_FORCE_DRIVER", "OpenGL", 1);
    {
        fna_driver::ForceD3D11 pin;
        CHECK(!pin.pinned());
        CHECK(get() && std::string(get()) == "OpenGL");
        CHECK(fna_driver::current() == "OpenGL");
    }
    CHECK(get() && std::string(get()) == "OpenGL");

    unsetenv("FNA3D_FORCE_DRIVER");
    std::printf("fna-driver: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
