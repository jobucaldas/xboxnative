#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Interface to tls_slot0.cpp (see there). A program that links that file first
// provides play_slot0_info; any other program (tests that never run a /GA EXE)
// links the absent stub instead and Play keeps using packaged TLS carriers.
#include <cstddef>
struct PlaySlot0 {
    unsigned char* block;   // this thread's slot-0 block start; guest offsets are relative to it
    size_t capacity;        // bytes from block usable for a guest template
    unsigned long index;    // the launcher EXE's native TLS index; slot 0 only if 0
    size_t offset;          // reserve offset inside the block
};
extern "C" bool play_slot0_info(PlaySlot0* out);
#ifndef PLAY_SLOT0_PROVIDER
extern "C" bool play_slot0_absent(PlaySlot0*) { return false; }
#pragma comment(linker,"/alternatename:play_slot0_info=play_slot0_absent")
#endif
