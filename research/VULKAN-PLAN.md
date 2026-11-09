# Vulkan for Play on the console: plan

Status: milestone 1 (a guest's `LoadLibrary("vulkan-1.dll")` succeeds and reports zero physical devices) is built,
packaged and verified off-device. Milestone 2 (a real Vulkan driver) is a plan only. Nothing here claims a game
renders through Vulkan on the console. Evidence labels: **[verified]** read in the pinned source or run off-device,
**[unverified]** needs the console.

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
milestone 1 is a robustness fix (a probe now answers like Windows), not a path to a currently blocked game. That is
why milestone 2 is a plan and not started: nothing owned is waiting on it.

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

## Milestone 2: a real driver (plan)

### The candidate: Mesa `dzn` (Vulkan on D3D12)

`dzn` is Mesa's Vulkan driver over D3D12 — the same backend the OpenGL stack already uses
([OPENGL-PLAN.md](OPENGL-PLAN.md)), so the device, queue and swap-chain work Play routes for Mesa's GL driver is
already done and exercised. It is MIT, in the pinned `mesa-src` tarball, and builds from the same meson tree.

**What it needs that the console may not give it** (read in the 25.3.6 source; **[unverified]** on device):

* **DXIL.** `dzn` compiles NIR to DXIL and signs it through `dxil.dll`, exactly as Mesa's d3d12 GL driver does. The
  GL driver already tolerates a missing signer (`NO_DXIL_VALIDATION`); whether `dzn` does the same, and whether the
  console's D3D12 accepts unsigned DXIL, has to be tested. This is the single largest risk and the same one
  OPENGL-PLAN.md records for GLon12.
* **Vulkan features DXVK requires.** `dzn`'s device table lacks `robustness2`, `transform_feedback`,
  `depth_clip_enable`, `maintenance5/6` and `extended_dynamic_state`, and reports `dualSrcBlend=false`,
  `logicOp=false`, `fullDrawIndexUint32=false` ([D3D-LAYERS-REPORT.md](D3D-LAYERS-REPORT.md) §2 #4, read in
  `dzn_device.c`). That makes `dzn` **not** a DXVK host: it cannot be the path to D3D9 games. It can still be a
  Vulkan driver for engines that ask for Vulkan directly.
* **W^X.** `dzn` is plain C compiled to a PE; it has no runtime codegen. The loader and ICD already pass the
  no-RWX-section gate; `dzn` has to be checked the same way.
* **ICD interface.** `dzn` must be built as an ICD (it is, in Mesa's `vulkan/dzn` target) and packaged with a
  manifest whose `library_path` is its bare name, so Play's guest search resolves it from `runtime/`. No loader
  change is needed: `publishVulkanDriverFiles` already seeds the registry key, and a second manifest is a second value
  in the same key. The null ICD should stay listed beside it, so a `dzn` that fails to load still leaves the guest
  with a clean "no devices" answer instead of `VK_ERROR_INCOMPATIBLE_DRIVER`.

### Which Vulkan extensions owned games would ask for

From the strings the engines carry (not a claim that they run Vulkan today):

* **Unity** (`UnityPlayer.dll`, Megabonk/Slime Rancher/Say No! More): `VK_KHR_swapchain`, `VK_KHR_surface`,
  `VK_KHR_win32_surface`, `VK_EXT_debug_utils`, `VK_EXT_memory_budget`, `VK_EXT_memory_priority`,
  `VK_EXT_conservative_rasterization`, `VK_EXT_scalar_block_layout`, `VK_EXT_hdr_metadata`,
  `VK_EXT_fragment_density_map`, `VK_EXT_load_store_op_none`. Most are optional feature probes; the swap-chain family
  is required.
* **SDL3** (Fields of Mistria): `VK_KHR_surface`/`VK_KHR_win32_surface` and the `VK_*_EXT_*` names it string-matches
  for error reporting; SDL only loads Vulkan when the game asks for it, and this game asks for OpenGL.

`dzn` advertises most of these; the two that matter for a first run are `VK_KHR_win32_surface` and
`VK_KHR_swapchain` on a guest HWND, which Play's DXGI CoreWindow adapter already serves for D3D12 and Mesa's GL
driver. **[unverified for `dzn`]**

### Effort and risks

| Step | Effort | Risk |
|---|---|---|
| Build `dzn` from the pinned Mesa source as a second ICD (reuse `build-mesa-d3d12.sh`'s meson setup and the UCRT mingw toolchain) | ~1 week | Medium: `dzn` may need Vulkan-Headers/loader versions newer than the pinned 1.4.313 |
| Package `dzn` + manifest beside the loader; list both manifests in the guest registry key | 1–2 days | Low: the plumbing exists |
| DXIL signing on the console | unknown | **High**: if the console's D3D12 refuses unsigned DXIL, `dzn` is dead and the OpenGL path (which has the same risk) is the fallback |
| Swap chain on a guest HWND | unknown | Medium: `dzn`'s WSI is Mesa's `wsi_common_win32.cpp`; Play's factory already serves the CoreWindow adapter for GL |
| A Vulkan game to test with | none owned | **Blocker for a milestone-2 acceptance**: no owned title runs Vulkan today, so acceptance would be a synthetic fixture, not a game |

**Recommendation: do not start milestone 2 until an owned game needs Vulkan.** Milestone 1 already gives every
Vulkan-optional engine the answer Windows gives it, and no owned title is blocked on a real driver. If one appears,
start with the DXIL question on the console (a probe that signs and submits a trivial DXIL shader through D3D12) before
building anything: it is the gate both `dzn` and Mesa's GL driver depend on, and it is cheap to test.

## Licensing and policy

Vulkan-Loader and Vulkan-Headers are Apache-2.0; the null ICD is this repository's own code (GPL-3.0-or-later); both
are listed in `NOTICE`, built from pinned sources, and their outputs are git-ignored and never committed (AGENTS.md
rules 5 and 9). No game or Valve files are touched: the loader is substituted in memory like the other Play modules,
and a game that ships its own `vulkan-1.dll` keeps it. Nothing here weakens a gate: the fixture fails (never passes)
when the loader, the instance or the zero-device answer is not exactly what Windows without a driver returns.

## Not covered

Real Vulkan rendering (milestone 2), DXVK/D3D9 over Vulkan (`dzn` cannot host it, per the layers report), 32-bit
Vulkan (the x86 plan in `X86-32BIT-PLAN.md` would need a 32-bit loader and ICD), Vulkan layers (validation, GPU
assist) beyond the loader's default discovery.
