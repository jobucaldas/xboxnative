// Construct an RVA-based delay import directory in a no-CRT TLS fixture.
// The mapper resolves the resulting IAT eagerly before callbacks run.
const fs = require('fs');
const input = fs.readFileSync(process.argv[2]);
const output = Buffer.from(input);
const pe = output.readUInt32LE(0x3c);
const optional = pe + 24;
const sections = optional + output.readUInt16LE(pe + 20);
let data;
for (let i = 0; i < output.readUInt16LE(pe + 6); ++i) {
  const off = sections + i * 40;
  const name = output.subarray(off, off + 8).toString('ascii').replace(/\0.*$/, '');
  if (name === '.data') data = off;
}
if (data === undefined) throw Error('missing .data section');
const va = output.readUInt32LE(data + 12), raw = output.readUInt32LE(data + 20);
const rawSize = output.readUInt32LE(data + 16), used = output.readUInt32LE(data + 8);
const start = (used + 15) & ~15, end = start + 160;
if (end > rawSize) throw Error('insufficient .data padding');
output.fill(0, raw + start, raw + end);
const put32 = (offset, value) => output.writeUInt32LE(value, raw + start + offset);
const rva = offset => va + start + offset;
// Delay descriptor, then null descriptor, DLL name, name+hint, INT and IAT.
put32(0,1); put32(4,rva(64)); put32(12,rva(136)); put32(16,rva(112));
output.write('strict-provider.dll\0',raw+start+64,'ascii');
output.write('fixture_named\0',raw+start+90,'ascii');
output.writeBigUInt64LE(BigInt(rva(88)),raw+start+112);
output.writeBigUInt64LE(0x8000000000000007n,raw+start+120);
output.writeBigUInt64LE(BigInt(rva(88)),raw+start+136);
output.writeBigUInt64LE(0x8000000000000007n,raw+start+144);
output.writeUInt32LE(end,data+8);
// PE32+ data-directory index 13 (delay imports).
output.writeUInt32LE(rva(0),optional+112+13*8);
output.writeUInt32LE(64,optional+116+13*8);
fs.writeFileSync(process.argv[3],output);
