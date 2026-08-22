# Codebase review findings — 2026-08-22

Cold read against `docs/superpowers/2026-08-22-review-brief.md`. Per finding:
file and line, what breaks and under what input, and confirmed (evidence in
hand) versus suspected (argued from code, not reproduced).

**Verification run:** `make test` 9/9 suites; the same 9/9 under
`-fsanitize=address,undefined`; `make firmware` builds at 17.7% flash /
18,564 B RAM, matching the brief. One environment note: a fresh worktree has an
empty `freeink-sdk/`, and `make firmware` then fails with a misleading
`PackageException: not a directory` — `git submodule update --init` first.

---

## 1. CONFIRMED — `JsonObject::parse` allocates without bound from a hand-edited settings file; under `-fno-exceptions` that is an `abort()` boot loop

**This is the fifth one.** Same shape as the other four: state that only exists
on the device (a ~230 KB heap and `-fno-exceptions`) reached through code no
test observes at that scale — the desktop runs the same parser with a desktop
heap and exceptions, so no desktop test *can* fail this way.

- `core/src/json.cpp:47` — `out.emplace_back(...)`: one
  `std::pair<std::string, Parsed>` per key/value pair, and nothing bounds the
  pair **count**. `Parsed` carries an `int64_t` plus a `std::string`, so each
  pair is ~64–72 bytes of vector payload on the C3 before any string heap.
- `core/src/json.cpp:96` — `parseString` grows one string to the size of the
  file: `{"a":"<63 KB of x>"}` builds a ~63 KB string byte by byte, with
  `push_back`'s geometric growth transiently holding old+new buffers.
- `core/src/json.cpp:209` — `built.setString(key, v.s)` **copies** while
  `pairs` still owns the parsed string and `loadSettings`' `text` buffer
  (`core/src/settings.cpp:86`) is still alive: for the one-giant-string file
  that is three ~64 KB copies live at once (~192 KB) against a heap whose
  measured floor is ~155 KB free and largest block ~115 KB.

**Input:** any `/.reader/settings.json` the 64 KB `readAll` cap deliberately
admits. `"a":0,` is 6 bytes, so a 64 KB file of minimal pairs is ~10,900 pairs
— several hundred KB of vector against a ~230 KB heap. Duplicate keys are legal
in this parser ("last wins"), so the flood file is *valid* input.

**What breaks:** the abort happens **during** parse, before validity is
decided, so `loadSettings` never returns false and the `DEFAULTED` path —
the entire "a bad file is replaced, not trusted" design — is unreachable. The
file is still on the card at the next boot, so it is a persistent boot loop
with no diagnostic, cleared only by removing the card or the file. A
well-meaning user pasting a large blob into the file the boot log advertises as
"hand-editable from here on" is the trigger; no hostility required.
`settingsFailure()` (`shell/src/main.cpp:324`) re-parses the same file and
would hit the same wall, but `loadSettings` aborts first.

**Fix shape:** cap pair count and per-string length in the parser (the file
legitimately needs ~4 short pairs), and `std::move(v.s)` at json.cpp:209.
Allocation math is from sizeof-arithmetic, not a device run — the conclusion
does not depend on the margins.

## 2. CONFIRMED (thresholds estimated) — the directory-listing chain has no entry cap and triple-buffers the listing

- `shell/src/sd_fs.cpp:355` builds `found` (one `DirEntry` per entry, name heap
  up to 255 B each), then **`:368` `out.insert(out.end(), found.begin(),
  found.end())` copies rather than moves** — two full sets of name strings live
  transiently.
- `core/src/booklist.cpp:119,126` — `raw` (everything) and `out` (filtered,
  **two** strings per entry: name + title) live simultaneously.
- `core/src/screen_library.cpp:68,81` — `entries` and `items_` simultaneous
  during `rescan()`, plus one extra full listing per subfolder
  (`:78` → `countBooks`).

**Input:** one folder with a few thousand files — an ordinary Calibre-managed
card, not a hostile one. ~2,000 entries at ~40-byte names is roughly 300–500 KB
transient across the layers → the same silent `abort()`. Even ~500 long-named
entries is uncomfortable next to the 52 KB frame and the ~115 KB largest block.
Cheap first steps: `std::make_move_iterator` at sd_fs.cpp:368, and a documented
entry cap with a `skippedNames`-style counter.

## 3. CONFIRMED — the default `make compare` covers 2 of the 6 implemented screens, and the docs say it covers everything

`Makefile:127` runs `tools/compare-design.py` with no arguments;
`tools/compare-design.py:297` includes `FLOW_SCREENS` only under `--all` — and
`--only` filters the *already-selected* list, so `--only sd_missing` without
`--all` silently matches nothing. The documented command therefore renders
Home and Library and reports "2/7 screens implemented"; ItemActions,
DeleteConfirm, BookDetails and SdMissing are never compared unless someone
remembers `--all`. All four **do** pass when asked
(`--all --only library_actions,delete_confirm,book_details,sd_missing`:
verified, 4/4 ok at both geometries).

