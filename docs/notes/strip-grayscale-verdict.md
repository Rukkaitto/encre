# Strip grayscale for page turns — the verdict

Issue #17, `roadmap:822`. Investigated 2026-08-28 against `freeink-sdk` at
`3d90c8a`. **No.** Not for the reader, not for chrome, and not for the reason
CLAUDE.md had recorded.

The ticket was right to re-ask: the recorded rejection was made about chrome and
its geometry argument does not transfer cleanly. But the re-derivation closes the
question harder than the original did, because the original was arguing against
the wrong API.

---

## 1. The claim being re-derived

CLAUDE.md, "Rendering model":

> **Windowed grayscale is not the escape hatch**: rotation is CCW, so a portrait
> row band becomes a full-height landscape column band and every gate line is
> driven anyway.

Two assertions in there:

- **(a)** a logical row band maps to a full-height panel column band;
- **(b)** every gate line is driven anyway, so windowing saves nothing.

**(a) survives and is verified from source.** **(b) survives as physics but was
attached to the wrong mechanism** — it argues about a windowed *refresh*, and the
strip API does not do one. The real blocker is simpler and is stated in the
driver's own comment.

## 2. What `writeGrayscalePlaneStrip` actually windows

`Uc8279Driver::writeGrayscalePlaneStrip` (`Uc8279Driver.cpp:259–288`) sets a PTL
partial window, pushes `numRows` of plane bytes into DTM1 or DTM2, and issues
`PARTIAL_OUT`. **It triggers no refresh.** The refresh is `displayGray`
(`Uc8279Driver.cpp:290–318`), which opens with `grayWindowIn(bus)` — and the
comment above that call says it outright:

> The refresh MUST run in the partial window (like the plane writes); also
> **resets PTL to full after any per-strip `writeGrayscalePlaneStrip` windows.**
> — `Uc8279Driver.cpp:300–302`

`grayWindowIn` (`Uc8279Driver.cpp:70–77`) writes a hardcoded full-panel window,
`kFullWindow = {0,0, 0x03,0x17, 0,0, 0x02,0x0F, 0x01}` — X 0..791, Y 0..527.

**So the strip API buys transfer time and can never buy waveform time.** The
waveform is 889 ms of the refinement's 1408 ms. That is the ceiling gone before
any geometry is considered.

`grayWindowIn` is private and `displayGray` is the only entry to the XTF_AA bank
load and its refresh, so this cannot be worked around without editing the
submodule, which the project does not do.

**The boot line `strip=1` means the driver has a windowed plane write, not a
windowed refresh.** It comes from `display.supportsStripGrayscale()`
(`shell/src/main.cpp:3090`). `roadmap:822` read it as evidence the mechanism was
available; it is evidence that half of it is. (The SDK's own
`docs/xteink-x3-uc8279-support.md:51` still says `supportsStripGrayscale()` is
false for this panel, which is stale — `Uc8279Driver.h:58` returns true. Do not
use that doc as the authority.)

## 3. How much transfer is even reachable

`paintGray` (`shell/src/main.cpp:~2330–2410`) issues **six full-plane writes**,
counted from the driver source:

| step | writes |
|---|---|
| `displayGrayscaleBase` | `sendPlaneFlipped(DTM2, fb)`, then `sendPlaneFlipped(DTM1, fb)` |
| `preconditionGrayscale` | none (waveform only) |
| `copyGrayscaleLsbBuffers` | `sendPlaneFlipped(DTM1, lsb)` |
| `copyGrayscaleMsbBuffers` | `sendPlaneFlipped(DTM2, msb)` |
| `displayGrayBuffer` | none (waveform only) |
| `cleanupGrayscaleBuffers` | `sendPlaneFlipped(DTM2, bw)`, then `sendPlaneFlipped(DTM1, bw)` |

**Only two of the six are reachable by the strip API** — `GrayPlane` has exactly
`Lsb` and `Msb` (`PanelDriver.h:132`). The base's and the cleanup's DTM1/DTM2
pairs go through `EpdBus::sendPlaneFlipped` (`EpdBus.cpp:379`), which has no
windowed form.

### The transfer figure, cross-checked

- A plane is 792 × 528 / 8 = **52,272 bytes** *(derived)*.
- At the datasheet-maximum 20 MHz that is 52,272 × 8 / 20e6 = **20.9 ms** of pure
  shift time *(datasheet + derived)*; the measured `up=25 ms` per plane write
  *(measured)* is that plus per-row CS and command overhead.
- Six writes × 25 ms = **150 ms** *(derived)*.
- Independent check *(measured)*: the refinement is 1408 ms = 1041 panel + 367
  render, and the three waveforms are 367 + 366 + 156 = **889 ms**. 1041 − 889 =
  **152 ms** of non-waveform panel time. That lands on the 150 ms derived from
  counting six plane writes, which is what confirms the count.

So transfer is **10.8% of the refinement**, and strips reach **2 of 6** of it —
**50 ms**.

## 4. The geometry, re-derived for the reader

Verified from `core/src/framebuffer.cpp:108–113`, not from prose:

```
physX = y, physY = width_ - 1 - x;      // Rotation::Ccw
```

So on the X3's 528 × 792 portrait canvas over a 792 × 528 panel:

- **panel x** (source lines, 792) **= canvas y**
- **panel y** (gate lines, 528) **= canvas x**

`writeGrayscalePlaneStrip` windows `yStart`/`numRows` — **panel y, the gate axis,
which is the canvas x axis** — and hardcodes the source axis to full width
(`xEnd = _w - 1`, x start `0x00,0x00`, `Uc8279Driver.cpp:264` and the `win[9]`
literal).

