# The web flasher

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

`https://rukkaitto.github.io/encre/` installs Encre from a browser over USB, the
way CrossPoint's own page does, so somebody with a reader and Chrome does not
need `pip install esptool`. `web/` is the site, `design/WebFlash*.dc.html` and
`design/WebRecovery.dc.html` are its boards, `.github/workflows/pages.yml`
publishes it, and `tools/webmanifest.py` generates what it knows about the
reader. Recovery lives at `/recovery`.

## Four facts the design rests on, and where each came from

**THE STOCK PARTITION TABLE IS BYTE-FOR-BYTE `partitions.csv`, AND THAT WAS
MEASURED WITHOUT A DEVICE.** `~/encre-device-backup/flash-full-16MB-*.bin` is
the dev X3's complete flash as it shipped, taken before Encre was ever written
to it, with a `.sha256` beside it that still verifies. The table is at offset
`0x8000` of that file. All six partitions agree — name, offset and size — which
turns `partitions.csv`'s own claim that the layout was "adopted deliberately
rather than invented" from CrossInk into a measurement. `app0` holds an ESP
image and **`app1` is fully erased**, zero non-`0xFF` bytes, so the spare slot on
a stock reader is genuinely spare. Before booking hardware time for anything
about partitions, offsets or what the stock firmware occupies, look in that file
first.

**GITHUB RELEASE ASSETS CANNOT BE FETCHED FROM A BROWSER.** Verified with curl
against a live asset: `github.com/.../releases/download/...` 302s to
`release-assets.githubusercontent.com` and **neither response carries
`Access-Control-Allow-Origin`**; the preflight 404s. So the binaries are
downloaded by the Pages **runner**, which is not a browser and has no such rule,
and served same-origin. That is the whole reason `pages.yml` exists as one
workflow rather than a step inside `release.yml` — and the other reason is that
Pages has a **single deployment**, so a release-triggered deploy carrying
binaries and a push-to-main deploy without them would overwrite each other.

**THE MOBILE REFUSAL IS COMPLETE BY CONSTRUCTION, NOT BY LUCK.** The only mobile
browser with Web Serial is Chrome on Android 148+; every iOS browser is WebKit
and Firefox Android has none. So `navigator.serial` alone refuses every mobile
browser but one — and that one is Chromium, which is exactly where
`navigator.userAgentData.mobile` exists to catch it. The two checks have no gap,
and no user-agent string is sniffed. **If Safari or Firefox ever ship Web Serial
on a phone, this stops being true**, and `web/encre.js` says so at the site.

**INSTALLS PIN TO `app1`, AND THE ALTERNATING SLOT WAS A BUG.** "Whichever slot
the reader is not using" is true of the first install and false of the second:
stock in `app0`, Encre to `app1`, then an UPDATE back to `app0` — writing over
the firmware the reader came with, silently, on a routine update, after the page
had twice told its owner that firmware was safe. Pinning costs nothing, because
there is no over-the-air update path here that needs a real A/B pair. The slot
comes from `web/manifest.json`, which takes it from `partitions.csv`, so the one
number that decides whether a reader keeps its original firmware is never typed
into JavaScript.

## What is on glass

**2026-09-23, X3.** Plugged in, connected from the live page, layout reported as
expected. That confirms, in one press, the things nothing on a desktop could:

- the reader appears in the Web Serial picker over its **native USB**, with no
  UART bridge in the path;
- `ESPLoader.main()` **resets a C3 into ROM download mode over that native
  USB** — the assumption the whole page rests on, and the one the research
  explicitly could not confirm either way;
- `romBaudrate` equal to `baudrate` works, so **the port is never reopened** and
  the question of whether a Web Serial handle survives that reopen does not
  arise;
- `readFlash` at `0x8000` returns a real table, and it is Encre's.

**WHAT THAT DOES NOT REACH.** Nothing about the **X4**, whose table has never
been read by anybody — the mismatch path exists for it and for readers already
carrying CrossPoint or the KO fork, both of which CrossPoint's own flasher names
as distinct layouts. Nothing about the **write** path. And nothing about whether
a reader is left **stuck in download mode** afterwards: esptool-js has no
`watchdog-reset`, esptool's own docs say RTS on USB-Serial/JTAG is a core reset
that does not re-sample the boot straps, and CrossPoint's answer is to tell the
user to unplug and replug. Ours says the same thing unconditionally rather than
claiming a reboot the page cannot observe.

## Two costs worth knowing before touching Recovery

**A FULL 16 MB READ TAKES ABOUT 25 MINUTES ON A C3.** esptool issue #936's own
re-measurement puts the C3 at 11,512 B/s against the C6's 311,705 — **27x
slower** — and the fix (esptool-legacy-flasher-stub PR #35) is still unmerged.
CrossPoint's shipped UI says "around 25 minutes" for the same operation on the
same chip, which is the independent corroboration. That figure is why the backup
is on Recovery rather than in front of the install: a 25-minute wall in front of
a one-click page is not a one-click page. It stays mandatory only on the
destructive path, where it is the only way back.

**`readFlash` IS UNRELIABLE ON LARGE READS** (esptool-js #218, open): "something
with the serial protocol is getting out of sync". CrossPoint works around it
with 1 MB chunks, a per-chunk MD5 check and three retries with
`transport.flushInput()` between. Copy that shape rather than rediscovering it.

## esptool-js is vendored, and the claim is checked

`web/vendor/esptool-js/bundle.js` is v0.7.0's rolled-up browser module, Apache-2.0,
fetched from npm with the tarball's own shasum reproduced. **Not a CDN**: this
file writes firmware to somebody's reader, so a CDN in that path means whoever
controls it controls what gets written, on the day they are compromised, while
the page keeps looking exactly right.

`tools/test_vendor.py` hashes the bytes below the vendoring block against the
digest recorded beside the file **and** the one in the file's own header, so the
two cannot drift. That exists because `third_party/stb_truetype.h` carries the
same "not one byte below this block is changed" sentence and CLAUDE.md records
its claim having become false. A provenance comment is worth whatever verifies
it.

## The tests, and the one that matters most

`make test-tools` runs them; it grew a `.mjs` loop for the two node ones, and a
missing `node` is an error rather than a skip.

- **`tools/test_parttable.mjs`** embeds the first 224 bytes of `0x8000` from the
  stock image and asserts the parser turns them into a layout matching
  `web/manifest.json`. **A parser proved only against a table this repository
  generated would be proving that two of its own ideas agree**; this proves it
  against Espressif's encoder and a real device.
- **`tools/test_webhooks.mjs`** checks that the markup and `encre.js` agree about
  every `data-` hook. They find each other by attribute, so a typo is silent:
  `querySelector` returns null, the guard skips it, and a state renders with a
  blank field. Its first version passed a mutation it should have failed —
  `setFact()` builds its selector by concatenation, and dynamic selectors were
  filtered out, leaving the commonest hooks unchecked.
- **`tools/test_webmanifest.py`** drives throwaway trees. The `app1` guard is
  proved by mutation: teaching the generator to pick the first app partition
  instead of refusing fails four checks, one of them being that install then
  names `app0` — which is the defect, not the test.

## What the page must never claim

The chip id reads `ESP32-C3` on **both** models, so the page can refuse
something that is not an Xteink and cannot tell an X3 from an X4. Screen
rotation has only ever been verified on the X3, and the install screen says so
once, beside the button. `make compare` does not measure any of these boards —
`compare-design.py` excludes browser pages by name, because there is no
simulator to render one against — so **the boards here are checked by eye and by
measuring renders, not by the sheet**.
