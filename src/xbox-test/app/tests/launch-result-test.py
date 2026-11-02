#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('check_play', Path(__file__).resolve().parents[2] / 'tools/check-play-result.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class LaunchResultTest(unittest.TestCase):
    def setUp(self):
        self.request = {'appid': 3405340, 'attemptId': '844:1'}
        self.result = dict(self.request, state='completed', terminal=True, ok=True,
                           gameCodeExecuted=True, stage='guest-execution', exitCode=0)
        self.session = ('launch: request written LocalState\\launch_request.json appid=3405340 attempt=844:1 dir=game\n'
                        'launch: RESULT attempt=844:1 ok=1 mapped=4/4 gameCodeExecuted=true: EXE returned code=0\n')

    def test_clean_completion(self):
        self.assertIsNone(checker.check(3405340, self.request, self.result, self.session))

    def test_reject_incomplete_failed_and_stale(self):
        for changes in [{'state': 'running', 'terminal': False}, {'ok': False},
                        {'gameCodeExecuted': False}, {'stage': 'loader'},
                        {'attemptId': '844:2'}, {'appid': 480}, {'exitCode': None},
                        {'exitCode': 37}, {'exitCode': False}, {'terminal': 1}]:
            with self.subTest(changes=changes):
                self.assertIsNotNone(checker.check(3405340, self.request, dict(self.result, **changes), self.session))

    def test_reject_old_schema_and_old_boot(self):
        self.assertIsNotNone(checker.check(3405340, {'appid': 3405340}, self.result, self.session))
        self.assertIsNotNone(checker.check(3405340, self.request, self.result, ''))
        self.assertIsNotNone(checker.check(3405340, self.request, self.result, self.session.replace('844:1', '844:10')))
        self.assertIsNotNone(checker.check(3405340, self.request, self.result, self.session.replace('RESULT', 'RUNNING')))


if __name__ == '__main__':
    unittest.main()
