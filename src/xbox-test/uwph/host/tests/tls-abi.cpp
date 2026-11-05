// Compile with both MinGW GCC and clang-cl: the resulting access ABIs differ.
thread_local int native_tls = 73;
extern "C" int read_native_tls() { return ++native_tls; }
