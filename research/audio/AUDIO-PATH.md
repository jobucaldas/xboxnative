# Megabonk audio routing audit

## Inputs and scope

The installed game's UnityPlayer.dll identifies itself as 2023.2.22f1, not
Unity 6000.x. `runs/aud/audio-imports.csv` records the real direct/delay imports
per image; `unity-imports.txt` retains LLVM's independent COFF import report.
`imports.csv` is the unchanged wine_gap.py full-game baseline: 628 pairs,
458 planned non-system imports, 120 Wine win32u dependencies. These are static
policy counts, not proof of callable exports or successful game execution.

There are 23 unique winmm imports: ten waveOut, ten waveIn and three timers.
There are fourteen unique ole32 imports. COM is also used for non-audio work;
CoCreateInstance alone cannot identify which audio CLSID executes. No static or
delay imports reference xaudio2_*, mmdevapi, dsound, audioses or avrt. Such DLLs
can still be selected dynamically. Binary strings include FMOD WASAPI,
DirectSound, WinMM and no-sound backend diagnostics; strings do not prove which
backend this executable selects at runtime.

FMOD's current Windows documentation describes WASAPI as the default on Vista
and later. This is evidence about modern FMOD, not a verified description of
Unity's embedded FMOD revision or this game's selected backend:
https://www.fmod.com/docs/2.04/api/platforms-win.html
The FMOD Unity integration documentation describes a separate integration; it
must not be mistaken for proof of Unity's internal engine configuration.

## Routing decision

Keep native COM allocation/apartment APIs on the real system ole32 route. A
WASAPI path needs real activation and interface calls, not a fake successful
CoCreateInstance. Establish endpoint/activation availability before implementing
an interface adapter. Do not substitute an XAudio2 vtable for IAudioClient.

For an application-owned PCM output adapter, use the documented system XAudio2
2.9/2.8 interface. Restrict DLL lookup to System32. Do not alias XAudio2 2.7 to
2.8/2.9: their interfaces differ. A future WinMM output adapter must implement
buffer lifetime, completion callbacks, reset, prepare/unprepare and position
semantics; returning zero for these imports does not implement audio. Input
capture availability is separate from output playback. The audio route now
resolves the 23 observed WINMM symbols directly from System32. Export discovery
alone does not establish working WinMM playback or endpoint activation.

Reproduce the static audit with:
`python3 xbox-test/audio/audit-imports.py /workspace/steam-owned/games/megabonk`.
It compares per-image LLVM and pefile imports, the saved CSV, and the two route
diagnostic symbol lists. It scans direct and delay imports from all top-level
game PEs; dynamically selected libraries remain outside this check.

## Native diagnostic

`xbox-test/audio/build.sh` builds an actual x64 PE DLL with mingw/lld; the signed
UWP launcher packages and loads it through the real Play mapper diagnostic hook.
It initializes the worker's COM MTA, creates the real system XAudio2 engine,
mastering and source voices, submits 24,000 stereo PCM frames with measured
non-zero values, and polls the system SamplesPlayed counter for at most two
seconds. It never executes Unity or substitutes a synthetic playback counter.
The queued buffer remains alive until the source voice is destroyed.

GetState uses flags=0. The buffer omits XAUDIO2_END_OF_STREAM to avoid the
end-of-stream counter reset; XAUDIO2_VOICE_NOSAMPLESPLAYED would leave that
counter unspecified. Microsoft documents these semantics at:
https://learn.microsoft.com/en-us/windows/desktop/api/xaudio2/ns-xaudio2-xaudio2_voice_state

The historical REBUILD.md finding supplies only 'XAudio2 played 0 samples';
no surviving return codes, voice flags or timing establish its root cause.
Fresh console evidence must distinguish engine creation, endpoint creation,
submission and sample progress before claiming that historical issue fixed.
