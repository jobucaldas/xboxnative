# Empty legacy HID/SETUPAPI backend

`uwph-hid.dll` exports the five SETUPAPI and fourteen HID functions imported
by UnityPlayer.dll in the four Megabonk import-map targets, plus the requested
SetupDiGetClassDevsW. `hid-test.dll` and `hid-test.exe` are mingw x64 PE callers.
They resolve the private backend explicitly, so native/Wine HID implementations
cannot silently satisfy a test. The host loads the DLL test inside its UWP
AppContainer; the existing dispatch harness runs the EXE under off-device Wine.

```
bash xbox-test/uwph/hid/run.sh
bash xbox-test/uwph/host/build.sh
```

Each call emits its export name through the installed callback, or through
OutputDebugStringA when no callback is set. No actual devices are accessed.
The DLLs have no CRT startup/imports; only kernel32 imports. The build marks
the PE AppContainer characteristic and Windows subsystem version 6.2, and
uses the inherited UWP host's GUI/MTA activation and packaging.

Semantics:

- GetClassDevsA/W allocate independent empty opaque sets, up to 64 live sets;
  exhaustion returns INVALID_HANDLE_VALUE / ERROR_NOT_ENOUGH_MEMORY.
- EnumDeviceInterfaces and EnumDeviceInfo return FALSE / ERROR_NO_MORE_ITEMS
  for a live set, regardless of index; unknown/destroyed sets fail with
  ERROR_INVALID_HANDLE. Destroy succeeds once. IDs are never reused.
- GetDeviceInterfaceDetailW fails ERROR_INVALID_PARAMETER for a live set,
  since no valid interface data can originate from this backend; unknown sets
  fail ERROR_INVALID_HANDLE. This stub does not fabricate a device path.
- HidD_GetHidGuid returns the standard HID interface GUID. Device attribute,
  string, preparsed-data acquisition and release fail ERROR_INVALID_HANDLE.
  GetPreparsedData also clears a supplied output pointer.
- HidP report/capability calls return HIDP_STATUS_INVALID_PREPARSED_DATA;
  MaxDataListLength returns zero. Failure output buffers remain untouched.

The empty-set policy follows Microsoft's [HID discovery sequence](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/finding-and-opening-a-hid-collection)
and [enumeration contract](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdienumdeviceinterfaces).
This is a limited backend, not a complete SDK argument-validation emulator.
The future Wine loader must route SETUPAPI/HID builtin calls to these exports;
this lane does not implement that loader integration or claim Unity tolerance.
Xbox controller support continues to belong to the existing gamepad path.

Actual Xbox Series S verification: [LocalState evidence](console-evidence.log),
`Joao.XboxSteam.Hid_0.1.0.0_x64__dk715arakrne0`, PID 6096. All 20 symbols
logged and 23 checks passed, ending `HID-STUB-OK symbols=20 checks=23` and
`HID RESULT failures=0`. A prelaunch process list had no HID process; the
postlaunch list matched PID 6096. Inherited kernel checks and 300 native D3D11
presents also passed. The process was absent at final cleanup (termination
request returned HTTP 400); long-term host stability is not established.
Off-device dispatch/Wine and import checks are separate from this console proof.
