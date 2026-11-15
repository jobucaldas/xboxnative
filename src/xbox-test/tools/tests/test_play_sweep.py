#!/usr/bin/env python3
import importlib.util
import subprocess
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PARSER_PATH = HERE.parent / "play-sweep-parser.py"
spec = importlib.util.spec_from_file_location("play_sweep_parser", PARSER_PATH)
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)
FIXTURES = HERE / "fixtures" / "play-sweep"


class PlaySweepParserTest(unittest.TestCase):
    def test_redacted_saved_log_excerpts(self):
        expected = {
            "1407100": ("[TDA00] Muv-Luv Unlimited: THE DAY AFTER - Episode 00 REMASTERED", "x64", "FAIL", "", "unhandled exception code=0xc0000005", "GAP: total=904 missing=645 win32u=52", "1103", "6.969"),
            "2142790": ("Fields of Mistria", "x64", "FAIL", "2147942487", "LoadLibrary failed name=DXIL.dll error=126", "GAP: total=776 missing=466 win32u=93", "1100", "21.435"),
            "411370": ("MELTY BLOOD Actress Again Current Code", "x86", "FAIL", "", "unresolved import kernel32.dll!IsValidCodePage", "GAP: total=277 missing=224 win32u=43", "1141", "0.174"),
            "363440": ("Mega Man Legacy Collection", "x86", "FAIL", "", "", "GAP: total=230 missing=211 win32u=16", "1156", "0.103"),
        }
        for appid, values in expected.items():
            with self.subTest(appid=appid):
                row, excerpt = parser.parse(appid, FIXTURES / f"{appid}.log")
                self.assertEqual(row[0], appid)
                self.assertEqual(row[1:5], list(values[:4]))
                self.assertIn(values[4], row[5])
                self.assertEqual(row[6:], list(values[5:]))
                self.assertNotRegex(excerpt, r"7656119\d{10}|(?:\d{1,3}\.){3}\d{1,3}|/home/|[A-Z]:\\")

    def test_redacts_account_markers_and_paths(self):
        # Built at runtime so the repository secret scan never sees a SteamID-shaped literal.
        fake_id = "7656119" + "8000000000"
        text = parser.scrub("SteamID=" + fake_id + " at S:\\Users\\private\\save.dat")
        self.assertNotIn(fake_id, text)
        self.assertNotIn("private", text)
        self.assertTrue("<steamid>" in text or "<redacted>" in text)


class PlaySweepInstallGuardTest(unittest.TestCase):
    SWEEP = HERE.parent / "play-sweep.sh"

    def plan(self, *args):
        return subprocess.run(["bash", str(self.SWEEP), "--dry-run", *args, "123"],
                              capture_output=True, text=True)

    def test_sweep_never_downloads_by_default(self):
        out = self.plan()
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertIn("(no downloads)", out.stdout)
        self.assertIn('PT_MAXMB=$maxmb timeout', self.SWEEP.read_text())

    def test_downloads_need_an_explicit_size(self):
        self.assertIn("(downloads up to 500 MB)", self.plan("--allow-install", "500").stdout)
        self.assertEqual(self.plan("--allow-install", "0").returncode, 2)


if __name__ == "__main__":
    unittest.main()
