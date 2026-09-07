---
name: design-change
description: Use when changing anything about how Encre's UI looks — type sizes, spacing, icons, colours, layout — or when the firmware does not match its design board. Enforces the design-first order, covers propagating a change across all 48 boards, regenerating assets, and republishing the canvas.
---

# Changing the design

## The rule

**The design HTML changes first, then the implementation.** Always, including
when the design itself is the thing that is wrong: fix the board, then follow it
in code.

Why this is not bureaucracy: `make compare` renders each board beside the
firmware's output and is the only check that they agree. Changing code alone
silently invalidates that comparison and the goldens stop meaning anything. This
has already caught a real failure — a generator holding *copies* of the design's
SVG paths regenerated to nothing when the boards were fixed.

## Which boards to touch

`design/*.dc.html`, 48 boards, in three groups:

- **Device screens** (root `480px × 800px`) — the V1 screens, the flow and state
  boards, and the V2 Instapaper set. A change to the look goes to **all** of
  these, or screens diverge.
- **`Direction*` / `Variant*`** — the historical record of a settled design
  decision. **Leave them alone.** Editing them falsifies the record.
- **`WebUpload` / `WebSetup`** — 960×620 *browser* pages served by the device,
  not panel screens. Normal web type sizes are correct there; the panel ramp is
  not.

## Making the change

Boards declare the type ramp once in their `<helmet>` as CSS variables carrying
both the pt intent and the device-pixel result. Change the variable, not the 60
call sites.

A scripted sweep across boards is the right tool, but **render and inspect
afterwards** — doubling the type once broke wrapping, collisions and clipping on
a third of the boards. For each board you touch, check:

- text wrapping that was not intended, especially a single word breaking mid-word
- content colliding with other content, or escaping its container
- clipping at the bottom edge, or crossing the left/right margin
- lists that lost a row

Fix in this order: tighten spacing, shrink a fixed graphic, shorten a label, drop
a genuinely redundant element, and only as a last resort step a role down one
ramp size. If a board cannot fit without a real design decision — fewer rows
versus smaller type — **stop and ask**; that is not yours to settle.

Render at **both** geometries. `tools/compare-design.py` overrides the root frame
at render time so one board serves both, and the X4 (480 wide) fails first.

## Then the implementation

```
make icons      # if any SVG or icon size changed — reads the boards directly
make fonts      # if the ramp changed
make test       # goldens will fail; inspect the candidates, then bless
make compare    # confirm board and firmware agree again
```

A change to a shared primitive (`components.cpp`, `text.cpp`, `dither.cpp`)
rather than a screen is almost always the right implementation. See the
`implement-screen` skill for the fidelity checklist.

## Republish the canvas

`design/ereader-v1-ui.html` is a **generated file** — the compiled canvas editor
plus every `design/*.dc.html` and `design/canvas.json` seeded into it. Never
hand-edit it. Reseed:

```bash
make canvas          # reseed from every board on disk
make canvas-check    # is the committed canvas what the boards say? names what drifted
```

Then publish with the `Artifact` tool passing `url:
https://claude.ai/code/artifact/49eef3a1-97f8-4f95-b290-75e4021df141` and
`contract: "0.1.31"` — that URL is the canvas; publishing without it creates a
stray duplicate.

**A new board needs an entry in `design/canvas.json` as well as the file.** Being
in the canvas's file record is *not* being on the canvas: with no `artboards`
entry the editor loads a board and never shows it, which is invisible in review.
`make canvas` refuses rather than guessing — which page a board belongs on is a
design decision — and prints the next free slot in the layout's 580/900 grid.
Every state board goes on `page-4`.

**This invocation used to name `<design skill dir>/seed-canvas.mjs`, and that file
did not exist.** It lived here untracked, so it was absent from every fresh clone
and worktree, and the canvas could only be hand-edited — which is how it went six
boards stale with nothing saying so (issue #60). The generator is
`tools/design-canvas/`, tracked, with the rest of this project's generators; the
`make` targets above are the supported interface, and `make compare
COMPARE_ARGS=--require-canvas-current` (which CI passes) is what makes a stale
canvas loud.

## Showing the user

They compare personally and have asked for this specifically — do not rely on
your own visual judgement alone:

```bash
make sim && python3 tools/compare-design.py --only <id> --export build/overlay
```

Send the contact sheet for a quick read, and the bare `--export` panels when
they want to overlay in a design tool. Each pair is exact panel size, so they
stack with no offset.
