# Overlays and lists

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

- **The App renders a STACK, not a screen.** `Screen::isOverlay()` marks a panel
  that leaves the screen beneath it visible under a veil; `App::render` walks down
  to the topmost non-overlay, renders that, then renders each overlay above it.
  **The shell must call `App::render`, never `top().render`** — that mistake paints
  an overlay as a panel floating on white, and *nothing on the desktop can catch
  it*: the simulator and all the goldens go through `App::render`, so they pass
  while the device is wrong. It has happened once.
- **Input and fidelity come from the top screen only.** An overlay whose parent
  still received events would move a focus the user cannot see.
- **A focus move inside an overlay repaints the OVERLAY ALONE**, over the frame
  the previous paint left — `App::renderTopOnly`, and `paintPlane` in
  `shell/src/main.cpp` is the one caller. Measured at 528×792 it takes an actions
  overlay repaint from 4.75 ms to 1.58 ms, of which the veil is most (below) and
  skipping the parent's text pass is the rest.
  - **The precondition is about the FRAME, not the stack**, and the frame is the
    one thing `App` cannot see — so `App` records what it painted and where, and
    `canRenderTopOnly` refuses unless the frame, plane, top screen and depth all
    match that record. A caller cannot be trusted with this check, because a
    caller is the only thing that could have clobbered the frame. **The clear
    belongs inside the full-paint branch**: clearing and then partially
    repainting is an overlay panel floating on paper.
  - **A push or a pop is never partial** (`transition()` is the signal) and
    neither is the first frame after boot, on two independent conditions. The
    push/pop rule is conservative on purpose: the pointer comparisons it would
    otherwise rest on are an ABA, since a popped screen's address can be reused
    by the next push.
  - **Grayscale never is.** That path renders three planes plus a rebase, and the
    frame between passes holds a different plane, so the precondition is false for
    every pass but the first. `Dithered` and `Mono` both qualify.
  - **`Screen::paintFootprint()` is the screen's own promise** that equal tokens
    mean the same pixels covered, so the new paint replaces the old one. Zero is
    "no promise" and is the default. `DeleteConfirm`'s is constant;
    **`ItemActions`' is not, and the reason is one pixel**: its panel's height is
    the sum of its rows, and the focused row loses its rule, so focusing the LAST
    row (whose rule is already gone) makes the panel a pixel taller and, being
    centred, a pixel higher. Moving the focus off it shrinks the panel and leaves
    the old top border standing — 226 pixels at y=212 on the X3, and the veil only
    takes 5 of every 9 of them out. So two of its four focus moves take the fast
    path and two do not. `test_partial_repaint.cpp` renders **every ordered pair**
    of both overlays' focus states through both paths and compares bytes.
- **There are THREE dither patterns for three jobs**, each from its own board
  declaration, and `dither.cpp` explains why they cannot be shared:
  `kClustered` black on a 4px grid for tints (`.dither-dots`), `kBayer` dispersed
  for glyph and icon edges, and
  `veilRect`'s clustered **white** on a **3px** grid for the overlay veil. A 4px
  veil is half as dense and reads as a smudge.
  - **`ditherRect`'s `Ink` PARAMETER HAS NO SCREEN CALLER ANY MORE, and this line
    used to name its one instance.** `.dither-dots-inv` was the FOCUSED Library
    row's placeholder cover — the tint reversed out of the black fill — and #95
    removed the placeholder from the rows. **The description is still accurate and
    `ditherRect` is now down to ONE caller rather than two**: this bullet said
    "both surviving callers pass black (Book details' cover slot, and
    `renderSleep`'s full-panel field)" and Book details' slot went with the rest of
    #95, so the surviving caller is the sleep field, and it passes black. It is
    kept as `ListRow::trackingEm1000` is kept: `test_dither.cpp` drives both inks
    across 30 rectangles × both rotations × all four levels, so this is **tested
    capability rather than working behaviour**, and `Bookmarks.dc.html` is a
    board that asks for a reversed tint again. Stated rather than assumed,
    because a producerless reader is the shape this file has been bitten by from
    two directions. **`core/include/reader/dither.h` said the same thing in the
    present tense and was corrected with this**, which is the half a CLAUDE.md-only
    fix leaves behind — a rule stated in two places is enforced in neither if only
    one is revised.
