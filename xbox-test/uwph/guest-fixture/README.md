# Synthetic guest EXE preparation

Run `bash xbox-test/uwph/guest-fixture/check-pe.sh` to compile a no-CRT x64 EXE in a temporary directory and inspect its entry, marker import, DIR64 relocation and unsupported runtime directories. Set `GUEST_TOOLCHAIN` to override the shared LLVM-MinGW bin directory. The script prints compiler/linker versions and source hashes; temporary binaries are removed at exit and never written into the repository. It does not execute the EXE. A real mapper harness, negative-import case, runtime and Xbox validation remain future work.
