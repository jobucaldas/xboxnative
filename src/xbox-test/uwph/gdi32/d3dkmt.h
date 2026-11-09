// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The D3DKMT kernel-mode-thunk family wined3d imports from gdi32 (research/D3D9-PLAN.md, milestone 2).
// wined3d asks the display driver for an adapter and a device before it can bring up a GL adapter
// (dlls/wined3d/directx.c wined3d_adapter_init: D3DKMTOpenAdapterFromLuid, then one output per
// EnumDisplayDevicesW entry). The console has one virtual output, described by user32's record display
// (\\.\DISPLAY1, LUID {1,0}); this file is that display's kernel-mode surface, nothing more.
//
// The mingw-w64 sysroot carries no ddk/d3dkmthk.h, so the structures are declared here with Wine 10's
// layouts (include/ddk/d3dkmthk.h, include/d3dukmdt.h) and pinned by static_assert. Semantics follow
// Wine's own dlls/win32u/d3dkmt.c, which is the reference implementation of this API on a machine with
// one software adapter: handles are registry entries, SetVidPnSourceOwner refuses a SHARED owner the way
// Windows' DMM clients do, and there is no GPU to query.
#include <windows.h>
#include <cstdint>
#include <map>
#include <mutex>

namespace gdi32_core {
namespace kmt {
using Handle = UINT32;                       // D3DKMT_HANDLE
using Format = UINT32;                       // D3DDDIFORMAT
using SourceId = UINT32;                     // D3DDDI_VIDEO_PRESENT_SOURCE_ID
constexpr LONG kSuccess = 0;
constexpr LONG kUnsuccessful = (LONG)0xC0000001;
constexpr LONG kInvalidParameter = (LONG)0xC000000D;
constexpr LONG kNotSupported = (LONG)0xC00000BB;
constexpr LONG kVidPnSourceInUse = (LONG)0xC01E0342;

// D3DDDIFORMAT values (include/d3dukmdt.h).
constexpr Format kFmtR8G8B8 = 0x14, kFmtA8R8G8B8 = 0x15, kFmtX8R8G8B8 = 0x16, kFmtR5G6B5 = 0x17;
constexpr Format kFmtX1R5G5B5 = 0x18, kFmtA1R5G5B5 = 0x19, kFmtA4R4G4B4 = 0x1a, kFmtX4R4G4B4 = 0x1e;
constexpr Format kFmtP8 = 0x29;
// D3DKMT_VIDPNSOURCEOWNER_TYPE
constexpr UINT32 kOwnerUnowned = 0, kOwnerShared = 1, kOwnerExclusive = 2, kOwnerExclusiveGdi = 3, kOwnerEmulated = 4;
// D3DKMT_MEMORY_SEGMENT_GROUP
constexpr UINT32 kMemoryLocal = 0, kMemoryNonLocal = 1;

struct CreatedeviceFlags { UINT LegacyMode : 1, RequestVSync : 1, DisableGpuTimeout : 1, Reserved : 29; };
struct OpenAdapterFromGdiDisplayName {
    WCHAR DeviceName[32]; Handle hAdapter; LUID AdapterLuid; SourceId VidPnSourceId;
};
struct OpenAdapterFromLuid { LUID AdapterLuid; Handle hAdapter; };
struct CreateDevice {
    union { Handle hAdapter; void* pAdapter; };
    CreatedeviceFlags Flags;
    Handle hDevice;
    void* pCommandBuffer;
    UINT CommandBufferSize;
    void* pAllocationList;
    UINT AllocationListSize;
    void* pPatchLocationList;
    UINT PatchLocationListSize;
};
struct DestroyDevice { Handle hDevice; };
struct SetVidPnSourceOwner {
    Handle hDevice;
    const UINT32* pType;
    const SourceId* pVidPnSourceId;
    UINT VidPnSourceCount;
};
struct CloseAdapter { Handle hAdapter; };
struct CreateDcFromMemory {
    void* pMemory;
    Format Format;
    UINT Width, Height, Pitch;
    HDC hDeviceDc;
    PALETTEENTRY* pColorTable;
    HDC hDc;
    HANDLE hBitmap;
};
struct DestroyDcFromMemory { HDC hDc; HANDLE hBitmap; };
struct QueryVideoMemoryInfo {
    HANDLE hProcess;
    Handle hAdapter;
    UINT32 MemorySegmentGroup;
    UINT64 Budget;
    UINT64 CurrentUsage;
    UINT64 CurrentReservation;
    UINT64 AvailableForReservation;
    UINT PhysicalAdapterIndex;
};
static_assert(sizeof(OpenAdapterFromGdiDisplayName) == 80, "D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME layout");
static_assert(sizeof(OpenAdapterFromLuid) == 12, "D3DKMT_OPENADAPTERFROMLUID layout");
static_assert(sizeof(CreateDevice) == 64, "D3DKMT_CREATEDEVICE layout");
static_assert(sizeof(DestroyDevice) == 4, "D3DKMT_DESTROYDEVICE layout");
static_assert(sizeof(SetVidPnSourceOwner) == 32, "D3DKMT_SETVIDPNSOURCEOWNER layout");
static_assert(sizeof(CloseAdapter) == 4, "D3DKMT_CLOSEADAPTER layout");
static_assert(sizeof(CreateDcFromMemory) == 56, "D3DKMT_CREATEDCFROMMEMORY layout");
static_assert(sizeof(DestroyDcFromMemory) == 16, "D3DKMT_DESTROYDCFROMMEMORY layout");
static_assert(sizeof(QueryVideoMemoryInfo) == 56, "D3DKMT_QUERYVIDEOMEMORYINFO layout");

// The record display's adapter identity, shared with user32's CCD path (display_config.h) and with
// EnumDisplayDevicesW's single \\.\DISPLAY1. One adapter, one output, one source.
inline constexpr LUID kDisplayLuid{1, 0};

struct State {
    std::mutex mutex;
    Handle next = 0;
    std::map<Handle, LUID> adapters;
    std::map<Handle, Handle> devices;        // device -> adapter
    std::map<Handle, UINT32> owners;         // device -> owner type of source 0
};
inline State& state() { static State s; return s; }
}  // namespace kmt

// ---- Adapters and devices (Wine win32u/d3dkmt.c).
// Opening an adapter always succeeds and returns a fresh handle: the console has exactly one adapter and
// no way to fail to open it. The LUID is the one the caller asked for (or the record display's).
inline LONG WINAPI d3dkmt_open_adapter_from_luid(kmt::OpenAdapterFromLuid* desc) {
    if (!desc) return kmt::kInvalidParameter;
    auto& s = kmt::state();
    std::lock_guard<std::mutex> l(s.mutex);
    desc->hAdapter = ++s.next;
    s.adapters[desc->hAdapter] = desc->AdapterLuid;
    return kmt::kSuccess;
}
// The name must be the record display; anything else is not an attached output here.
inline LONG WINAPI d3dkmt_open_adapter_from_gdi_display_name(kmt::OpenAdapterFromGdiDisplayName* desc) {
    if (!desc || !desc->DeviceName[0]) return kmt::kUnsuccessful;
    if (_wcsicmp(desc->DeviceName, L"\\\\.\\DISPLAY1") && _wcsicmp(desc->DeviceName, L"DISPLAY1"))
        return kmt::kUnsuccessful;
    kmt::OpenAdapterFromLuid luid{};
    luid.AdapterLuid = kmt::kDisplayLuid;
    if (d3dkmt_open_adapter_from_luid(&luid)) return kmt::kUnsuccessful;
    desc->hAdapter = luid.hAdapter;
    desc->AdapterLuid = kmt::kDisplayLuid;
    desc->VidPnSourceId = 0;
    return kmt::kSuccess;
}
inline LONG WINAPI d3dkmt_create_device(kmt::CreateDevice* desc) {
    if (!desc) return kmt::kInvalidParameter;
    auto& s = kmt::state();
    std::lock_guard<std::mutex> l(s.mutex);
    auto adapter = s.adapters.find(desc->hAdapter);
    if (adapter == s.adapters.end()) return kmt::kInvalidParameter;
    desc->hDevice = ++s.next;
    s.devices[desc->hDevice] = adapter->first;
    return kmt::kSuccess;
}
inline LONG WINAPI d3dkmt_destroy_device(const kmt::DestroyDevice* desc) {
    if (!desc || !desc->hDevice) return kmt::kInvalidParameter;
    auto& s = kmt::state();
    std::lock_guard<std::mutex> l(s.mutex);
    if (!s.devices.erase(desc->hDevice)) return kmt::kInvalidParameter;
    s.owners.erase(desc->hDevice);
    return kmt::kSuccess;
}
inline LONG WINAPI d3dkmt_close_adapter(const kmt::CloseAdapter* desc) {
    if (!desc || !desc->hAdapter) return kmt::kInvalidParameter;
    auto& s = kmt::state();
    std::lock_guard<std::mutex> l(s.mutex);
    return s.adapters.erase(desc->hAdapter) ? kmt::kSuccess : kmt::kInvalidParameter;
}

// ---- Output ownership. The CoreWindow owns the console's output, so a SHARED claim is refused exactly as
// Windows refuses one from a DMM-owned source; wined3d maps that status to DXGI_ERROR_NOT_CURRENTLY_AVAILABLE
// and takes its non-exclusive path. EXCLUSIVE and EMULATED are granted (the guest is the only renderer in
// this process); EXCLUSIVEGDI is not a mode this backend has.
inline LONG WINAPI d3dkmt_set_vidpn_source_owner(const kmt::SetVidPnSourceOwner* desc) {
    if (!desc || !desc->hDevice || (desc->VidPnSourceCount && (!desc->pType || !desc->pVidPnSourceId)))
        return kmt::kInvalidParameter;
    auto& s = kmt::state();
    std::lock_guard<std::mutex> l(s.mutex);
    if (!s.devices.count(desc->hDevice)) return kmt::kInvalidParameter;
    for (UINT i = 0; i < desc->VidPnSourceCount; ++i) {
        if (desc->pVidPnSourceId[i] != 0) return kmt::kInvalidParameter;   // one source, id 0
        UINT32 type = desc->pType[i];
        if (type == kmt::kOwnerShared) return kmt::kVidPnSourceInUse;
        if (type == kmt::kOwnerExclusiveGdi || type > kmt::kOwnerEmulated) return kmt::kInvalidParameter;
        auto owner = s.owners.find(desc->hDevice);
        if (owner != s.owners.end()) {
            if (owner->second == kmt::kOwnerExclusive && type != kmt::kOwnerExclusive) return kmt::kInvalidParameter;
            if (owner->second == kmt::kOwnerEmulated && type == kmt::kOwnerExclusive) return kmt::kInvalidParameter;
        }
    }
    if (!desc->VidPnSourceCount) { s.owners.erase(desc->hDevice); return kmt::kSuccess; }
    s.owners[desc->hDevice] = desc->pType[0];
    return kmt::kSuccess;
}

// ---- Video memory. The console's GPU shares system memory (UMA), so the local segment's budget is the
// memory the process can see; this reports that, not a dedicated VRAM size the hardware does not have.
// wined3d falls back to its own fake numbers when this fails, so a wrong answer here is not fatal.
inline LONG WINAPI d3dkmt_query_video_memory_info(kmt::QueryVideoMemoryInfo* desc) {
    if (!desc || !desc->hAdapter) return kmt::kInvalidParameter;
    if (desc->MemorySegmentGroup != kmt::kMemoryLocal && desc->MemorySegmentGroup != kmt::kMemoryNonLocal)
        return kmt::kInvalidParameter;
    if (desc->PhysicalAdapterIndex > 0) return kmt::kInvalidParameter;   // no linked adapters
    {
        auto& s = kmt::state();
        std::lock_guard<std::mutex> l(s.mutex);
        if (!s.adapters.count(desc->hAdapter)) return kmt::kInvalidParameter;
    }
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) return kmt::kNotSupported;
    desc->Budget = desc->MemorySegmentGroup == kmt::kMemoryLocal ? status.ullTotalPhys : 0;
    desc->CurrentUsage = desc->MemorySegmentGroup == kmt::kMemoryLocal ? status.ullTotalPhys - status.ullAvailPhys : 0;
    desc->CurrentReservation = 0;
    desc->AvailableForReservation = desc->Budget / 2;
    return kmt::kSuccess;
}

