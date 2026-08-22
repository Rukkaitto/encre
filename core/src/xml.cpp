#include "reader/xml.h"

#include <cstdint>

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
void appendUtf8(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

Xml::Xml(std::string_view doc) : doc_(doc) {
  // A BOM is not content. Skipping it here rather than in every caller is the
  // difference between one line and a class of "the first tag did not match" bug.
  constexpr std::string_view kBom = "\xEF\xBB\xBF";
  if (doc_.size() >= kBom.size() && doc_.substr(0, kBom.size()) == kBom) at_ = kBom.size();
}

Xml::Node Xml::fail(const char* why) {
  error_ = why;
  return Node::Error;
}

void Xml::skipSpace() {
  while (at_ < doc_.size() && isSpace(doc_[at_])) ++at_;
}

bool Xml::parseName(std::string_view& out) {
  const size_t start = at_;
  while (at_ < doc_.size() && isNameChar(doc_[at_])) ++at_;
  if (at_ == start) return false;
  if (at_ - start > kMaxNameBytes) return false;
  std::string_view name = doc_.substr(start, at_ - start);
  // THE PREFIX IS DROPPED, not resolved -- see the header. `dc:title` is `title`.
  const size_t colon = name.rfind(':');
  if (colon != std::string_view::npos) name = name.substr(colon + 1);
  if (name.empty()) return false;  // a bare ":" is not a name
  out = name;
  return true;
}

bool Xml::decodeInto(std::string_view raw, std::string& out) {
  out.clear();
  for (size_t i = 0; i < raw.size();) {
    if (raw[i] != '&') {
      out.push_back(raw[i++]);
      continue;
    }
    const size_t semi = raw.find(';', i + 1);
    // AN UNTERMINATED OR UNKNOWN ENTITY IS MALFORMED, not passed through. A
    // literal "&nbsp;" surviving into a paragraph reads as a rendering bug and is
    // really a parsing one, and the file is well-formed XML by specification --
    // so an entity we do not know means we are wrong about the file, not that the
    // file is being casual.
    if (semi == std::string_view::npos) return false;
    const std::string_view ref = raw.substr(i + 1, semi - i - 1);
    if (ref.empty()) return false;

    if (ref == "amp") {
      out.push_back('&');
    } else if (ref == "lt") {
      out.push_back('<');
    } else if (ref == "gt") {
      out.push_back('>');
    } else if (ref == "quot") {
      out.push_back('"');
    } else if (ref == "apos") {
      out.push_back('\'');
    } else if (ref[0] == '#') {
      const bool hex = ref.size() > 1 && (ref[1] == 'x' || ref[1] == 'X');
      const std::string_view digits = ref.substr(hex ? 2 : 1);
      if (digits.empty()) return false;
      uint32_t cp = 0;
      for (char c : digits) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return false;
        cp = cp * static_cast<uint32_t>(hex ? 16 : 10) + static_cast<uint32_t>(v);
        if (cp > 0x10FFFF) return false;  // past the last code point there is
      }
      // Surrogates are not characters, and a file naming one is describing
      // something that cannot be encoded as UTF-8.
      if (cp >= 0xD800 && cp <= 0xDFFF) return false;
      if (cp == 0) return false;
      appendUtf8(cp, out);
    } else {
      return false;
    }
    i = semi + 1;
  }
  return true;
}

