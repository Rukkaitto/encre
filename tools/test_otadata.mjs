// The otadata encoder and the slot decision, against the bytes a real reader
// shipped with.
//
//     node tools/test_otadata.mjs
//
// THIS IS THE FILE THAT CAN STOP A READER BOOTING. Writing Encre into a spare
// slot changes nothing by itself; 32 bytes of otadata are what move the
// bootloader, and an entry with a wrong sequence or checksum is one it ignores.
// So the CRC convention is not taken from documentation here -- it is pinned
// against a stock X3's own otadata, both sectors, one with a sequence of 1 and
// one with 0.
//
// Fixture: the 32-byte head of each otadata sector at 0xe000 and 0xf000 of
// `~/encre-device-backup/flash-full-16MB-20260820-1433.bin`, the dev X3's
// complete flash as it shipped.
import {
  crc32, entryCrc, parseEntry, parseOtadata, activeSlot, selectSlot,
  ENTRY_BYTES, SECTOR_BYTES,
} from '../web/otadata.js';

const STOCK_B64 = 'AQAAAP///////////////////////////////5qYQ0cAAAAA/////////////////////////////////////w==';

let failures = 0;
const check = (ok, what) => {
  console.log('  ' + (ok ? 'ok  ' : 'FAIL') + ' ' + what);
  if (!ok) failures += 1;
};

// The fixture is the two 32-byte heads; otadata is two 4 KB sectors, so lay
// them out the way a real read returns them.
function stockOtadata() {
  const heads = Buffer.from(STOCK_B64, 'base64');
  const full = new Uint8Array(SECTOR_BYTES * 2).fill(0xff);
  full.set(heads.subarray(0, ENTRY_BYTES), 0);
  full.set(heads.subarray(ENTRY_BYTES, ENTRY_BYTES * 2), SECTOR_BYTES);
  return full;
}

console.log("the CRC convention, pinned to a real reader's own bytes");
const entries = parseOtadata(stockOtadata());
check(entries[0].seq === 1, 'sector 0 carries sequence 1');
check(entries[0].crc === 0x4743989a, "...and the checksum the reader stored");
check(entryCrc(1) === 0x4743989a,
      'entryCrc(1) reproduces it, so the seed and inversions are right');
check(entries[0].valid, 'sector 0 is an entry the bootloader would consider');
check(entries[1].seq === 0 && entries[1].crc === 0xffffffff,
      'sector 1 carries sequence 0 with checksum 0xffffffff');
check(entryCrc(0) === 0xffffffff,
      '...which the same function reproduces, on a second independent value');
check(entries[1].valid, 'sector 1 is valid too, just older');

console.log('\nwhich slot that actually selects');
check(activeSlot(entries) === 0,
      'a stock reader starts slot 0 -- app0, the firmware it came with');
check(activeSlot([]) === null, 'no entries at all means the bootloader chooses');
check(activeSlot([{ seq: 5, crc: 0, valid: false }]) === null,
      'an entry that fails its checksum is not considered');

console.log('\nselecting app1, which is what an install has to do');
const pick = selectSlot(entries, 1);
check(pick.seq === 2, 'the sequence is 2: the smallest above 1 that lands on slot 1');
check(pick.sector === 1, 'it is written to the sector NOT currently in charge');
check(pick.offsetInPartition === SECTOR_BYTES, '...at 0x1000 into the partition');
check(pick.bytes.length === ENTRY_BYTES, 'the entry is 32 bytes');

// The decision has to survive a round trip: write it back and ask again.
const after = stockOtadata();
after.set(pick.bytes, pick.offsetInPartition);
check(activeSlot(parseOtadata(after)) === 1,
      'after writing it, the bootloader would start slot 1 -- app1');

console.log('\n...and it keeps working when it is done repeatedly');
let state = after;
for (const [n, want] of [[2, 1], [3, 1], [4, 1]]) {
  const p = selectSlot(parseOtadata(state), 1);
  const next = state.slice();
  next.set(p.bytes, p.offsetInPartition);
  check(activeSlot(parseOtadata(next)) === want,
        'update ' + n + ' still selects slot 1 (sequence ' + p.seq + ')');
  state = next;
}
check(parseOtadata(state).some((e) => e.seq > 2),
      'the sequence climbs rather than sticking, so a later write always wins');

console.log('\ngoing back to the firmware the reader came with');
const back = selectSlot(parseOtadata(state), 0);
const restored = state.slice();
restored.set(back.bytes, back.offsetInPartition);
check(activeSlot(parseOtadata(restored)) === 0,
      'Recovery can select slot 0 again, which is the whole un-install story');

console.log('\nthe live entry is never the one overwritten');
// An interrupted write must damage the entry NOBODY is relying on. If it
// overwrote the live one, a cancelled flash could leave neither selectable.
for (const slot of [0, 1]) {
  const p = selectSlot(entries, slot);
  const liveSector = entries[0].seq >= entries[1].seq ? 0 : 1;
  check(p.sector !== liveSector,
        'selecting slot ' + slot + ' writes sector ' + p.sector
        + ', not the live sector ' + liveSector);
}

console.log('\nthe CRC function itself');
// `seed` means "continue from this finalized CRC", which is zlib's and
// binascii's convention. So the standard check value needs seed 0, and ESP-IDF's
// UINT32_MAX seed on an empty buffer gives UINT32_MAX straight back.
check(crc32(new TextEncoder().encode('123456789'), 0) === 0xcbf43926,
      'seeded at 0 it produces CRC-32\'s published check value, so this is '
      + 'CRC-32 and not a variant');
check(crc32(new Uint8Array(0)) === 0xffffffff,
      'seeded at UINT32_MAX an empty buffer returns UINT32_MAX, which is what '
      + 'makes an erased entry self-consistent');

console.log();
if (failures) {
  console.log(failures + ' failure(s)');
  process.exit(1);
}
console.log('all checks passed');
