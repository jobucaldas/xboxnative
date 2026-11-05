// SPDX-License-Identifier: GPL-3.0-or-later
// Owned CRT-free logging fixture only; does not claim to render.
#include <windows.h>
static const char* volatile message="SWC mapped-entry logging fixture";
DWORD WINAPI swc_log_entry(void) { OutputDebugStringA(message); return 42; }