Xml::Node Xml::next() {
  // A self-closing tag owed an EndTag; pay it before reading anything more.
  if (endPending_) {
    endPending_ = false;
    name_ = pendingEnd_;
    return Node::EndTag;
  }

  for (;;) {
    if (at_ >= doc_.size()) return Node::Eof;

    if (doc_[at_] != '<') {
      // TEXT, up to the next '<'. Whitespace between elements is text and is kept:
      // `<em>a</em> <em>b</em>` has a space that is part of the sentence, and a
      // parser that dropped it would join words.
      const size_t start = at_;
      while (at_ < doc_.size() && doc_[at_] != '<') ++at_;
      if (!decodeInto(doc_.substr(start, at_ - start), textBuf_))
        return fail("a text run contains an entity we do not know");
      text_ = textBuf_;
      return Node::Text;
    }

    // Everything that begins '<!' or '<?' carries no content a book needs.
    if (doc_.compare(at_, 4, "<!--") == 0) {
      const size_t end = doc_.find("-->", at_ + 4);
      if (end == std::string_view::npos) return fail("an unterminated comment");
      at_ = end + 3;
      continue;
    }
    if (doc_.compare(at_, 9, "<![CDATA[") == 0) {
      const size_t end = doc_.find("]]>", at_ + 9);
      if (end == std::string_view::npos) return fail("an unterminated CDATA section");
      // UNDECODED, which is its whole purpose: it holds characters that would
      // otherwise be markup.
      textBuf_.assign(doc_.substr(at_ + 9, end - (at_ + 9)));
      text_ = textBuf_;
      at_ = end + 3;
      return Node::Text;
    }
    if (doc_.compare(at_, 2, "<?") == 0) {
      const size_t end = doc_.find("?>", at_ + 2);
      if (end == std::string_view::npos) return fail("an unterminated processing instruction");
      at_ = end + 2;
      continue;
    }
    if (doc_.compare(at_, 2, "<!") == 0) {
      // A DOCTYPE, which may carry a bracketed internal subset. Refusing one
      // would refuse most real EPUBs.
      size_t depth = 0;
      size_t i = at_ + 2;
      for (; i < doc_.size(); ++i) {
        if (doc_[i] == '[') ++depth;
        else if (doc_[i] == ']') { if (depth > 0) --depth; }
        else if (doc_[i] == '>' && depth == 0) break;
      }
      if (i >= doc_.size()) return fail("an unterminated declaration");
      at_ = i + 1;
      continue;
    }

    // A closing tag.
    if (doc_.compare(at_, 2, "</") == 0) {
      at_ += 2;
      if (!parseName(name_)) return fail("a closing tag with no name");
      skipSpace();
      if (at_ >= doc_.size() || doc_[at_] != '>') return fail("a closing tag that never closes");
      ++at_;
      return Node::EndTag;
    }

    // An opening tag.
    ++at_;
    if (!parseName(name_)) return fail("an opening tag with no name");
    attrCount_ = 0;
    attrBuf_.clear();

    for (;;) {
      skipSpace();
      if (at_ >= doc_.size()) return fail("a tag that never closes");
      if (doc_[at_] == '>') {
        ++at_;
        return Node::StartTag;
      }
      if (doc_.compare(at_, 2, "/>") == 0) {
        at_ += 2;
        // Owe an EndTag, so the caller's stack balances -- see the header.
        pendingEnd_ = name_;
        endPending_ = true;
        return Node::StartTag;
      }

      std::string_view attrName;
      if (!parseName(attrName)) return fail("a tag attribute with no name");
      skipSpace();
      // NO BARE ATTRIBUTES. HTML permits `<input disabled>`; XML does not, and
      // accepting it here would be guessing at a value.
      if (at_ >= doc_.size() || doc_[at_] != '=') return fail("an attribute with no value");
      ++at_;
      skipSpace();
      if (at_ >= doc_.size() || (doc_[at_] != '"' && doc_[at_] != '\''))
        return fail("an attribute value that is not quoted");
      const char quote = doc_[at_++];
      const size_t vs = at_;
      while (at_ < doc_.size() && doc_[at_] != quote) ++at_;
      if (at_ >= doc_.size()) return fail("an unterminated attribute value");
      const std::string_view rawValue = doc_.substr(vs, at_ - vs);
      ++at_;  // the closing quote

      if (attrCount_ >= kMaxAttrs) return fail("more attributes on one tag than we will read");
      std::string decoded;
      if (!decodeInto(rawValue, decoded))
        return fail("an attribute value contains an entity we do not know");
      // Appended to one reused buffer and addressed by offset, so a tag's
      // attributes cost no allocation per attribute.
      attrs_[attrCount_].name = attrName;
      attrs_[attrCount_].valueAt = attrBuf_.size();
      attrs_[attrCount_].valueLen = decoded.size();
      attrBuf_ += decoded;
      ++attrCount_;
    }
  }
}

bool Xml::hasAttr(std::string_view attrName) const {
  for (size_t i = 0; i < attrCount_; ++i)
    if (attrs_[i].name == attrName) return true;
  return false;
}

std::string_view Xml::attr(std::string_view attrName) const {
  for (size_t i = 0; i < attrCount_; ++i)
    if (attrs_[i].name == attrName)
      return std::string_view(attrBuf_).substr(attrs_[i].valueAt, attrs_[i].valueLen);
  return {};
}

}  // namespace reader
