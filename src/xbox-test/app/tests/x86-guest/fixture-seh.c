/* SPDX-License-Identifier: GPL-3.0-or-later
 * A real PE32 fixture that installs an x86 FS:[0] registration chain and asks
 * RtlUnwind to visit two intermediate handlers plus the target handler. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdint.h>

struct guest_seh_frame { struct guest_seh_frame* next; EXCEPTION_ROUTINE* handler; };
static struct guest_seh_frame *g_target, *g_middle, *g_inner, *g_old_head;
static volatile DWORD g_order[3], g_flags[3], g_count, g_valid = 1, g_fallback = 0, g_result;
DWORD seh_saved_esp;
void *seh_resume_ip;

__attribute__((naked, noinline)) static void seh_resume_stub(void) {
    __asm__ __volatile__("movl _seh_saved_esp, %esp\n\tjmp *_seh_resume_ip");
}

static EXCEPTION_DISPOSITION NTAPI guest_seh_handler(PEXCEPTION_RECORD record, PVOID frame,
                                                       PCONTEXT context, PVOID dispatcher) {
    DWORD slot = g_count++;
    if (slot >= 3 || !record || !context || !dispatcher || record->ExceptionCode != 0x12345678u)
        g_valid = 0;
    if (slot < 3) {
        g_order[slot] = frame == g_inner ? 1u : frame == g_middle ? 2u : frame == g_target ? 3u : 0u;
        g_flags[slot] = record ? record->ExceptionFlags : 0;
    }
    return ExceptionContinueSearch;
}

static __attribute__((noinline)) int test_unwind(void) {
    struct guest_seh_frame target, middle, inner;
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof record);
    record.ExceptionCode = 0x12345678u;
    target.handler = guest_seh_handler; middle.handler = guest_seh_handler; inner.handler = guest_seh_handler;
    g_target = &target; g_middle = &middle; g_inner = &inner;
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(g_old_head));
    target.next = g_old_head; middle.next = &target; inner.next = &middle;
    __asm__ __volatile__("movl %0, %%fs:0" : : "r"(&inner) : "memory");
    __asm__ __volatile__("movl %%esp, %0" : "=m"(seh_saved_esp));
    seh_resume_ip = &&resumed;
    RtlUnwind(&target, (PVOID)(uintptr_t)seh_resume_stub, &record, (PVOID)(uintptr_t)0x87654321u);
    g_fallback = 1;
resumed:
    __asm__ __volatile__("movl %%eax, %0" : "=m"(g_result));
    {
        struct guest_seh_frame* head;
        __asm__ __volatile__("movl %%fs:0, %0" : "=r"(head));
        if (head != &target) g_valid = 0;
        __asm__ __volatile__("movl %0, %%fs:0" : : "r"(g_old_head) : "memory");
    }
    return g_valid && !g_fallback && g_count == 3 && g_order[0] == 1 && g_order[1] == 2 && g_order[2] == 3 &&
           g_flags[0] == EXCEPTION_UNWINDING && g_flags[1] == EXCEPTION_UNWINDING &&
           g_flags[2] == (EXCEPTION_UNWINDING | EXCEPTION_TARGET_UNWIND) && g_result == 0x87654321u;
}

static DWORD WINAPI exit_after_free_thread(LPVOID module) {
    FreeLibraryAndExitThread((HMODULE)module, 0x7654u);
    return 0;
}

static int test_kernel32(void) {
    int ok = 1;
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    OSVERSIONINFOW version = {0}; version.dwOSVersionInfoSize = sizeof(version);
    ok &= GetVersionExW(&version) && version.dwMajorVersion == 10 && version.dwBuildNumber == 19045;
    HMODULE module = NULL;
    ok &= GetModuleHandleExA(0, "kernel32.dll", &module) && module != NULL;
    ok &= DisableThreadLibraryCalls(GetModuleHandleW(NULL)) != 0;
    HANDLE exit_thread = CreateThread(NULL, 0, exit_after_free_thread, GetModuleHandleW(NULL), 0, NULL);
    ok &= exit_thread != NULL;
    if (exit_thread) {
        DWORD exit_code = 0;
        ok &= WaitForSingleObject(exit_thread, 5000) == WAIT_OBJECT_0;
        ok &= GetExitCodeThread(exit_thread, &exit_code) != 0 && exit_code == 0x7654u;
        CloseHandle(exit_thread);
    }
    MEMORYSTATUSEX memory = {0}; memory.dwLength = sizeof(memory);
    SetLastError(0); ok &= !GlobalMemoryStatusEx(&memory) && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED;
    ok &= SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)(uintptr_t)0x1234u) != 0;
    ok &= GetStdHandle(STD_OUTPUT_HANDLE) == (HANDLE)(uintptr_t)0x1234u;
    ok &= SetStdHandle(STD_OUTPUT_HANDLE, stdout_handle) != 0;
    ok &= GetStdHandle(STD_OUTPUT_HANDLE) == stdout_handle;
    SetLastError(0); ok &= GetConsoleCP() == 0 && GetLastError() == ERROR_INVALID_HANDLE;
    SetLastError(0); ok &= GetConsoleOutputCP() == 0 && GetLastError() == ERROR_INVALID_HANDLE;
    DWORD mode = 0;
    SetLastError(0); ok &= !GetConsoleMode(stdout_handle, &mode) && GetLastError() == ERROR_INVALID_HANDLE;
    SetLastError(0); ok &= !SetConsoleMode(stdout_handle, 7) && GetLastError() == ERROR_INVALID_HANDLE;
    SetLastError(0); ok &= GetUserDefaultLCID() == 0 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED;
    SetLastError(0); ok &= GetSystemDefaultLCID() == 0 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED;
    SetLastError(0); ok &= GetThreadLocale() == 0 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED;
    HANDLE wide = CreateEventW(NULL, TRUE, FALSE, L"seh-wide-event");
    HANDLE wide_open = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"seh-wide-event");
    HANDLE other = CreateEventA(NULL, TRUE, FALSE, "seh-other-event");
    HANDLE events[2] = {wide, other};
    ok &= wide != NULL && wide_open != NULL && other != NULL;
    ok &= WaitForMultipleObjects(2, events, FALSE, 0) == WAIT_TIMEOUT;
    ok &= SetEvent(wide) != 0 && WaitForMultipleObjects(2, events, FALSE, 0) == WAIT_OBJECT_0;
    ok &= WaitForMultipleObjects(2, events, TRUE, 0) == WAIT_TIMEOUT;
    ok &= SetEvent(other) != 0 && WaitForMultipleObjects(2, events, TRUE, 0) == WAIT_OBJECT_0;
    ok &= ResetEvent(wide) != 0 && ResetEvent(other) != 0;
    CloseHandle(wide_open); CloseHandle(wide); CloseHandle(other);
    return ok;
}

#ifdef X86_DEBUGBREAK_FIXTURE
int main(void) { DebugBreak(); return 42; }
#else
int main(void) {
    int api_ok = test_kernel32();
    int ok = test_unwind() && api_ok;
    printf("seh: handlers=%lu order=%lu,%lu,%lu flags=%lu,%lu,%lu transfer=%08lx fallback=%lu api=%lu\n",
           (unsigned long)g_count, (unsigned long)g_order[0], (unsigned long)g_order[1],
           (unsigned long)g_order[2], (unsigned long)g_flags[0], (unsigned long)g_flags[1],
           (unsigned long)g_flags[2], (unsigned long)g_result, (unsigned long)g_fallback, (unsigned long)api_ok);
    return ok ? 42 : 43;
}
#endif
