#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/emphasis.h"        // Span
#include "reader/inflate_stream.h"  // ByteSource

namespace reader {

// --- WHAT THE MARKUP CLAIMED, FOR DIAGNOSIS ONLY -----------------------------
//
// A word that should be italic and is not has three explanations and they look
// identical on glass. `[page] ... emph=N ital=N` separates the last two; this
// separates the FIRST, which is the one that cannot be tested from here because it
// is a property of somebody's book.
//
// document.cpp reads `<em>`, `<i>` and `<cite>`. It does not read a class plus a
// stylesheet (`<span class="calibre3">`, which is what a Calibre conversion emits)
// and it does not read an inline `style="font-style: italic"`. Those are two
// different jobs -- one needs the OPF's CSS inflated and parsed, the other is a
// handful of lines -- so which one a real book uses decides the work, and this
// project's rule where an answer decides a design is to parse the thing rather than
// argue about it.
//
// Counted over the chapter last built, reset per document. Bounded: three counters
// and one fixed sample buffer, no allocation.
struct MarkupHints {
  int styledSpans = 0;    // any inline tag carrying a `style` attribute
  int italicStyles = 0;   // ...whose style mentions `italic`
  int classedSpans = 0;   // any inline tag carrying a `class` attribute
  int emphasisTags = 0;   // <em>, <i>, <cite> -- what IS understood
  // The first class value seen, truncated. One sample is enough to say what shape
  // the book uses; a list would be an allocation on the open path for a diagnostic.
  char sampleClass[32] = {0};
};

// The hints from the most recent buildDocument. A file-scope reading rather than a
// return value because it is diagnostic and every layer between here and the shell
// would otherwise have to carry it -- the same reason Profile and Progress are
// installed rather than passed.
const MarkupHints& lastMarkupHints();
void resetMarkupHints();

// A book's chapter, as a list of blocks. Deliberately the smallest model that
// serves design/Reader.dc.html, because a model is a promise to render what it
// holds and this slice renders paragraphs.
//
// EVERYTHING NOT MODELLED IS DROPPED, NOT APPROXIMATED. A `<table>` contributes
// its text in reading order or nothing; it never becomes a guess at a layout. The
// same for `<img>`, which 3C owns. Approximating is how a reader ends up showing
// something that looks like a bug in the book.
//
// INLINE EMPHASIS IS MODELLED AS BYTE RANGES, not as a run-per-style structure the
// text is broken into -- see emphasis.h for why, and for the measurement that
// decided it: `<em>` covers 0.6% to 5.8% of a chapter's characters across eight real
// books, so almost every line has none and the empty case must be free.
//
// `<em>`, `<i>` and `<cite>` all become emphasis; the distinction between them is
// not modelled for the same reason h1-h6 collapse to one Heading -- there is one
// face to render them with.
//
// `<strong>` AND `<b>` ARE DELIBERATELY NOT EMPHASIS, and are dropped the way they
// always were. Measured over the same eight books: 17 runs and **220 characters in
// total**, six of the eight having none at all. Mapping them onto the italic would
// be a lie about the author's markup; giving them their own face means a second
// prepped asset and a second glyph cache for 220 characters. Both are worse than
// setting them roman.
//
// THE ITALIC IS A SECOND FONT FILE, and it had to be: `assets/fonts/Literata.ttf`
// carries exactly two axes, `opsz` and `wght`, with `head.macStyle = 0x00` and
// `post.italicAngle = 0` -- no `ital` axis and no `slnt` axis, so no instancing of
// it produces a slanted glyph. `assets/fonts/LiterataItalic.ttf` is vendored beside
// it (OFL, same Reserved Font Name), prepped to 164,360 bytes, carrying the same two
// axes and the same `unitsPerEm` of 1000. That last number is what lets ONE line
// hold both faces without the measuring pass and the drawing pass disagreeing about
// where a run ends.
//
// AND THE TWO FACES ARE NOT THE SAME WIDTH: measured at ppem 32, the italic runs
// **6% to 9% NARROWER** than the roman over the same string. So emphasis cannot be
// measured with the roman and drawn with the italic -- on a 444px column a 20-byte
// italic phrase mis-measures by ~20px, which is most of a word, enough to break a
// line in the wrong place and to compute the wrong justification slack for it. The
// wrap therefore measures per run; see `StyledFace` in components.h.
enum class BlockKind : uint8_t {
  Paragraph,
  Heading,     // h1-h6 all land here; the level is not modelled because the board
               // draws one heading style
  Blockquote,
  ListItem,
};

struct Block {
  BlockKind kind = BlockKind::Paragraph;
  std::string text;
  // Byte ranges of `text` that are emphasised, sorted and non-overlapping. Empty
  // for the overwhelming majority of blocks, which is the case emphasis.h is
  // designed around.
  std::vector<Span> emphasis;
};

struct Document {
  std::vector<Block> blocks;
};

// A CHAPTER'S BLOCKS, ONE AT A TIME.
//
// The resumable form, and the one the reader uses. `buildDocument` below is this
// drained into a vector, kept because a whole-chapter Document is still the right
// thing for a test and for anything small.
//
// It exists because a Document is the OTHER half of why a real book could not be
// opened: Le Fléau's longest chapter is 315,852 bytes of XHTML and 228,849 bytes of
// blocks, and holding all of the second while producing it from all of the first is
// how a 546 KB peak happened. Nothing needs every block at once -- a page spans a
// handful -- so this hands them over as they finish and forgets them.
//
// Its whole memory is the Xml it drives (2,560 bytes), a tag stack of 64 truncated
// names, and ONE block.
//
// AND THE BLOCK IS BOUNDED BY `kMaxBlockBytes`, NOT BY WHAT THE BOOK WROTE. This used
// to read "the largest single block over a real book is 4,406 bytes, so the peak is
// bounded by a paragraph" -- true of Le Fléau and false of the corpus, where one block
// reaches 232,388 bytes, and an argument from a typical case is not a bound at all.
// The buffer is RESERVED at `kMaxBlockBytes + 2` and never grown, so the peak really
// is bounded now: two buffers plus one transient, 18,308 bytes, whatever the book
// says. See `kMaxBlockBytes` below and #90.
//
// UNBALANCED TAGS ARE CAUGHT HERE, which is the boundary xml.h documents: this is
// the layer that keeps a stack to know which block it is in, so an unclosed tag is
// free to notice at Eof and the parser is spared a second copy of that stack. The
// stack holds truncated COPIES, not views -- see document.cpp, where relying on
// views silently made `<blockquote><p>` a plain paragraph and made a mis-nested
// document parse clean.
class BlockReader {
 public:
  explicit BlockReader(ByteSource& src);

