# Checking the native C++ test toolchain

Portable tests run on the development host, not on the Xbox. Use a **native**
C++ compiler and standard library; a MinGW compiler creates Windows executables
and cannot establish that the host toolchain works.

Select a compiler with `CXX` (default `c++`). Install it using your development
environment's package manager, or provide a wrapper on `PATH`. Keep downloaded
toolchains, local wrappers, credentials and validation logs outside the source
tree. The repository does not require a particular host distribution or private
shared directory.

## Check compilation, the standard library and threads

From a shell with a native compiler available:

```sh
set -eu
CXX=${CXX:-c++}
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
"$CXX" --version
"$CXX" -std=c++17 -O0 -pthread "$tmp/smoke.cpp" -o "$tmp/smoke"
"$tmp/smoke"
```

Both compilation and execution must exit zero. An executable-format error often
means a cross-compiler was selected. Missing C++ headers, startup objects or
shared libraries mean the selected compiler's development/runtime dependencies
are incomplete; repair the development environment rather than removing test
assertions or substituting a cross-build for a native run.

## Run the application tests

Run each test using its checked-in runner from the repository root. Runners that
support `CXX` accept a compiler executable or wrapper path, not a string of
compiler flags. Consult the test's source and runner for its language standard
and library requirements.

A working native compiler is only a prerequisite: run the relevant application
tests themselves. This smoke program does not test the launcher, PE mapper,
Windows/MSVC ABI, UWP FromApp APIs, MSIX packaging or real controller input.
Console-affecting changes still require the full `xbox-test/smoke-all.sh` run;
launcher acceptance is `xbox-test/tools/play-test.sh <appid>` and the criteria in
[`docs/PLAYABLE.md`](../PLAYABLE.md). Keep private evidence local and summarize
actual results, including failures, in the pull request.