What breaks is the process, not the pixels: CLAUDE.md calls this "the
design comparison tool … contact sheet for **every** screen" and "`make
compare` is what keeps them honest" — but the honesty check for four of the six
implemented screens only runs when explicitly invoked, which is the same
quiet-degradation shape as the probe defect this project already shipped once.
Fix: default the tool to comparing every board whose sim subcommand exists, or
make `--only` reach FLOW_SCREENS without `--all`.

## 4. SUSPECTED — a dropped release edge breaks "one physical press is exactly one event"

`shell/src/input_task.cpp:38,42` drop edges when the 32-deep raw queue fills —
which a paint makes possible, because the loop that drains it is blocked for
the paint's duration. A dropped **release** leaves
`PressRecognizer::State::down` latched (`core/src/input.cpp:43` ignores the
next release-less state; `:36-39` keeps the original `downAt` on the next
down). Consequences: `tick()` fires a **phantom Long** for a button the user
already released the moment it is in the long-press mask
(`core/src/input.cpp:71-73`), or the next short press inherits the stale
`downAt` and classifies as Long (`:57` — on a list that is "open the actions
overlay" instead of "open the item"). Today's exposure window is a FULL paint
(~825 ms ≈ 16 raw edges at a fast mash) — narrow, and grows with anything that
blocks the loop longer (a `Grayscale` screen, Phase 3 layout). The `dropped=`
counters on the `[alive]` line are the only mitigation, and they diagnose
after the wrong action already happened. Cheap hardening: on a drop, also
force-release the recognizer's state for that button, or size the queue past
the worst loop stall.

## 5. CONFIRMED (latent, no runtime bug today) — `drawText` violates the written `Glyph::bitmap` borrow contract

`core/include/reader/glyphsource.h` says the bitmap is valid "until the next
call into the same GlyphSource". `core/src/text.cpp` obtains the glyph
(`:22`), then calls `font.kerning(prev, cp)` (`:44`), then blits the bitmap
(`:46-74`). Safe with both current implementations because `kerning()`
provably never touches the ring arena (`core/src/scalablefont.cpp:380-388`) —
but the day `ScalableFont::kerning` rasterises anything (GPOS-from-outlines is
the obvious future), every kerned pair in body text reads a possibly-
overwritten bitmap, and it passes every test in which the arena never wraps
mid-pair. Either narrow the contract comment to "until the next `glyph()`
call" (making the code conformant), or hoist the kerning lookup above
`glyph()` — both operands are known first.

Two adjacent facts established while auditing (worth recording, not defects):
the arena gives **no** guarantee that the last-returned glyph survives the next
`glyph()` call (a wrap overwrites it — `scalablefont.cpp:263-284`), and a
cache **hit** on a null-arena source returns `bitmap == nullptr` for a cached
zero-ink glyph (`scalablefont.cpp:405`), contradicting the miss path's
"valid pointer rather than null" comment. Never dereferenced today
(`bitmapH == 0`), but it would trip the first caller that null-checks.

## 6. Ranking of the brief's known-unbounded three (item 5)

1. **`drawDetailRow`'s value** (`core/src/components.cpp:535`) — first because
   it is reachable in shipping V1 today: Book details' `Location` holds a real
   path, and a long value makes `fb.width() - kMargin - vf.measure(value)`
   negative, drawing right-to-left over the label. Visual only —
   `Framebuffer::setPixel` clips (`core/src/framebuffer.cpp:78`) — but it is
   the one of the three a user can produce this week by nesting folders.
   Needs its board pass first, per the rule.
2. **`countLibrary` at boot** (`core/src/booklist.cpp:83` via
   `shell/src/main.cpp:569`) — one listing per folder **before the first
   paint**; a 50-folder card adds visible seconds to every boot and retry. It
   degrades, never breaks (a failed listing is `-1` → blank count), and
   Phase 3's background scan is already its named home. Second.
3. **`elideToWidth` cutting codepoints** (`core/src/text.cpp:127`) — third
   because nothing today can reach it: the typographic set and Latin-1 are all
   single-codepoint. It becomes real with Phase 3 EPUB metadata (combining
   marks, ZWJ emoji), at which point it is a rendering wrongness, not a crash.

## 7. Focus-area verdicts the brief asked for

- **SPI bus discipline (item 1): holds.** `SdMan`/`SDCardManager` is reached
  only from `sd_fs.cpp` and `sd_selftest.cpp` (grep; the main.cpp hit is a
  comment). Every public `SdFileSystem` method, every `FileHandle` operation
  (destructor included), `wipeScratch`, `explainWipeFailure` and the selftest's
  final `removeDir` take the guard; `renderTop()` holds it across the whole
  paint; `pollCardPresence` wraps probe+`mounted()` as one atomic answer.
  `SdFileHandle::openAt` is deliberately unguarded (its one caller holds it).
  No unguarded path to the card found.
- **`input_task.cpp` exclusivity (item 2): holds.** `update()` appears exactly
  once, in `pollTask`; the loop task touches only `popRawSample`. Two residual
  notes: `xTaskCreate` failure is unchecked (`input_task.cpp:55` — buttons
  silently dead, `[alive]` keeps printing, `dropped` never increments, so the
  log would not name it), and the queue-overflow consequence is finding 4.
- **Borrowed pixels (item 3): no dangling dereference exists today.** Every
  call site finishes with a bitmap before the next `glyph()` on the same
  source; the pre-rendered `Font` path points into the embedded blob and is
  stable; an oversized glyph bypasses the arena without evicting anything
  (`scalablefont.cpp:264`). The contract-vs-code gap is finding 5.
- **Partial repaint (item 6): correct as far as this review could push it.**
  Probed: multi-event bursts coalesced into one paint (compare is against the
  last *painted* state, so intermediate states are irrelevant); grayscale plane
  interleaving (the plane check refuses every pass but the first, and fidelity
  refuses the whole path); push-then-pop in one burst (transition latches until
  `clearDirty`); App replacement (fresh record, null frame); pop-then-push
  address reuse (transition covers the ABA); a `pushScreen` that fails
  (nothing marked dirty, correctly). The one condition App cannot see — same
  address, replaced bytes — is exactly what `gFrameContentsUnknown` covers in
  the shell (`main.cpp:1060-1068`). Found nothing wrong.

## 8. SUSPECTED (benign consequence) — the session record's "version written last" is a commit record only for the first write

`shell/src/session.cpp:223-228`. The claim is that a write dying half way
reads back as "no session". True for the first write of a namespace; on an
**update** the previous record's valid version key is already present, so a
power cut after `putString(scr)` and before `putUShort(focus)` leaves
version=2 + new screen + **old focus** — a valid-looking mixed record. Each
`nvs_set` is individually atomic, so the blast radius is one stale field, and
a wrong focus clamps on restore. Worth a comment correction more than a code
change; a real fix would write the pair as one blob.

## 9. CONFIRMED — the log line in `homeVmForCard` defeats its own guard

`shell/src/main.cpp:574-576`. The assignment is guarded by
`!vm.menu.empty()` — the comment says an empty menu "would be a crash rather
than a wrong label" — and the `Serial.printf` two lines later indexes
`vm.menu[0].value` whenever `books >= 0`, unguarded. Unreachable today
(`demoHomeVm()` always fills two rows), so this is latent; but the guard's
entire stated purpose is undone in the same screenful of code. One-line fix.

## 10. Doc and comment drift (the brief asked for what the two documents are wrong about)

- **CLAUDE.md:351-353 — "The stored focus is always 0 — `reader::Screen` has
  no focus accessor"** — false on every clause: `Screen::focus()/setFocus()`
  exist (`core/include/reader/app.h:124-125`), the shell stores the real focus
  clamped to uint16 (`shell/src/main.cpp:657-660`) and restores it on wake
  (`:1733-1735`). The roadmap carries the same claim at lines 289-294.
- **`core/include/reader/app.h:266-268`** — "chrome constructs its policy with
  fullOnTransition false, so a transition is an ordinary FAST refresh" — the
  shipped default is **true** (`settings.h`, and the whole knobs comment atop
  `main.cpp`). Stale from before the direction flipped; it contradicts the
  documented behaviour in the very header that implements the stack.
- **CLAUDE.md — "All ten shipped marks"** — eleven: `kSdCard` landed
  (`core/src/icons.cpp:49-59`). Same sentence was wrong once before, same way.
- **CLAUDE.md type table lists ten roles; the code ships eleven** — Body700 is
  missing from the table (`core/include/reader/fontset.h`, asset built and
  embedded). Roadmap:674 "all ten (size, weight) pairs" ditto. CLAUDE.md is
  internally inconsistent — its kerning section already says "eleven embedded
  chrome faces".
- **CLAUDE.md `drawActionButton` paragraph** — "no border, one centred
  Value700 label … the outlined secondary variant belongs in this function
  when DeleteConfirm or BookError lands, not before" — the outlined variant
  already exists (`core/src/components.cpp:247-275`, `bool filled`, outlined
  labels are Label500) and DeleteConfirm landed.
- **Roadmap:9 — "39 boards"** — matches nothing measurable: `design/` holds 47
  boards (the roadmap itself says 47 at line 635) and the two contract pages
  hold 28.
- **CLAUDE.md "contact sheet for every screen"** — see finding 3.
- Trivia, recorded so nobody re-measures: the grayscale waveform figure is
  quoted 366+366+156 in CLAUDE.md and 367+366+156 in the roadmap (the roadmap's
  is the log-derived one), and CLAUDE.md's flash figure (1,159,720) is one
  commit stale against the current build (1,160,796).

## What was checked and found sound (so the next review does not re-derive it)

`App::canRenderTopOnly` and the paint-record logic; the three-copy path
normaliser (behaviourally pinned by the contract); `SdFileHandle` lifetime and
`DESTRUCTOR_CLOSES_FILE=0` handling; `writeAll`'s short-write verify and
remove; the probe/backstop layering and its arming logic; `handleRetry`'s two
branches; the session wire-name format and version gating (modulo finding 8);
the factory's `forgetLibrary` discipline at all three App-swap sites (a wake
record naming an overlay or BookDetails is refused by the null-`library_`
check and logged, matching the documented approximation);
`LibraryScreen::rescan`/`descend`/`ascend` focus behaviour including failed
rescans; `RefreshPolicy::next` including the `cadence == 1` and `kNever`
edges; `Framebuffer` view refusal and clipping; `settings.cpp` clamping
(sound, given a parse that does not abort — finding 1 sits below it).

