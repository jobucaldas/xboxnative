// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>

namespace d3d12_sm_cap {

// Returns the shader-model minor version (0..8), or -1 for invalid input.
inline int parse(const char* text, size_t size) {
    if (!text) return -1;
    while (size && (text[size - 1] == '\r' || text[size - 1] == '\n' || text[size - 1] == ' ' || text[size - 1] == '\t')) --size;
    size_t start = 0;
    while (start < size && (text[start] == ' ' || text[start] == '\t')) ++start;
    if (size - start != 3 || text[start] != '6' || text[start + 1] != '.' || text[start + 2] < '0' || text[start + 2] > '8') return -1;
    return text[start + 2] - '0';
}

} // namespace d3d12_sm_cap
