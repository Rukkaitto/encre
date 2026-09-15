#include "reader/json_stream.h"

namespace reader {
namespace {

bool isSpace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Append one codepoint as UTF-8, and say whether it fitted. THE CAP IS CHECKED
// PER CODEPOINT, not per byte, which is what "truncated at a UTF-8 boundary"
// means: a cut mid-sequence would leave a byte the font layer cannot draw and
// that upperLatin1 would read as the wrong thing.
bool appendUtf8(std::string& out, uint32_t cp, size_t cap) {
  char buf[4];
  int n = 0;
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    n = 1;
  } else if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    buf[0] = static_cast<char>(0xF0 | (cp >> 18));
    buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    n = 4;
  }
  if (out.size() + static_cast<size_t>(n) > cap) return false;
  out.append(buf, static_cast<size_t>(n));
  return true;
}

bool hexNibble(int c, uint32_t& out) {
  if (c >= '0' && c <= '9') { out = static_cast<uint32_t>(c - '0'); return true; }
  if (c >= 'a' && c <= 'f') { out = static_cast<uint32_t>(c - 'a' + 10); return true; }
  if (c >= 'A' && c <= 'F') { out = static_cast<uint32_t>(c - 'A' + 10); return true; }
  return false;
}

}  // namespace

int JsonScanner::get() {
  if (pushed_ >= 0) {
    const int c = pushed_;
    pushed_ = -1;
    return c;
  }
  uint8_t b = 0;
  if (src_.read(&b, 1) != 1) return -1;
  return b;
}

int JsonScanner::peekNonSpace() {
  int c = get();
  while (c >= 0 && isSpace(c)) c = get();
  return c;
}

bool JsonScanner::expect(char c) {
  const int got = peekNonSpace();
  if (got != static_cast<unsigned char>(c)) {
    pushed_ = got;
    return false;
  }
  return true;
}

JsonScanner::Token JsonScanner::fail(const char* why) {
  failed_ = true;
  if (error_.empty()) error_ = why;
  return Token::Error;
}

bool JsonScanner::readString(std::string& out) {
  out.clear();
  truncated_ = false;
  for (;;) {
    const int c = get();
    if (c < 0) return false;
    if (c == '"') return true;
    if (c != '\\') {
      // A RAW BYTE PASSES THROUGH, UTF-8 included: the server's own encoding is
      // what the fonts are subset for, and re-encoding it here would be a second
      // opinion about bytes that are already right.
      if (out.size() + 1 > kJsonStreamMaxStringBytes) {
        // TRUNCATED, NOT AN ERROR, and the rest of the string is consumed so the
        // scan stays in step -- a long title must not cost the page.
        if (!truncated_) {
          truncated_ = true;
          ++truncations_;
        }
        continue;
      }
      out.push_back(static_cast<char>(c));
      continue;
    }
    const int e = get();
    if (e < 0) return false;
    uint32_t cp = 0;
    switch (e) {
      case '"': cp = '"'; break;
      case '\\': cp = '\\'; break;
      case '/': cp = '/'; break;
      case 'b': cp = '\b'; break;
      case 'f': cp = '\f'; break;
      case 'n': cp = '\n'; break;
      case 'r': cp = '\r'; break;
      case 't': cp = '\t'; break;
      case 'u': {
        // `\uXXXX`, AND SURROGATE PAIRS. wallabag serialises non-ASCII titles
        // this way, and a title the subset cannot draw is a notdef box -- so
        // this has to become the real codepoint rather than pass through.
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
          uint32_t nib = 0;
          const int h = get();
          if (h < 0 || !hexNibble(h, nib)) return false;
          v = (v << 4) | nib;
        }
        if (v >= 0xD800 && v <= 0xDBFF) {
          // A HIGH SURROGATE MUST BE FOLLOWED BY ITS LOW ONE. An unpaired one is
          // not a codepoint, and emitting it as UTF-8 would be emitting bytes no
          // decoder accepts.
          if (get() != '\\') return false;
          if (get() != 'u') return false;
          uint32_t lo = 0;
          for (int i = 0; i < 4; ++i) {
            uint32_t nib = 0;
            const int h = get();
            if (h < 0 || !hexNibble(h, nib)) return false;
            lo = (lo << 4) | nib;
          }
          if (lo < 0xDC00 || lo > 0xDFFF) return false;
          v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
        } else if (v >= 0xDC00 && v <= 0xDFFF) {
          return false;  // a low surrogate with nothing in front of it
        }
        cp = v;
        break;
      }
      default:
        return false;
    }
    if (!appendUtf8(out, cp, kJsonStreamMaxStringBytes)) {
      if (!truncated_) {
        truncated_ = true;
        ++truncations_;
      }
    }
  }
}

