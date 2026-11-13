// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time check of the vtable slot the stream hook uses (host/d3d12_diag.h,
// createPipelineStateStreamSlot): taken from the generated C vtable structs in
// d3d12.h, which exist only with CINTERFACE, so this unit defines it alone.
// Build: x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -DCINTERFACE tests/d3d12-vtbl-layout.cpp
#define CINTERFACE
#include <windows.h>
#include <d3d12.h>
#include <cstddef>
static_assert(sizeof(ID3D12DeviceVtbl)/sizeof(void*)==44,"ID3D12Device has 44 methods (slots 0-43)");
static_assert(offsetof(ID3D12Device2Vtbl,CreatePipelineState)/sizeof(void*)==47,"ID3D12Device2::CreatePipelineState is slot 47");
int main() { return 0; }
