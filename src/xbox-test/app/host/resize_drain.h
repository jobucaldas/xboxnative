// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU drain the ResizeBuffers hook runs before the real resize releases
// the back buffers. Pure D3D11 (no winrt), so tests can drive it with fake
// COM objects and assert the exact call order.
#pragma once
#include "d3d11_diag.h"   // the diag sink the resize hook logs through
#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>

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
    // CreateQuery can fail (device removed, out of memory); the flush still
    // happens — the unbind above must reach the driver before the resize
    // releases the buffers, with or without a query to wait on.
    BOOL flushed=SUCCEEDED(dev->CreateQuery(&d,&q))&&q;
    if(flushed) {
        ctx->End(q);
    }
    ctx->Flush();
    if(flushed) {
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

// ---- Redundant-resize elision (RCA mitigation, Slime Rancher 0.5.303.0)
// The failing run's resize sequence was #1 800x600, #2 1920x1080, #3 1920x1080,
// #4 1920x1080 — #3 and #4 request the chain's CURRENT size/format/flags, and
// #4 itself returned DEVICE_REMOVED (0x887a0005). Hypothesis: a same-mode
// ResizeBuffers on the d3d11on12 flip-model chain removes the device; the
// earlier clean runs never issued a redundant resize. Elision answers a resize
// whose normalised request matches the chain's current mode with S_OK without
// touching the chain: GetBuffer(0) keeps returning the same live buffer (the
// chain is never released), and a game that released its back-buffer views
// before resizing (Unity does) has nothing to recreate that we removed.
//
// The comparison is against what the chain would actually become, using the
// same normalisations the forwarding path applies: BufferCount 0 keeps the
// current count (and the hook never raises a count below 3, matching
// resize_count), DXGI_FORMAT_UNKNOWN keeps the current format, and the
// FRAME_LATENCY_WAITABLE flag is stripped (the chain was created without it;
// a resize that adds it back is refused E_INVALIDARG anyway). Everything else
// must match GetDesc exactly.
inline std::atomic<bool> elideEnabled{true};   // opt-out: LocalState\resizeelide.txt containing "off"

// True when a normalised ResizeBuffers request would leave the chain unchanged.
// `requested` is the post-normalisation target (resize_count/resize_flags
// applied, UNKNOWN resolved); `cur` is the chain's GetDesc. A test seam reads
// this directly with hand-built descs.
inline bool same_mode(const DXGI_SWAP_CHAIN_DESC1& cur,UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags) {
    const UINT wantCount=count?std::max<UINT>(count,3):cur.BufferCount;
    const DXGI_FORMAT wantFormat=f==DXGI_FORMAT_UNKNOWN?cur.Format:f;
    const UINT wantFlags=flags&~UINT(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    return wantCount==cur.BufferCount&&w==cur.Width&&h==cur.Height&&
           wantFormat==cur.Format&&wantFlags==cur.Flags;
}

}
