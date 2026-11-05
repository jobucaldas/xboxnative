#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a fresh terminal runtime result, not visual/playable acceptance."""
import json
from pathlib import Path
import re
import sys


def check(appid, request, result, session):
    if request.get('appid') != appid or result.get('appid') != appid:
        return 'request/result app does not match'
    attempt = request.get('attemptId')
    if not isinstance(attempt, str) or not re.fullmatch(r'[0-9]+:[0-9]+', attempt):
        return 'missing valid attempt identity (old launcher results are not acceptance)'
    if result.get('attemptId') != attempt:
        return 'stale result: attempt identity does not match'
    identity = re.escape(attempt)
    if not re.search(r'launch: request written [^\r\n]*\battempt=' + identity + r'(?:\s|$)', session):
        return 'no matching request in this boot session'
    if not re.search(r'launch: RESULT attempt=' + identity + r' ok=1\b', session):
        return 'no successful terminal result in this boot session'
    if result.get('state') != 'completed' or result.get('terminal') is not True or result.get('ok') is not True:
        return 'runtime did not complete successfully (a window/device milestone is not success)'
    if result.get('gameCodeExecuted') is not True or result.get('stage') != 'guest-execution':
        return 'no observed guest execution'
    if type(result.get('exitCode')) is not int or result['exitCode'] != 0:
        return 'guest did not exit with code zero'
    return None


def main():
    if len(sys.argv) != 3:
        print('usage: check-play-result.py <appid> <evidence-dir>')
        return 2
    try:
        appid = int(sys.argv[1])
        out = Path(sys.argv[2])
        request = json.loads((out / 'launch_request.json').read_text())
        result = json.loads((out / 'launch_result.json').read_text())
        session = (out / 'boot.session.log').read_text(errors='replace')
        if not isinstance(request, dict) or not isinstance(result, dict):
            raise ValueError('expected JSON objects')
        reason = check(appid, request, result, session)
    except (OSError, ValueError, TypeError):
        reason = 'missing or invalid launch evidence'
    if reason:
        print('LAUNCH RESULT FAIL: ' + reason)
        return 1
    print('LAUNCH RESULT PASS: fresh clean guest completion; visual/playable acceptance still required')
    return 0


if __name__ == '__main__':
    sys.exit(main())
