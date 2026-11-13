// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <d3d9.h>
#include <d3d11.h>

namespace d3d9_detail {
// D3DPOOL_MANAGED has no D3D11 equivalent. Keep it sampleable as a DEFAULT
// resource and use the shim's staging shadow for LockRect; on the console UMD,
// a managed texture left DYNAMIC can remove the D3D11-on-12 device.
constexpr void applyManagedTextureBacking(D3D11_TEXTURE2D_DESC& desc,D3DPOOL pool) {
    if(pool==D3DPOOL_MANAGED&&desc.Usage==D3D11_USAGE_DYNAMIC) {
        desc.Usage=D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags=0;
        if(!desc.BindFlags)desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    }
}
}
