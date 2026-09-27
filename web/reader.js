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
import { parseOtadata, activeSlot, selectSlot } from './otadata.js';

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

/**
 * ESP32-C3 RTC watchdog registers, and the only way out of download mode.
 *
 * CONNECTING RESETS THE CHIP INTO ITS ROM LOADER, which is what connecting
 * means: the firmware stops, and on e-ink the panel keeps whatever was last
 * painted. The reader looks frozen and its buttons do nothing because nothing
 * is listening to them. That is expected -- what was NOT acceptable is leaving
 * it that way.
 *
 * `hard_reset` does not help. esptool's own documentation: on USB-Serial/JTAG
 * the peripheral "interprets the RTS serial control signal as a core reset",
 * and that reset "does not re-sample the boot strapping pins", so a chip that
 * entered download mode stays there. A WATCHDOG reset does re-sample them, which
 * is why esptool has `--after watchdog-reset` at all.
 *
 * esptool-js exposes no such mode, but it does expose `writeReg`, so this is
 * esptool's own `ESP32C3ROM.watchdog_reset()` written out: unlock the watchdog,
 * set a 2000-tick timeout, enable it, lock again, and let it fire. Addresses and
 * values are from esptool/targets/esp32c3.py rather than from memory.
 */
const RTC_CNTL_WDTCONFIG0_REG = 0x60008090;
const RTC_CNTL_WDTCONFIG1_REG = 0x60008094;
const RTC_CNTL_WDTWPROTECT_REG = 0x600080a8;
const RTC_CNTL_WDT_WKEY = 0x50d83aa1;

/**
 * Reboot the reader out of download mode so it runs its firmware again.
 *
 * EVERY FAILURE HERE IS SWALLOWED, deliberately. The chip reboots part way
 * through, so the transport can and does throw as the device goes away -- and a
 * reset that worked is indistinguishable from one that failed at that layer.
 * The page tells the user what to do if the reader stays put, which is the only
 * honest position: this improves the odds, it does not guarantee them.
 */
export async function rebootOutOfDownloadMode(loader) {
  try {
    await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY);
    await loader.writeReg(RTC_CNTL_WDTCONFIG1_REG, 2000);
    await loader.writeReg(RTC_CNTL_WDTCONFIG0_REG,
                          ((1 << 31) | (5 << 28) | (1 << 8) | 2) >>> 0);
    await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, 0);
    await new Promise((resolve) => setTimeout(resolve, 500));
    return true;
  } catch (error) {
    return false;
  }
}

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
    loader,
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
    async close({ reboot = true } = {}) {
      // The reader goes back to running its firmware. Without this a check --
      // which writes nothing at all -- still left the device stopped, with its
      // last screen frozen and its buttons dead, until somebody found the reset
      // button. Found on glass on the first real try.
      if (reboot) await rebootOutOfDownloadMode(loader);
      await transport.disconnect().catch(() => {});
    },
  };
}


