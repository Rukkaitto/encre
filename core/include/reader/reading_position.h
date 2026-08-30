#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "reader/layout.h"  // Cursor

namespace reader {

// WHERE THE READER WAS IN ONE BOOK, and whether that is still true.
//
// It lives ON THE CARD, in `/.reader/state/`, which is the location spec 4.0
// already named when it said deleting a book "never erases reading progress".
// The alternative was NVS, where the session record lives, and the argument that
// puts the session record there does not reach this:
//
//   * A WAKE MUST WORK WITH NO CARD -- that is the whole state SdMissingScreen
//     exists for -- so which screen you were on has to survive an empty slot. A
//     reading position does not: with no card there is no book to open, so the
//     position is unusable whatever we did with it.
//   * A CARD SWAP IS THEN CORRECT BY CONSTRUCTION. In NVS the position is keyed
//     by a path like `/books/Fleau.epub`; put in a different card and that path
//     either does not exist or holds a DIFFERENT book, and restoring page 400
//     into a hundred-page novel is a worse failure than forgetting. Detecting
//     that needs a book-identity check -- which is this struct's `bookBytes`,
//     doing the job a correct location does for free.
//   * And the position travels with the book, which is what a user means by
//     putting their library on a card.
//
// --- IT DEGRADES INSTEAD OF BEING DISCARDED ------------------------------------
//
// A saved position is three numbers of decreasing durability, and the honest thing
// is to keep whichever of them still mean something rather than to treat the record
// as all-or-nothing:
//
//   `spine`  survives nearly everything. It indexes the OPF's spine, which is the
//            book's own structure.
//   `block`  survives a RE-LAYOUT. Blocks come from document.h -- paragraphs and
//            headings -- and owe nothing to a column width or a type size.
//   `line`   does not. It is a line WITHIN a block at one ppem and one column
//            width, so changing either makes it a number about a layout that no
//            longer exists.
//
// So the record carries the three facts that decide which tier is usable, and
// `fitOf` grades them. Landing at the top of the right block is a small, visible
// imprecision; landing on line 9 of a block that now has four lines is a wrong page
// that looks like a bug.
struct ReadingPosition {
  // WHICH BOOK, stored inside the file rather than only encoded in its name. The
  // filename is a hash (see statePathFor) and a hash can collide; the path is what
  // makes a collision detectable instead of silently restoring another book's
  // position. It is also what makes the file readable on a computer.
  std::string bookPath;

  int spine = 0;
  int block = 0;
  int line = 0;

  // THE STALENESS FACTS. Each one invalidates a different tier above.
  //
  // `bookBytes` is the EPUB's size, which is the cheapest identity a FileSystem
  // with no timestamps and no hashes can offer -- `DirEntry` already carries it, so
  // checking it costs a listing this device does anyway. It is not a checksum and
  // does not pretend to be: two different books of exactly equal size compare
  // equal. What it reliably catches is the case that actually happens, a book
  // re-exported or replaced on the card.
  uint32_t bookBytes = 0;
  int ppem = 0;     // the body size `line` was laid at
  int columnW = 0;  // the column width likewise
  // AND TWO TYPOGRAPHY SETTINGS ARE DELIBERATELY *NOT* FIT INPUTS: alignment and
  // line spacing. Neither moves a line BREAK -- justification is applied to a
  // finished line (layout.h) and the lead only sets how far down the next baseline
  // goes -- so `line` survives a change to either and recording them here would
  // throw away a usable position for nothing. Stated because the silence was
  // previously accidental: adding `justify` to fitOf on the instinct that
  // "alignment is layout" would fail no test.
  //
  // What DOES belong here is anything that changes the measure a line was wrapped
  // at, which is exactly the two fields above -- `bodyPpem` arrives as `ppem` and
  // `margins` as `columnW`.

  // HOW FAR THROUGH THE BOOK, 0..100, stored rather than derived.
  //
  // Derived data in a record is usually a smell, and this is the exception that
  // earns itself: recovering it needs the book's chapter byte layout, which means
  // OPENING THE EPUB -- a central directory and an OPF parse each. The Library shows
  // a percentage per ROW, so deriving it would be one archive open per book on the
  // card, hundreds of milliseconds each, on a screen that has to paint.
  //
  // It is exact when written (progressPercent, at the moment the position was saved)
  // and it goes stale only if the book itself changes -- which `bookBytes` already
  // detects, and which drops the position anyway.
  int percent = 0;

  // THE CHAPTER'S NAME AT THIS POSITION, for Book details' "Current story" row.
  //
  // Stored for the SAME reason `percent` is, and it is a stronger case: recovering it
  // means opening the book's archive AND parsing its NCX, where the Library needs the
  // answer for a row it draws without opening anything. It is exact when written -- the
  // Reader's own header label, so a book with no contents stores the `CH. 08` fallback
  // and the row says that, which is honest.
  std::string chapter;

