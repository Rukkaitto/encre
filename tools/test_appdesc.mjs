// Reading what a firmware says about itself, against a real one.
//
//     node tools/test_appdesc.mjs
//
// FIXTURE: the first 144 bytes of `esp_app_desc_t` from app0 of
// `~/encre-device-backup/flash-full-16MB-20260820-1433.bin`, the dev X3's flash
// as it shipped. That is CrossInk, written by Espressif's own build, so the
// field offsets are checked against a producer rather than against this
// repository's idea of them.
import { parseAppDescription } from '../web/reader.js';

const CROSSINK_B64 = 'MlTNqwAAAAAAAAAAAAAAAHYxLjUuMC0zLWdjYWI0ZjI0OS1kaXJ0eQAAAAAAAAAAY3Jvc3NpbmstdWktdGhlbWUtZGVzaWduLWM2OTY3MwAyMzo0MjoyOQAAAAAAAAAAQXVnIDE5IDIwMjYAAAAAADUuNS4yLjI2MDIwNgAAAAAAAAAAAAAAAAAAAAAAAAAA';

let failures = 0;
const check = (ok, what) => {
  console.log('  ' + (ok ? 'ok  ' : 'FAIL') + ' ' + what);
  if (!ok) failures += 1;
};

console.log('a real stock reader describes itself');
const d = parseAppDescription(new Uint8Array(Buffer.from(CROSSINK_B64, 'base64')));
check(d !== null, 'the descriptor is recognised by its magic');
check(d.projectName === 'crossink-ui-theme-design-c69673',
      'project_name reads back exactly, so the 48..80 window is right');
check(d.version === 'v1.5.0-3-gcab4f249-dirty',
      'version reads back exactly, so 16..48 is right');
check(d.date === 'Aug 19 2026' && d.time === '23:42:29',
      'date and time land in their own fields rather than bleeding');
check(d.idfVersion === '5.5.2.260206', 'and the IDF version behind them');

console.log('\nand it refuses anything that is not one');
check(parseAppDescription(new Uint8Array(256).fill(0xff)) === null,
      'erased flash is not a descriptor');
check(parseAppDescription(new Uint8Array(256)) === null,
      'a zeroed region is not one either');
check(parseAppDescription(new Uint8Array(8)) === null,
      'a read too short to contain one is refused rather than parsed');

// A wrong magic must not be read. Everything after it could be anything.
const wrongMagic = new Uint8Array(Buffer.from(CROSSINK_B64, 'base64'));
wrongMagic[0] ^= 0xff;
check(parseAppDescription(wrongMagic) === null, 'a corrupted magic is refused');

// Binary in a string field means the offsets are wrong or the region is junk,
// and showing it would put control characters on screen.
const junkName = new Uint8Array(Buffer.from(CROSSINK_B64, 'base64'));
junkName[50] = 0x01;
check(parseAppDescription(junkName) === null,
      'non-printable bytes in project_name are refused, not rendered');

console.log();
if (failures) { console.log(failures + ' failure(s)'); process.exit(1); }
console.log('all checks passed');