bool JsonScanner::readAtom(const char* word) {
  for (const char* p = word + 1; *p != '\0'; ++p)
    if (get() != static_cast<unsigned char>(*p)) return false;
  return true;
}

bool JsonScanner::readNumber(int c) {
  text_.clear();
  bool neg = false;
  if (c == '-') {
    neg = true;
    text_.push_back('-');
    c = get();
  }
  if (c < '0' || c > '9') return false;
  int64_t v = 0;
  bool overflow = false;
  while (c >= '0' && c <= '9') {
    text_.push_back(static_cast<char>(c));
    if (!overflow) {
      if (v > (9223372036854775807LL - (c - '0')) / 10) overflow = true;
      else v = v * 10 + (c - '0');
    }
    c = get();
  }
  // A FRACTION OR AN EXPONENT IS KEPT IN `text_` AND DROPPED FROM `number()`.
  // Nothing this reads is fractional -- an id and a reading time are integers --
  // but refusing a `1.0` the server is entitled to send would lose the page.
  if (c == '.' || c == 'e' || c == 'E') {
    while (c >= 0 && (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' ||
                      (c >= '0' && c <= '9'))) {
      text_.push_back(static_cast<char>(c));
      c = get();
    }
  }
  // A number ends on a delimiter it does NOT consume, which is the one place the
  // pushback is needed.
  pushed_ = c;
  num_ = overflow ? 0 : (neg ? -v : v);
  return true;
}

JsonScanner::Token JsonScanner::next() {
  // NOTHING IS READ AFTER End OR Error, which is the contract that keeps a
  // malformed document from being walked twice.
  if (failed_) return Token::Error;
  if (ended_) return Token::End;

  int c = peekNonSpace();
  if (c < 0) {
    if (depth_ != 0) return fail("input ended inside a value");
    ended_ = true;
    return Token::End;
  }

  // A separator before a value carries no meaning this scanner has to report --
  // the shape is already known from the container.
  while (c == ',' || c == ':') c = peekNonSpace();
  if (c < 0) {
    if (depth_ != 0) return fail("input ended after a separator");
    ended_ = true;
    return Token::End;
  }

  // A CLOSE WHERE A VALUE WAS OWED is a key with nothing after it.
  if (expectValue_ && (c == '}' || c == ']')) return fail("a key with no value");
  expectValue_ = false;

  switch (c) {
    case '{':
      if (depth_ >= kJsonMaxDepth) return fail("nested too deep");
      inObject_[depth_] = true;
      ++depth_;
      wantKey_ = true;
      return Token::ObjectStart;
    case '[':
      if (depth_ >= kJsonMaxDepth) return fail("nested too deep");
      inObject_[depth_] = false;
      ++depth_;
      wantKey_ = false;
      return Token::ArrayStart;
    case '}':
      if (depth_ <= 0 || !inObject_[depth_ - 1]) return fail("unbalanced }");
      --depth_;
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::ObjectEnd;
    case ']':
      if (depth_ <= 0 || inObject_[depth_ - 1]) return fail("unbalanced ]");
      --depth_;
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::ArrayEnd;
    case '"': {
      const bool isKey = wantKey_;
      std::string& into = isKey ? key_ : text_;
      if (!readString(into)) return fail("a string did not end");
      if (isKey) {
        wantKey_ = false;
        expectValue_ = true;
        return Token::Key;
      }
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::String;
    }
    case 't':
      if (!readAtom("true")) return fail("expected true");
      bool_ = true;
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::Bool;
    case 'f':
      if (!readAtom("false")) return fail("expected false");
      bool_ = false;
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::Bool;
    case 'n':
      if (!readAtom("null")) return fail("expected null");
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::Null;
    default:
      if (!readNumber(c)) return fail("not a number");
      wantKey_ = depth_ > 0 && inObject_[depth_ - 1];
      return Token::Number;
  }
}

bool JsonScanner::skipValue() {
  const Token t = next();
  switch (t) {
    case Token::Error:
    case Token::End:
      return false;
    case Token::ObjectStart:
    case Token::ArrayStart: {
      // COUNTED RATHER THAN RECURSED, because the depth cap is the scanner's and
      // a recursive skip would put a second bound on the C++ stack -- which is
      // the budget this project already blew once on stb's inflate.
      int want = 1;
      while (want > 0) {
        const Token u = next();
        if (u == Token::Error || u == Token::End) return false;
        if (u == Token::ObjectStart || u == Token::ArrayStart) ++want;
        if (u == Token::ObjectEnd || u == Token::ArrayEnd) --want;
      }
      return true;
    }
    default:
      return true;  // a scalar is one token
  }
}

}  // namespace reader
