// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Process policies and services a desktop guest observes, answered as for a
// classic desktop process (the guest is a desktop game; the host package's
// own UWP answers would e.g. make the MSVC CRT RoInitialize every thread).
//  - kernel.appcore / api-ms-win-appmodel-runtime: AppPolicy* queries.
//  - ktmw32: Transactional NTFS/registry is not available here: a transaction
//    cannot be created (as on a volume without TxF), so none can be committed.
#include <windows.h>
#include <appmodel.h>
#include <cstring>
namespace desktop_policy {
inline LONG WINAPI thread_initialization(HANDLE,AppPolicyThreadInitializationType* out) {
    if(!out)return ERROR_INVALID_PARAMETER;
    *out=AppPolicyThreadInitializationType_None;return ERROR_SUCCESS;
}
inline LONG WINAPI process_termination(HANDLE,AppPolicyProcessTerminationMethod* out) {
    if(!out)return ERROR_INVALID_PARAMETER;
    *out=AppPolicyProcessTerminationMethod_ExitProcess;return ERROR_SUCCESS;
}
inline LONG WINAPI windowing_model(HANDLE,AppPolicyWindowingModel* out) {
    if(!out)return ERROR_INVALID_PARAMETER;
    *out=AppPolicyWindowingModel_ClassicDesktop;return ERROR_SUCCESS;
}
inline LONG WINAPI show_developer_diagnostic(HANDLE,AppPolicyShowDeveloperDiagnostic* out) {
    if(!out)return ERROR_INVALID_PARAMETER;
    *out=AppPolicyShowDeveloperDiagnostic_ShowUI;return ERROR_SUCCESS;
}
inline HANDLE WINAPI create_transaction(LPSECURITY_ATTRIBUTES,LPGUID,DWORD,DWORD,DWORD,DWORD,LPWSTR) {
    SetLastError(ERROR_NOT_SUPPORTED);return INVALID_HANDLE_VALUE;
}
inline BOOL WINAPI end_transaction(HANDLE) {   // Commit/Rollback: no transaction was ever issued
    SetLastError(ERROR_INVALID_HANDLE);return FALSE;
}
inline uintptr_t resolve(const char* dll,const char* name) {
    if(!dll||!name)return 0;
    const bool appcore=!_stricmp(dll,"kernel.appcore.dll")||!_strnicmp(dll,"api-ms-win-appmodel-runtime-",28);
    if(appcore) {
        if(!strcmp(name,"AppPolicyGetThreadInitializationType"))return (uintptr_t)&thread_initialization;
        if(!strcmp(name,"AppPolicyGetProcessTerminationMethod"))return (uintptr_t)&process_termination;
        if(!strcmp(name,"AppPolicyGetWindowingModel"))return (uintptr_t)&windowing_model;
        if(!strcmp(name,"AppPolicyGetShowDeveloperDiagnostic"))return (uintptr_t)&show_developer_diagnostic;
        return 0;
    }
    if(!_stricmp(dll,"ktmw32.dll")) {
        if(!strcmp(name,"CreateTransaction"))return (uintptr_t)&create_transaction;
        if(!strcmp(name,"CommitTransaction")||!strcmp(name,"RollbackTransaction"))return (uintptr_t)&end_transaction;
    }
    return 0;
}
}
