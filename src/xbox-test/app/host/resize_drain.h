// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU drain the ResizeBuffers hook runs before the real resize releases
// the back buffers. Pure D3D11 (no winrt), so tests can drive it with fake
// COM objects and assert the exact call order.
#pragma once
#include <d3d11.h>

namespace resize_drain {

// A resize releases and recreates the back buffers. Real DXGI refuses it with
// DXGI_ERROR_INVALID_CALL while any reference is still outstanding, which is
// what forces apps to unbind first; a chain that tolerates outstanding
// references instead submits commands against freed buffers, and the
// d3d11on12 layer surfaces that to the driver as an invalid command — the
// Slime Rancher device reset (0x887a0007) hit between the 3rd and 4th resize,
// with Unity's deferred contexts still holding the old RTVs. So the caller
// unbinds the immediate context's render targets first (the references that
// matter for buffer release; the game rebinds its targets every frame anyway
// — no ClearState, which would also wipe shader/IA state the engine caches on
// the CPU and only rebinds when dirty), then this drain makes the in-flight
// work finish: End records the event query, Flush submits the recorded work
// (unbind + query) to the driver, only then does polling GetData mean
// anything — a wait before that Flush spins on a query the driver has not
// received while real work stays queued. The poll is bounded: a wedged device
// must not wedge the guest; the resize then fails honestly with the driver's
// error instead of adding a hang. No new GPU work is recorded after the
// Flush, so once the query completes the device is genuinely idle.
inline void drain(ID3D11Device* dev) {
    ID3D11DeviceContext* ctx=nullptr;dev->GetImmediateContext(&ctx);
    if(!ctx)return;
    ctx->OMSetRenderTargets(0,nullptr,nullptr);
    ID3D11Query* q=nullptr;
    D3D11_QUERY_DESC d{};d.Query=D3D11_QUERY_EVENT;
    if(SUCCEEDED(dev->CreateQuery(&d,&q))&&q) {
        ctx->End(q);
        ctx->Flush();
        BOOL done=FALSE;DWORD spins=0;
        while(++spins<=1000) {   // ~1 s at 1 ms/spin; a healthy idle is sub-ms
            HRESULT hr=ctx->GetData(q,&done,sizeof done,0);
            if(hr!=S_FALSE)break;   // S_OK (done) or an error: leave the loop
            Sleep(1);
        }
        q->Release();
    }
    ctx->Release();
}

}