---

## Disposition — 2026-08-22

Every claim spot-checked in code before acting; all of them held.

| # | What | Status |
|---|---|---|
| 1 | JSON parser unbounded → `abort()` boot loop | **FIXED** — `kJsonMaxPairs` 64, `kJsonMaxStringBytes` 256, both malformed past the limit. 6 tests. |
| 2 | Directory listing unbounded + triple-buffered | **HALF FIXED** — the copy at `sd_fs.cpp:368` is now a move. The entry cap is **open**, below. |
| 3 | `make compare` covered 2 of 6 implemented screens | **FIXED** — default is all 28 boards (6/28 implemented, ~2.5 min); `--only` errors on an unknown id instead of reporting `0/0`. |
| 4 | Dropped release edge latches the recognizer | **FIXED** — `PressRecognizer::forgetPresses()`, called from the loop when the drop counter advances. 4 tests. |
| 5 | `drawText` violates the `Glyph::bitmap` borrow contract | **FIXED** — kerning hoisted above `glyph()`, applied on the glyph path only so the notdef case still matches `measure()`. Every golden unchanged, which is the proof. |
| 6 | Ranking of the three known-unbounded | **ACCEPTED** — see below. |
| 7 | SPI / input-task / borrow / partial-repaint verdicts | **NOTHING TO DO** — all four held. Recorded so the next review does not re-derive them. |
| 8 | Session "version last" is a commit record only for the first write | **COMMENT FIXED**, code deliberately unchanged: one stale field, and a bad focus clamps on restore. |
| 9 | `homeVmForCard`'s log line indexes past its own guard | **FIXED** — one line. |
| 10 | Eight false documented claims | **FIXED** — all eight, plus the empty-`freeink-sdk` worktree trap added to CLAUDE.md. |

