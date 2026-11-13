// SPDX-License-Identifier: GPL-3.0-or-later
// The redundant-resize elision decision (RCA mitigation, Slime Rancher
// 0.5.303.0), free of every include so the host test (tests/resize-elision.cpp)
// runs it with plain toolchains and the swap-chain hook applies it unchanged.
//
// The failing run's resize sequence was #1 800x600, #2 1920x1080, #3 1920x1080,
// #4 1920x1080 — #3 and #4 request the chain's CURRENT size/format/flags, and
// #4 itself returned DEVICE_REMOVED (0x887a0005). Hypothesis: a same-mode
// ResizeBuffers on the d3d11on12 flip-model chain removes the device; the
// earlier clean runs never issued a redundant resize. Elision answers such a
// resize with S_OK without touching the chain: GetBuffer(0) keeps returning
// the same live buffer (the chain is never released), and a game that released
// its back-buffer views before resizing (Unity does) has nothing to recreate
// that we removed.
//
// The comparison is against what the chain would actually become, using the
// same normalisations the forwarding path applies: BufferCount 0 keeps the
// current count (and the hook never raises a count below 3, matching
// resize_count), DXGI_FORMAT_UNKNOWN keeps the current format, and the
// FRAME_LATENCY_WAITABLE flag is stripped (the chain was created without it;
// a resize that adds it back is refused E_INVALIDARG anyway). Everything else
// must match the current mode exactly.
#pragma once
#include <algorithm>
#include <string>

namespace resize_elide {

// One swap-chain mode: the fields a ResizeBuffers request can change, exactly
// as the chain reports them (GetDesc) and as the hook would forward them.
struct Mode {
    unsigned count = 0;   // real buffer count (GetDesc's, >= 3 on our chains)
    unsigned width = 0;
    unsigned height = 0;
    unsigned format = 0;  // DXGI_FORMAT value; 0 is DXGI_FORMAT_UNKNOWN
    unsigned flags = 0;   // post-strip flags as the chain reports them
};

// The request normalisation the forwarding path applies, expressed on Mode:
// count 0 means "keep the current count" and is never raised below 3
// (guest_overlay's resize_count), format 0 (DXGI_FORMAT_UNKNOWN) means "keep
// the current format", and the frame-latency-waitable bit (0x40) is stripped
// because the chain was created without it — a resize that adds it back is
// refused E_INVALIDARG anyway.
inline Mode normalize(const Mode& current,Mode request) {
    if(request.count==0)request.count=current.count;
    else request.count=request.count<3?3:request.count;
    if(request.format==0)request.format=current.format;
    request.flags&=~0x40u;   // DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
    return request;
}

// True when a normalised ResizeBuffers request would leave the chain unchanged.
// The waitable-flag bit is stripped from the CURRENT mode too: a desc that
// somehow carries it (the hook strips it at forwarding and at creation, so a
// live chain should not have it) must not make every keep-request forward.
inline bool same_mode(const Mode& current,const Mode& request) {
    Mode cur=current;cur.flags&=~0x40u;
    const Mode want=normalize(cur,request);
    return want.count==cur.count&&want.width==cur.width&&
           want.height==cur.height&&want.format==cur.format&&
           want.flags==cur.flags;
}

// Opt-out switch (LocalState\resizeelide.txt): default ON; a file whose
// contents contain "off" disables elision — every resize is forwarded. Any
// other text (empty, "on", stray whitespace) keeps it on, matching how the
// launcher reads its other toggle files.
inline bool enabled(const std::string& fileContents) {
    return fileContents.find("off")==std::string::npos;
}

}
