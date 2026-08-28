#include "reader/xml.h"

#include <cstdint>
#include <cstring>

#include "entity_table.h"

namespace reader {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// XML name characters, narrowed to what an EPUB actually uses. Deliberately not
// the specification's full Unicode NameStartChar set: EPUB's vocabulary is ASCII,
// and a table for the rest would be machinery no caller can reach.
bool isNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '_' || c == '-' || c == '.' || c == ':';
}

// One code point as UTF-8. Numeric character references are the only place this
// parser creates bytes rather than copying them.
size_t appendUtf8(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

// The longest entity this parser accepts, plus room: "&#x10FFFF;" is 10.
constexpr size_t kMaxEntityBytes = 16;

}  // namespace

size_t BufferSource::read(void* dst, size_t bytes) {
  const size_t got = b_.size() - at_ < bytes ? b_.size() - at_ : bytes;
  std::memcpy(dst, b_.data() + at_, got);
  at_ += got;
  return got;
}

Xml::Xml(ByteSource& src) : src_(&src), own_(std::string_view{}) {}

Xml::Xml(std::string_view doc) : src_(nullptr), own_(doc) { src_ = &own_; }

void Xml::restart(ByteSource& src) {
  src_ = &src;
  inLen_ = inAt_ = 0;
  consumed_ = 0;
  sourceEnded_ = false;
  nameLen_ = textLen_ = 0;
  pendingEndLen_ = 0;
  endPending_ = false;
  attrUsed_ = attrCount_ = 0;
  error_ = "";
}

Xml::Node Xml::fail(const char* why) {
  error_ = why;
  return Node::Error;
}

size_t Xml::ensure(size_t n) {
  if (n > kInputBytes) n = kInputBytes;
  if (avail() >= n) return avail();
  // COMPACT, then refill. The unread bytes move to the front so a lookahead that
  // straddles the buffer's end can still be satisfied -- which is the whole reason
  // the tokenizer can ask for nine bytes of `<![CDATA[` without caring where the
  // source's reads happened to land.
  if (inAt_ > 0) {
    std::memmove(in_, in_ + inAt_, avail());
    inLen_ = avail();
    inAt_ = 0;
  }
  while (inLen_ < n && !sourceEnded_) {
    const size_t got = src_->read(in_ + inLen_, kInputBytes - inLen_);
    if (got == 0) {
      sourceEnded_ = true;
      break;
    }
    inLen_ += got;
    consumed_ += got;
  }
  return avail();
}

bool Xml::matches(const char* lit, size_t n) {
  if (ensure(n) < n) return false;
  for (size_t i = 0; i < n; ++i)
    if (at(i) != lit[i]) return false;
  return true;
}

void Xml::skipSpace() {
  while (ensure(1) >= 1 && isSpace(at(0))) bump(1);
}

// Scans forward until `lit` is consumed. Used for the constructs this parser skips
// wholesale -- comments, processing instructions, CDATA's terminator -- where the
// content between is not wanted. One byte at a time on purpose: a terminator can
// straddle any refill, and `ensure` is what makes that invisible.
bool Xml::skipUntil(const char* lit, size_t n) {
  for (;;) {
    if (ensure(n) < n) return false;
    if (matches(lit, n)) {
      bump(n);
      return true;
    }
    bump(1);
  }
}

bool Xml::parseName(char* out, size_t& outLen) {
  size_t len = 0;
  while (ensure(1) >= 1 && isNameChar(at(0))) {
    if (len >= kMaxNameBytes) return false;
    out[len++] = at(0);
    bump(1);
  }
  if (len == 0) return false;
  // THE PREFIX IS DROPPED, not resolved -- see the header. `dc:title` is `title`.
  size_t start = 0;
  for (size_t i = len; i > 0; --i) {
    if (out[i - 1] == ':') {
      start = i;
      break;
    }
  }
  if (start > 0) {
    const size_t kept = len - start;
    if (kept == 0) return false;  // a name ending in ':' is not a name
    std::memmove(out, out + start, kept);
    len = kept;
  }
  outLen = len;
  return true;
}

