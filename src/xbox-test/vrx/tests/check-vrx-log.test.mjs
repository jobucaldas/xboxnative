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
test('accepts LF and CRLF records amid unrelated probe output', () => {
  for (const newline of ['\n', '\r\n']) {
    const log = [
      '[   0.018] boot: Initialize (pid=1234)',
      ...good.split('\n'),
      '[   0.080] vrx: all cases done',
      '[   0.197] frame=1 t=0.2',
      '',
    ].join(newline);
    assert.deepEqual(checkVrxLog(log), { ok: true, outcomes: { A: 'rejected', B: 'executed', C: 'executed' }, errors: [] });
  }
});
test('rejects missing and duplicate results for every case', () => {
  const lines = good.split('\n');
  for (const [index, line] of lines.entries()) {
    const id = 'ABC'[index];
    const missing = checkVrxLog(lines.filter((_, i) => i !== index).join('\n'));
    assert.equal(missing.ok, false, `missing case${id}`);
    assert.deepEqual(missing.errors, [`case${id}: expected exactly one result, found 0`]);
    const duplicate = checkVrxLog(`${good}\n${line}`);
    assert.equal(duplicate.ok, false, `duplicate case${id}`);
    assert.deepEqual(duplicate.errors, [`case${id}: expected exactly one result, found 2`]);
  }
});
test('rejects unexpected allocation, protection, execution, and return-value outcomes for every case', () => {
  const lines = good.split('\n');
  const outcomes = [
    'ALLOC_FAILED err=5', 'ALLOC_FAILED err=87', 'PROTECT_FAILED err=5',
    'EXCEPTION code=0xc0000005', 'OK value=0', 'OK value=43', 'OK value=42',
  ];
  for (const [index, line] of lines.entries()) {
    const id = 'ABC'[index];
    for (const outcome of outcomes) {
      if (line.endsWith(`: ${outcome}`)) continue;
      const changed = [...lines];
      changed[index] = line.replace(/: .*$/, `: ${outcome}`);
      const result = checkVrxLog(changed.join('\n'));
      assert.equal(result.ok, false, `case${id}: ${outcome}`);
      assert.deepEqual(result.errors, [`case${id}: missing or contradictory expected outcome`]);
      assert.equal(Object.hasOwn(result.outcomes, id), false);
    }
  }
});
test('rejects extra case results with malformed label separators', () => {
  for (const line of good.split('\n')) {
    const contradictory = line.replace(/ALLOC_FAILED err=87|OK value=42/, 'FAILED err=5');
    for (const separator of ['\t', ': ']) {
      const extra = contradictory.replace(/(case[ABC]) /, `$1${separator}`);
      assert.equal(checkVrxLog(`${good}\n${extra}`).ok, false, extra);
    }
    const truncated = line.match(/^.*?case[ABC]/)[0];
    assert.equal(checkVrxLog(`${good}\n${truncated}`).ok, false, truncated);
  }
});
test('rejects malformed timestamps on each case result', () => {
  for (const timestamp of ['', '.', '...', '0..079', '0.079.1', '-0.079', '+0.079', '1e3', 'NaN', 'Infinity']) {
    for (let index = 0; index < 3; index++) {
      const lines = good.split('\n');
      lines[index] = lines[index].replace(/\[.*?\]/, `[   ${timestamp}]`);
      assert.equal(checkVrxLog(lines.join('\n')).ok, false, `case ${index}: ${timestamp}`);
    }
  }
});
test('rejects malformed/non-text input', () => {
  assert.equal(checkVrxLog('').ok, false);
  for (const input of [null, undefined, 42, true, {}, [], Buffer.from(good)]) {
    assert.throws(() => checkVrxLog(input), TypeError);
  }
});