**One correction to the review**, since it will otherwise be re-attempted: finding
1's suggested `std::move(v.s)` at `json.cpp:209` cannot help — `setString` takes a
`std::string_view`, so it copies whatever it is given. The diagnosis was right and
the string cap addresses it better: the transient copies are bounded at 256 bytes
each instead of at the file's size.

### Still open

- **Finding 2's entry cap is a PRODUCT decision, not a cleanup**: how many books
  must V1 hold? A Calibre card with a few thousand files costs ~300–500 KB
  transient across `sd_fs` → `booklist` → `screen_library`, against a ~155 KB
  floor. Capping means a library that silently stops listing at N, which is a UX
  answer before it is a memory one. Do not pick the number in code without
  deciding the behaviour.
- **Finding 6's three, in the review's own order.** `drawDetailRow`'s value is
  first and reachable this week by nesting folders — but it needs its board pass
  first, per the rule that governs UI work. `countLibrary` at boot belongs to
  Phase 3's background scan, which is already its named home. `elideToWidth`'s
  grapheme clusters become real with EPUB metadata, not before.
- **Finding 5's two adjacent facts**, neither a defect today, both worth closing
  with 3B: the arena gives no guarantee the last-returned glyph survives the next
  `glyph()` call (a wrap overwrites it), and a cache **hit** on a null-arena source
  returns `bitmap == nullptr` for a cached zero-ink glyph, contradicting the miss
  path's "valid pointer rather than null" comment. Never dereferenced (`bitmapH ==
  0` guards every caller) — it would trip the first caller that null-checks
  instead.