  // WHICH CLASS NAMES MEAN ITALIC, from the book's own stylesheets -- see
  // reader/css.h for the measurement that makes this necessary rather than nice.
  // A POINTER because the set belongs to the book and outlives every chapter read
  // from it; null, and the default, is "this book says nothing", which is what the
  // in-memory constructor and every test that does not care get.
  //
  // Set before the first next(). Changing it mid-chapter would emphasise part of a
  // block and not the rest, which is a wrong the reader cannot see and the writer
  // cannot debug.
  void setItalicClasses(const std::vector<std::string>* classes);
  BlockReader(const BlockReader&) = delete;
  BlockReader& operator=(const BlockReader&) = delete;
  ~BlockReader();

  // Fills `out` with the next block and returns true. False means either the
  // chapter ended (`error()` empty) or it was refused (`error()` says why) --
  // `ok()` tells them apart without a second call.
  bool next(Block& out);

  // Re-reads from the beginning of a new source, reusing the buffers. A backward
  // page turn on a stream that cannot be seeked means decoding the chapter again,
  // and constructing a second reader would mean a second 4.3 KB allocation.
  void restart(ByteSource& src);

  bool ok() const { return error_[0] == '\0'; }
  const char* error() const { return error_; }

  // How many blocks have been handed over. The global index of the NEXT one, which
  // is what a page cursor names.
  int emitted() const { return emitted_; }

  // HOW MANY TIMES A BLOCK HAS BEEN CUT AT `kMaxBlockBytes`, cumulative since the
  // last `restart()`, and it exists for `Xml::attrsDropped()`'s reason: a caller
  // that finds more blocks than the book has paragraphs can tell "the book wrote
  // them" from "we could not hold what it wrote". Zero for 208 of the 225-book
  // corpus, and 190 cuts across the other 17 -- it read "the two Gutenberg
  // mathematics texts" while the cap was 64 KB, which is #90's whole subject.
  //
  // CUTS, NOT EXTRA BLOCKS, and they differ only in one case: a cut is made when a
  // byte arrives with the block already full, so the continuation always receives that
  // byte and is normally emitted -- but a piece that is nothing but whitespace is
  // dropped like any other empty block, and then a cut happened with no extra block to
  // show for it. A cut is the thing that occurred, so a cut is what this counts.
  size_t blocksSplit() const { return blocksSplit_; }

