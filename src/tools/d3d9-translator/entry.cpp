// SPDX-License-Identifier: GPL-3.0-or-later
// The translator's only export.
//
// `code` is a D3D9 shader bytecode blob (its first token is the version),
// `out`/`capacity` the caller's buffer as a count of 32-bit words, and `size`
// receives the DXBC length in bytes. Returns 0 on success, 1 when the donor
// refuses the shader, 2 on bad arguments, 3 when the result would not fit (a
// truncated shader is never returned: D3D11 would reject it anyway).
#include <windows.h>
#include "dx9asm/dx9asm_translator.h"
#include "dx9asm/dxbc_bytecode.h"

extern "C" __declspec(dllexport) int d3d9Translate(const unsigned* code, unsigned* out,
                                                   unsigned words, unsigned* size) {
    if (size) *size = 0;
    if (!code || !out || !size) return 2;
    dxup::dx9asm::ShaderBytecode* bytecode = nullptr;
    dxup::dx9asm::toDXBC(code, &bytecode);
    if (!bytecode) return 1;
    const unsigned bytes = bytecode->getByteSize();
    if (bytes > words * 4u) { delete bytecode; return 3; }
    const unsigned* source = reinterpret_cast<const unsigned*>(bytecode->getBytecode());
    for (unsigned i = 0; i < bytes / 4; ++i) out[i] = source[i];
    *size = bytes;
    delete bytecode;
    return 0;
}
