// SPDX-License-Identifier: GPL-3.0-or-later
/** Validate the three expected VRX case outcomes in a captured or synthetic log. */
export function checkVrxLog(log) {
  if (typeof log !== 'string') throw new TypeError('log must be a string');
  const lines = log.split(/\r?\n/);
  const specs = [
    { id: 'A', re: /^\[\s*\d+(?:\.\d+)?\]\s+caseA VirtualAlloc\(PAGE_EXECUTE_READWRITE\)\+exec: ALLOC_FAILED err=87$/ },
    { id: 'B', re: /^\[\s*\d+(?:\.\d+)?\]\s+caseB VirtualAlloc\(RW\)\+VirtualProtect\(RX\)\+exec: OK value=42$/ },
    { id: 'C', re: /^\[\s*\d+(?:\.\d+)?\]\s+caseC VirtualAllocFromApp\(RW\)\+VirtualProtectFromApp\(RX\)\+exec: OK value=42$/ },
  ];
  const outcomes = {};
  const errors = [];
  for (const { id, re } of specs) {
    // Count malformed case records too, so they cannot hide contradictory results.
    const caseLabel = new RegExp(`case${id}\\b`);
    const candidates = lines.filter(line => caseLabel.test(line));
    if (candidates.length !== 1) errors.push(`case${id}: expected exactly one result, found ${candidates.length}`);
    else if (!re.test(candidates[0])) errors.push(`case${id}: missing or contradictory expected outcome`);
    else outcomes[id] = id === 'A' ? 'rejected' : 'executed';
  }
  return { ok: errors.length === 0, outcomes, errors };
}