/** SHA-256 of a buffer, as lowercase hex. */
async function sha256Hex(buffer) {
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/**
 * Write Encre into the install slot and point the bootloader at it.
 *
 * ORDER MATTERS AND IT IS THE SAFE ONE. The image goes in first and otadata
 * second, so a write that is interrupted leaves a half-written slot the reader
 * is not being told to boot from -- it still starts the firmware it already had.
 * Flipping otadata first would mean an interrupted flash produces a reader
 * pointed at an incomplete image.
 *
 * NOTHING IS WRITTEN TO THE SLOT THE READER CAME WITH. The target comes from the
 * manifest, which takes it from partitions.csv, and `web/otadata.js` writes its
 * entry into the otadata sector that is NOT currently in charge.
 */
export async function install(session, manifest, onProgress = () => {}) {
  const { loader } = session;
  const firmware = manifest.firmware;
  const layout = manifest.layout;
  if (!firmware) throw new Error('This page has no firmware attached to install.');

  onProgress({ stage: 'fetching', written: 0, total: firmware.app.bytes });
  const response = await fetch(firmware.app.path, { cache: 'no-cache' });
  if (!response.ok) {
    throw new Error('The firmware did not download (' + response.status + ').');
  }
  const buffer = await response.arrayBuffer();

  // THE DIGEST IS CHECKED BEFORE A BYTE REACHES THE READER. The manifest records
  // what the release workflow measured, so a download that is truncated, cached
  // wrong or interfered with is refused here rather than written and discovered
  // afterwards -- and "afterwards" on a flash write is a reader that will not
  // boot.
  const digest = await sha256Hex(buffer);
  if (digest !== firmware.app.sha256) {
    throw new Error('The firmware that downloaded is not the one this page expects. '
                    + 'Nothing was written to the reader.');
  }
  const image = new Uint8Array(buffer);

  onProgress({ stage: 'writing', written: 0, total: image.length });
  await loader.writeFlash({
    fileArray: [{ data: image, address: Number(layout.install.offset) }],
    // "keep" leaves the image's own header alone. Rewriting mode, frequency or
    // size is for an image written at the bootloader offset; this one is an app
    // in a slot, and the values it was built with are the right ones.
    flashMode: 'keep',
    flashFreq: 'keep',
    flashSize: 'keep',
    // Erase only what is written. eraseAll would take the other slot, the
    // settings in nvs and the reader's own firmware with it.
    eraseAll: false,
    compress: true,
    reportProgress: (_i, written, total) => onProgress({ stage: 'writing', written, total }),
  });

  onProgress({ stage: 'selecting', written: image.length, total: image.length });
  const otaPartition = layout.partitions.find((p) => p.name === 'otadata');
  if (!otaPartition) {
    throw new Error('This reader has no otadata partition, so nothing can choose '
                    + 'which firmware it starts. Encre was written but not selected.');
  }
  const otaOffset = Number(otaPartition.offset);
  const before = parseOtadata(await loader.readFlash(otaOffset, Number(otaPartition.size)));
  const target = layout.install.slot - 1;          // the manifest counts from one
  const pick = selectSlot(before, target);
  await loader.writeFlash({
    fileArray: [{ data: pick.bytes, address: otaOffset + pick.offsetInPartition }],
    flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
    eraseAll: false, compress: false,
  });

  // READ IT BACK. The write can report success and still leave the bootloader
  // reading the other entry -- a wrong sequence selects nothing new and looks
  // exactly like a flash that worked. Eight kilobytes is about a second even at
  // this chip's read speed, which is a cheap price for knowing.
  const after = parseOtadata(await loader.readFlash(otaOffset, Number(otaPartition.size)));
  if (activeSlot(after) !== target) {
    throw new Error('Encre was written to slot ' + layout.install.slot
                    + ', but the reader is still set to start the other one. '
                    + 'Your original firmware is untouched.');
  }

  onProgress({ stage: 'done', written: image.length, total: image.length });
  // The watchdog reset, not `hard_reset`: on this chip RTS is a core reset that
  // does not re-sample the boot straps, so the reader would stay in download
  // mode looking frozen. The page still tells the user what to do if it does,
  // because this improves the odds rather than guaranteeing them.
  await rebootOutOfDownloadMode(loader);
  return { slot: layout.install.slot, seq: pick.seq, bytes: image.length };
}


/** The ESP image magic: the first byte of anything the bootloader can start. */
const IMAGE_MAGIC = 0xe9;

/**
 * What is on the reader right now, for Recovery's rail.
 *
 * Each app slot is probed with a ONE-BYTE read rather than measured. Nothing on
 * the device records how much of a 6.4 MB slot is used, and deriving it means
 * walking the image's segment headers -- about ten round trips at this chip's
 * ~356 ms packet latency. Whether a slot holds firmware is the question that
 * decides whether booting it is sensible, and one byte answers it.
 */
export async function readState(session, manifest) {
  const { loader } = session;
  const layout = manifest.layout;
  const ota = layout.partitions.find((p) => p.name === 'otadata');
  const apps = layout.partitions.filter((p) => p.name === 'app0' || p.name === 'app1');

  const slots = [];
  for (const partition of apps) {
    const head = await loader.readFlash(Number(partition.offset), 1);
    slots.push({ name: partition.name, hasFirmware: head[0] === IMAGE_MAGIC });
  }

  let starts = null;
  if (ota) {
    starts = activeSlot(parseOtadata(
      await loader.readFlash(Number(ota.offset), Number(ota.size))));
  }
  // A bootloader with no valid otadata entry starts the first app partition.
  return { slots, startsFrom: starts === null ? 0 : starts };
}

/**
 * Point the bootloader at a different slot. Erases nothing.
 *
 * This is the whole un-install story and it is 32 bytes: Encre stays in its
 * slot, whatever is in the other one stays there, and only the choice
 * between them changes. It is also the cheapest write this page makes, which is
 * why it is the one worth trying on a device first.
 */
export async function bootSlot(session, manifest, slot) {
  const { loader } = session;
  const layout = manifest.layout;
  const ota = layout.partitions.find((p) => p.name === 'otadata');
  if (!ota) {
    throw new Error('This reader has no otadata partition, so nothing chooses which '
                    + 'firmware it starts.');
  }
  const apps = layout.partitions.filter((p) => p.name === 'app0' || p.name === 'app1');
  const target = apps[slot];
  if (!target) throw new Error('This reader has no slot ' + (slot + 1) + '.');

  // REFUSE TO POINT AT AN EMPTY SLOT. Selecting a slot with no image is a reader
  // that starts nothing, and it would look exactly like a successful switch
  // until the next power-on.
  const head = await loader.readFlash(Number(target.offset), 1);
  if (head[0] !== IMAGE_MAGIC) {
    throw new Error('Slot ' + (slot + 1) + ' has no firmware in it, so the reader would '
                    + 'have nothing to start. Nothing was changed.');
  }

  const offset = Number(ota.offset);
  const before = parseOtadata(await loader.readFlash(offset, Number(ota.size)));
  if (activeSlot(before) === slot) {
    return { slot: slot + 1, seq: null, alreadySelected: true };
  }
  const pick = selectSlot(before, slot);
  await loader.writeFlash({
    fileArray: [{ data: pick.bytes, address: offset + pick.offsetInPartition }],
    flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
    eraseAll: false, compress: false,
  });

  // Read it back, for install()'s reason: a wrong sequence selects nothing new
  // and looks exactly like a write that worked.
  const after = parseOtadata(await loader.readFlash(offset, Number(ota.size)));
  if (activeSlot(after) !== slot) {
    throw new Error('The reader is still set to start the other slot. Nothing was erased.');
  }
  await rebootOutOfDownloadMode(loader);
  return { slot: slot + 1, seq: pick.seq, alreadySelected: false };
}
