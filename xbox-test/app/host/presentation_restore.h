#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Run on the UI thread after dispatching guest swapchain releases. A guest
// release only clears the handoff flag; it cannot recreate CoreWindow objects
// on the releasing worker thread. Also discard partial resources before retrying
// a failed initialization (com_ptr::put requires an empty destination).
namespace graphics_core {
template<class Device, class Context, class Chain, class Buffer, class Initialize>
bool restore_launcher_presentation(bool guest, Device& device, Context& context,
                                   Chain& chain, Buffer& buffer, Initialize initialize) {
    if(guest || (buffer && chain))return true;
    if(context)context->ClearState();
    buffer=nullptr;
    chain=nullptr;
    if(context)context->Flush();
    context=nullptr;
    device=nullptr;
    return initialize();
}
}
