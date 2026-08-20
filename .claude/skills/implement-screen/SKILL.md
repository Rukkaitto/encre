---
name: implement-screen
description: Use when implementing an Encre screen from its design board, or when adding a view-model, theme render method, simulator subcommand or golden for a screen. Covers the design-first order, the view-model/theme split, goldens at both panel geometries, and the fidelity checks that catch the defects this project keeps hitting.
---

# Implementing a screen from its design board

Encre has ~28 screens designed and one implemented. This is the loop for the
rest. Read `CLAUDE.md` first if you have not — the type ramp, the three-pass
rendering model and the invariants are assumed here.

## Order of work

**1. Read the board.** `design/<Name>.dc.html` is the authority. Note the type
roles it uses (`--t-meta`, `--t-label`, …, each carrying a weight — a run with
no `font-weight` is CSS default **400**, which is a different role from 500),
its structural padding, and its icons.

If the board is wrong, **fix the board first**, then follow it. Never fix only
the implementation.

**2. Add the view-model.** In `core/include/reader/viewmodel.h`: semantic
content plus interaction state, **no geometry and no styling**. If you are
tempted to put a pixel value in it, that belongs in the theme.

**3. Add the theme method.** `Theme` gets a virtual, `QuietTheme` implements it.
Signature mirrors the others: `(Framebuffer&, const FontSet&, const XViewModel&, Plane)`.
Build it out of the existing primitives — `drawHeaderBand`, `drawRow`,
`drawHintBar`, `drawIcon`, `ditherRect`, `drawText`, `baselineIn`, `iconTopIn`.

**If a primitive cannot express what the board does, extend the primitive** —
do not special-case the screen. Every fidelity defect in this project so far
belonged in the shared layer, and a screen-local workaround means the next
screen inherits the bug.

**4. Add the simulator subcommand.** `sim/main.cpp` dispatches on the screen id.
Use the same id `tools/compare-design.py` expects (see its `V1_SCREENS` /
`FLOW_SCREENS` tables) or the comparison will report the screen as not
implemented.

**5. Golden at both geometries.** X4 is 480×800, X3 is 528×792. Every screen
needs both — a layout that fits one can clip the other, and the X3 is the dev
device. Use the existing golden test as the pattern; render all three planes and
compare the composed 4-level image.

**6. Compare and fix.** `make compare COMPARE_ARGS="--only <id>"`, then
`--export build/overlay` for bare panel PNGs the user can overlay.

## The checks that catch this project's recurring defects

Run these against the candidate before blessing anything. Each one is a bug that
has actually shipped here:

- **Text not optically centred in its box.** Use `baselineIn(font, boxTop, boxH)`.
  Never `ascent/2` — ascent includes accent space and sits text low.
- **Icon not aligned with its label.** Use `iconTopIn(boxTop, boxH, iconH)`.
  Works for any icon height beside any type size.
- **A pinned height the board computes.** If the board declares padding and a
  border rather than a height, derive it. Content-box 80 + `border-top: 1px` is
  **81**, and pinning 80 drifts a pixel per row down a list.
- **Wrong role, or the right size at the wrong weight.** Check the board's
  `font-weight` per run, not just its size variable.
- **Tracking.** The board's `letter-spacing` is in em; carry it as a fraction
  via `trackingEm(font, em1000)`, never a pre-rounded pixel count.
- **Furniture carrying grey.** Rules, fills, dither and icons must be pure black
  or white in the composed image. Grey on a rule is a plane bug, not
  anti-aliasing.
- **Overflow on the narrower canvas.** The X4 is 48px narrower than the X3. Long
  strings, right-aligned values and multi-slot bars fail there first.

## Blessing a golden

Render the candidate, **open it with the Read tool**, walk the board item by
item, and only then copy it over the golden. Describe honestly what you see,
including anything that looks wrong even if you bless it — a silent bless is how
a wrong baseline gets locked in.

**Never re-bless to make a red test green.** `text_sample.png` is Literata body
text; if it changes and you did not intend to touch body rendering, stop.

## Verify before you are done

```
make test                                   # all cases, both goldens
make sim
make firmware                               # the ESP32 toolchain is stricter
make compare COMPARE_ARGS="--only <id>"
```

Then hand the flash to the user (see the `flash-device` skill) and name the two
or three things only the panel can answer.
