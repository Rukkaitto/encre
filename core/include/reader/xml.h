#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "reader/inflate_stream.h"  // ByteSource

namespace reader {

// A ByteSource over a buffer already in memory. For the documents that are small
// by nature -- `container.xml` is a few hundred bytes and the largest OPF measured
// in a real book is 8,472 -- where streaming would be machinery for nothing.
class BufferSource : public ByteSource {
 public:
  explicit BufferSource(std::string_view bytes) : b_(bytes) {}
  size_t read(void* dst, size_t bytes) override;

  // Point at different bytes, or back at the start of the same ones. What a rewind
  // is for an in-memory chapter.
  void reset(std::string_view bytes) {
    b_ = bytes;
    at_ = 0;
  }

 private:
  std::string_view b_;
  size_t at_ = 0;
};

// A PULL PARSER for the XML an EPUB contains, and only that.
//
// Written rather than vendored, on the same reasoning as the JSON reader: EPUB
// content is well-formed XML *by specification*, so none of the tag-soup recovery
// that makes an HTML parser large is needed here. What is needed is elements,
// attributes, text, the five predefined entities and numeric character
// references -- and a malformed file that produces a clean refusal rather than an
// `abort()`, because this parses bytes off a user's card.
//
// PULL, not callbacks. The document builder above it is a stack machine, and a
// callback API would have it maintaining its own resumption state between events;
// `next()` lets the caller's own loop be the state. expat was the alternative and
// its streaming callbacks fit exactly this awkwardly.
//
// --- IT READS A STREAM, NOT A BUFFER -----------------------------------------
//
// It used to take a `std::string_view` of the whole document and index into it.
// That is why a real book could not be opened: `Le Fléau`'s longest chapter is
// 315,852 bytes of XHTML, and holding it while the blocks were built from it was
// half of a 546 KB peak against a heap with ~142 KB free.
//
// So it reads from a `ByteSource` -- an Inflater's output, or a `BufferSource` over
// a document small enough not to care. Its whole memory is the buffers below,
// about 2.3 KB, WHATEVER THE DOCUMENT'S LENGTH.
//
// THE CONSEQUENCE FOR CALLERS IS A LIFETIME RULE: `name()`, `text()` and `attr()`
// return views into those internal buffers, valid only until the next `next()`.
// `text()` always worked that way; `name()` did not -- it used to be a view into
// the caller's own document and lived as long as it did. **A caller that keeps a
// name across a `next()` must copy it.** The document builder's tag stack is
// exactly that caller, and it holds truncated inline copies.
//
// A TEXT RUN LONGER THAN THE BUFFER ARRIVES AS SEVERAL `Text` NODES, and that is
// not an error or a limit -- `kTextBytes` is a chunking granularity. Measured over
// a real book: 45,217 text runs, median 46 bytes, longest 812. Any caller that
// accumulates text already handles several nodes in a row, because
// `a <em>b</em> c` is three of them.
//
// WHAT IT SKIPS, silently and by design: the XML declaration, DOCTYPE, comments,
// and processing instructions. None carries content a book needs, and a reader
// that refused a DOCTYPE would refuse most real EPUBs.
//
// IT IS A TOKENIZER, NOT A VALIDATOR, and the line matters: it does NOT check
// that tags nest or that they close. `<p>unclosed` yields StartTag, Text, Eof
// without complaint.
//
// That is the caller's job because the caller already has to do it: a document
// builder keeps a stack to know where to attach a node, so it notices an unclosed
// tag at Eof for free. Tracking depth here would be a second copy of that stack
// -- with its own depth cap and its own allocation -- for a check the layer above
// cannot avoid making. A test asserting this parser refuses `<p>unclosed` was
// written and then deleted for exactly that reason.
//
// WHAT IT IGNORES: namespace PREFIXES. `opf:package` reports as `package`, and
// that is right for EPUB rather than lazy -- the prefixes here decorate a known
// vocabulary, so resolving them properly would be machinery for a distinction no
// caller can act on.
class Xml {
 public:
  enum class Node : uint8_t {
    Eof,       // the document ended cleanly
    StartTag,  // name() and attr() are valid
    Text,      // text() is valid, entity-decoded
    EndTag,    // name() is valid
    Error,     // error() and offset() say what and where
  };

  // Bounded like the JSON reader's pairs, and for the same reason: these numbers
  // size buffers, and a file is free to claim anything.
  static constexpr size_t kMaxAttrs = 16;
  static constexpr size_t kMaxNameBytes = 128;

