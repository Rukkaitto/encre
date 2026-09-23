// Which app slot the bootloader starts from.
//
// THIS IS THE FILE THAT CAN STOP A READER BOOTING, so it is the one worth
// reading slowly. Writing Encre into a spare slot changes nothing on its own;
// the reader keeps starting the firmware it already had. What moves it is 32
// bytes in the `otadata` partition, and an entry whose sequence number or CRC is
// wrong is an entry the bootloader ignores -- which at best leaves the old
// firmware running and at worst leaves neither selected.
//
// Nothing here talks to a device. It turns bytes into a decision and a decision
// into bytes, so all of it is testable on a desktop against the otadata a real
// reader actually shipped with -- which is what tools/test_otadata.mjs does.

'use strict';

/** ESP-IDF's `esp_ota_select_entry_t`: one per otadata sector. */
export const ENTRY_BYTES = 32;
/** otadata holds two sectors, one entry at the top of each. */
export const SECTOR_BYTES = 0x1000;
export const SECTORS = 2;

/** An erased 32-bit word, and the value ESP-IDF reads as "no entry here". */
const EMPTY = 0xffffffff;

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i += 1) {
    let c = i;
    for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[i] = c >>> 0;
  }
  return t;
})();

/**
 * CRC-32 with an explicit starting value, zlib's convention.
 *
 * ESP-IDF computes an entry's checksum as `esp_rom_crc32_le(UINT32_MAX, &seq,
 * 4)`. That seed and these inversions are not a guess: both otadata sectors of a
 * stock X3 reproduce their stored CRC exactly under this function, one of them
 * for a sequence of 1 and the other for 0, and tools/test_otadata.mjs pins both.
 */
export function crc32(bytes, seed = 0xffffffff) {
  let c = (seed ^ 0xffffffff) >>> 0;
  for (let i = 0; i < bytes.length; i += 1) {
    c = (c >>> 8) ^ TABLE[(c ^ bytes[i]) & 0xff];
  }
  return (c ^ 0xffffffff) >>> 0;
}

function seqBytes(seq) {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setUint32(0, seq >>> 0, true);
  return b;
}

/** The checksum ESP-IDF expects for a given sequence number. */
export function entryCrc(seq) {
  return crc32(seqBytes(seq));
}

/**
 * Read one 32-byte entry.
 *
 * `valid` mirrors the bootloader's own test: a sequence that is not the erased
 * value, and a checksum that agrees. An entry failing either is not "slot
 * unknown", it is an entry the bootloader will not consider at all.
 */
export function parseEntry(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const seq = view.getUint32(0, true);
  const state = view.getUint32(24, true);
  const crc = view.getUint32(28, true);
  return { seq, state, crc, valid: seq !== EMPTY && crc === entryCrc(seq) };
}

/** Read both entries out of a full otadata read. */
export function parseOtadata(bytes) {
  const out = [];
  for (let i = 0; i < SECTORS; i += 1) {
    const at = i * SECTOR_BYTES;
    out.push(at + ENTRY_BYTES <= bytes.length
      ? parseEntry(bytes.subarray(at, at + ENTRY_BYTES))
      : { seq: EMPTY, state: EMPTY, crc: EMPTY, valid: false });
  }
  return out;
}

/**
 * Which app slot the bootloader will start from, or null if it would choose by
 * itself.
 *
 * ESP-IDF picks the valid entry with the HIGHEST sequence and starts slot
 * `(seq - 1) % slots`. With no valid entry it falls back to the first app
 * partition, which is why a blank otadata boots a freshly flashed device.
 */
export function activeSlot(entries, slots = SECTORS) {
  const valid = entries.filter((e) => e.valid);
  if (valid.length === 0) return null;
  const best = valid.reduce((a, b) => (b.seq > a.seq ? b : a));
  return ((best.seq - 1) % slots + slots) % slots;
}

/**
 * The 32 bytes that make the bootloader start `slot`, and which sector to put
 * them in.
 *
 * TWO THINGS HAVE TO BE TRUE AT ONCE and each is easy to get right alone. The
 * sequence has to SELECT the slot -- `(seq - 1) % slots` -- and it has to BEAT
 * whatever is already there, or the bootloader keeps reading the other entry and
 * nothing appears to happen. So the sequence is the smallest one above the
 * current highest that also lands on the slot wanted.
 *
 * It is written to the OTHER sector, never over the entry currently in charge:
 * a write that is interrupted half way then damages an entry the reader is not
 * relying on, and the old one still selects the old firmware. Overwriting the
 * live entry turns a cancelled flash into a reader that boots nothing.
 */
export function selectSlot(entries, slot, slots = SECTORS) {
  const valid = entries.filter((e) => e.valid);
  const highest = valid.length ? Math.max(...valid.map((e) => e.seq)) : 0;

  let seq = highest + 1;
  while (((seq - 1) % slots + slots) % slots !== slot) seq += 1;

  // The sector holding the current highest is the one in charge, so write the
  // other. With nothing valid at all, sector 0 is as good as either.
  const liveIndex = valid.length
    ? entries.indexOf(valid.reduce((a, b) => (b.seq > a.seq ? b : a)))
    : -1;
  const sector = liveIndex === 0 ? 1 : 0;

  const bytes = new Uint8Array(ENTRY_BYTES).fill(0xff);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, seq >>> 0, true);
  // seq_label and ota_state stay erased, exactly as the stock entry has them.
  // ota_state is only read when the rollback feature is compiled in, and this
  // firmware does not use it; writing a value there would be inventing state.
  view.setUint32(28, entryCrc(seq), true);

  return { sector, seq, offsetInPartition: sector * SECTOR_BYTES, bytes };
}