- **The veil was the most expensive thing on the screen, and it is now byte-wise.**
  A veil covers the WHOLE frame, and the per-pixel form cost four integer
  divisions and a bit-addressed read-modify-write per pixel: 2.33 ms at 528×792
  against `ditherRect`'s 1.12 ms and a full-frame `clear`'s 0.001 ms, so ~150 ms
  of every overlay repaint at this project's ~65× desktop-to-device ratio. It now
  ORs eight columns at a time into the physical store — 0.17 ms, 13.6× — which
  made it **the FIRST drawing routine in `core/` that knows `Rotation` exists**:
  under CCW a logical row is a physical *column*, so it walks logical **columns**
  instead, and the tile is symmetric under transposition, which is what lets the
  two cases just swap axes. A byte-wise path that assumed a logical row is a
  physical row would pass every desktop test and every golden and smear the veil
  diagonally on glass. `test_dither.cpp` keeps the per-pixel form as its reference
  and asserts byte-identity at both geometries, under both rotations, and for runs
  that start and end mid-byte — the panel widths are multiples of 8, so nothing on
  the device exercises the edge masks.
  - **THERE ARE FOUR OF THEM NOW, and this line said "the one" for three
    conversions after it stopped being true.** `Framebuffer::fillRect`, the glyph
    blit in `text.cpp` and `ditherRect` each took the same structure for the same
    reason, and each carried its own paragraph calling itself the first, second or
    only one. They share one hazard, and it is worth stating once: **under CCW the
    outer loop is the logical x, and getting it wrong is invisible to the whole
    desktop** — the simulator, every golden and every comparison sheet are
    `Rotation::None`. Only a byte-identity test run under both rotations, and
    proved by mutation, stands between that mistake and the panel.
  - `PhysRun`/`physRunFor` — clip a run to a first byte, a last byte and two edge
    masks — is `reader/physrun.h`, shared by `fillRect` and `ditherRect`. It was
    written twice before it was a header, which is this file's own second-copy
    rule arriving one copy late again. `veilRect` still computes its own inline,
    because its masks are interleaved with the per-byte phase advance the other
    two do not have.
- **`ScrollWindow` owns list movement** — a `Focus` (see Storage) plus
  first-visible, scrolling by a row rather than a page, and it CLAMPS, which is
  what lets a held button's 40-row step land on the last row instead of past it. `Theme::libraryVisibleRows` derives how many rows fit
  from the panel and the type; the shell must set it before the first Library
  paint or the list correctly renders empty.
- **A scrollable list shows its position as a RAIL** in a 14px gutter
  (`kListGutterW`), not as a number in the header band: the band's right slot
  already means "books, counting one level down" and a position means "rows", so
  putting both there produced `1–7 OF 12` — two units in one expression. A rail
  says *where* without claiming a count. `drawScrollRail`, outlined track with a
  solid proportional thumb.
  - **The gutter exists only when the rail does.** Reserving it on every list was
    tried, to spare a library crossing the visible-row count one reflow of its
    right-aligned values — and it left a white strip beside the FULL-BLEED focused
    row on every list that fits, which reads as a rendering fault. A defect you
    see every time beats a reflow you see once. One condition drives both, and
    `drawScrollRail` refuses the same case independently so they cannot disagree.
  - **A rail cannot live in the outer margin**, which was the first attempt: the
    focused row is full-bleed inverted, so a black thumb crossing it is black on
    black and vanishes, and each row's 1px rule runs straight through the track.
    It needs a column the rows do not enter — that is the real cost of a
    scrollbar here, 14px off every row.
  - **A vertical rail is the BEST case on this glass, not the worst.** It is
    axis-aligned and coverage 0-or-3, so track and thumb are identical in every
    plane and pass. The thin-stroke warning this project records is about
    DIAGONALS (`kChevron`); it was wrongly cited against a rail once.
  - **This governs every scrollable list**, and today that is the Library and
    V1.1's Wi-Fi picker — which is the second user of the rail and the first
    since it was written. This line said "Library alone" through the picker
    landing.
    Settings scrolled for about an hour: adding its `Refresh on screen change` row
    pushed it past the panel, and then Wi-Fi was cut from V1 and CONNECTIONS went
    with it — eleven items where twelve fit, the SLEEP SCREEN section took it back
    to nine, V1.1's CONNECTIONS row put it at eleven, and **the `Wallabag` row has
    now put it at TWELVE** — which is exactly the number that fits, so there is
    still no rail and no gutter and the rows still run to the panel edge, with
    nothing left over. **The next row to land starts the scroll**, and it will do
    so with no code change, because `renderSettings` reads `totalRows > rows`
    rather than assuming. That figure has moved FIVE times and is the thing to
    re-read rather than inherit — count `kItems`. Contents
    and Bookmarks are Phase 3's and will want it too.
  - **It is compared against its board now**, and for a while it was not: Library's
    golden shows seven rows of seven, so it does not overflow and no rail draws in
    it, and Settings stopped scrolling when Wi-Fi was cut. So the rail shipped with
    unit tests and nothing that looked at a pixel. The `library_scrolled` state
    needs a LONG list — a rail's proportions come from the list's length — so the
    factory takes demo items and the state uses 24 books.
    - **Reaching the board's window takes one press past it and one back.** Twelve
      Downs is the obvious route and gives the wrong window: `ScrollWindow` scrolls
      only as far as it must, so arriving from above lands the focus on the
      window's BOTTOM edge. Both states are real; the board's has list on both
      sides of the thumb, which is the better illustration.
- **The session record stores a screen NAME, not an enum ordinal.** 2C-2 inserted
  three screens into the middle of `ScreenId` and a stored ordinal silently became
  a different screen. Names also mean `nvs_get encre_sess scr str` is readable on
  a device. They are deliberately not `screenName()`'s strings — that is a log
  label, free to be reworded; this is a storage format.
