// Talking to the reader: connect, say what it is, and work out which install
// path applies. NOTHING HERE WRITES TO THE DEVICE. Every call is a read, so a
// page that gets this far cannot damage a reader, which is what makes it
// testable on real hardware before the write lands.

'use strict';

import {
  ESPLoader,
  Transport,
  ESPRESSIF_VID,
  USB_JTAG_SERIAL_PID,
} from './vendor/esptool-js/bundle.js';

/**
 * The chip every Xteink X3 and X4 carries.
 *
 * It is the strongest identity check available and it is WEAKER THAN IT LOOKS:
 * one binary drives both models and the chip id is the same for both, so this
 * can only refuse something that is not an Xteink at all. It cannot tell an X3
 * from an X4, and the page must not claim otherwise.
 */
const EXPECTED_CHIP = 'ESP32-C3';

/**
 * 115200 for both, and the second one is the interesting half.
 *
 * esptool-js reopens the port to switch to a faster baud rate once the stub is
 * up, and its own documentation says setting `romBaudrate` equal to `baudrate`
 * "skips the port reopen entirely, which avoids rebooting boards whose EN/IO0
 * lines are disturbed by reopening the port". This device is an ESP32-C3 on its
 * NATIVE USB rather than behind a UART bridge, and whether a Web Serial port
 * handle survives that reopen is the one thing nobody has been able to confirm
 * either way. Not reopening costs some speed on a 2.6 MB write and removes the
 * question.
 */
const BAUD = 115200;

/** ESP-IDF partition table entry layout. */
const ENTRY_BYTES = 32;
const ENTRY_MAGIC = 0x50aa;   // a real entry
const MD5_MAGIC = 0xebeb;     // the table's trailing checksum entry

/**
 * Parse an ESP-IDF partition table.
 *
 * Returns `{name, offset, size}` per entry, offsets and sizes as hex strings so
 * they compare directly against the manifest, which states them the way
 * partitions.csv does.
 *
 * TYPE AND SUBTYPE ARE READ PAST, NOT RETURNED, and that is a decision rather
 * than an omission. Checking them would mean mapping `ota_1` to 0x11 and the
 * rest of ESP-IDF's subtype table, which is THEIR table -- a copy of it here
 * would be one more thing to keep in step with somebody else's project, and
 * this repository's standing rule is that a second copy drifts. A label, an
 * offset and a size are what decide where a write lands, they all come from
 * partitions.csv, and matching all three on all six entries is not something a
 * differently-laid-out reader does by accident.
 */
export function parsePartitionTable(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = [];
  for (let at = 0; at + ENTRY_BYTES <= bytes.length; at += ENTRY_BYTES) {
    const magic = view.getUint16(at, true);
    if (magic === MD5_MAGIC) continue;          // checksum entry, not a partition
    if (magic !== ENTRY_MAGIC) break;           // 0xFF padding: the table is done
    const offset = view.getUint32(at + 4, true);
    const size = view.getUint32(at + 8, true);
    let name = '';
    for (let i = 0; i < 16; i += 1) {
      const c = bytes[at + 12 + i];
      if (c === 0) break;
      name += String.fromCharCode(c);
    }
    out.push({ name, offset: '0x' + offset.toString(16), size: '0x' + size.toString(16) });
  }
  return out;
}

/**
 * Does this reader's table match the one Encre expects?
 *
 * Returns `{matches, expected, found, differences}`. `differences` is for the
 * person reading a refusal, so it says WHICH entry disagreed rather than only
 * that one did.
 */
export function compareLayout(found, expected) {
  const differences = [];
  if (found.length !== expected.length) {
    differences.push('the reader has ' + found.length + ' partitions, Encre expects '
                     + expected.length);
  }
  const count = Math.min(found.length, expected.length);
  for (let i = 0; i < count; i += 1) {
    const a = found[i];
    const b = expected[i];
    if (a.name !== b.name) {
      differences.push('partition ' + (i + 1) + ' is "' + a.name + '", expected "' + b.name + '"');
    } else if (a.offset !== b.offset || a.size !== b.size) {
      differences.push(a.name + ' is at ' + a.offset + ' size ' + a.size
                       + ', expected ' + b.offset + ' size ' + b.size);
    }
  }
  return { matches: differences.length === 0, found, expected, differences };
}

/**
 * Ask the browser for a reader.
 *
 * Filtered to Espressif's native USB Serial/JTAG identity, which both models
 * present, so the picker offers the reader rather than every serial device on
 * the machine. The two constants come from esptool-js rather than being typed
 * in here.
 *
 * Throws if the person cancels the picker; that is not an error worth a state,
 * and the caller treats it as "nothing happened".
 */
export async function requestReader() {
  return navigator.serial.requestPort({
    filters: [{ usbVendorId: ESPRESSIF_VID, usbProductId: USB_JTAG_SERIAL_PID }],
  });
}

/**
 * Connect, identify, and read the partition table. Writes nothing.
 *
 * The caller owns disconnecting: `session.close()`.
 */
export async function identify(port, manifest, onStage = () => {}) {
  const transport = new Transport(port, false);
  const loader = new ESPLoader({ transport, baudrate: BAUD, romBaudrate: BAUD });

  onStage('connecting');
  // main() resets the chip into its ROM loader and reports what it found.
  const description = await loader.main();
  const chip = loader.chip && loader.chip.CHIP_NAME ? loader.chip.CHIP_NAME : description;

  if (!String(chip).toUpperCase().startsWith(EXPECTED_CHIP)) {
    // Refused BEFORE anything is read, and long before anything could be
    // written. The wording matters: somebody who picked the wrong device needs
    // to know their device is untouched, not merely that the page gave up.
    await transport.disconnect().catch(() => {});
    const error = new Error(
      'That device is an ' + chip + '. An Xteink X3 or X4 is an ' + EXPECTED_CHIP
      + '. Nothing was read from it and nothing was written to it.');
    error.kind = 'wrong-chip';
    throw error;
  }

  onStage('reading-table');
  const layout = manifest.layout;
  const table = await loader.readFlash(Number(layout.tableOffset), Number(layout.tableSize));
  const found = parsePartitionTable(table);
  const comparison = compareLayout(found, layout.partitions);

  return {
    chip,
    description,
    // esptool-js's own SPI-flash-id table, rather than a copy of it here. It
    // answers undefined when it cannot map the id, which the page shows as
    // unknown rather than inventing a number.
    flashSize: await loader.detectFlashSize().catch(() => undefined),
    layout: comparison,
    // Where an install would go, if one were built yet. Straight from the
    // manifest, which takes it from partitions.csv.
    install: layout.install,
    async close() {
      await transport.disconnect().catch(() => {});
    },
  };
}
