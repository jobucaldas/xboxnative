// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Process policies and services a desktop guest observes, answered as for a
// classic desktop process (the guest is a desktop game; the host package's
// own UWP answers would e.g. make the MSVC CRT RoInitialize every thread).
//  - kernel.appcore / api-ms-win-appmodel-runtime: AppPolicy* queries.
//  - ktmw32: Transactional NTFS/registry is not available here: a transaction
//    cannot be created (as on a volume without TxF), so none can be committed.
//  - shell32 shell UI: nothing can be dropped onto a guest window, and there is
//    no folder picker or notification area, so these report "none" (Wine's
//    answers for an invalid drop handle / cancelled dialog / no tray).
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
inline void WINAPI drag_accept_files(HWND,BOOL) {}
inline void WINAPI drag_finish(HANDLE) {}
inline UINT WINAPI drag_query_file_w(HANDLE,UINT,LPWSTR out,UINT size) {
    if(out&&size)out[0]=0;
    SetLastError(ERROR_INVALID_HANDLE);return 0;   // no HDROP is ever issued
}
inline void* WINAPI browse_for_folder_w(void*) {return nullptr;}   // as if cancelled
inline BOOL WINAPI path_from_id_list_w(const void*,LPWSTR out) {if(out)out[0]=0;return FALSE;}
inline BOOL WINAPI notify_icon_w(DWORD,void*) {SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
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
    if(!_stricmp(dll,"shell32.dll")) {
        if(!strcmp(name,"DragAcceptFiles"))return (uintptr_t)&drag_accept_files;
        if(!strcmp(name,"DragFinish"))return (uintptr_t)&drag_finish;
        if(!strcmp(name,"DragQueryFileW"))return (uintptr_t)&drag_query_file_w;
        if(!strcmp(name,"SHBrowseForFolderW"))return (uintptr_t)&browse_for_folder_w;
        if(!strcmp(name,"SHGetPathFromIDListW"))return (uintptr_t)&path_from_id_list_w;
        if(!strcmp(name,"Shell_NotifyIconW"))return (uintptr_t)&notify_icon_w;
        return 0;
    }
    if(!_stricmp(dll,"ktmw32.dll")) {
        if(!strcmp(name,"CreateTransaction"))return (uintptr_t)&create_transaction;
        if(!strcmp(name,"CommitTransaction")||!strcmp(name,"RollbackTransaction"))return (uintptr_t)&end_transaction;
    }
    return 0;
}
}
