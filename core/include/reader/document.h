#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/inflate_stream.h"  // ByteSource

namespace reader {

// A book's chapter, as a list of blocks. Deliberately the smallest model that
// serves design/Reader.dc.html, because a model is a promise to render what it
// holds and this slice renders paragraphs.
//
// EVERYTHING NOT MODELLED IS DROPPED, NOT APPROXIMATED. A `<table>` contributes
// its text in reading order or nothing; it never becomes a guess at a layout. The
// same for `<img>`, which 3C owns. Approximating is how a reader ends up showing
// something that looks like a bug in the book.
//
// INLINE EMPHASIS IS NOT MODELLED, and that is this slice's honest limit rather
// than an oversight. `<em>` and `<strong>` contribute their text inline with no
// marker, so a sentence reads correctly and reads unemphasised. The reason is that
// there is nothing to render it WITH: the body face is one TTF with one weight,
// and italics are a second asset nobody has decided on. Modelling it now would be
// a field layout must ignore, and a run-per-emphasis structure that line breaking
// would have to straddle for no visible gain -- so 3C adds runs when it adds a
// face that can show them.
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
