# CONTEXT

The vocabulary this codebase uses, and the distinctions that stop two words
meaning one thing. A glossary and nothing else: no implementation detail, no
decisions, no status. Decisions live in `docs/superpowers/specs/` and in
`CLAUDE.md`'s section files under `docs/notes/`.

Created 2026-09-19 while settling the Names family, which is where the first
terms that could collide with each other turned up.

## Names

**Name** — a capitalised entity the scan found in a book: a person, a place, an
organisation, anything the heuristic ranks. The screen is called NAMES rather
than CHARACTERS because nothing here can tell a person from a place, and a
place is a thing readers look up too.

**Run** — one maximal sequence of capitalised tokens as it appears on the page:
`Stu`, `Stuart Redman`, `La Poubelle`. A run is what the scan counts and what
the card stores. It is not a person; several runs can be one.

**Group** — the runs the grouping rules decided are one entity, with a
**display name** (the form the book uses most, so `Stu`) and a **fullest form**
(the longest member, so `STUART REDMAN`). Groups exist only once a screen has
opened; the card holds runs.

**Mention** — one stored sighting of a name: a `(spine, block)` cursor and its
extract. A name keeps at most eight.

**mentionCount** — how many times a run was seen mid-sentence, which is what the
thresholds are applied to. **Not the same number as how many Mentions a name
has**, and this is the one collision in the vocabulary: `Stu` has a
mentionCount in the hundreds and eight Mentions. The count is never displayed,
so a reader never meets both; code and prose keep the two spellings apart.

**Extract** — the ~64 bytes of sentence stored with a Mention, a window centred
on the name. Never a whole sentence, and never taken from the sentence's start.
It is a field of a Mention rather than a thing of its own; no screen, type or
file is named after it.

**Admission** — a run earning a slot on the card, by being seen at least twice
mid-sentence within one chapter. Distinct from the **display threshold**, which
is how many mid-sentence mentions a group needs before a reader sees its row.
Two thresholds, two questions: what the card keeps so a count can go on
growing, and how many rows a reader scrolls.

**Store** — everything the card holds for one book's names: the index and the
per-chapter extract files. Deleted with its book.

**Scan** — the pass that finds runs in a chapter, riding the pagination walk.
**Capture** — the second pass that writes that chapter's extracts once
admission is known. **Merge** — folding a chapter's runs into the index.
Three separate things; "scanning a chapter" means only the first.

**Backfill** — scanning chapters *behind* the reader that were never opened on
this device. Always backwards-looking; scanning ahead is what the design
refuses.

## Reading

**Spine** — a chapter's index in the EPUB's reading order. The firmware says
spine, not chapter number, because a book's own chapter numbering is its
content and frequently disagrees.

**Block** — one paragraph-level unit of a chapter's text, as the document layer
yields it. The unit a Cursor names and the smallest thing a peek can open at.

**Cursor** — `(block, line)` within one chapter. **AnchorPos** — `(spine,
block, line)` across a book. The `line` is the field that survives neither a
re-layout nor a re-bind, which is why a Mention stores no line.

**Return anchor** — the high-water mark a reader can come back to after
jumping. One transition: it only ever rises.

**Peek** — a panel of a chapter's text over the page you are on, opened at a
position, left with CLOSE or committed to with GO HERE.

## Screens

**Screen** — one entry in `ScreenId`, with a view model and a theme render
method. **Overlay** — a screen drawn over the one beneath it rather than
replacing it. **Variant** — a different rendering of the *same* ScreenId and
view model, such as Names with nothing to show; not a second screen.

**Board** — a `design/*.dc.html` file, the source of truth for what a screen
looks like. **Canvas** — the generated `design/ereader-v1-ui.html` that
publishes every board; a board with no `canvas.json` entry is loaded and never
shown.

**Row** vs **unit**: a row is one item in a list; a unit is what a ScrollWindow
measures. They differ where a row carries a section header it cannot be
separated from.

## Hardware

**Panel** — the e-ink glass, which holds its image with no power.
**Controller** — the chip driving it, whose DTM1 baseline does not survive a
reset. Conflating the two has disguised a crash loop as "nothing happened".

**Fidelity** — how a screen's coverage reaches the panel: `Mono`, `Dithered` or
`Grayscale`. A property a screen declares, not a quality judgement.

**Quiet window** — the gap in the main loop after the reader has stopped
pressing, where deferred work runs.

**Scratch** — the 37,056-byte inflate block a chapter stream holds. Only one
can be live, which is why reading a second chapter costs a release and a
reacquire.
