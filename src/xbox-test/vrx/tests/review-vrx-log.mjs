// SPDX-License-Identifier: GPL-3.0-or-later
import { readFile } from 'node:fs/promises';
import { checkVrxLog } from './check-vrx-log.mjs';

const path = process.argv[2];
if (!path || process.argv.length !== 3) {
  console.error('Usage: node review-vrx-log.mjs <log-path>');
  process.exit(2);
}

let log;
try {
  log = await readFile(path, 'utf8');
} catch (error) {
  console.error(`Cannot read log: ${error.message}`);
  process.exit(2);
}

console.log('Offline log validation only; freshness/device identity unverified.');
const result = checkVrxLog(log);
if (!result.ok) {
  console.error(`Invalid VRX log: ${result.errors.join('; ')}`);
  process.exit(1);
}
console.log('VRX outcomes: A=expected RWX rejection; B=RW→RX executed; C=FromApp RW→RX executed.');
