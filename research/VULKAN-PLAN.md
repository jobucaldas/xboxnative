# Vulkan for Play on the console: plan

Status: milestone 1 (a guest's `LoadLibrary("vulkan-1.dll")` succeeds and reports zero physical devices) is built,
packaged and verified off-device. Milestone 2 (Mesa's `dzn`, Vulkan on D3D12, as the real ICD) is built and packaged
and verified off-device as far as the host allows: the loader finds a real physical device and creates a
`VK_KHR_win32_surface`, but `vkCreateDevice` stops under Wine because Wine's vkd3d DXIL parser rejects the shader
model `dzn` emits. Nothing here claims a game renders through Vulkan on the console. Evidence labels: **[verified]**
read in the pinned source or run off-device, **[unverified]** needs the console.

## Problem

A Vulkan-optional engine probes Vulkan at startup with `LoadLibrary("vulkan-1.dll")` and falls back to another API
when the load fails. On Windows without a Vulkan driver that load fails with `ERROR_MOD_NOT_FOUND` and the engine
takes its fallback. The console has no `vulkan-1.dll` at all, so the probe fails the same way — until an engine
treats a *present* loader with *no devices* differently from an absent one. The two are not the same answer:
`vkCreateInstance` against a loader with no ICD returns `VK_ERROR_INCOMPATIBLE_DRIVER` and `vkEnumeratePhysicalDevices`
is never reached, which some engines report as an error rather than "no Vulkan".

Milestone 1 closes that gap: Play ships the Khronos loader built from pinned source plus a null ICD, so the load
succeeds, `vkCreateInstance` returns `VK_SUCCESS`, and `vkEnumeratePhysicalDevices` answers
`VK_ERROR_INITIALIZATION_FAILED` with a count of zero — exactly what a desktop loader returns on a machine with no GPU.
An engine's normal "no Vulkan" path is then the one it takes. **[verified off-device]**

## Which owned games use Vulkan?

Surveyed 2026-11-09 over `/home/jobu/steam-owned/games/*` (7 installed titles), by static imports
(`llvm-readobj --coff-imports`) and by strings:

| Game | Vulkan evidence | Needs Vulkan today? |
|---|---|---|
| Megabonk | `UnityPlayer.dll` carries the strings `vulkan-1.dll` and `force-vulkan`; no static import | No — Unity's default on Windows is D3D11, which it already uses |
| Slime Rancher | same (`UnityPlayer.dll`) | No |
| Say No! More | same (`UnityPlayer.dll`) | No |
| Fields of Mistria | `SDL3.dll` carries `vulkan-1.dll` and the `VK_*` error names; the game renders through OpenGL (SDL3 WGL), no static import | No |
| Halo Wars: DE, Puyo Puyo Tetris 2, Mega Man Legacy Collection | no Vulkan strings, no import | No |

**No owned game statically imports `vulkan-1.dll`.** The four hits are engines that *can* use Vulkan when asked
(Unity's `-force-vulkan`, SDL3's `SDL_Vulkan_LoadLibrary`), all of which fall back to D3D11/OpenGL by default. So
milestone 1 is a robustness fix (a probe now answers like Windows), not a path to a currently blocked game, and
milestone 2's acceptance is a synthetic fixture rather than a title (see its last section).

## Milestone 1: a loader that answers "no GPU" cleanly

### What is built

* **`src/tools/build-vulkan-loader.sh`** cross-builds the Khronos **Vulkan-Loader 1.4.313** against **Vulkan-Headers
  1.4.313** (both Apache-2.0, pinned in `deps.pin` as SHA-256 of the release tarballs) with clang-cl/lld-link for
  Windows x64, and links `src/tools/vulkan/null-icd.c` (our own driver, GPL-3.0-or-later) into
  `vulkan-null-icd.dll`. It writes the ICD manifest `vulkan-null-icd.json`, the licences, `SHA256SUMS` and
  `SOURCE.txt`. `--dry-run` prints the recipe (what CI runs); `--build` compiles and gates the result.
* **Gates in the build script**, all passing: both DLLs import only `kernel32`, `advapi32` and `setupapi` (DLLs the
  app partition provides); neither has a writable+executable section; the ICD exports the four entry points the loader
  looks up by name (`vk_icdNegotiateLoaderICDInterfaceVersion`, `vk_icdGetInstanceProcAddr`,
  `vk_icdGetPhysicalDeviceProcAddr`, `vk_icdEnumerateAdapterPhysicalDevices`). **[verified]**
* **Packaging**: `src/xbox-test/app/build.sh` copies the loader, the ICD, the manifest and the licences into
  `layout/runtime/` behind `PLAY_VULKAN` (default on; `PLAY_VULKAN=0` builds a launcher without them, and the load
  then fails as it did before), the same shape as the Mesa OpenGL block.
* **Routing**: `redistributable()` in `uwph/kernel32/guest_loader_search.h` lists `vulkan-1.dll` and
  `vulkan-null-icd.dll`, so a guest's load resolves from the runtime directory after any copy the game ships — a game
  that brings its own loader keeps it.
* **Driver discovery**: the loader finds drivers through a JSON manifest, and its Windows search reads
  `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` (`RegOpenKeyExA`/`RegEnumValueA`) or the `Microsoft.D3DMappingLayers` app
  package, with `VK_DRIVER_FILES` as an environment override that, when honoured, replaces both. The console has
  neither registry key nor package, so Play seeds the registry key in the guest registry (`regb`,
  `LocalState\registry.sqlite`) before the loader is mapped (`publishVulkanDriverFiles`): the value name is the
  packaged manifest's path and the data is `0`, the layout the loader requires, and it is the same registry the
  loader's `advapi32` imports already reach. The manifest's `library_path` is a bare `vulkan-null-icd.dll`, which the
  loader opens with `LoadLibraryW` and Play's guest search resolves from the runtime directory. The value is removed
  before the next launch (`reset_launch_config` and the start of `start()`), so a game never inherits a driver entry
  it did not ask for. **[verified off-device]**
* **Why not `VK_DRIVER_FILES`**: the loader only honours the environment override when the process is not elevated.
  `loader_secure_getenv` calls `is_high_integrity()`, which reads the process token's integrity level
  (`OpenProcessToken`/`GetTokenInformation(TokenIntegrityLevel)`/`GetSidSubAuthority*`) and returns true at
  `SECURITY_MANDATORY_HIGH_RID` (0x3000) or above; Play's process is at 0x3000, so the loader logs "Loader is running
  with elevated permissions. Environment variable VK_DRIVER_FILES will be ignored" and drops it. Setting the variable
  therefore looked correct in the process environment and still produced `VK_ERROR_INCOMPATIBLE_DRIVER`; the registry
  path is the one that works. **[verified off-device: instrumented probe read `level=12288` and the loader logged the
  "will be ignored" line for every `VK_*` variable]**
* **Registry enumeration**: `regb` serves `RegEnumValueA`/`RegEnumValueW` (the loader's registry read) and
  `RegDeleteValueW` (removing the entry again). Values are ordered by lowercase name (`std::map`), which is a valid
  order — Windows promises none — and a short buffer reports the required size and `ERROR_MORE_DATA`, as `advapi32`
  does.

### Why a null ICD and not "no ICD at all"

Without any ICD the loader's `vkCreateInstance` returns `VK_ERROR_INCOMPATIBLE_DRIVER` ("Found no drivers!") and
`vkEnumeratePhysicalDevices` is never reached. With the null ICD the instance is created and the device enumeration
answers zero devices with `VK_ERROR_INITIALIZATION_FAILED`. There is no configuration in which the loader returns
`VK_SUCCESS` with zero devices: `setup_loader_term_phys_devs` sums the per-ICD counts and, when the total is zero,
logs "Failed to detect any valid GPUs in the current config" and returns `VK_ERROR_INITIALIZATION_FAILED`. **[verified
in the pinned source]** So the fixture and the test accept exactly that, and no weaker.

The loader's DXGI interaction is benign here: `windows_read_sorted_physical_devices` asks for `IDXGIFactory6`;
Play's factory returns `E_NOINTERFACE`, the loader logs "Physical devices will not be sorted" and returns
`VK_SUCCESS` with a count of zero. The legacy registry path asks for `IDXGIFactory1` and succeeds, but it only does
that for the *known* vendor manifests (`igvk64.json`, `nv-vk64.json`, `amd-vulkan64.json`, `amdvlk64.json`); our
manifest is not one of them, so the loader logs "Driver ... is not recognized as a known driver. It will be assumed
to be active" and skips the DXGI adapter check entirely. **[verified in the pinned source]**

### Two defects found while making this work

Both were real, and both would have hit the console:

1. **`LoadLibraryW` of an absolute System32 path mapped the file as a guest image.** The loader's
   `windows_initialization` loads `C:\Windows\System32\dxgi.dll` by absolute path. Play mapped that file as a guest
   image, its `user32` imports were refused, and the loader's `fpCreateDXGIFactory1` stayed NULL. Windows resolves an
   absolute path into the OS directory to the OS module; `systemDirectoryPath()` in
   `uwph/kernel32/guest_loader_search.h` now makes `loadExW` do the same.
2. **The fixture did not run the donor gate a real launch runs.** The loader statically imports `setupapi.dll`'s
   `cfgmgr32` family, which Play serves from the Wine `setupapi` donor (`wine-pe`). A real launch probes the donors
   before mapping anything; the fixture did not, so the load failed for a reason a launch never hits. The runner's
   `vulkan` mode now runs `donor_pe::probe()` and installs `mlp::donor_resolve`, and `play-runtime.sh` stages
   `layout/donors/*.dll` beside the runner.

### Acceptance: `app/tests/play-runtime.sh vulkan`

A mingw-built guest EXE (`app/tests/play-vulkan.cpp`, hand-declared Vulkan ABI — mingw has no `vulkan.h`) loads
`vulkan-1.dll` by name through Play's loader, creates an instance, enumerates physical devices twice (count-only and
with a buffer) and destroys the instance. It exits 42 only when the load succeeded, the instance was created, and both
enumerations return `VK_ERROR_INITIALIZATION_FAILED` without writing a device. The count check asserts the loader's
real behaviour: `terminator_EnumeratePhysicalDevices` returns before it writes `*pPhysicalDeviceCount` when
`setup_loader_term_phys_devs` finds no devices, so the caller's sentinel survives — asserting a zero there would be a
fiction. The runner additionally asserts the loader module is mapped and initialized, and the script greps Play's log
for the registry publication and the ICD mapping. This runs under Wine off-device; the console check is in the PR.

Milestone 2 changes what the packaged runtime directory answers: once `dzn` ships, the same fixture finds a real
physical device, so its zero-device assertion only holds against a runtime directory without `dzn`. `play-runtime.sh`
therefore runs it twice: against the packaged directory (asserting the dzn-first publication) and against a copy with
the dzn files removed (`PRUN_RUNTIME_DIR`, the `PLAY_DZN=0` shape), where the milestone-1 answer is asserted exactly
as before. The fixture itself is unchanged, so the zero-device contract is still tested, not weakened.

## Milestone 2: Mesa `dzn` (Vulkan on D3D12) as the real ICD

`dzn` is Mesa's Vulkan driver over D3D12 — the same backend the OpenGL stack already uses
([OPENGL-PLAN.md](OPENGL-PLAN.md)), so the device, queue and swap-chain work Play routes for Mesa's GL driver is
already done and exercised. It is MIT, in the pinned `mesa-src` tarball, and builds from the same meson tree.

### What is built

* **`src/tools/build-mesa-dzn.sh`** cross-builds `vulkan_dzn.dll` from the pinned `mesa-src` (SHA-256 verified) with
  the UCRT-flavoured mingw-w64 toolchain, `meson setup` with only the Vulkan driver (`microsoft-experimental`),
  `spirv-to-dxil`, no Gallium/LLVM/GLES/EGL/GLX/video/CL/tests. It writes the ICD manifest, licences, `SHA256SUMS`
  and `SOURCE.txt`; `--dry-run` prints the recipe (what CI runs) and `--build` compiles and gates the result.
  **[verified: the DLL builds]**
  * Gates, all passing: only DLLs the console supplies are imported (`kernel32`, `ntdll`, `user32`, `gdi32`,
    `shell32`, `version`, `advapi32`, `ws2_32`, `ole32`, `api-ms-win-*`); no writable+executable section; the three
    ICD entry points the loader looks up by name are exported. **[verified off-device]**
  * `pe-drop-empty-pdata.py` drops the zero-length x64 unwind entries GNU ld emits (19 in this build), which Play's
    loader refuses.
  * The DirectX-Headers subproject is pinned by commit in `deps.pin` (`64c650b0…`, v1.618.1) and fetched at setup
    time; `--wrap-mode=nodownload` means meson downloads nothing else.
* **Patches** (`patches/mesa-src/`, applied with `patch --fuzz=0`): 0003 makes the DXIL signer optional (the console
  has no `dxil.dll`; `dzn` then emits unsigned DXIL instead of failing), 0004 makes DirectComposition optional and
  creates the swap chain for the surface's HWND (the console has no DirectComposition; Play maps a guest HWND to its
  CoreWindow), 0005 turns a failed `EnableExperimentalFeatures` into a warning, 0006 makes the meta cleanup idempotent
  on a failed device creation, and 0007 fills in the DXBC container checksum (MD5 with DXBC's variant padding) so an
  unsigned container is well-formed. 0001 and 0002 are the shared d3d12-backend patches the GL driver needs.
  0006 and 0007 were corrected after review: 0007's two-block tail is 56 or 60 bytes (the payload after the 20-byte
  header is 4-byte aligned) and now pads the first block as a plain block with the whole remainder, which matches
  vkd3d's `md5_final(VKD3D_MD5_DXBC)`; 0006 now destroys `shaders_lock`/`contexts_lock` once, guarded by a flag, so
  the second `dzn_meta_finish()` on the failure path does not destroy a mutex twice. The checksum is checked by
  compiling the patched function against a port of vkd3d's implementation over container sizes whose tail is 56 and
  60 bytes. **[verified off-device]**
* **Packaging**: `app/build.sh` copies `vulkan_dzn.dll`, `vulkan-dzn.json`, the licences, the patches, `SHA256SUMS`
  and `SOURCE.txt` into `layout/runtime/` behind `PLAY_DZN` (default on; a dzn build failure fails the launcher build;
  `PLAY_DZN=0` is the explicit way to package without it). `redistributable()` in
  `uwph/kernel32/guest_loader_search.h` lists `vulkan_dzn.dll`, so a guest's or the loader's `LoadLibraryW` of the bare
  manifest name resolves from the runtime directory.
* **Driver discovery**: `publishVulkanDriverFiles` (`host/play_runtime.h`) seeds `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`
  with every packaged manifest that exists, in order `vulkan-dzn.json` then `vulkan-null-icd.json`. The loader's
  `terminator_CreateInstance` skips an ICD whose `CreateInstance` fails and only returns
  `VK_ERROR_INCOMPATIBLE_DRIVER` when none succeed, so a `dzn` that cannot initialise leaves the guest with milestone
  1's clean "no devices" answer rather than a hard failure. A manifest that is not packaged is skipped, so a
  `PLAY_DZN=0` launcher behaves exactly as before. Registration **replaces** rather than appends
  (`shared/vulkan_driver_registry.h`): the key is machine-wide and survives an upgrade, and the loader de-duplicates
  it by manifest basename keeping the first entry it enumerates, so an entry left by an earlier install would shadow
  the live manifest and the loader would report no driver at all. Every Play manifest pointing anywhere but the
  runtime directory being registered is deleted first, as is any entry whose manifest file no longer exists; a
  third-party driver whose manifest is on disk is left alone. **[verified off-device]**
  * Console evidence for why this matters: 0.5.264.0 onwards failed the `vulkan-dzn` self-test with exit 12 (no
    driver found) rather than 0.5.263.0's exit 19, because four stale entries from two earlier installs shadowed the
    live `vulkan-dzn.json`, with a byte-identical driver DLL.
  * The fixture's own stage lines reach the console only through `OutputDebugStringA` (Play routes it into the probe
    log): a packaged process has no stdout handle, so `WriteFile` to `STD_OUTPUT_HANDLE` writes nowhere.