// Positioned on '&'. Decodes one reference into `out`, or fails.
//
// AN UNKNOWN ENTITY IS PASSED THROUGH, AND THAT REVERSES WHAT THIS SAID.
//
// It said a literal "&nbsp;" reaching a paragraph "reads as a rendering bug and is
// really a parsing one", and that an unknown entity means "we are wrong about the
// file, not that the file is being casual". The first half was right and the table
// below is what acts on it -- 252 names, generated, so `&nbsp;` and `&rsquo;` are no
// longer unknown.
//
// The second half was measured and is false. Erroring here does not REPORT anything:
// document.cpp stops on Node::Error and ChapterReader::next() then returns false,
// which is INDISTINGUISHABLE from the chapter ending. Across sixteen real books that
// cost `Dark Plagueis` 177 of its 183 chapters -- a book that opens, and is empty.
//
// So a visible wrong beats an invisible one, which is the call css.h already makes
// for over-matched italics. A stray "&unknown;" on the page is a typographic error a
// reader can see and report; a discarded chapter is not.
//
// EVERY EXIT THAT IS NOT A DECODED CHARACTER IS NOW TEXT. There are three ways to
// fail -- the reference never terminates, the name is not in the table, the numeric
// form does not parse -- and all three emit the bytes the document actually held.
// The only remaining `false` is "the output buffer cannot hold them", which the
// callers make unreachable by reserving kMaxEntityBytes + 2.
bool Xml::decodeEntity(char* out, size_t cap, size_t& outLen) {
  char ref[kMaxEntityBytes];
  size_t refLen = 0;
  bump(1);  // '&'
  bool terminated = false;
  for (;;) {
    if (ensure(1) < 1) break;  // the source ended inside a reference
    const char c = at(0);
    if (c == ';') {
      bump(1);
      terminated = true;
      break;
    }
    // A reference holds name characters or a numeric form. Anything else -- a space,
    // a '<' -- means this '&' was never a reference at all, which is what "Tom &
    // Jerry" is, and real books are full of them. The character is NOT consumed, so
    // it is read as ordinary text next.
    if (!isNameChar(c) && c != '#') break;
    if (refLen >= sizeof(ref)) break;  // longer than any entity we accept
    ref[refLen++] = c;
    bump(1);
  }

  // What the document held, for any of the three failures: '&', the reference, and
  // the ';' if there was one.
  const size_t rawLen = refLen + (terminated ? 2 : 1);
  if (rawLen > cap) return false;
  const auto passThrough = [&]() {
    out[0] = '&';
    std::memcpy(out + 1, ref, refLen);
    if (terminated) out[refLen + 1] = ';';
    outLen = rawLen;
    return true;
  };

  if (!terminated || refLen == 0) return passThrough();

  const std::string_view r(ref, refLen);
  if (r == "amp") { out[0] = '&'; outLen = 1; return true; }
  if (r == "lt") { out[0] = '<'; outLen = 1; return true; }
  if (r == "gt") { out[0] = '>'; outLen = 1; return true; }
  if (r == "quot") { out[0] = '"'; outLen = 1; return true; }
  if (r == "apos") { out[0] = '\''; outLen = 1; return true; }

  if (r[0] != '#') {
    // The generated HTML 4 table, binary-searched. std::lower_bound would need
    // <algorithm> for four lines that are clearer written out.
    size_t lo = 0, hi = kNamedEntityCount;
    while (lo < hi) {
      const size_t mid = lo + (hi - lo) / 2;
      const int cmp = r.compare(kNamedEntities[mid].name);
      if (cmp == 0) {
        if (cap < 4) return false;
        outLen = appendUtf8(kNamedEntities[mid].cp, out);
        return true;
      }
      if (cmp < 0) hi = mid;
      else lo = mid + 1;
    }
    return passThrough();
  }

  const bool hex = refLen > 1 && (r[1] == 'x' || r[1] == 'X');
  const std::string_view digits = r.substr(hex ? 2 : 1);
  if (digits.empty()) return passThrough();
  uint32_t cp = 0;
  for (const char c : digits) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return passThrough();
    cp = cp * static_cast<uint32_t>(hex ? 16 : 10) + static_cast<uint32_t>(v);
    if (cp > 0x10FFFF) return passThrough();  // past the last code point there is
  }
  // Surrogates are not characters, and a file naming one is describing something
  // that cannot be encoded as UTF-8.
  if (cp >= 0xD800 && cp <= 0xDFFF) return passThrough();
  if (cp == 0) return passThrough();
  if (cap < 4) return false;
  outLen = appendUtf8(cp, out);
  return true;
}

