// The partition-table parser and the layout comparison, against the bytes a
// real reader actually returns.
//
//     node tools/test_parttable.mjs
//
// THE FIXTURE IS NOT SYNTHETIC. It is the first 224 bytes of offset 0x8000 in
// `~/encre-device-backup/flash-full-16MB-20260820-1433.bin`, the dev X3's
// complete flash as it shipped, taken before Encre was ever written to it. A
// parser proved only against a table this repository generated would be proving
// that two of its own ideas agree; this proves it against Espressif's encoder
// and a real device.
//
// Six partitions plus the trailing MD5 entry, 0xFF padding stripped and added
// back by the test so the padding is exercised as padding.
//
// Not wired into `make test`, for tools/test_compare_design.py's reason.
// `make test-tools` globbed tools/test_*.py only, which would not have caught a
// .mjs file at all -- this is the first one there, so that target now runs both
// suffixes, and refuses out loud if node is missing rather than counting a
// module it did not run as a pass.
import { readFileSync } from 'node:fs';
import { parsePartitionTable, compareLayout } from '../web/reader.js';

const STOCK_TABLE_B64 = 'qlABAgCQAAAAUAAAbnZzAAAAAAAAAAAAAAAAAAAAAACqUAEAAOAAAAAgAABvdGFkYXRhAAAAAAAAAAAAAAAAAKpQABAAAAEAAABkAGFwcDAAAAAAAAAAAAAAAAAAAAAAqlAAEQAAZQAAAGQAYXBwMQAAAAAAAAAAAAAAAAAAAACqUAGCAADJAAAANgBzcGlmZnMAAAAAAAAAAAAAAAAAAKpQAQMAAP8AAAABAGNvcmVkdW1wAAAAAAAAAAAAAAAA6+v//////////////////7Mtzbg7FnSvJ3aMkrH4imI=';

let failures = 0;
const check = (ok, what) => {
  console.log('  ' + (ok ? 'ok  ' : 'FAIL') + ' ' + what);
  if (!ok) failures += 1;
};

// 0xC00 is what the manifest asks the device for; the rest is erased flash.
function stockTable() {
  const used = Buffer.from(STOCK_TABLE_B64, 'base64');
  const full = new Uint8Array(0xC00).fill(0xff);
  full.set(used, 0);
  return full;
}

console.log('parsing a real reader\'s table');
const found = parsePartitionTable(stockTable());
check(found.length === 6, 'six partitions, and the MD5 entry is not one of them');
check(found.map((p) => p.name).join(',') === 'nvs,otadata,app0,app1,spiffs,coredump',
      'every label, in table order');
check(found[3].name === 'app1' && found[3].offset === '0x650000'
      && found[3].size === '0x640000',
      'app1 is at 0x650000, size 0x640000 -- the slot Encre installs to');
check(found[0].offset === '0x9000' && found[0].size === '0x5000',
      'nvs is where the session record expects it');

console.log('\n...and it is Encre\'s layout');
const manifest = JSON.parse(readFileSync(new URL('../web/manifest.json', import.meta.url)));
const verdict = compareLayout(found, manifest.layout.partitions);
check(verdict.matches,
      'a stock X3 matches the table in web/manifest.json'
      + (verdict.matches ? '' : ': ' + verdict.differences.join('; ')));
check(verdict.differences.length === 0, 'with no differences to report');

console.log('\na table that does not match says which entry');
const moved = manifest.layout.partitions.map((p) =>
  p.name === 'app1' ? { ...p, offset: '0x660000' } : p);
const bad = compareLayout(found, moved);
check(!bad.matches, 'a moved partition is a mismatch');
check(bad.differences.length === 1 && bad.differences[0].includes('app1'),
      'and the difference names app1 rather than only counting one');
const renamed = manifest.layout.partitions.map((p) =>
  p.name === 'spiffs' ? { ...p, name: 'data' } : p);
check(compareLayout(found, renamed).differences[0].includes('"spiffs"'),
      'a renamed partition reports the name the reader actually has');
const short = manifest.layout.partitions.slice(0, 4);
check(compareLayout(found, short).differences[0].includes('6 partitions'),
      'a different partition COUNT is reported, not silently zipped');

console.log('\npadding and truncation');
check(parsePartitionTable(new Uint8Array(0xC00).fill(0xff)).length === 0,
      'an erased table parses as no partitions rather than throwing');
check(parsePartitionTable(new Uint8Array(0)).length === 0,
      'an empty read parses as no partitions');
const truncated = stockTable().slice(0, 100);
check(parsePartitionTable(truncated).length === 3,
      'a short read yields only the whole entries it actually contains');

console.log();
if (failures) {
  console.log(failures + ' failure(s)');
  process.exit(1);
}
console.log('all checks passed');
