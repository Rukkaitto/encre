# Phase 3C — A reader whose memory does not depend on the book

**Status:** design approved 2026-08-22. Supersedes nothing; extends the 3B slice.

## The problem, measured

3B reads one chapter at a time. That was the right unit and it is not enough: **a
chapter is not a bounded quantity.** Measured on a real book — `Le Fléau` (French
*The Stand*), 12.7 MB, 92 spine chapters, mean 75 KB of XHTML per chapter:

| Chapter | XHTML | Document | Peak the heap must serve |
|---|---|---|---|
| ch 8 | 54 KB | 38 KB | 93 KB |
| ch 25 | 118 KB | 83 KB | 203 KB |
| ch 52 | 298 KB | 211 KB | 510 KB |
| ch 56 | 316 KB | 229 KB | **546 KB** |

Against **~142 KB free** on the device, with a largest free block well under that
because 203 library entries have fragmented it.

Per-**book** cost is not the problem and never was: the zip central directory plus
the entry list is 5,800 bytes and the OPF parse plus spine is 4,416 — **10,216 bytes
before a word of the book is read.** Loading "only the current chapter" is already
what happens.

The peak is two doublings, both inside one chapter:

1. **`raw` + `out` during the inflate.** 64,677 + 315,852 for ch 56. stb_image's
   zlib is one-shot buffer-to-buffer, so the whole compressed entry and the whole
   decompressed entry are live together.
2. **`xhtml` + `document` during the parse.** 315,852 + 228,849. `buildDocument`
   reads from the XHTML while filling the blocks, so both are live.

**What is readable today: 62 of 92 chapters (67%).** Freeing the Library's ~59 KB
while reading gets 69 (75%) — worth having, not a fix. The reader refuses the rest
gracefully as of 3B, but a third of this book cannot be opened.

## Where the leverage is, quantified

| Change | Chapters that fit |
|---|---|
| today | 62 / 92 (67%) |
| free the Library while reading | 69 / 92 (75%) |
| never materialise the XHTML | 84 / 92 (91%) |
| …and free the Library too | 90 / 92 (98%) |
| **also window the document** | **92 / 92, at ~36 KB constant** |

Only the last one is a reader. The others are a reader that stops working partway
through a book, which is worse than a slow one.

## The decision

**Write our own streaming raw-DEFLATE decoder.** stb's is one-shot; there is no way
to get chunks out of it, so streaming is unavoidable whichever route is taken —
including the "inflate to a temp file on the card" route, which needs incremental
output to write incrementally. Given that, the choice was ours-versus-miniz, and
ours wins on the same grounds 3B's parsers did:

- It removes stb's **6,608-byte single stack frame**, which already crashed the
  device once and is 41% of the loop task's whole stack.
- Complete control of the output window, and **zero allocation** beyond the window
  itself, which is what makes the memory constant.
- The test bed is unusually strong for this kind of code: 11 zip fixtures, 200
  generated EPUBs, and all 92 chapters of a real 12.7 MB book to validate
  byte-for-byte against Python's `zlib`.

The honest risk: **bit-level parsing of untrusted input is the most delicate code in
this repo.** Every loop must be bounded, every table index checked, and the fixtures
must include malformed streams, not only valid ones.

## Architecture

DEFLATE back-references reach up to 32,768 bytes, so a streaming decoder **must**
retain 32 KB of prior output. That is the format, not a choice, and it sets the
floor:

```
  32 KB   the inflate window (DEFLATE's maximum match distance)
 ~1.5 KB  Huffman tables — counts and symbol order, NOT stb's 512-entry fast tables
 ~2 KB    the blocks of the page being shown
 ~0.3 KB  the page index: one cursor per page of this chapter
 ─────────
 ~36 KB   constant, whatever the chapter's length
```

Six layers change. The boundaries 3B established hold; what changes is that each one
becomes **resumable** instead of buffer-at-once.

| Layer | Today | Becomes |
|---|---|---|
| `inflate.h` | one-shot, out pre-sized | `Inflater`: feed bytes, pull chunks, 32 KB window |
| `xml.h` | `Xml(string_view doc)` | takes a byte source; names copied to a small buffer |
| `document.h` | `buildDocument` → all blocks | a resumable builder emitting one block at a time |
| `layout.h` | `layoutPage(Document&, …)` | lays out over a block *stream* |
| `screen_reader.h` | paginates a whole `Document` | holds a page index and a positioned reader |
| `book.h` | returns a `Document` | opens a chapter as a stream |

### Why `Xml` needs to change, and what it costs

`Xml::name()` returns a view into the whole document, stable for its lifetime.
Against a stream there is no such buffer, so names must be copied into a reused
internal buffer — exactly as `text()` already is.

That breaks `document.cpp`'s tag stack, which holds `std::string_view` names into the
document. It will hold **truncated inline copies** instead: `char[16]` plus a length,
compared on the truncation. Every HTML block element name fits in 16 bytes
(`blockquote` is 10); a longer name compares on its first 16 bytes and its length,
so a mis-nesting between two 20-character tags agreeing in both could pass. That is
an acceptable narrowing of a check that exists to catch generator bugs, and it is
1,280 bytes of stack instead of 8 KB.

### Paging without re-reading the chapter every turn

Forward reading is the common case and must be free. The reading position keeps its
inflater and builder alive, so **turning forward continues the stream** — no
re-inflate at all.

- **Open a chapter:** stream once, laying out blocks as they arrive and discarding
  them, recording one cursor per page. Then position a reader at page 0. Two streams
  at open; the index is ~8 bytes a page.
- **Forward turn:** continue the live stream. Free.
- **Backward turn or a jump:** re-stream from the chapter start to the target. DEFLATE
  cannot be seeked without its window, and checkpointing costs 32 KB a checkpoint, so
  re-streaming is the answer. ~100–300 ms on device against a ~520 ms panel refresh.

A page count in the footer is why the index pass exists. Without it the footer cannot
say `P / N` until the chapter has been read to its end.

## What this does not do

- **Book-wide page numbers.** Still chapter-relative. A book-wide index is a pass
  over every chapter, and 3A measured why it cannot be eager: 17.3 µs/char over a
  1.8M-character novel is ~31 seconds.
- **Free the Library while reading.** Worth doing and independent of this — it is a
  stack/ownership change, not a parsing one. Tracked separately.
- **Images.** Unchanged; `<img>` is still dropped.

## Verification

Each task carries its own tests. Three checks span the whole phase:

1. **Byte-for-byte against `zlib`.** Every deflated entry of all 200 generated EPUBs
   and all 92 chapters of `Le Fléau`, streamed, must equal Python's `zlib.decompress`.
2. **The memory claim, asserted not estimated.** The accounting allocator used to
   produce the table above becomes a test: opening and reading any chapter of
   `Le Fléau` must peak under 48 KB, whatever the chapter's length.
3. **The stack, on the same instrument 3B added.** The new inflater replaces a
   6,608-byte frame; the `test_inflate.cpp` pthread probe must show the streaming
   path well under it.
