Triangle shaders target Shader Model 5.0 (DXBC). They are compiled off-device;
the UWP app only imports D3D11, with no D3DCompile dependency.

Regenerate on Linux from this directory:

```sh
x86_64-w64-mingw32-g++ compile.cpp -static -ld3dcompiler -o /tmp/d3tri-compile.exe
env -u DP_PASS -u TS_AUTHKEY WINEDEBUG=-all wine /tmp/d3tri-compile.exe
python3 embed.py
```

Original generation used Wine 10's d3dcompiler_47. Both raw DXBC and embedded
arrays are checked in, alongside their HLSL and compiler utility source.
