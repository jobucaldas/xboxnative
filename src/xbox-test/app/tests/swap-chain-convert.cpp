// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device contract test for the one legacy->flip-model swap chain
// conversion both guest routes share (corewindow_dxgi::legacy_swap_chain_desc1):
// the factory's CreateSwapChain override and the host's
// D3D11CreateDeviceAndSwapChain route must apply the same rules, so the
// function is checked directly. Runs under Wine; no D3D device is created.
#include <windows.h>
#include <dxgi1_2.h>
#include <cstdio>
#include "../../shared/corewindow_factory.h"

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

using corewindow_dxgi::legacy_swap_chain_desc1;

int main() {
    // A null descriptor is refused, not dereferenced.
    DXGI_SWAP_CHAIN_DESC1 out{};
    CHECK(legacy_swap_chain_desc1(nullptr, &out) == DXGI_ERROR_INVALID_CALL);

    // A normal windowed legacy chain: fields carried over, discard mapped to
    // flip, buffer count raised to 2, scaling and alpha set for CoreWindow.
    DXGI_SWAP_CHAIN_DESC legacy{};
    legacy.BufferDesc.Width = 640; legacy.BufferDesc.Height = 480;
    legacy.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    legacy.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    legacy.SampleDesc.Count = 1;
    legacy.BufferCount = 1;
    legacy.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    legacy.OutputWindow = (HWND)0x1234;
    legacy.Windowed = TRUE;
    legacy.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    HRESULT hr = legacy_swap_chain_desc1(&legacy, &out);
    CHECK(hr == S_OK);
    if (SUCCEEDED(hr)) {
        CHECK(out.Width == 640 && out.Height == 480);
        CHECK(out.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
        CHECK(out.BufferUsage == DXGI_USAGE_RENDER_TARGET_OUTPUT);
        CHECK(out.SampleDesc.Count == 1 && out.SampleDesc.Quality == 0);
        CHECK(out.BufferCount == 2);
        CHECK(out.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
        CHECK(out.Scaling == DXGI_SCALING_STRETCH);
        CHECK(out.AlphaMode == DXGI_ALPHA_MODE_IGNORE);
    }

    // Sequential maps the same way; an already-flip effect is kept as asked.
    legacy.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == S_OK &&
          out.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
    legacy.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == S_OK &&
          out.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD);

    // A larger buffer count is the caller's choice, not raised further.
    legacy.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    legacy.BufferCount = 3;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == S_OK && out.BufferCount == 3);

    // Fullscreen legacy chains are refused: a CoreWindow chain has no
    // exclusive-fullscreen mode to switch into.
    legacy.Windowed = FALSE;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == DXGI_ERROR_INVALID_CALL);
    legacy.Windowed = TRUE;

    // Any legacy flag beyond ALLOW_MODE_SWITCH is refused (mode switching is
    // the only one with a meaning on a CoreWindow chain).
    legacy.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == DXGI_ERROR_INVALID_CALL);
    legacy.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    // Multisampled legacy buffers are refused: a resolve pass is outside this
    // route. Quality follows the same rule.
    legacy.SampleDesc.Count = 4;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == DXGI_ERROR_INVALID_CALL);
    legacy.SampleDesc.Count = 1; legacy.SampleDesc.Quality = 1;
    CHECK(legacy_swap_chain_desc1(&legacy, &out) == DXGI_ERROR_INVALID_CALL);

    printf("%s: %u checks, %u failures\n", "SWAP-CHAIN-CONVERT", checks, failures);
    return failures ? 1 : 0;
}