Xml::Node Xml::next() {
  // A self-closing tag owed an EndTag; pay it before reading anything more.
  if (endPending_) {
    endPending_ = false;
    std::memcpy(nameBuf_, pendingEnd_, pendingEndLen_);
    nameLen_ = pendingEndLen_;
    return Node::EndTag;
  }

  // A BOM is not content. Skipped on the first call rather than in every caller,
  // which is the difference between one line and a class of "the first tag did not
  // match" bug. It can only appear at offset 0, so this is checked once.
  if (consumed_ == 0 && offset() == 0 && matches("\xEF\xBB\xBF", 3)) bump(3);

  for (;;) {
    if (ensure(1) < 1) return Node::Eof;

    if (at(0) != '<') {
      // TEXT, up to the next '<' or the buffer's capacity. Whitespace between
      // elements is text and is kept: `<em>a</em> <em>b</em>` has a space that is
      // part of the sentence, and a parser that dropped it would join words.
      textLen_ = 0;
      while (ensure(1) >= 1 && at(0) != '<') {
        // Stop with room for the longest single decoded character, so a reference
        // is never split across two nodes.
        // kMaxEntityBytes + 2 because a passthrough writes '&' + the reference
        // + ';', which is longer than any character it could have decoded to.
        if (textLen_ + kMaxEntityBytes + 2 > kTextBytes) break;
        if (at(0) == '&') {
          size_t n = 0;
          if (!decodeEntity(textBuf_ + textLen_, kTextBytes - textLen_, n))
            return fail("a text run contains an entity we do not know");
          textLen_ += n;
          continue;
        }
        textBuf_[textLen_++] = at(0);
        bump(1);
      }
      // A run of nothing cannot happen: the loop above is entered only with a
      // non-'<' byte available, and every branch consumes at least one.
      return Node::Text;
    }

    // Everything that begins '<!' or '<?' carries no content a book needs.
    if (matches("<!--", 4)) {
      bump(4);
      if (!skipUntil("-->", 3)) return fail("an unterminated comment");
      continue;
    }
    if (matches("<![CDATA[", 9)) {
      bump(9);
      // UNDECODED, which is its whole purpose: it holds characters that would
      // otherwise be markup. Chunked like ordinary text, so a long section is
      // several nodes rather than a refusal.
      textLen_ = 0;
      for (;;) {
        if (ensure(3) < 1) return fail("an unterminated CDATA section");
        if (matches("]]>", 3)) {
          bump(3);
          break;
        }
        if (textLen_ + 1 > kTextBytes) break;
        textBuf_[textLen_++] = at(0);
        bump(1);
      }
      return Node::Text;
    }
    if (matches("<?", 2)) {
      bump(2);
      if (!skipUntil("?>", 2)) return fail("an unterminated processing instruction");
      continue;
    }
    if (matches("<!", 2)) {
      // A DOCTYPE, which may carry a bracketed internal subset. Refusing one would
      // refuse most real EPUBs.
      bump(2);
      size_t depth = 0;
      for (;;) {
        if (ensure(1) < 1) return fail("an unterminated declaration");
        const char c = at(0);
        bump(1);
        if (c == '[') {
          ++depth;
        } else if (c == ']') {
          if (depth > 0) --depth;
        } else if (c == '>' && depth == 0) {
          break;
        }
      }
      continue;
    }

    // A closing tag.
    if (matches("</", 2)) {
      bump(2);
      if (!parseName(nameBuf_, nameLen_)) return fail("a closing tag with no name");
      skipSpace();
      if (ensure(1) < 1 || at(0) != '>') return fail("a closing tag that never closes");
      bump(1);
      return Node::EndTag;
    }

    // An opening tag.
    bump(1);
    if (!parseName(nameBuf_, nameLen_)) return fail("an opening tag with no name");
    attrCount_ = 0;
    attrUsed_ = 0;

    for (;;) {
      skipSpace();
      if (ensure(2) < 1) return fail("a tag that never closes");
      if (at(0) == '>') {
        bump(1);
        return Node::StartTag;
      }
      if (matches("/>", 2)) {
        bump(2);
        // Owe an EndTag, so the caller's stack balances -- see the header.
        std::memcpy(pendingEnd_, nameBuf_, nameLen_);
        pendingEndLen_ = nameLen_;
        endPending_ = true;
        return Node::StartTag;
      }

      if (attrCount_ >= kMaxAttrs) return fail("more attributes on one tag than we will read");

      // The name goes straight into the shared buffer; the value follows it.
      char scratch[kMaxNameBytes];
      size_t scratchLen = 0;
      if (!parseName(scratch, scratchLen)) return fail("a tag attribute with no name");
      if (attrUsed_ + scratchLen > kMaxAttrBytes)
        return fail("an element carries more attribute bytes than we will hold");
      Attr& a = attrs_[attrCount_];
      a.nameAt = static_cast<uint16_t>(attrUsed_);
      a.nameLen = static_cast<uint16_t>(scratchLen);
      std::memcpy(attrBuf_ + attrUsed_, scratch, scratchLen);
      attrUsed_ += scratchLen;

      skipSpace();
      // NO BARE ATTRIBUTES. HTML permits `<input disabled>`; XML does not, and
      // accepting it here would be guessing at a value.
      if (ensure(1) < 1 || at(0) != '=') return fail("an attribute with no value");
      bump(1);
      skipSpace();
      if (ensure(1) < 1 || (at(0) != '"' && at(0) != '\''))
        return fail("an attribute value that is not quoted");
      const char quote = at(0);
      bump(1);

      a.valueAt = static_cast<uint16_t>(attrUsed_);
      size_t valueLen = 0;
      for (;;) {
        if (ensure(1) < 1) return fail("an unterminated attribute value");
        if (at(0) == quote) {
          bump(1);
          break;
        }
        if (attrUsed_ + kMaxEntityBytes + 2 > kMaxAttrBytes)
          return fail("an element carries more attribute bytes than we will hold");
        if (at(0) == '&') {
          size_t n = 0;
          if (!decodeEntity(attrBuf_ + attrUsed_, kMaxAttrBytes - attrUsed_, n))
            return fail("an attribute value contains an entity we do not know");
          attrUsed_ += n;
          valueLen += n;
          continue;
        }
        attrBuf_[attrUsed_++] = at(0);
        ++valueLen;
        bump(1);
      }
      a.valueLen = static_cast<uint16_t>(valueLen);
      ++attrCount_;
    }
  }
}

bool Xml::hasAttr(std::string_view attrName) const {
  for (size_t i = 0; i < attrCount_; ++i)
    if (std::string_view(attrBuf_ + attrs_[i].nameAt, attrs_[i].nameLen) == attrName) return true;
  return false;
}

std::string_view Xml::attr(std::string_view attrName) const {
  for (size_t i = 0; i < attrCount_; ++i)
    if (std::string_view(attrBuf_ + attrs_[i].nameAt, attrs_[i].nameLen) == attrName)
      return std::string_view(attrBuf_ + attrs_[i].valueAt, attrs_[i].valueLen);
  return {};
}

}  // namespace reader
