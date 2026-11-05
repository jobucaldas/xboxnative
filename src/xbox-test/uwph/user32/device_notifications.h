#pragma once
// Included inside u32c after the guest window registry. Wine user32/input.c
// defines validation order. No host USER32 handle or service is used here.
struct DeviceBroadcastHeader { DWORD size, type, reserved; };
static_assert(sizeof(DeviceBroadcastHeader) == 12, "DEV_BROADCAST_HDR ABI");
extern "C" HANDLE WINAPI c_RegisterDeviceNotificationW(HANDLE recipient, void* filter, DWORD flags) {
    // DEVICE_NOTIFY_SERVICE_HANDLE=1, ALL_INTERFACE_CLASSES=4.
    if ((flags & ~5u) || (!(flags & 1u) && !c_IsWindow((HWND)recipient))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    auto header = static_cast<const DeviceBroadcastHeader*>(filter);
    // Wine accepts NULL filters, device interfaces (5), and handle filters (6).
    if (header && header->type != 5 && header->type != 6) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    // Registration requires actual arrival/removal delivery. Until that exists,
    // fail explicitly instead of issuing a token that silently loses events.
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return nullptr;
}
extern "C" BOOL WINAPI c_UnregisterDeviceNotification(HANDLE notification) {
    // Wine sechost/service.c rejects NULL without changing last error.
    if (!notification) return FALSE;
    // This backend has issued no notification handles; never dereference tokens.
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
