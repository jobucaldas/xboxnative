# kernel32 / historical PE tail audit

Inputs: Wine 11 source in `/workspace/research/wine`, report and archived CSVs
in `/workspace/x64-run/research`. The CSV is a historical 391-row subset, not
all game imports. The report already carries lane/wgap's full-set corrections.

The non-system PE_PURE/PE_NEEDS_NT subset has 82 pairs: 10 kernel32, 20 user32,
52 other DLLs. The classifier cannot establish a portable implementation:

- `kernel32/kernel32.spec` declares all ten APIs and their parameter ABI.
  `kernel_main.c:GetStartupInfoA` copies cached process parameters, without
  an Nt call; “all ten need Nt” contradicts both source and CSV.
- `kernel32/toolhelp.c:next_thread` validates THREADENTRY32 size, resets its
  cursor on First, advances on Next, and returns ERROR_NO_MORE_FILES at end.
  `CreateToolhelp32Snapshot` uses real process/thread information and a section.
- `kernel32/thread.c:SetThreadAffinityMask` queries the previous mask and then
  sets the new one. It returns the previous mask, not unconditional failure.
- `kernel32/version.c:VerifyVersionInfoW` evaluates predicates and rejects
  invalid masks; unconditional ERROR_OLD_WIN_VERSION is not an implementation.
- `kernel32/computername.c:GetComputerNameW` uses the actual NetBIOS name and
  translates ERROR_MORE_DATA into ERROR_BUFFER_OVERFLOW.
- `kernel32/module.c:SetDllDirectoryW` changes loader search state.
- `kernel32/powermgnt.c:GetSystemPowerStatus` queries battery state. Its unknown
  record is only a fallback for STATUS_NOT_IMPLEMENTED, not all consoles.
- `user32/win.c:ScreenToClient`, `user32/sysparams.c:GetMonitorInfoW`, and
  `imm32/imm.c:ImmGetContext` call window/input syscalls despite PE_PURE labels.
- `opengl32/unixlib.h` defines UNIX_CALL through WINE_UNIX_CALL. All six WGL
  rows require a graphics backend; the regex classifier missed that wrapper.
- `winmm/waveform.c` routes preparation and device operations through its
  driver/handle state. HID device queries use DeviceIoControl, while HID report
  parsers need correctly formed preparsed data. Neither is implemented by an
  empty device count or a named failure stub.

Console discovery through launcher 0.5.2.0 found all ten kernel exports in the
real kernel32 DLL. The old probe searched kernelbase/API sets only. This is
export evidence, not evidence of successful invocation. Behavioral checks and
remaining tail discovery are recorded in LANE_NOTES.md as they complete.

The shared kernel adapter uses only the system loader and real exports. Memory
calls retain VirtualAllocFromApp/VirtualProtectFromApp and reject RWX. No direct
syscall instruction, OS/TEB patch, signing bypass, or game modification is used.
