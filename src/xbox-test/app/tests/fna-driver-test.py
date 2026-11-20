# SPDX-License-Identifier: GPL-3.0-or-later
# Guard: the FNA smoke pins FNA3D to its D3D11 driver. Default FNA3D drivers
# (SDLGPU, built by -DBUILD_SDL3=ON) do not create the guest DXGI swapchain.
import unittest
from pathlib import Path

PLAY_RUNTIME = Path(__file__).resolve().parent / "play-runtime.sh"


class FnaDriverGuardTest(unittest.TestCase):
    def test_mode_loop_forces_d3d11(self):
        lines = PLAY_RUNTIME.read_text().splitlines()
        start = next(i for i, l in enumerate(lines) if l.startswith("for mode in mono fna "))
        run = next(l for l in lines[start:] if 'wine "$out/play-runner.exe" "$mode" >' in l)
        self.assertIn("FNA3D_FORCE_DRIVER=D3D11 ", run)


if __name__ == "__main__":
    unittest.main()
