# x86 stage 2 part A — ISA coverage matrix

Branch `feature/x86-isa-coverage`. Baseline: `origin/main` (a9c9cb4) already had
x87 (f64 arithmetic suite, hardware-compared), integer ALU/shift/string/cmov/setcc/
bt*/cmpxchg/xadd/shrd/shld, and a first SSE slice (movss/d, arithmetic 58-5F, cvt,
movd/movq, movmsk, pmaxub/pminub-era basics, packsswb/packuswb, pcmpeq/pcmpgt,
punpck, pcmpeqb, pshufd-era partial). This pass closed the remaining gaps found by
running an MSVC-style i686 corpus (built `-O2 -msse2` with mingw) through the
interpreter and fixing every fault/until it ran to exit 42 (9649 instructions),
plus targeted tests per opcode class.

## What this pass added

| Opcode class | Encoding | Test | Status |
|---|---|---|---|
| pshufd full imm sweep | 66 0F 70 /r ib | 256 software-model cases + 4 hardware-compared | PASS |
| pshuflw / pshufhw | F2/F3 0F 70 /r ib | software model, 3 imm each | PASS |
| shufps | 0F C6 /r ib | software model, 6 imm | PASS |
| shufpd | 66 0F C6 /r ib | software model, 4 imm | PASS |
| psllw/psrlw/psraw imm8 | 66 0F 71 /2 /4 /6 ib | 18 cases incl. count 0/16 saturation | PASS |
| pslld/psrld/psrad imm8 | 66 0F 72 /2 /4 /6 ib | incl. count 32 saturation | PASS |
| pslldq/psrldq imm8 | 66 0F 73 /7 /3 ib | incl. count 16/255 saturation | PASS |
| psrlw/d/q xmm,xmm | 66 0F D1/D2/D3 | count from low 64 bits, incl. 16/32/64 saturation | PASS |
| psraw/d, pslld/d reg forms | 66 0F E1/E2/F1/F2/F3 | same | PASS |
| paddq | 66 0F D4 | both 64-bit lanes, wrap | PASS |
| pmullw | 66 0F D5 | low 16 of products, both operand orders | PASS |
| pmuludq | 66 0F F4 | 32x32→64 in qword lanes | PASS |
| pmaddwd | 66 0F F5 | incl. saturation-edge operands | PASS |
| psadbw | 66 0F F6 | byte-abs sums per 64-bit lane | PASS |
| pavgb | 66 0F E0 | (a+b+1)>>1 per byte | PASS |
| paddusb/paddsw | 66 0F DC/DD | unsigned/signed saturation | PASS |
| psubusb/psubsw | 66 0F E8/E9/EC/ED | saturation at 0 and 0x8000 | PASS |
| pminub/pmaxub | 66 0F DA/DE | per byte unsigned | PASS |
| pminsw/pmaxsw | 66 0F EA/EE | per word signed | PASS |
| pand/pandn | 66 0F DB/DF | 128-bit bitwise | PASS |
| punpckhqdq | 66 0F 6D | high qwords interleaved | PASS |
| rdtsc | 0F 31 | strictly increasing synthetic counter | PASS |
| cpuid | 0F A2 | leaf 0 GenuineIntel, leaf 1 feature bits (SSE2 set), unknown leaves zero | PASS |
| movnti | 66 0F C3 | dword store | PASS |
| prefetcht0/t1/t2 | 0F 18 /0-2 | hint, no fault | PASS |
| fxsave/fxrstor | 0F AE /0 /1 | full 512-byte layout: x87 stack + MXCSR + XMM round trip | PASS |
| repne scasb | F2 AE | stops on match (ZF=1) | PASS |
| repe cmpsb | F3 A6 | stops on mismatch, ZF reflects last compare | PASS |
| _lrotl/_lrotr | host import | guest CRT helper added to x86_guest.h | PASS |

Totals: 61,222 hardware-compared cases across the whole suite (up from 60,874).

## Encoding notes locked down by tests

- `0F 71/72/73`: the reg field is the /n shift selector; the XMM operand is **r/m**
  (mod=3 required). This contradicts the rest of the SSE map where reg is the
  destination.
- `66 0F 73 /3` = psrldq (right), `/7` = pslldq (left).
- Register-form shift counts come from the low 64 bits of the xmm/m64 source and
  saturate per-lane (16/32/64 by granularity).

## Known gaps (deliberate, fault by encoding)

- MMX (MM0-MM7) forms without the 66 prefix: fault `0f 71/72/73 (MMX)` etc. MSVC
  x86 SSE2 builds emit the 66 forms; MMX-only code is out of scope for this pass.
- `0F AE /5 /6` (xsave/xrstor): accepted as no-ops; no xsave-area layout.
- fxsave stores fop/ip/cs/dp/ds as zeros (no real 80387-style tracking); games
  reading their own saved FPU IP would see 0.
- cpuid leaves beyond 0/1 return zeros; no cache/brand strings.
- No AVX/AVX2 (VEX), no SSE4.x, no SSSE3 (palignr etc.) — not seen in the target
  corpus; fault by encoding as designed.

## How it was verified

- `x86-core.sh`: all suites PASS, 61,222 hardware-compared cases, fixture exit 42
  at both ImageBases.
- `x86-core.sh --wine`: fixture stdout + exit code identical under Wine.
- `x86-guest.sh --wine`: mingw CRT program (printf doubles, heap, qsort, TLS,
  VirtualAlloc) exit 42 both ways.
- MSVC-style corpus (`/tmp/msvc-corpus.exe`, kept out of the repo): exit 42,
  9649 instructions, no faults.
