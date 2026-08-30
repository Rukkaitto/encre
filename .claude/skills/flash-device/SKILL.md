---
name: flash-device
description: Use when putting an Encre build on the physical Xteink reader, or when the device shows the wrong thing, seems frozen, appears not to have updated, or stops responding to buttons. Covers the flash handoff, reading the staged serial log, and diagnosing panel-vs-firmware faults.
---

# Flashing and diagnosing the device

## The two facts that cause most confusion

**You cannot flash it yourself.** The permission classifier blocks the upload
command from an agent. Give the user the command and wait. Do not try to route
around it.

**E-ink holds its last image with no power.** A screen showing something is not
evidence the firmware ran. This has already disguised a crash loop *and* a
bootloader hang as "nothing changed" — twice. **Read the serial log before
drawing any conclusion from the panel.**

## Flashing

Give the user exactly this:

```bash
cd ~/dev/encre && ~/.platformio/penv/bin/pio run -e xteink -t upload --upload-port /dev/cu.usbmodem1101
```

Check the port first if anything is odd — do not use `ls` output in a variable,
it carries ANSI colour codes that break the path:

```bash
set -- /dev/cu.usbmodem*; echo "$1"
```

Before asking for a flash, say what you want them to look at. "Does it work?" is
a wasted round trip; name the two or three things only the panel can answer.

## Reading the log

```bash
cd ~/dev/encre && ~/.platformio/penv/bin/python tools/serial-log.py --seconds 20 --no-reset
```

The firmware announces each stage and then repeats the last one reached from
`loop()`, so a hang is locatable whenever you attach. A healthy boot:

```
[stage] serial-up
[detect] i2c verdict=X3Confirmed ... [detect] active controller=6
[stage] panel-profile-x3 → display-begin-returned → frames-allocated → fonts-ok
        8279_PON (40 ms) → 8279_DRF (693 ms) → 8279_gray_DRF (366 ms)
[stage] gray-base-displayed → gray-preconditioned → gray-planes-written
[stage] gray-displayed → refresh-complete
[alive] 1 last-stage=refresh-complete heap=...
```

`[alive]` with a stable heap is the success signal. Note that opening the port
resets the chip, so each capture is a fresh boot even with `--no-reset`.

## Diagnosing by symptom

| Symptom | What it means | Where to look |
|---|---|---|
| Log stops at a stage, no `[alive]` | `setup()` never returned — hung in the call after that stage | The named SDK call |
| A `Wait complete` reporting exactly `30000 ms` | The panel never signalled completion. Almost always the **wrong controller driver** | Check `[detect] active controller`; 6 = UC8279, 2 = UC8253 |
| `abort() was called` then `Rebooting...`, looping | Allocation failure. The firmware is `-fno-exceptions`, so a `std::vector` that cannot allocate aborts | Compare frame size against `largest block` in the log — **total** free heap is not the constraint, fragmentation is |
| No serial port at all | Deep sleep powered down USB, or the chip is parked in the bootloader | Ask the user to unplug USB, hold power ~10s, then boot |
| Screen unchanged, log shows `refresh-complete` | The paint ran. Compare against the golden — this is a fidelity question, not a failure | `make compare` |
| Buttons do nothing | **Never expected** — input dispatch shipped in 2B. Either one screen promises an action it does not bind, or the loop lost its `gApp->dispatch(ev)` (which builds clean and passes every desktop test, because `shell/` has no harness) | `[i] ... paint=none` marks a press that changed nothing — that is the line worth having |

## Never

- Conclude anything about the firmware from the panel alone.
- Run more than one esptool command before flashing. Each drives the reset line
  and can leave the chip parked in the bootloader, which then looks like a dead
  device with a perfectly normal-looking screen.
- Re-flash repeatedly to "see if it works". Read the log; it says where it got to.

## Recovery

`~/encre-device-backup/restore.sh` puts the device back to CrossInk — full 16MB
image including NVS and SPIFFS, checksum-verified. Download mode is available
(no secure boot, no flash encryption), so the device is always recoverable.
