#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace reader {

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
// ONE ALLOCATION, NOT ONE PER NODE. The parser borrows the caller's buffer and
// returns views into it, except where a value has to be entity-decoded -- those
// land in two internal buffers that are reused for every token. So a chapter is
// the caller's one string plus a few hundred bytes, whatever its node count.
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

  explicit Xml(std::string_view doc);

  // Advances. Every call returns exactly one node; a self-closing element yields
  // a StartTag and then an EndTag, so a caller's stack balances without it having
  // to know the tag was self-closing.
  Node next();

  // Valid after StartTag and EndTag. Prefix already stripped.
  std::string_view name() const { return name_; }

  // Valid after Text, entity-decoded. A view into the parser's own buffer, so it
  // lives only until the next `next()`.
  std::string_view text() const { return text_; }

  // The attribute's value on the current StartTag, or an empty view if absent --
  // and `hasAttr` tells an absent attribute from an empty one, which matters for
  // `<item properties=""/>`.
  std::string_view attr(std::string_view attrName) const;
  bool hasAttr(std::string_view attrName) const;
  size_t attrCount() const { return attrCount_; }

  // Where parsing stopped, and why. A byte offset rather than a line: this reads
  // machine-written XML, and an offset is what a hex dump needs.
  size_t offset() const { return at_; }
  const char* error() const { return error_; }

 private:
  Node fail(const char* why);
  bool decodeInto(std::string_view raw, std::string& out);
  bool parseName(std::string_view& out);
  void skipSpace();

  std::string_view doc_;
  size_t at_ = 0;

  std::string_view name_;
  std::string_view text_;
  std::string textBuf_;  // reused: entity-decoded text
  std::string attrBuf_;  // reused: entity-decoded attribute values

  struct Attr {
    std::string_view name;
    size_t valueAt = 0;  // into attrBuf_
    size_t valueLen = 0;
  };
  Attr attrs_[kMaxAttrs];
  size_t attrCount_ = 0;

  // A self-closing tag owes an EndTag, which the next call pays.
  std::string_view pendingEnd_;
  bool endPending_ = false;

  const char* error_ = "";
};

}  // namespace reader
