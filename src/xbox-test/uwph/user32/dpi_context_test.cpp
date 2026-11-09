// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device differential check of the DPI_AWARENESS_CONTEXT process/thread
// state machine. That state is first-choice-wins and cannot be reset, so the
// same fixed sequence runs twice in two fresh processes -- once against host
// Wine user32 ("host") and once against the packaged record DLL ("dll") -- and
// the transcripts must be identical. Run through dpi-context-test.sh.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

typedef DPI_AWARENESS_CONTEXT (WINAPI *GetThreadCtx)(void);
typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadCtx)(DPI_AWARENESS_CONTEXT);
typedef BOOL (WINAPI *SetProcessCtx)(DPI_AWARENESS_CONTEXT);
typedef BOOL (WINAPI *IsProcessAware)(void);
typedef UINT (WINAPI *GetDpiForSystemFn)(void);
typedef BOOL (WINAPI *SetProcessAwareness)(int);
typedef BOOL (WINAPI *GetProcessAwareness)(HANDLE, int *);

static GetThreadCtx getThread;
static SetThreadCtx setThread;
static SetProcessCtx setProcess;
static IsProcessAware isProcessAware;
static GetDpiForSystemFn getDpiForSystem;
static SetProcessAwareness setAwareness;
static GetProcessAwareness getAwareness;

// Each call and its GetLastError are sequenced explicitly: passing both as
// arguments to one printf leaves the evaluation order unspecified.
static void report(const char *label, UINT_PTR value) {
 DWORD error = GetLastError();
 printf("%s=%#zx error=%lu\n", label, (size_t)value, error);
}
static void reportBool(const char *label, BOOL value) {
 DWORD error = GetLastError();
 printf("%s=%d error=%lu\n", label, value ? 1 : 0, error);
}
#define CALL(label, expression) do { SetLastError(0xdead); auto value_ = (expression); report(label, (UINT_PTR)value_); } while (0)
#define CALLB(label, expression) do { SetLastError(0xdead); auto value_ = (expression); reportBool(label, value_); } while (0)

int main(int argc, char **argv) {
 bool host = argc > 1 && !strcmp(argv[1], "host");
 HMODULE module = nullptr;
 if (host) {
  getThread = GetThreadDpiAwarenessContext; setThread = SetThreadDpiAwarenessContext;
  setProcess = SetProcessDpiAwarenessContext; isProcessAware = IsProcessDPIAware;
  getDpiForSystem = GetDpiForSystem;
  module = GetModuleHandleW(L"user32.dll"); CHECK(module);
 } else {
  module = LoadLibraryW(L"uwph-u32c.dll"); CHECK(module);
  getThread = (GetThreadCtx)GetProcAddress(module, "GetThreadDpiAwarenessContext");
  setThread = (SetThreadCtx)GetProcAddress(module, "SetThreadDpiAwarenessContext");
  setProcess = (SetProcessCtx)GetProcAddress(module, "SetProcessDpiAwarenessContext");
  isProcessAware = (IsProcessAware)GetProcAddress(module, "IsProcessDPIAware");
  getDpiForSystem = (GetDpiForSystemFn)GetProcAddress(module, "GetDpiForSystem");
  CHECK(getThread && setThread && setProcess && isProcessAware && getDpiForSystem);
 }
 // The shcore-facing Wine internals have no SDK declaration; both sides export them.
 setAwareness = (SetProcessAwareness)GetProcAddress(module, "SetProcessDpiAwarenessInternal");
 getAwareness = (GetProcessAwareness)GetProcAddress(module, "GetProcessDpiAwarenessInternal");
 CHECK(setAwareness && getAwareness);

 // Fresh process: nothing has chosen a context, so both answers are unaware.
 CALL("thread.initial", getThread());
 CALLB("aware.initial", isProcessAware());

 // A thread override wins over the process context and reports the process
 // context (flagged) as the previous value.
 CALL("thread.set.v2", setThread(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
 CALL("thread.after.v2", getThread());
 CALLB("aware.after.v2", isProcessAware());
 CALL("thread.set.unaware", setThread(DPI_AWARENESS_CONTEXT_UNAWARE));
 CALL("thread.after.unaware", getThread());

 // An encoded context carrying the process flag drops the override.
 CALL("thread.set.processflag", setThread((DPI_AWARENESS_CONTEXT)(UINT_PTR)0x80006010));
 CALL("thread.after.processflag", getThread());

 // An encoded context that fails validation is refused and changes nothing.
 CALL("thread.set.invalid", setThread((DPI_AWARENESS_CONTEXT)(UINT_PTR)0x1234));
 CALL("thread.after.invalid", getThread());

 // The process context: first choice wins for the process lifetime.
 CALLB("process.set.system", setProcess(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE));
 CALLB("process.set.again", setProcess(DPI_AWARENESS_CONTEXT_UNAWARE));
 CALL("thread.after.process", getThread());
 CALLB("aware.after.process", isProcessAware());
 CALL("dpi.system", getDpiForSystem());
 // The shcore-facing internal entry point shares that one process context.
 CALLB("process.set.internal", setAwareness(2));
 int awareness = -1;
 CALLB("process.get.internal", getAwareness(GetCurrentProcess(), &awareness));
 CALL("process.awareness", (UINT_PTR)(awareness + 1));   // +1 so -1 never prints as unsigned
 printf("%s transcript complete\n", host ? "host" : "dll");
 return 0;
}
