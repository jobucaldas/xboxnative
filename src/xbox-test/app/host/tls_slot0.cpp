// SPDX-License-Identifier: GPL-3.0-or-later
// Static TLS slot 0 for the guest EXE.
//
// Executables built with MSVC /GA read their static TLS through the TEB, slot 0
// (gs:[0x58] -> [0] + offset), instead of through _tls_index, because an EXE is
// assumed to own index 0. Play maps the guest EXE inside the launcher process,
// whose own static TLS block is slot 0. This file reserves the first bytes of
// that block for the guest EXE's TLS template. Guest offsets are relative to
// the block start (the launcher's StartAddressOfRawData), so the reserve must
// be the first TLS variable the launcher defines.
//
// Link order decides that: the linker concatenates same-named (.tls$) sections
// in input order and puts the CRT's one-byte _tls_start in front of them. List
// this file FIRST among the objects of any program that includes
// play_runtime.h. The runtime checks the result (see play_slot0_info) and keeps
// using packaged carrier DLLs when the layout is not as required.
#define PLAY_SLOT0_PROVIDER
#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>
#include "tls_slot0.h"
extern "C" {
extern const IMAGE_TLS_DIRECTORY64 _tls_used;   // the CRT's TLS directory of this EXE
extern ULONG _tls_index;                        // this EXE's native index (0 for the EXE)
}
namespace {
constexpr size_t kBytes = 65536;
// MSVC aligns large arrays to 16, so the CRT's one-byte _tls_start sentinel is
// followed by padding. Nothing else may sit in front of it (see link order).
__declspec(thread) unsigned char reserve[kBytes + 1];
}
// This thread's block, if the reserve really covers its first kBytes.
extern "C" bool play_slot0_info(PlaySlot0* out) {
    auto slots = reinterpret_cast<unsigned char**>(__readgsqword(0x58));   // TEB.ThreadLocalStoragePointer
    if (!slots || !out) return false;
    unsigned char* base = slots[_tls_index];
    if (!base || reserve < base) return false;
    size_t offset = size_t(reserve - base);
    size_t total = size_t(_tls_used.EndAddressOfRawData - _tls_used.StartAddressOfRawData) + _tls_used.SizeOfZeroFill;
    // Only the sentinel and its alignment padding may precede the reserve.
    if (offset > 16 || offset + sizeof reserve > total) return false;
    *out = {base, kBytes, _tls_index, offset};
    return true;
}