  // How much text one `Text` node carries at most. NOT a limit on a run's length
  // -- a longer run is split across nodes. 1 KB against a longest-observed run of
  // 812 bytes, so in practice a run arrives whole.
  static constexpr size_t kTextBytes = 1024;

  // All of one tag's attribute names and decoded values, together. Refused above
  // this, because unlike a text run it cannot be split: `attr()` answers about the
  // current tag as a whole. Measured over a real book's 143,119 attributes: the
  // longest single value is 49 bytes and the fattest tag carries 160, so this is
  // 3x the observed worst case.
  static constexpr size_t kMaxAttrBytes = 512;

  // How much of the source is held at once. Only ever a few bytes are examined --
  // `<![CDATA[` is the longest thing needing lookahead -- so this is about how
  // often the source is asked, not about what the parser can see.
  static constexpr size_t kInputBytes = 512;

  explicit Xml(ByteSource& src);

  // A whole document already in memory. Holds a BufferSource internally, so the
  // bytes must outlive the parser.
  explicit Xml(std::string_view doc);

  Xml(const Xml&) = delete;
  Xml& operator=(const Xml&) = delete;

  // Points the parser at a new source and forgets everything about the old one.
  // For re-reading a chapter from its beginning, which is what a backward page turn
  // costs on a stream that cannot be seeked -- and it reuses the buffers rather
  // than constructing a second parser.
  void restart(ByteSource& src);

  // Advances. Every call returns exactly one node; a self-closing element yields
  // a StartTag and then an EndTag, so a caller's stack balances without it having
  // to know the tag was self-closing.
  Node next();

  // Valid after StartTag and EndTag, until the next `next()`. Prefix stripped.
  std::string_view name() const { return {nameBuf_, nameLen_}; }

  // Valid after Text, entity-decoded, until the next `next()`.
  std::string_view text() const { return {textBuf_, textLen_}; }

  // The attribute's value on the current StartTag, or an empty view if absent --
  // and `hasAttr` tells an absent attribute from an empty one, which matters for
  // `<item properties=""/>`.
  std::string_view attr(std::string_view attrName) const;
  bool hasAttr(std::string_view attrName) const;
  size_t attrCount() const { return attrCount_; }

  // How many bytes of the source have been consumed, and why parsing stopped. A
  // byte offset rather than a line: this reads machine-written XML, and an offset
  // is what a hex dump needs. Against a stream it is the offset in the
  // DECOMPRESSED document, which is also what a page index would key on.
  size_t offset() const { return consumed_ - avail(); }
  const char* error() const { return error_; }

 private:
  Node fail(const char* why);

  // --- The character reader ---------------------------------------------------
  // `ensure(n)` makes up to n bytes visible, compacting and refilling as needed,
  // and returns how many there really are -- fewer than asked only at the end of
  // the source. `at(i)` reads one of them; `bump(n)` consumes.
  size_t ensure(size_t n);
  size_t avail() const { return inLen_ - inAt_; }
  char at(size_t i) const { return in_[inAt_ + i]; }
  void bump(size_t n) { inAt_ += n; }
  bool matches(const char* lit, size_t n);

  void skipSpace();
  bool parseName(char* out, size_t& outLen);
  bool skipUntil(const char* lit, size_t n);
  // Reads one entity reference, already positioned on '&', into `out`.
  bool decodeEntity(char* out, size_t cap, size_t& outLen);

  ByteSource* src_;
  BufferSource own_;  // backs the string_view constructor; unused otherwise

  char in_[kInputBytes];
  size_t inLen_ = 0, inAt_ = 0;
  size_t consumed_ = 0;  // bytes taken FROM the source, including those still held
  bool sourceEnded_ = false;

  char nameBuf_[kMaxNameBytes];
  size_t nameLen_ = 0;
  char textBuf_[kTextBytes];
  size_t textLen_ = 0;

  // A self-closing tag owes an EndTag, which the next call pays. A COPY, not a
  // view: the name buffer is reused by whatever the next call parses.
  char pendingEnd_[kMaxNameBytes];
  size_t pendingEndLen_ = 0;
  bool endPending_ = false;

  // One tag's attributes, names and values packed into a single buffer and
  // addressed by offset, so a tag costs no allocation per attribute.
  char attrBuf_[kMaxAttrBytes];
  size_t attrUsed_ = 0;
  struct Attr {
    uint16_t nameAt, nameLen, valueAt, valueLen;
  };
  Attr attrs_[kMaxAttrs];
  size_t attrCount_ = 0;

  const char* error_ = "";
};

}  // namespace reader
