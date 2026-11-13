#!/usr/bin/env python3
# Builds a minimal PE32 DLL whose export function sits immediately after its
# export directory — OUTSIDE the directory's real Size but INSIDE the 64 KiB
# window a guessed bound would use. Discriminates Bugbot 4560048276: a
# forwarder check that tests [exp_rva, exp_rva+0x10000) misreads the real
# function RVA (0x1080, inside the guessed window) as a forwarder STRING and
# answers ASCII instead of code; the documented test is
# [IMAGE_DATA_DIRECTORY[0].VirtualAddress, +Size) exactly.
# Layout (all RVAs == file offsets):
#   0x0000  headers (MZ/PE/opt/sections), one page
#   0x1000  export directory (0x40 bytes of tables)
#   0x1080  the export's code: real i386, mov eax,0x0DDBA11; ret
#   0x10a0  dll name / export name strings + EAT/name-ptr/ordinal tables
# The directory Size (0x40) covers the directory struct and its three tables;
# the function RVA 0x1080 falls OUTSIDE it — a real function, not a forwarder.
# The interpreter's mapper (wxl::Pe32) needs: MZ/PE, i386/PE32, >=1 section of
# 4096-byte alignment, a relocation-free fixed image, page-aligned SizeOfImage.
import struct, sys

out = sys.argv[1]
IMG = 0x2000   # two pages: SizeOfImage
HDRS = 0x1000  # SizeOfHeaders
EXP = 0x1000   # export directory RVA (the section's own page)
FN = 0x1080    # the export's code RVA: OUTSIDE the directory Size, INSIDE 64K
STRS = 0x10a0  # strings + tables

b = bytearray(IMG)
# --- headers ---
b[0:2] = b'MZ'
NT = 0x40
OPT = NT + 24  # 0x58
struct.pack_into('<I', b, 0x3c, NT)  # e_lfanew
b[NT:NT + 4] = b'PE\x00\x00'
# COFF: machine i386, 1 section, no symbols; opt header size 0xe0
struct.pack_into('<HHIIIHH', b, NT + 4, 0x14c, 1, 0, 0, 0, 0xe0, 0x10e)
opt = bytearray(0xe0)
struct.pack_into('<H', opt, 0, 0x10b)        # PE32
struct.pack_into('<I', opt, 16, FN)          # AddressOfEntryPoint = the export
struct.pack_into('<I', opt, 28, 0x64000000)  # ImageBase (the interpreter's section arena)
struct.pack_into('<I', opt, 32, 4096)        # SectionAlignment
struct.pack_into('<I', opt, 36, 4096)        # FileAlignment
struct.pack_into('<H', opt, 40, 4)           # MajorOSVersion (mingw-like)
struct.pack_into('<I', opt, 56, IMG)         # SizeOfImage
struct.pack_into('<I', opt, 60, HDRS)        # SizeOfHeaders
struct.pack_into('<H', opt, 68, 2)           # SubSystem console
struct.pack_into('<I', opt, 92, 1)           # NumberOfRvaAndSizes
# data directory 0 (export): RVA 0x1000, Size 0x40 — the directory struct and
# its three tables ONLY; the function at 0x1080 is OUTSIDE these bounds.
struct.pack_into('<II', opt, 96, EXP, 0x40)
b[OPT:OPT + len(opt)] = opt
# section table at OPT + 0xe0 = 0x138: one .flat section covering everything
# past the headers (RVA 0x1000..0x2000, raw at file offset 0x1000), R|W|X.
# The mapper's headers span (0..SizeOfHeaders) must not overlap it, and the
# section VA must be SectionAlignment-aligned.
SEC_VA = 0x1000
sec = struct.pack('<8sIIIIIIHHI', b'.flat\0\0\0', IMG - SEC_VA, SEC_VA, IMG - SEC_VA, SEC_VA, 0, 0, 0, 0, 0xE0000040)
b[OPT + 0xe0:OPT + 0xe0 + 40] = sec

# --- export directory at 0x1000 ---
# The layout the Win32 loader reads, 4-byte aligned: Char u16 @0, pad u16 @2,
# TimeDateStamp u32 @4, Major u16 @8, Minor u16 @10, Name u32 @12, Base u32 @16,
# NumberOfFunctions u32 @20, NumberOfNames u32 @24, EAT u32 @28, Names u32 @32,
# Ords u32 @36 — 0x40 bytes total, exactly the directory Size declared above.
# The EAT is a table at STRS+0x18 (one u32 holding the function RVA FN), the
# name-pointer table at STRS+0x10 (one u32 pointing at the export-name string
# at STRS+0x20), the ordinal table at STRS+0x14 (one u16, index 0).
struct.pack_into('<HHIHHIIIIIII', b, EXP,
                 0, 0, 0, 0, 0, STRS, 1, 1, 1, STRS + 0x18, STRS + 0x10, STRS + 0x14)
# --- the export's code at 0x1080: mov eax,0x0DDBA11 ; ret ---
b[FN:FN + 6] = b'\xb8\x11\xba\xdd\x00\xc3'
# --- strings/tables ---
b[STRS:STRS + 0x10] = b'exp-after.dll\x00\x00'          # dll name @0x10a0
struct.pack_into('<I', b, STRS + 0x10, STRS + 0x20)     # name ptr table -> 0x10c0
struct.pack_into('<H', b, STRS + 0x14, 0)               # ordinal table: idx 0
struct.pack_into('<I', b, STRS + 0x18, FN)              # EAT: function RVA 0x1080
b[STRS + 0x20:STRS + 0x30] = b'after_fn\x00\x00\x00\x00\x00\x00\x00\x00'  # @0x10c0

b += bytes(IMG - len(b))  # raw section covers the whole image: pad the file
open(out, 'wb').write(bytes(b))
print(f"wrote {out}: opt@0x{OPT:x}, export dir RVA 0x{EXP:x} size 0x40, "
      f"function RVA 0x{FN:x} (OUTSIDE the directory bounds, INSIDE the old 0x10000 guess)")
