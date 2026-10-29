// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

const cli = resolve('xbox-test/vrx/tests/review-vrx-log.mjs');
const valid = `[   0.079] caseA VirtualAlloc(PAGE_EXECUTE_READWRITE)+exec: ALLOC_FAILED err=87
[   0.079] caseB VirtualAlloc(RW)+VirtualProtect(RX)+exec: OK value=42
[   0.080] caseC VirtualAllocFromApp(RW)+VirtualProtectFromApp(RX)+exec: OK value=42`;

async function inTemp(fn) {
  const dir = await mkdtemp(join(tmpdir(), 'vrx-review-'));
  try { await fn(dir); } finally { await rm(dir, { recursive: true, force: true }); }
}
function run(path) { return spawnSync(process.execPath, [cli, path], { encoding: 'utf8' }); }

test('CLI accepts a synthetic valid log and labels the review offline', async () => inTemp(async dir => {
  const path = join(dir, 'valid.log'); await writeFile(path, valid);
  const result = run(path);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /offline log validation only; freshness\/device identity unverified/i);
  assert.match(result.stdout, /A=expected RWX rejection/);
}));
test('CLI rejects contradictory outcomes with nonzero exit', async () => inTemp(async dir => {
  const path = join(dir, 'bad.log'); await writeFile(path, valid.replace('ALLOC_FAILED err=87', 'OK value=42'));
  const result = run(path);
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Invalid VRX log/);
}));
test('CLI reports a missing log and exits nonzero', async () => inTemp(async dir => {
  const result = run(join(dir, 'missing.log'));
  assert.equal(result.status, 2);
  assert.match(result.stderr, /Cannot read log/);
}));
