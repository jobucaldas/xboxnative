# Synthetic guest EXE preparation

Run `bash xbox-test/uwph/guest-fixture/check-pe.sh` to compile a no-CRT x64 EXE
in a temporary directory and inspect its entry, marker import, DIR64 relocation
and unsupported runtime directories. It does **not** execute the EXE.

The build needs an x64 MinGW C compiler, MinGW objdump, and LLVM's `llvm-dlltool`,
`lld-link`, and `llvm-readobj` on `PATH`. LLVM-MinGW is supported; set
`GUEST_TOOLCHAIN` to its bin directory. If MinGW Clang is unavailable, the runner
uses `x86_64-w64-mingw32-gcc`. Individual tools can be selected with `GUEST_CC`,
`GUEST_OBJDUMP`, `GUEST_DLLTOOL`, `GUEST_LINKER`, and `GUEST_READOBJ`.

The script prints compiler/linker versions and source hashes. Temporary binaries
are removed at exit and never written into the repository. A real mapper harness,
negative-import case, runtime and Xbox validation remain separate work; passing
this inspection does not establish game or console execution.
