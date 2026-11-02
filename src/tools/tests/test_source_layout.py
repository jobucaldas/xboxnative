# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-layout regression; all subprocesses start outside the checkout."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class SourceLayoutTest(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.cwd = Path(self.scratch.name)

    def run_command(self, *args, input=None):
        return subprocess.check_output(args, cwd=self.cwd, input=input, text=True)

    def test_root_and_tracked_layout(self):
        self.assertEqual(Path(self.run_command('git', '-C', str(ROOT), 'rev-parse',
                                              '--show-toplevel').strip()), ROOT)
        tracked = self.run_command('git', '-C', str(ROOT), 'ls-files').splitlines()
        self.assertFalse(any(p.startswith(('xbox-test/', 'tools/')) for p in tracked))
        code_suffixes = {'.py', '.sh', '.js', '.mjs', '.c', '.cpp', '.h', '.hpp', '.ps1'}
        self.assertEqual([p for p in tracked if Path(p).suffix in code_suffixes
                          and not p.startswith('src/')], [])
        self.assertFalse((ROOT / 'xbox-test').exists())
        self.assertFalse((ROOT / 'tools').exists())
        for path in ('src/xbox-test/app/main.cpp', 'src/tools/fetch-dep.sh',
                     'deps.pin', 'patches', 'NOTICE', 'LICENSE', 'docs', 'research'):
            self.assertTrue((ROOT / path).exists(), path)
        scripts = ['src/tools/fetch-dep.sh', 'src/tools/build-wine-donors.sh',
                   'src/tools/build-msix-packaging.sh',
                   'src/tools/check-uwp-build-env.sh', 'src/xbox-test/smoke-all.sh',
                   'src/xbox-test/kit/build-kit.sh', 'src/xbox-test/kit/check-install-guide.sh']
        scripts += [f'src/xbox-test/uwph/host/run-{lane}.sh'
                    for lane in ('regsql', 'reg', 'mlp', 'cs32', 'swc')]
        for name in scripts:
            script = ROOT / name
            # Evaluate only the existing root-discovery assignments, never device code.
            lines = [line.split(';')[0] for line in script.read_text().splitlines()
                     if line.startswith(('here=', 'root='))]
            discovery = '\n'.join(lines).replace('${BASH_SOURCE[0]}', '$0')
            resolved = self.run_command('bash', '-ec', discovery + '\nprintf "%s" "$root"', str(script))
            self.assertEqual(Path(resolved), ROOT, name)

    def test_dependency_pins(self):
        pins = {row[0]: row[1:] for line in (ROOT / 'deps.pin').read_text().splitlines()
                if (row := line.split()) and not line.startswith('#')}
        for script, pin in (('build-wine-donors.sh', 'wine-src'),
                            ('fetch-boxedwine.sh', 'boxedwine'),
                            ('fetch-uwp-crossbuild.sh', 'uwp-crossbuild')):
            output = self.run_command('bash', str(ROOT / 'src/tools' / script), '--dry-run')
            for value in pins[pin]:
                self.assertIn(value, output)
        self.assertIn(str(ROOT / 'out/wine-donors'), self.run_command(
            'bash', str(ROOT / 'src/tools/build-wine-donors.sh'), '--dry-run'))
        output = self.run_command('bash', str(ROOT / 'src/tools/fetch-dep.sh'),
                                  'msix-packaging', '--dry-run')
        for value in pins['msix-packaging']:
            self.assertIn(value, output)
        self.assertIn(str(ROOT / 'third_party/msix-packaging'), output)

    def test_generated_source_paths(self):
        sandbox = self.cwd / 'repo'
        files = ('src/tools/xbox/import_map.json', 'src/tools/xbox/gen-gap-routes.py',
                 'src/xbox-test/uwph/host/gen-mlp-map.py',
                 'src/xbox-test/uwph/display/generate_routes.py')
        for name in files:
            dest = sandbox / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, dest)
        (sandbox / 'src/xbox-test/app/host').mkdir(parents=True)
        for name in files[1:]:
            args = [str(sandbox / 'src/xbox-test/uwph/display/routes.generated.h')] if name.endswith('generate_routes.py') else []
            self.run_command(sys.executable, '-I', str(sandbox / name), *args)
        for name in ('app/wine_gap_routes.h', 'app/host/mlp_routes.h',
                     'uwph/host/mlp_routes.h', 'uwph/host/mlp_overrides.h',
                     'uwph/display/routes.generated.h'):
            self.assertIn('src/tools/xbox/import_map.json',
                          (sandbox / 'src/xbox-test' / name).read_text(), name)

    def test_ci_entrypoints(self):
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertEqual(set(re.findall(r'^  ([\w-]+):$', workflow.split('\njobs:\n', 1)[1], re.M)),
                         {'offline-checks', 'registry-persistence', 'strict-imports', 'guest-pe-inspection'})
        paths = re.findall(r'\bsrc/(?:xbox-test|tools)/[\w./*-]+', workflow)
        self.assertTrue(paths)
        for name in paths:
            self.assertTrue(list(ROOT.glob(name)), name)
        self.assertIn('python3 -I src/tools/tests/test_source_layout.py', workflow)

    def test_relocated_outputs_stay_ignored(self):
        paths = ['src/xbox-test/app/out/layout/game.exe', 'src/xbox-test/app/out/test.pfx',
                 'src/xbox-test/uwph/host/third_party/sqlite/sqlite3.c',
                 'src/tools/out/wine-donors/hid.dll', 'src/tools/third_party/source/source.c',
                 'src/xbox-test/uwph/host/out-tls/guest.obj', 'src/tools/source.tar.xz',
                 'src/xbox-test/runs/frame.png', 'src/xbox-test/captures/frame.png',
                 'src/xbox-test/app/boot.log', 'src/xbox-test/app/package.msix',
                 'src/xbox-test/app/steam_session.bin']
        ignored = self.run_command('git', '-C', str(ROOT), 'check-ignore', '--stdin',
                                   input='\n'.join(paths) + '\n').splitlines()
        self.assertEqual(ignored, paths)


if __name__ == '__main__':
    unittest.main()