 private:
  struct State;
  State* st_;  // one heap allocation, for the reason Inflater's Scratch gives
  int emitted_ = 0;
  size_t blocksSplit_ = 0;
  const char* error_ = "";
};

// A chapter's XHTML into blocks, all of them. False on malformed markup or a
// document past the caps below, with `*reason` naming which -- never an abort,
// because this parses bytes off a user's card.
//
// UNBALANCED TAGS ARE CAUGHT HERE, which is the boundary xml.h documents: this is
// the layer that keeps a stack to know which block it is in, so an unclosed tag is
// free to notice at Eof and the parser is spared a second copy of that stack.
bool buildDocument(std::string_view xhtml, Document& out, const char** reason);

// Caps, because a chapter is a file and a file may claim anything. A long
// chapter of a novel is a few hundred paragraphs.
inline constexpr size_t kMaxBlocks = 4096;

// WHERE A BLOCK IS CUT IN TWO, not where the document is refused -- issue #37, and
// `Xml::kTextBytes`'s "NOT a limit on a run's length; a longer run is split across
// nodes" one layer up. It used to be a refusal, which stopped BlockReader, which ended
// the chapter with nothing able to report it: two Gutenberg mathematics texts in the
// 225-book corpus lost everything after one paragraph of a hundred thousand digits.
// `BlockReader::blocksSplit()` is what makes a cut observable, and document.cpp's text
// branch carries the reasoning for splitting rather than truncating.
//
// AN EMITTED BLOCK CAN EXCEED IT BY TWO BYTES, which is worth knowing before sizing
// anything against it: `appendSpace` may add a word boundary at the cap, and the
// dialogue-dash glue replaces one byte with two. Neither is new -- both predate the
// cut -- and it is why document.cpp reserves `kMaxBlockBytes + 2` rather than the cap.
//
// 8 KB, AND IT WAS 64 KB UNTIL #90 -- A BOUND THE HEAP COULD NOT HONOUR. The old
// figure was calibrated ~1.5x above the measured reading floor, so the heap gave out
// first and the failure was `abort()` with no diagnostic under -fno-exceptions,
// arriving as a reboot onto Home. It was worse than 1.5x, because `push_back` grows
// GEOMETRICALLY and a cap of N does not cost N: on libstdc++, which is what the ESP32
// toolchain ships, the ladder is 15*2^k, so a 64 KB block ended at a capacity of
// 122,880 and its last reallocation held 61,440 and 122,880 AT ONCE. And blocks far
// below the cap were already impossible -- measured over the 225-book corpus, with the
// binding floor being the 42,152 bytes free when a book is opened through the Library:
//
//   natural block   final capacity   peak while growing   vs the 42,152 B floor
//        8,192 B         15,360 B             38,400 B            91%
//       16,384 B         30,720 B             76,800 B           182%   <- 7 books
//       32,768 B         61,440 B            153,600 B           364%   <- 4 books
//       65,536 B        122,880 B            307,200 B           729%   <- 2 books
//      232,388 B        245,760 B            614,400 B          1458%   <- the largest
//
// So SEVEN of 225 books (3.1%) could not be read on this device, not the two #37
// found, and the cap named none of them.
//
// THE NUMBER IS THREE BOUNDS THAT AGREE, none of them a round figure:
//
//  1. THE CORPUS'S 99.99th PERCENTILE. 69 of the 537,474 blocks 225 real books write
//     exceed 8,192 bytes -- 0.0128% -- and 208 of the 225 books have no block over it
//     at all. Above it are Gutenberg's plain-text conversions (`Paradise Lost` as one
//     50,983-byte block, `The Online World`) and the two mathematics texts.
//  2. WHAT IT COSTS AGAINST THE FLOOR. document.cpp reserves the buffer instead of
//     growing it, so the steady state is two buffers and nothing else -- the piece
//     handed to the caller and the one being built -- 2 * (8,192 + 2) = 16,388 B, and
//     the peak is that plus one 1,920-byte transient at the first cut, 18,308 B. That
//     is 43.4% of the floor, largest single request 8,194 B (19.4%). The next power of
//     two doubles both to 82%, and "the largest free BLOCK decides, not the free
//     total" is not a rule you can satisfy at 82% of a fragmented heap.
//  3. IT IS NOT A REGRESSION IN THE COMMON CASE, measured rather than assumed. A
//     caller's `out` keeps its capacity between blocks and never shrinks, so a book
//     already pays 2 * ladder(its largest paragraph): the MEDIAN corpus book pays
//     15,360 B today and the p90 book 30,720 B. 16,388 B is therefore +1,028 B on the
//     median book and -475,132 B on the worst.
//
// THE FLOOR IS ALSO A FLOOR: it cannot go below `Xml::kTextBytes` (1,024) without
// breaking the one-cut-per-text-node invariant document.cpp asserts, so the feasible
// band was (1,024, ~8,192].
//
// WHAT IT COSTS, in the same unit #37 priced itself in: 190 cuts across 17 of the 225
// books, against 4 cuts in 2 books at 64 KB -- one spurious paragraph indent per 8,192
// bytes of UNBROKEN text, about once per 15 pages of it, where #37 said once per 121.
// On 208 of the 225 books the rate is zero, because they write no such paragraph.
inline constexpr size_t kMaxBlockBytes = 8u * 1024u;
inline constexpr size_t kMaxNestDepth = 64;

}  // namespace reader
