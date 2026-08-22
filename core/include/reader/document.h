#pragma once
#include <string>
#include <string_view>
#include <vector>

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

// A chapter's XHTML into blocks. False on malformed markup or a document past the
// caps below, with `*reason` naming which -- never an abort, because this parses
// bytes off a user's card.
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