### What `dzn` does off-device, and where it stops

`app/tests/play-vulkan-dzn.cpp` runs the startup path a Vulkan game takes: instance with `VK_KHR_surface` and
`VK_KHR_win32_surface`, physical-device enumeration, a win32 surface on a guest HWND, queue-family selection, surface
capabilities and formats, device, swap chain, render pass, framebuffers, a cleared frame and two presents. Under Wine
11.0 with its built-in vkd3d (Intel HD 4000) it gets this far: **[verified off-device]**

```
PLAY-VULKAN-DZN STAGE instance-created
PLAY-VULKAN-DZN STAGE physical-device count=1 api=0x402148 driver=0x… name="Microsoft Direct3D12 (Intel(R) HD Graphics 4000)"
PLAY-VULKAN-DZN STAGE win32-surface-created hwnd=…
PLAY-VULKAN-DZN STAGE queue-family index=0 count=4
PLAY-VULKAN-DZN STAGE surface formats=2 chosen=44 minImages=3 maxImages=3 extent=632x453
PLAY-VULKAN-DZN BLOCKED device-creation result=-3 (…)
```

So the loader, the registry publication, the ICD mapping, instance creation, a real physical device, a win32 surface
and the surface queries all work. `vkCreateDevice` returns `VK_ERROR_INITIALIZATION_FAILED` because Wine's vkd3d
cannot parse the DXIL `dzn` emits for its meta shaders:

* `dzn` probes `D3D12_FEATURE_SHADER_MODEL` for 6.8 down to **6.1** (`dzn_device.c`), and Wine's vkd3d answers
  `E_INVALIDARG` for all of them, reporting `HighestShaderModel = 0x60` (SM 6.0) instead. SM 6.0 is the D3D12
  baseline, so `pdev->shader_model` stays 0 and `dzn_get_shader_model()` returns 0.
* Independently of that, vkd3d's DXIL parser fails on the meta shader regardless: `sm6_parser_read_bits Failed
  assertion: length < 32` (61×), `E8009: A DXIL function declaration is invalid`, and `create_shader_stage Failed to
  compile shader, vkd3d result -4`. vkd3d's SM 6.0 support is not what `dzn` writes against (it emits validator
  version 1.8 metadata).

Getting this far needed one Play fix: `dzn` statically imports `gdi32!StretchBlt` (its WSI software path, used when
there is no DXGI swap chain), which the gdi32 route did not resolve, so the image was refused before `DllMain`. The
route now implements `StretchBlt` as a nearest-neighbour `SRCCOPY` between guest memory DCs, the same contract as its
`BitBlt`; a window DC still has no surface and the call fails with `ERROR_NOT_SUPPORTED`. `uwph/gdi32/test.cpp` covers
it (copy, scaling, non-`SRCCOPY`, unknown DC). **[verified off-device]**

This is a **Wine-host limitation, not a proven console limitation**: the console's D3D12 is Microsoft's, not vkd3d.
Whether it accepts unsigned DXIL from `dzn` is **[unverified]** and is the console check in the PR. If it refuses, the
failure is graceful: the loader falls through to the null ICD and the guest sees milestone 1's answer.

### Needed extensions and `dzn` gaps

The extensions the fixture needs, and the ones owned engines would ask for (from the strings the engines carry, not a
claim that they run Vulkan today):

* **Required for a frame**: `VK_KHR_surface`, `VK_KHR_win32_surface`, `VK_KHR_swapchain`. `dzn` advertises all three;
  the surface is created on a guest HWND that Play's DXGI CoreWindow adapter serves for D3D12 and Mesa's GL driver.
  **[verified off-device: surface created]**
* **Unity** (`UnityPlayer.dll`, Megabonk/Slime Rancher/Say No! More): `VK_EXT_debug_utils`, `VK_EXT_memory_budget`,
  `VK_EXT_memory_priority`, `VK_EXT_conservative_rasterization`, `VK_EXT_scalar_block_layout`, `VK_EXT_hdr_metadata`,
  `VK_EXT_fragment_density_map`, `VK_EXT_load_store_op_none`. Most are optional feature probes.
* **SDL3** (Fields of Mistria): `VK_KHR_surface`/`VK_KHR_win32_surface` and the `VK_*_EXT_*` names it string-matches
  for error reporting; SDL only loads Vulkan when the game asks for it, and this game asks for OpenGL.
* **`dzn` gaps, read in `dzn_device.c`**: no `robustness2`, `transform_feedback`, `depth_clip_enable`,
  `maintenance5/6` or `extended_dynamic_state`; `dualSrcBlend=false`, `logicOp=false`, `fullDrawIndexUint32=false`
  ([D3D-LAYERS-REPORT.md](D3D-LAYERS-REPORT.md) §2 #4). That makes `dzn` **not** a DXVK host: it cannot be the path to
  D3D9 games. It can still be a Vulkan driver for engines that ask for Vulkan directly. `dzn` is also upstream
  `microsoft-experimental` (non-conformant), which it announces at instance creation.

### Acceptance

`app/tests/play-runtime.sh` runs three Vulkan modes:

* `vulkan` against the packaged runtime directory: the loader is mapped and the guest registry lists both manifests
  (dzn first, null ICD as fallback).
* `vulkan` against a runtime directory with the dzn files removed (`PRUN_RUNTIME_DIR` points the runner at it, which
  is what a `PLAY_DZN=0` launcher ships): the milestone-1 zero-device answer, unchanged.
* `vulkan-dzn`: the fixture above. It prints `PLAY-VULKAN-DZN PASS frame-presented` when a frame is presented, or
  `PLAY-VULKAN-DZN BLOCKED device-creation` with the reason when the host's D3D12 refuses the device. The script
  prints PASS or BLOCKED from those lines and never counts BLOCKED as a pass; `PRUN_REQUIRE_DZN=1` turns BLOCKED into
  a failure for a host that should run it.

Off-device the result is BLOCKED at device creation (Wine's vkd3d). The console answer is still open, and the
launcher package cannot produce it today: `app/build.sh` ships `layout/xbtest.exe` and the Play runtime, not
`play-runner.exe` or the fixtures, which `play-runtime.sh` builds off-device only. So `vulkan-dzn` is an off-device
proof, and a console verdict needs either the runner packaged (a deliberate change, not made here) or a title that
actually asks for Vulkan. **[unverified on console]**

### A Vulkan game to test with

No owned title runs Vulkan today (see the survey above), so milestone 2's acceptance is a synthetic fixture, not a
game. The first owned title that asks for Vulkan (Unity's `-force-vulkan`, an SDL3 Vulkan renderer) is the real
acceptance; until then nothing here claims a game renders through Vulkan.

## Licensing and policy

Vulkan-Loader and Vulkan-Headers are Apache-2.0; Mesa and DirectX-Headers are MIT; the null ICD is this repository's
own code (GPL-3.0-or-later). All are listed in `NOTICE`, built from pinned sources, and their outputs are git-ignored
and never committed (AGENTS.md rules 5 and 9). No game or Valve files are touched: the loader and the ICD are
substituted in memory like the other Play modules, and a game that ships its own `vulkan-1.dll` keeps it. Nothing here
weakens a gate: the fixture fails (never passes) when the loader, the instance or the zero-device answer is not
exactly what Windows without a driver returns, and the dzn fixture prints BLOCKED (never PASS) when the host's D3D12
refuses the device.

## Not covered

DXVK/D3D9 over Vulkan (`dzn` cannot host it, per the layers report), 32-bit Vulkan (the x86 plan in
`X86-32BIT-PLAN.md` would need a 32-bit loader and ICD), Vulkan layers (validation, GPU assist) beyond the loader's
default discovery, and `dxil.dll` signing (the console has no signer; patch 0003 makes it optional and patch 0007
makes the unsigned container well-formed).