**A strip is therefore a vertical slice of the portrait page, running its full
height.**

That is the ticket's insight, and it is correct as far as it goes: for *chrome* a
focus move is a logical **row** band — a small canvas-y range — which lands on the
source axis the strip API cannot window at all. Assertion (a) holds. For the
*reader* the changed region is a column, which lies on the axis the strip API
*can* window. **The geometry genuinely differs.**

**It differs in the wrong direction.** A reader page turn changes the full height
of the text column, and that column is **492 px wide on a 528 px canvas**
(CLAUDE.md, "The reader"; the X4's is 444 on 480). In gate terms the changed
range is 492 of 528 gate rows. The window is **93.2% of the panel**; the 36 px of
outer margin is all there is to exclude — and the footer's page counter and the
header band both sit inside that column, so nothing widens it back.

## 5. The arithmetic

| quantity | value | label |
|---|--:|---|
| refinement, end to end | 1408 ms | measured |
| — of which waveform (367+366+156) | 889 ms | measured |
| — of which render (4 passes) | 367 ms | measured |
| — of which plane transfer | 152 ms | measured (1041 − 889) |
| plane size | 52,272 B | derived |
| SPI shift at 20 MHz | 20.9 ms | datasheet + derived |
| per plane write, observed | 25 ms | measured |
| plane writes in `paintGray` | 6 | derived from source |
| writes reachable by the strip API | 2 | source (`GrayPlane` has Lsb/Msb only) |
| reachable transfer | 50 ms | derived |
| windowable fraction of the gate axis | 6.8% | derived (36/528) |
| **saving** | **3.4 ms** | **derived** |
| **as a share of the refinement** | **0.24%** | **derived** |
| waveform saving | **0 ms** | source (`grayWindowIn` resets PTL to full) |

**3.4 ms of 1408 ms**, in a pass that runs 5 s after the user stopped pressing
buttons.

## 6. The one thing that does window a refresh, and why it also fails

`Uc8279Driver::preconditionGrayscale(x, y, w, h)` (`Uc8279Driver.cpp:356–386`)
**is** a genuinely windowed refresh — it sets a PTL rect on both axes and calls
`triggerGrayRefresh` inside it. It is exposed to us
(`FreeInkDisplay::preconditionGrayscale(x,y,w,h)`, `FreeInkDisplay.h:112`) and
`paintGray` currently calls the full-frame overload. It drives the 366 ms settle
pass — a quarter of the refinement's waveform. This is the only real opening
found, and it closes on three counts:

1. **This is where CLAUDE.md's assertion (b) actually belongs.** Waveform time
   goes as the **gate** count: a refresh scans gates serially and each gate drives
   all source lines at once, so narrowing the source axis costs nothing back.
   Windowing canvas y (the source axis) saves no time; only windowing canvas x
   (gates) does. *(Standard UC-series behaviour and what CLAUDE.md records; I do
   not have the UC8279 datasheet, so label this **estimated**.)*
2. **The gate range is 93.2% anyway**, by §4. Best case 6.8% of 366 ms = **25 ms**
   — and 6.8% of the whole 889 ms if every pass could be windowed, which is
   **60 ms, 4.3% of the refinement**. Still bought in a window nobody is watching.
3. **The settle pass is the one step in `paintGray` proved dangerous to touch.**
   Its comment records that removing it looked like a safe de-duplication, saved
   366 ms, and made the panel accumulate ink on device — "Identical command
   sequences are not identical operations." Narrowing its window is a change to
   the same particle-conditioning behaviour, with device-only evidence, for 25 ms.

## 7. Which cost was being attacked, stated plainly

Strips cannot touch the page turn. A page turn is already the **dithered
one-pass** path — ~570 ms, ~478 of it waveform — and `Plane::BwDithered` writes no
LSB/MSB planes at all, so there is no strip write on that path to window.

Strips reach only the **refinement**, and only its transfer component. The
refinement is already the deliberately-expensive half of a trade CLAUDE.md
defends on other grounds: 1408 ms against the ~1056 ms single grayscale paint it
replaced, accepted because what the reader waits for is text.

The refinement's real cost to the user is not its duration but that it **cannot be
interrupted** — a press landing inside it waits it out. Shortening it by 3.4 ms
(0.24%) does not change that. If the refinement is ever worth attacking, the
lever is making it *interruptible*, the way `completeIndex` was given a stop
predicate, not making it 0.24% shorter.

## 8. Memory

Neutral, and not the blocker. Strips need no second frame — they read bands out of
the same 52,272-byte plane the full write reads. They also **save** nothing:
`core/`'s `Framebuffer` has no windowed render, so all three planes are still
rendered full-frame before any of them is transmitted. The 45,840-byte floor
(page on glass) and 42,152-byte floor (book opened through the Library) are
untouched either way.

## 9. What would have to be true for this to come back

All three, together:

1. `Uc8279Driver::displayGray` would have to stop calling `grayWindowIn` and honour
   a caller-supplied window — an SDK change, in a submodule this project does not
   edit.
2. The reader's changed region would have to shrink on the **canvas x** axis
   specifically. It will not: a reflowed page changes its whole column. A design
   where only a narrow vertical band changed would qualify, and no such screen
   exists or is planned.
3. The refinement would have to be worth optimising at all, rather than made
   interruptible.

Absent all three, the answer stays no.

---

**Verified:** `make test` 9/9 green, tree otherwise untouched. Base `1b7dd48`,
submodule `3d90c8a`. No source file changed by this investigation.