  // THE READER SAID THEY WERE DONE, which is not the same claim as `percent == 100`
  // and is why this is not derived from it. Reading to the last byte of a book whose
  // final 8% is an appendix, an index and a colophon is a MEASUREMENT; this is an
  // ASSERTION, made by pressing a button. Conflating them would mark a reader who
  // abandoned a book in its endnotes as having finished it.
  //
  // WRITTEN ONLY WHEN TRUE -- see serialise(). That is the anchor's own rule three
  // keys below, and it is what keeps an unfinished record byte-identical to one from
  // before this field existed, which is what keeps writeIfChanged's `Unchanged`
  // answer true instead of rewriting every sidecar on the card once.
  //
  // READING THE BOOK AGAIN UN-MARKS IT: a position save from the Reader builds the
  // record fresh, so the flag clears. There is no board for a toggle, so the
  // alternative is finished-forever, which is worse than the cost -- opening a
  // finished book and immediately leaving also clears it, recoverably and visibly.
  bool finished = false;

  // --- THE WAY BACK, and -1 in `anchorSpine` means there is none ----------------
  //
  // The return anchor (return_anchor.h), which is the same {spine, block, line}
  // shape as the position above -- so it needs no new representation and no nesting,
  // which json.h does not have. Three flat integers.
  //
  // IT RIDES THIS RECORD'S SAVE EDGES AND ADDS NONE. `saveReadingPosition` already
  // fires on leaving the book, crossing a chapter and sleeping; the anchor goes with
  // it. In particular NOT one write per page turn: losing an anchor to a power cut
  // costs the reader a shortcut and nothing else, because they are still sitting on
  // a real page. A benign failure does not justify a write on an edge that does not
  // already take one.
  int anchorSpine = -1;
  int anchorBlock = 0;
  int anchorLine = 0;

  bool hasAnchor() const { return anchorSpine >= 0; }

  bool operator==(const ReadingPosition& o) const;
};

// How much of a saved position still applies, given what the book and the layout
// are NOW. Ordered weakest-last, and a caller should switch on it rather than
// compare -- the point is that there are four answers, not two.
enum class PositionFit : uint8_t {
  Exact,     // spine, block and line all usable
  Relaid,    // the type or the column moved: spine and block usable, line is not
  Rebound,   // the book's bytes changed: spine only
  Unusable,  // not this book at all
};

// `bookPath` must match or the answer is Unusable, whatever else agrees: a record
// reached through a colliding filename is about a different book.
PositionFit fitOf(const ReadingPosition& saved, std::string_view bookPath, uint32_t bookBytes,
                  int ppem, int columnW);

// The spine entry and cursor to actually open, with whatever the fit does not
// support zeroed. Zeroing rather than refusing is the whole point of grading: the
// top of the right chapter beats the front of the book, which beats nothing.
struct PositionRestore {
  bool any = false;  // false only for Unusable
  int spine = 0;
  Cursor cursor{};

  // THE ANCHOR DEGRADES WITH THE POSITION, NOT INDEPENDENTLY, and that is the whole
  // rule: its `line` is exactly as fragile as the position's, so anything below
  // `Exact` drops it rather than keeping a part of it. An anchor that lands the
  // reader on the WRONG page is worse than no anchor -- the same reasoning that
  // already zeroes `line` on a re-layout, and the reason this is not graded
  // separately.
  bool anchorAny = false;
  int anchorSpine = 0;
  Cursor anchorCursor{};
};
PositionRestore restoreFrom(const ReadingPosition& saved, PositionFit fit);

// --- The wire format ---------------------------------------------------------
//
// One flat JSON object through json.h, which is exactly what that subset holds --
// no nesting and no arrays. (Bookmarks WILL need an array and are a different file
// for that reason; this one must not grow into them.)
//
// Keys are sorted by dump(), so saving an unchanged position produces a
// byte-identical file -- which is what lets the shell skip a write that would
// change nothing, and a card write is the expensive thing here.
std::string serialise(const ReadingPosition& p);

// Total: any malformed, truncated or wrong-version text is a clean false with `out`
// untouched. A half-finished write is the case this must survive, and it is a real
// one -- the device can lose power mid-save.
bool parsePosition(std::string_view text, ReadingPosition& out);

// Where one book's sidecar lives: `/.reader/state/<hash>.json`.
//
// A HASH, NOT THE PATH, because a book path is not a filename: it contains `/` by
// construction, FAT forbids more (`\ : * ? " < > |`), and the names on a real card
// are whatever a user's computer wrote -- accents, quotes, a 90-character title.
// Escaping all that into one safe name is a second format to get wrong, and it
// would still need a length cap.
//
// FNV-1a 32-bit, eight hex characters. Collisions are POSSIBLE and handled rather
// than assumed away: the path is stored in the file, and `fitOf` answers Unusable
// when it does not match, so a collision costs one book its position and never
// misapplies another's.
std::string statePathFor(std::string_view bookPath);

// The record's own version. Bumped when the meaning of a field changes, which makes
// every older file read as "no position" rather than as a wrong one.
inline constexpr int kPositionVersion = 1;

}  // namespace reader
