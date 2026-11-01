# WINMM routes

`timeGetTime` uses the AppContainer performance counter. `wave.h` represents
an unavailable wave driver, not a working audio device. Both device counts
are zero. Capability queries return `MMSYSERR_NODRIVER` without touching
buffers. ANSI queries check null output pointers first and return
`MMSYSERR_INVALPARAM`; Unicode queries report driver failure first. No route
sets last error. These branches follow Wine 10 `dlls/winmm/waveform.c`
(`waveInGetNumDevs`, `waveOutGetNumDevs`, and the four `GetDevCaps` variants);
argument widths and calling conventions follow `winmm.spec` and the Windows
SDK. Capability pointers stay opaque because the UWP SDK hides desktop wave
structures; the implementation never dereferences them.

All six discovery exports are imported by Megabonk. No guest handle is sent
to host audio APIs. Playback, capture, and timer-period functionality remain unavailable. There is no claim that audio plays or that capture works. Replace
counts and capability queries together when a real wave driver is added.

`bash test.sh` runs off-device under Wine. It checks the resolved function
pointers, zero device counts, mapper and arbitrary device IDs, null buffers,
size extremes, preserved output bytes, and last error. The no-driver error
branches are source-referenced; this test does not force Wine's host audio
driver to fail or claim a live audio differential comparison.

Five handle operations (`waveInClose`, `waveOutClose`, `waveInReset`,
`waveOutReset`, `waveInStart`) return `MMSYSERR_INVALHANDLE`, preserving last
error. This follows Wine's initial handle-validation branches. Open routes
cannot succeed, so this backend cannot issue a valid wave handle. These routes must
be replaced with the handle registry when open support is introduced. The
host test compares these results and last error against Wine's exports for
null and arbitrary invalid handles; it does not exercise valid devices.

Seven buffer operations now follow Wine's validation order without providing
an audio device: `waveIn/OutPrepareHeader`, `waveIn/OutUnprepareHeader`,
`waveInAddBuffer`, `waveOutWrite`, and `waveOutGetPosition`. Prepare returns
success for an already prepared header; unprepare returns success for an
unprepared header and `WAVERR_STILLPLAYING` for an enqueued header. Other
branches reject invalid parameters, unprepared input buffers, or unavailable
handles as Wine does. No header or time buffer is modified. The local x64
WAVEHDR mirror is checked against the desktop SDK in the host test and against
fixed size/offset assertions in the UWP build. MMTIME has its SDK size of 12.
Replace these operations together with any future valid wave-handle backend.

The host test differentially checks all seven resolved exports against Wine
with four invalid handles, five header flag combinations, seven sizes, and
null/non-null buffers (1,960 cases), including preserved bytes and last error.
These are off-device invalid-handle and validation tests, not audio playback.

`waveInOpen` and `waveOutOpen` now validate a required output pointer (except
`WAVE_FORMAT_QUERY`) and a non-null `CALLBACK_WINDOW` against the guest
USER32 `IsWindow` registry, then return `MMSYSERR_NODRIVER`. They preserve the
output handle and last error and never read the format, invoke callbacks,
create a thread, or pass guest HWNDs to host APIs. The launcher supplies the
guest validator through the resolver; absent a validator, non-null window
callbacks fail validation. Other callback kinds, including unknown kinds,
follow Wine 10 `WINMM_CheckCallback` and reach the unavailable-driver result.
The validator is stored atomically because imports can resolve concurrently.
Replace all wave routes together when a functioning driver is introduced.

Host tests exercise both opens over query flags, callback kinds, null/output
pointers, and synthetic guest-window validity. Only pre-driver failures are
compared to Wine; the no-driver result is source-referenced because Wine's
host audio environment can differ. No console audio or Play result is claimed.
