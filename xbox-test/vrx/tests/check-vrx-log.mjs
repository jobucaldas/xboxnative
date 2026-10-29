// SPDX-License-Identifier: GPL-3.0-or-later
/** Validate the three expected VRX case outcomes in a captured or synthetic log. */
export function checkVrxLog(log) {
  if (typeof log !== 'string') throw new TypeError('log must be a string');
  const lines = log.split(/\r?\n/);
  const specs = [
    { id: 'A', re: /^\[\s*[\d.]+\]\s+caseA VirtualAlloc\(PAGE_EXECUTE_READWRITE\)\+exec: ALLOC_FAILED err=87$/ },
    { id: 'B', re: /^\[\s*[\d.]+\]\s+caseB VirtualAlloc\(RW\)\+VirtualProtect\(RX\)\+exec: OK value=42$/ },
    { id: 'C', re: /^\[\s*[\d.]+\]\s+caseC VirtualAllocFromApp\(RW\)\+VirtualProtectFromApp\(RX\)\+exec: OK value=42$/ },
  ];
  const outcomes = {};
  const errors = [];
  for (const { id, re } of specs) {
    const candidates = lines.filter(line => line.includes(`case${id} `));
    const exact = candidates.filter(line => re.test(line));
    if (candidates.length !== 1) errors.push(`case${id}: expected exactly one result, found ${candidates.length}`);
    else if (exact.length !== 1) errors.push(`case${id}: missing or contradictory expected outcome`);
    else outcomes[id] = id === 'A' ? 'rejected' : 'executed';
  }
  return { ok: errors.length === 0, outcomes, errors };
}
