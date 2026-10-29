// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { checkVrxLog } from './check-vrx-log.mjs';

const good = `[   0.079] caseA VirtualAlloc(PAGE_EXECUTE_READWRITE)+exec: ALLOC_FAILED err=87
[   0.079] caseB VirtualAlloc(RW)+VirtualProtect(RX)+exec: OK value=42
[   0.080] caseC VirtualAllocFromApp(RW)+VirtualProtectFromApp(RX)+exec: OK value=42`;

test('accepts exactly the expected RWX rejection and both RW to RX successes', () => {
  assert.deepEqual(checkVrxLog(good), { ok: true, outcomes: { A: 'rejected', B: 'executed', C: 'executed' }, errors: [] });
});
test('rejects missing, duplicate, and contradictory outcomes', () => {
  assert.equal(checkVrxLog(good.split('\n').slice(0, 2).join('\n')).ok, false);
  assert.equal(checkVrxLog(`${good}\n${good.split('\n')[0]}`).ok, false);
  assert.equal(checkVrxLog(good.replace('ALLOC_FAILED err=87', 'OK value=42')).ok, false);
  assert.equal(checkVrxLog(good.replace('OK value=42', 'FAILED err=5')).ok, false);
});
test('rejects malformed/non-text input', () => {
  assert.equal(checkVrxLog('').ok, false);
  assert.throws(() => checkVrxLog(null), TypeError);
});