// ---- Memory DCs. wined3d wraps a texture's or back buffer's own pixel memory in a DC so GDI can blit it
// (dlls/wined3d/texture.c wined3d_texture_create_dc, swapchain.c swapchain_blit_gdi). The DC is a registry
// memory DC whose selected bitmap points at the caller's memory: no copy, no allocation, and the pixels
// stay the texture's.
struct DcFromMemoryFormat { kmt::Format format; int bitCount; bool paletted; };
inline bool dc_from_memory_format(kmt::Format format, DcFromMemoryFormat& out) {
    switch (format) {
    case kmt::kFmtR8G8B8: out = {format, 24, false}; return true;
    case kmt::kFmtA8R8G8B8: case kmt::kFmtX8R8G8B8: out = {format, 32, false}; return true;
    case kmt::kFmtR5G6B5: case kmt::kFmtX1R5G5B5: case kmt::kFmtA1R5G5B5: case kmt::kFmtA4R4G4B4:
    case kmt::kFmtX4R4G4B4: out = {format, 16, false}; return true;
    case kmt::kFmtP8: out = {format, 8, true}; return true;
    default: return false;
    }
}
// DIB rows are word aligned (Wine win32u get_dib_stride).
inline UINT dib_stride(UINT width, int bitCount) { return ((width * UINT(bitCount) + 31) & ~31u) / 8; }

