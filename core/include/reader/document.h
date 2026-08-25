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
// names, and ONE block. Measured over a real book, the largest single block is
// 4,406 bytes, so the peak is bounded by a paragraph rather than by a chapter.
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

 private:
  struct State;
  State* st_;  // one heap allocation, for the reason Inflater's Scratch gives
  int emitted_ = 0;
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
inline constexpr size_t kMaxBlockBytes = 64u * 1024u;
inline constexpr size_t kMaxNestDepth = 64;

}  // namespace reader
