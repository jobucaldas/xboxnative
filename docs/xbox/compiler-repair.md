# Native compiler recovery checkpoint

Scope: provision a user-local host C++ compiler for the portable `launch-result.test.cpp`
fixture. The vrx-owned source was read only; its test was not compiled or run.

## Provisioned toolchain

- Host: Ubuntu 24.04.5 x86_64, UID 1000; no native `c++`, `g++`, or `clang++` was initially on
  `PATH`.
- Downloaded compiler, C++ standard library headers, libc development headers, binutils and
  required shared libraries as `.deb` packages from the configured Ubuntu Noble archive with
  `apt-get download`; extracted them with `dpkg-deb -x` without sudo or system installation.
- Toolchain root: `/data/OpenBot/Shared/toolchains/native-cxx/root`.
- Reusable compiler wrapper: `/data/OpenBot/Shared/toolchains/native-cxx/bin/cxx`. It sets the
  local library search path, sysroot, include paths and compiler/binutils prefixes; call it as
  `CXX=/data/OpenBot/Shared/toolchains/native-cxx/bin/cxx`.
- Compiler identity: `g++-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`.
- Downloaded package hashes: `/data/OpenBot/Shared/toolchains/native-cxx/logs/package-sha256.txt`.

## Validation evidence

A temporary C++17 program using `std::string`, `assert`, and `std::thread` was compiled and run
successfully. Recreate it and invoke the same wrapper with these commands:

```sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/smoke.cpp" <<'CPP'
#include <cassert>
#include <string>
#include <thread>
int main() {
  std::string value = "native-cxx-ok";
  std::thread worker([&value] { value += "-thread"; });
  worker.join();
  assert(value == "native-cxx-ok-thread");
}
CPP
CXX=/data/OpenBot/Shared/toolchains/native-cxx/bin/cxx
"$CXX" --version
timeout 12s "$CXX" -std=c++17 -O0 -pthread "$tmp/smoke.cpp" -o "$tmp/smoke"
timeout 10s "$tmp/smoke"
```

Observed output was `g++-13 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`; compile and run both
exited 0. Captured version, validation statuses, build log, run log and package hashes are under
`/data/OpenBot/Shared/toolchains/native-cxx/logs/` and are not included in this repository.
The packages were extracted from Ubuntu Noble apt packages into the user-local toolchain; no
system installation was performed. This verifies a native Linux C++ compiler path only; it says
nothing about the MinGW or MSVC/UWP toolchain.

## Next bounded use

The vrx owner can now run its own portable fixture when ready, from
`/data/OpenBot/Shared/xboxnative/wt/vrx/xbox-test/app`:

```sh
CXX=/data/OpenBot/Shared/toolchains/native-cxx/bin/cxx
"$CXX" -std=c++17 -O0 -pthread tests/launch-result.test.cpp -o /tmp/launch-result.test
timeout 10s /tmp/launch-result.test
```

That test has not been built or run by this task. The shared LLVM-MinGW compiler's earlier
slow compile was not retried. Native test readiness is separate from UWP/MSIX packaging and
Xbox runtime acceptance.