inline LONG WINAPI d3dkmt_create_dc_from_memory(kmt::CreateDcFromMemory* desc) {
    if (!desc || !desc->pMemory) return kmt::kInvalidParameter;
    DcFromMemoryFormat format{};
    if (!dc_from_memory_format(desc->Format, format)) return kmt::kInvalidParameter;
    const UINT minimum = dib_stride(desc->Width, format.bitCount);
    if (!desc->Width || desc->Height > 0x7fffffffu || !desc->Pitch || desc->Pitch < minimum)
        return kmt::kInvalidParameter;
    if (!desc->hDeviceDc) return kmt::kInvalidParameter;
    std::lock_guard<std::recursive_mutex> l(objectsM);
    if (!any(desc->hDeviceDc)) return kmt::kInvalidParameter;
    // The bitmap wraps caller memory: storage stays empty, so nothing here frees it.
    auto bitmap = std::make_unique<Object>();
    bitmap->kind = Kind::Bitmap;
    bitmap->width = int(desc->Width);
    bitmap->height = int(desc->Height);
    bitmap->bpp = format.bitCount;
    bitmap->stride = int(desc->Pitch);
    bitmap->topDown = true;                    // CreateDCFromMemory describes a top-down DIB (negative biHeight)
    bitmap->bits = static_cast<BYTE*>(desc->pMemory);
    bitmap->section = true;                    // GetObject reports it as a DIBSECTION, as Wine's memory DIB does
    Object* raw = bitmap.get();
    HGDIOBJ handle = add(std::move(bitmap));
    auto dc = std::make_unique<Object>();
    dc->kind = Kind::MemoryDc;
    dc->selectedBitmap = raw;
    dc->selectedBrush = stock(Kind::Brush);
    dc->selectedFont = stock(Kind::Font);
    ++raw->selections;
    desc->hDc = (HDC)add(std::move(dc));
    desc->hBitmap = (HANDLE)handle;
    return kmt::kSuccess;
}
inline LONG WINAPI d3dkmt_destroy_dc_from_memory(const kmt::DestroyDcFromMemory* desc) {
    if (!desc || !desc->hDc || !desc->hBitmap) return kmt::kInvalidParameter;
    std::lock_guard<std::recursive_mutex> l(objectsM);
    auto dc = find(desc->hDc, Kind::MemoryDc);
    auto bitmap = find((HGDIOBJ)desc->hBitmap, Kind::Bitmap);
    if (!dc || !bitmap) return kmt::kInvalidParameter;
    if (dc->selectedBitmap == bitmap && bitmap->selections) --bitmap->selections;
    objects.erase(desc->hDc);
    objects.erase((HGDIOBJ)desc->hBitmap);
    return kmt::kSuccess;
}

// ---- GDI entry points wined3d imports besides the D3DKMT family.
// SetDIBColorTable changes a paletted DIB's colours. Every bitmap this backend stores is 32-bpp, so there
// is no colour table to change and the answer is zero entries, as Wine answers for a non-paletted bitmap.
inline UINT WINAPI set_dib_color_table(HDC, UINT, UINT, const RGBQUAD*) { return 0; }
// StretchBlt would draw into a window DC, and this backend has no drawing surface behind one (the same
// reason StretchDIBits is refused). wined3d only reaches it from the GDI present fallback, which is not the
// path the console takes: the GL present goes through Mesa's wglSwapBuffers.
inline BOOL WINAPI stretch_blt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
}  // namespace gdi32_core
