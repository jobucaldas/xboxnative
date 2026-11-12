# OpenGL feature coverage matrix (Play on the console)

Owner order 2026-11-12: "don't stop until every single piece works". This matrix maps the GL
features games use to the test that proves each one, and stays honest about gaps. Evidence
labels: **[off-device]** proven by a test that runs under Wine here, **[console-proven]** proven
by the console test service on the device, **[unverified]** needs the console, **[gap]** no test
closes it yet. Base: research/OPENGL-PLAN.md (plan + milestones 1–2) and PRs #75, #94, #135,
#140, #142 (the merged OpenGL work; nothing here duplicates those).

## Stack under test

```
game ──> opengl32.dll (Mesa libgl-gdi, 20 wgl* + 336 gl* exports) ──> libgallium_wgl.dll
                                                                     (stw frontend + d3d12/softpipe)
Play: both mapped from runtime/; gdi32 forwards ChoosePixelFormat/SetPixelFormat/
GetPixelFormat/DescribePixelFormat/SwapBuffers to the mapped module's wgl*.
```

Mesa is pinned at 25.3.6 (`deps.pin`, SHA-256), built UCRT-flavoured by
`src/tools/build-mesa-d3d12.sh` with patches `0001` (CoreWindow WGL) and `0010`
(truthful WGL errors) of `patches/mesa-src/`. Mesa's WGL frontend serves these
extensions (read in the built DLL): `WGL_ARB_pixel_format`,
`WGL_ARB_create_context(_profile, _robustness)`, `WGL_ARB_multisample`,
`WGL_ARB_make_current_read`, `WGL_EXT_swap_control`,
`WGL_EXT_create_context_es(2)_profile`, `WGL_ARB_extensions_string`.

## Matrix

