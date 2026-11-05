// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const cli = fileURLToPath(new URL('./review-vrx-log.mjs', import.meta.url));
const valid = `[   0.079] caseA VirtualAlloc(PAGE_EXECUTE_READWRITE)+exec: ALLOC_FAILED err=87
[   0.079] caseB VirtualAlloc(RW)+VirtualProtect(RX)+exec: OK value=42
[   0.080] caseC VirtualAllocFromApp(RW)+VirtualProtectFromApp(RX)+exec: OK value=42`;

async function inTemp(fn) {
  const dir = await mkdtemp(join(tmpdir(), 'vrx-review-'));
  try { await fn(dir); } finally { await rm(dir, { recursive: true, force: true }); }
}
function run(args, options = {}) { return spawnSync(process.execPath, [cli, ...args], { encoding: 'utf8', ...options }); }

test('CLI accepts a synthetic valid log and labels the review offline', async () => inTemp(async dir => {
  const path = join(dir, 'valid.log'); await writeFile(path, valid);
  const result = run([path]);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /offline log validation only; freshness\/device identity unverified/i);
  assert.match(result.stdout, /A=expected RWX rejection/);
}));
test('CLI rejects contradictory outcomes with nonzero exit', async () => inTemp(async dir => {
  const path = join(dir, 'bad.log'); await writeFile(path, valid.replace('ALLOC_FAILED err=87', 'OK value=42'));
  const result = run([path]);
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Invalid VRX log/);
}));
test('CLI rejects malformed timestamps and hidden contradictory results', async () => inTemp(async dir => {
  const path = join(dir, 'malformed.log');
  const logs = [
    valid.replace('0.079', '0..079'),
    `${valid}\n[   0.100] caseA\tVirtualAlloc(PAGE_EXECUTE_READWRITE)+exec: OK value=42`,
  ];
  for (const log of logs) {
    await writeFile(path, log);
    const result = run([path]);
    assert.equal(result.status, 1, result.stderr);
    assert.match(result.stderr, /Invalid VRX log/);
    assert.doesNotMatch(result.stdout, /VRX outcomes:/);
  }
}));
test('CLI reports a missing log and exits nonzero', async () => inTemp(async dir => {
  const result = run([join(dir, 'missing.log')]);
  assert.equal(result.status, 2);
  assert.match(result.stderr, /Cannot read log/);
}));
test('CLI reports a directory path as a read error, not valid log text', async () => inTemp(async dir => {
  const result = run([dir]);
  assert.equal(result.status, 2);
  assert.match(result.stderr, /Cannot read log/);
  assert.doesNotMatch(result.stdout, /VRX outcomes:/);
}));
test('CLI requires one explicit log path', () => {
  const result = run([]);
  assert.equal(result.status, 2);
  assert.match(result.stderr, /Usage: node review-vrx-log\.mjs <log-path>/);
  assert.doesNotMatch(result.stdout, /VRX outcomes:/);
});
test('CLI rejects extra arguments instead of ignoring them', () => {
  const result = run(['unused.log', 'extra.log']);
  assert.equal(result.status, 2);
  assert.match(result.stderr, /Usage:/);
  assert.doesNotMatch(result.stdout, /VRX outcomes:/);
});
test('CLI rejects an empty log without reporting successful outcomes', async () => inTemp(async dir => {
  const path = join(dir, 'empty.log'); await writeFile(path, '');
  const result = run([path]);
  assert.equal(result.status, 1);
  assert.match(result.stderr, /caseA: expected exactly one result, found 0/);
  assert.match(result.stderr, /caseB: expected exactly one result, found 0/);
  assert.match(result.stderr, /caseC: expected exactly one result, found 0/);
  assert.doesNotMatch(result.stdout, /VRX outcomes:/);
}));
test('CLI resolves a relative log path from outside the repository', async () => inTemp(async dir => {
  await writeFile(join(dir, 'relative log.log'), valid.replaceAll('\n', '\r\n'));
  const result = run(['relative log.log'], { cwd: dir });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /freshness\/device identity unverified/);
  assert.match(result.stdout, /B=RW→RX executed; C=FromApp RW→RX executed/);
}));
