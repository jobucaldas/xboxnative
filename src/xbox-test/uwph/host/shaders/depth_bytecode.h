// SPDX-License-Identifier: GPL-3.0-or-later
// Depth-test pass shaders: POSITION is float3 so the vertex data can carry a
// real clip-space z, which the depth test orders draws with. Committed DXBC
// (depth_vs.dxbc / depth_ps.dxbc, from depth.hlsl via compile_depth.cpp under
// Wine) — no compiler at build or package time.
#pragma once
#include "depth_vs_bytecode.h"
#include "depth_ps_bytecode.h"