| Feature games use | WGL/GL surface | Test that proves it | Status |
|---|---|---|---|
| `LoadLibrary("opengl32.dll")` resolves to Play's module / Mesa | WGL | `uwph/opengl32/runtime-test.sh`; `play-sdl3-gl.sh` stage `gl-library-loaded`; `play-runtime.sh` opengl mode | **[off-device]** |
| Built-in stub WGL state queries (no driver present) | WGL | `uwph/opengl32/test.sh` (Wine differential, 2 threads, last-error) | **[off-device]** |
| gdi32 pixel-format forwarders (Choose/Set/Get/Describe, SwapBuffers) | WGL | `uwph/gdi32/test.cpp` (fake GL exports); `play-opengl.cpp` stage `pixel-format` | **[off-device]** |
| Legacy compat context (`wglCreateContext`), GL 1.1 exports | GL 1.x | `play-opengl.cpp` stages `context-current` + `pixel-readback` | **[off-device]**; console: **[unverified]** (0.5.276 log reached `frame-presented`; readback was BLOCKED, fixed by #142, not re-run) |
| PFD pixel format through DescribePixelFormat round-trip | WGL | `play-opengl.cpp` stage `pixel-format` (index, color, depth reported) | **[off-device]** |
| `wglCreateContextAttribsARB` version refusal with real `GetLastError` | WGL_ARB_create_context | `play-opengl.cpp` stage `version-refusal` (patch 0010; gate greps `err=[1-9]` or `skipped version=`) | **[off-device]** |
| Core 3.3 context via `wglCreateContextAttribsARB` (attrs 0x2091/0x2092, profile 0x9126) | WGL_ARB_create_context_profile | `play-opengl.cpp` stage `core-context` (added here) | **[off-device]** (on hosts where the driver caps at 2.1 the stage reports `skipped version=2.1`, never a false pass) |
| VBO + VAO + `glVertexAttribPointer` + `glDrawArrays` (core pipeline) | GL 3.0+ | `play-opengl.cpp` stage `core-draw` (added here; verdict is a readback pixel, not a call) | **[off-device]** on 3.0+ hosts; otherwise reported skipped |
| GLSL shader compile/link (glShaderSource/glCompileShader/glLinkProgram/glUseProgram) | GL 2.0+ | `play-opengl.cpp` stage `core-draw` (a shader draws the triangle; link log reported on failure) | **[off-device]** on 3.0+ hosts |
| FBO render-to-texture + readback | GL 3.0 (`glGenFramebuffers`) | `play-opengl.cpp` stage `fbo-readback` (added here) | **[off-device]** on 3.0+ hosts |
| MSAA: multisample pixel format (`WGL_SAMPLE_BUFFERS`/`WGL_SAMPLES`) | WGL/GL | `play-opengl.cpp` stage `choose-pixel-format-arb` (a 4x request's result reported: what it gets, or the base-list fallback) | **[off-device]** (4x format 132 on host Mesa); MSAA **resolve** readback: **[gap]** |
| `wglChoosePixelFormatARB` (IAttributes-based, the call SDL3/GLFW actually make) | WGL_ARB_pixel_format | `play-opengl.cpp` stages `wgl-extensions` (extension string advertises it), `pixel-format-attribs` (supportGL/doubleBuffer/colorBits/pixelType queried and checked) and `choose-pixel-format-arb` (SDL3's attribute list; 4x MSAA falls back to the base list) | **[off-device]**; console: **[unverified]** |
| Swap interval (`wglSwapIntervalEXT`/`wglGetSwapIntervalEXT`) vsync | WGL_EXT_swap_control | `play-opengl.cpp` stage `swap-interval` (set 1, read back; skipped when the extension is absent) | **[off-device]** (host Mesa); console: **[unverified]** |
| Context sharing (`wglShareLists`) | WGL | **[gap]** — exported by Mesa, never exercised | open |
| Texture compression (S3TC/BCn via `GL_EXT_texture_compression_s3tc`) | GL | **[gap]** — no fixture uploads a BC texture | open |
| PBO readback (`glReadPixels` from a PIXEL_UNPACK/PIXEL_PACK buffer) | GL 2.1+ | **[gap]** — fixture reads to client memory only | open |
| Context resize (window resize → new client rect → fresh framebuffer) | WGL | patch 0001 polls the client rect at make-current/swap (stw_framebuffer.c); no fixture resizes | **[gap]** off-device; console resize logging exists only for D3D11 (#152) |
| Core 4.x context (4.1+: the console's d3d12 reports 4.1 per OPENGL-PLAN) | GL 4.x | **[gap]** — no fixture asks for 4.1 | open |
| GLES profiles (`WGL_EXT_create_context_es2_profile`) | WGL | served by Mesa; no fixture | **[gap]** (low priority: GLES games route differently) |
| SDL3 GL window creation end-to-end (`SDL_GL_LoadLibrary` → context → swap) | WGL | `play-sdl3-gl.sh` (two passes: d3d12 default + softpipe) | **[off-device]**; console: **[unverified]** |
| GLFW window creation | WGL | **[gap]** — no GLFW fixture (SDL3 covers the same WGL path; GLFW adds no unique surface) | open (low priority) |
| D3D12 device creation from Mesa (`D3D12CreateDevice` with the real adapter) | backend | `play-runtime.sh` opengl mode logs `guest D3D12CreateDevice request/returned` | **[off-device]** (Wine); console: **[unverified]** |
| `CreateCommandQueue1` fallback (patch 0002) | backend | opengl fixture fails at pixel-format without it; gate asserts pixel-format succeeds | **[off-device]** (Wine 11) |
| CoreWindow swapchain present (`CreateSwapChainForHwnd`, 3 buffers, no tearing) | backend | `play-sdl3-gl.sh` d3d12 pass logs `guest DXGI CoreWindow swapchain returned hr=0x00000000` | **[off-device]** (Wine); console: **[unverified]** |
| Software-rasterizer fallback (softpipe) for diagnosis | backend | `play-sdl3-gl.sh` pass 2 (`renderer="softpipe"`) | **[off-device]** |
| Truthful WGL errors on refusal (patch 0010) | WGL | version-refusal stage + `play-runtime.sh` gate | **[off-device]** |
| `wglGetProcAddress` isolation (stub backend never serves host GL) | WGL | `uwph/opengl32/test.cpp` (unknown names, host-differential) | **[off-device]** |

## Console status (from the console test service, 0.5.276.0 evidence in PR #142)

- `SELFTEST RESULT opengl BLOCKED pixel-readback got=0,0,0,0 want=0,64,128,255` — pixel
  format, context creation and swap all **worked** (Mesa d3d12 alive on the console); the
  readback ran after three presents, which legitimately returns undefined content. #142 moved
  the readback before the present. **The fixed fixture has not been re-run on the console.**
- Everything in this matrix marked [unverified] needs one console run of the updated
  `opengl` selftest mode.

## What this PR adds

1. This matrix (honest gaps stay listed).
2. `play-opengl.cpp` stages for the biggest off-device-closable gaps: core 3.3 context
   (`wglCreateContextAttribsARB` with a core profile), a VBO/VAO/GLSL draw whose verdict is a
   readback pixel, an FBO render-to-texture readback, the swap interval
   (`wglSwapIntervalEXT` set + `wglGetSwapIntervalEXT` read back), and the
   `WGL_ARB_pixel_format` path SDL3/GLFW use (`wglGetExtensionsStringARB`,
   `wglGetPixelFormatAttribivARB` attribute checks, `wglChoosePixelFormatARB` with SDL3's
   attribute list). Each stage reports `skipped`
   when the host driver caps below 3.0 or lacks the extension (never a false pass) and BLOCKED
   on a mismatch.
3. Remaining gaps (MSAA resolve, share lists, PBO,
   texture compression, resize, 4.x) stay listed above until closed in a later slice.
