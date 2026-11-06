#pragma once
// Display configuration (CCD) for the one record display that GetMonitorInfo,
// EnumDisplayMonitors, EnumDisplaySettings and EnumDisplayDevices already
// describe: \\.\DISPLAY1, desktopWidth x desktopHeight, 32 bpp, 60 Hz. Unity's
// display layout uses these APIs; without them a game sees no monitor at all.
// One active path, one source and one target mode; nothing else is invented.
// The name structures are desktop-partition only in the SDK: same Win64 layout.
struct DisplaySourceName { DISPLAYCONFIG_DEVICE_INFO_HEADER header; WCHAR viewGdiDeviceName[32]; };
struct DisplayTargetName { DISPLAYCONFIG_DEVICE_INFO_HEADER header; UINT32 flags; UINT32 outputTechnology;
    UINT16 edidManufactureId, edidProductCodeId; UINT32 connectorInstance; WCHAR monitorFriendlyDeviceName[64], monitorDevicePath[128]; };
static_assert(sizeof(DisplaySourceName)==84&&sizeof(DisplayTargetName)==420,"DISPLAYCONFIG name layouts");
static const LUID displayAdapter{1,0};
static bool displayPathFlags(UINT32 flags) {
 const UINT32 kinds=QDC_ALL_PATHS|QDC_ONLY_ACTIVE_PATHS|QDC_DATABASE_CURRENT;
 UINT32 kind=flags&kinds;
 return kind==QDC_ALL_PATHS||kind==QDC_ONLY_ACTIVE_PATHS||kind==QDC_DATABASE_CURRENT;
}
extern "C" LONG WINAPI c_GetDisplayConfigBufferSizes(UINT32 flags,UINT32* paths,UINT32* modes) {
 if(!paths||!modes||!displayPathFlags(flags))return ERROR_INVALID_PARAMETER;
 *paths=1;*modes=2;return ERROR_SUCCESS;
}
extern "C" LONG WINAPI c_QueryDisplayConfig(UINT32 flags,UINT32* pathCount,DISPLAYCONFIG_PATH_INFO* paths,
                                           UINT32* modeCount,DISPLAYCONFIG_MODE_INFO* modes,DISPLAYCONFIG_TOPOLOGY_ID* topology) {
 if(!pathCount||!paths||!modeCount||!modes||!displayPathFlags(flags))return ERROR_INVALID_PARAMETER;
 if(((flags&QDC_DATABASE_CURRENT)!=0)!=(topology!=nullptr))return ERROR_INVALID_PARAMETER;
 if(*pathCount<1||*modeCount<2)return ERROR_INSUFFICIENT_BUFFER;
 std::lock_guard<std::recursive_mutex> l(mutex);
 DISPLAYCONFIG_PATH_INFO path{};
 path.sourceInfo.adapterId=displayAdapter;path.sourceInfo.id=0;path.sourceInfo.modeInfoIdx=0;
 path.sourceInfo.statusFlags=DISPLAYCONFIG_SOURCE_IN_USE;
 path.targetInfo.adapterId=displayAdapter;path.targetInfo.id=0;path.targetInfo.modeInfoIdx=1;
 path.targetInfo.outputTechnology=DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
 path.targetInfo.rotation=DISPLAYCONFIG_ROTATION_IDENTITY;path.targetInfo.scaling=DISPLAYCONFIG_SCALING_IDENTITY;
 path.targetInfo.refreshRate={60,1};path.targetInfo.scanLineOrdering=DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
 path.targetInfo.targetAvailable=TRUE;path.targetInfo.statusFlags=DISPLAYCONFIG_TARGET_IN_USE;
 path.flags=DISPLAYCONFIG_PATH_ACTIVE;
 paths[0]=path;
 DISPLAYCONFIG_MODE_INFO source{};
 source.infoType=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;source.id=0;source.adapterId=displayAdapter;
 source.sourceMode.width=UINT32(desktopWidth);source.sourceMode.height=UINT32(desktopHeight);
 source.sourceMode.pixelFormat=DISPLAYCONFIG_PIXELFORMAT_32BPP;source.sourceMode.position={0,0};
 DISPLAYCONFIG_MODE_INFO target{};
 target.infoType=DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;target.id=0;target.adapterId=displayAdapter;
 auto& signal=target.targetMode.targetVideoSignalInfo;
 signal.activeSize={UINT32(desktopWidth),UINT32(desktopHeight)};signal.totalSize=signal.activeSize;
 signal.vSyncFreq={60,1};signal.hSyncFreq={UINT32(desktopHeight)*60,1};
 signal.pixelRate=UINT64(desktopWidth)*UINT64(desktopHeight)*60;
 signal.videoStandard=255;/* D3DKMDT_VSS_OTHER */signal.scanLineOrdering=DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
 modes[0]=source;modes[1]=target;
 *pathCount=1;*modeCount=2;
 if(topology)*topology=DISPLAYCONFIG_TOPOLOGY_INTERNAL;
 return ERROR_SUCCESS;
}
extern "C" LONG WINAPI c_DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER* header) {
 if(!header)return ERROR_INVALID_PARAMETER;
 if(header->adapterId.LowPart!=displayAdapter.LowPart||header->adapterId.HighPart!=displayAdapter.HighPart||header->id!=0)
  return ERROR_INVALID_PARAMETER;
 if(header->type==DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME) {
  if(header->size!=sizeof(DisplaySourceName))return ERROR_INVALID_PARAMETER;
  auto name=(DisplaySourceName*)header;
  wcscpy_s(name->viewGdiDeviceName,L"\\\\.\\DISPLAY1");return ERROR_SUCCESS;
 }
 if(header->type==DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
  if(header->size!=sizeof(DisplayTargetName))return ERROR_INVALID_PARAMETER;
  auto name=(DisplayTargetName*)header;
  name->flags=0;name->outputTechnology=DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
  name->edidManufactureId=0;name->edidProductCodeId=0;name->connectorInstance=0;
  wcscpy_s(name->monitorFriendlyDeviceName,L"Xbox Display");
  wcscpy_s(name->monitorDevicePath,L"\\\\?\\DISPLAY#XBOX#1");
  return ERROR_SUCCESS;
 }
 return ERROR_NOT_SUPPORTED; // other queries describe hardware this record does not have
}
