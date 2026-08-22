#include "reader/json.h"

#include <utility>
#include <vector>

namespace reader {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// What the scanner hands back. Deliberately not JsonObject's own private Value:
// the parser builds a plain list of pairs and JsonObject::parse applies them
// through the public setters, which is also what makes last-wins fall out for a
// duplicated key without the parser knowing the rule.
struct Parsed {
  enum class Kind { Int, Bool, String } kind = Kind::Int;
  int64_t i = 0;
  bool b = false;
  std::string s;
};
using ParsedPairs = std::vector<std::pair<std::string, Parsed>>;

// A recursive-descent scanner over the subset json.h documents. It never
// recurses (there is no nesting in the grammar) and it never reads past the
// end, which is what makes a truncated file a refusal rather than a crash: every
// read goes through eof()/peek().
class Parser {
 public:
  explicit Parser(std::string_view text) : t_(text) {}

  bool parseObject(ParsedPairs& out) {
    skipWs();
    if (!take('{')) return false;
    skipWs();
    if (take('}')) {
      skipWs();
      return eof();  // an empty object, and nothing after it
    }
    for (;;) {
      std::string key;
      if (!parseString(key)) return false;
      skipWs();
      if (!take(':')) return false;
      skipWs();
      Parsed v;
      if (!parseValue(v)) return false;
      // Bounded here rather than at the caller because this is the line that
      // allocates. Counting PAIRS and not distinct keys is deliberate: last-wins
      // means a file of one repeated key has size() == 1 while having allocated
      // every one of them.
      if (out.size() >= kJsonMaxPairs) return false;
      out.emplace_back(std::move(key), std::move(v));
      skipWs();
      if (take(',')) {
        skipWs();
        continue;  // a '}' here is a trailing comma, and parseString rejects it
      }
      if (take('}')) {
        skipWs();
        return eof();  // trailing junk after the object is malformed
      }
      return false;
    }
  }

 private:
  bool eof() const { return i_ >= t_.size(); }
  char peek() const { return t_[i_]; }

  bool take(char c) {
    if (eof() || peek() != c) return false;
    ++i_;
    return true;
  }

  bool takeWord(std::string_view word) {
    if (t_.size() - i_ < word.size()) return false;
    if (t_.substr(i_, word.size()) != word) return false;
    i_ += word.size();
    return true;
  }

  void skipWs() {
    while (!eof()) {
      const char c = peek();
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
      ++i_;
    }
  }

  bool parseString(std::string& out) {
    if (!take('"')) return false;
    out.clear();
    while (!eof()) {
      const char c = t_[i_++];
      if (c == '"') return true;
      if (c != '\\') {
        // A raw control byte is accepted, which strict JSON forbids: dump()
        // emits them raw for everything outside the five shorthands, so
        // refusing them here would break the round trip.
        if (out.size() >= kJsonMaxStringBytes) return false;
        out.push_back(c);
        continue;
      }
      if (eof()) return false;  // a trailing backslash
      // The escaped path allocates too, and each case below appends exactly one
      // byte. Checking here rather than only on the literal path is what makes
      // the cap DECODED bytes: "\n\n\n..." is two input bytes per output byte,
      // so a cap enforced on input would admit twice the limit.
      if (out.size() >= kJsonMaxStringBytes) return false;
      switch (t_[i_++]) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        // \uXXXX included: an escape this subset does not implement makes the
        // file malformed rather than half-decoded.
        default: return false;
      }
    }
    return false;  // unterminated
  }

  bool parseNumber(int64_t& out) {
    const bool neg = take('-');
    if (eof() || !isDigit(peek())) return false;
    // No leading zeros, as JSON requires: "01" is malformed, "0" and "-0" are
    // not.
    if (peek() == '0' && i_ + 1 < t_.size() && isDigit(t_[i_ + 1])) return false;

    // The magnitude accumulates unsigned so INT64_MIN is representable, and an
    // overflow is a refusal rather than a saturation: a clamped value read back
    // as if it were the file's would be a lie.
    const uint64_t limit = neg ? 9223372036854775808ull : 9223372036854775807ull;
    uint64_t mag = 0;
    while (!eof() && isDigit(peek())) {
      const uint64_t d = static_cast<uint64_t>(peek() - '0');
      if (mag > (limit - d) / 10) return false;
      mag = mag * 10 + d;
      ++i_;
    }
    // A '.', 'e' or 'E' is left unconsumed on purpose: the caller's delimiter
    // check then rejects the whole file, which is what this subset promises for
    // a fraction or an exponent.
    out = neg ? static_cast<int64_t>(~mag + 1ull) : static_cast<int64_t>(mag);
    return true;
  }

  bool parseValue(Parsed& out) {
    if (eof()) return false;
    const char c = peek();
    if (c == '"') {
      out.kind = Parsed::Kind::String;
      return parseString(out.s);
    }
    if (c == 't' || c == 'f') {
      out.kind = Parsed::Kind::Bool;
      if (takeWord("true")) {
        out.b = true;
        return true;
      }
      if (takeWord("false")) {
        out.b = false;
        return true;
      }
      return false;
    }
    if (c == '-' || isDigit(c)) {
      out.kind = Parsed::Kind::Int;
      return parseNumber(out.i);
    }
    return false;  // null, {, [, and anything else
  }

  std::string_view t_;
  std::size_t i_ = 0;
};

void appendEscaped(const std::string& in, std::string& out) {
  out.push_back('"');
  for (char c : in) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      // Everything else raw, UTF-8 included -- there is no \u in this subset,
      // and parse() accepts what this writes.
      default: out.push_back(c); break;
    }
  }
  out.push_back('"');
}

}  // namespace

bool JsonObject::parse(std::string_view text) {
  // Built to the side and committed only on success, so a refusal leaves the
  // object empty rather than holding the fragment that parsed.
  ParsedPairs pairs;
  Parser p(text);
  if (!p.parseObject(pairs)) {
    values_.clear();
    return false;
  }
  JsonObject built;
  for (const auto& [key, v] : pairs) {
    // Applied in file order, so a duplicated key ends up with the last one --
    // the rule json.h states.
    switch (v.kind) {
      case Parsed::Kind::Int: built.setInt(key, v.i); break;
      case Parsed::Kind::Bool: built.setBool(key, v.b); break;
      case Parsed::Kind::String: built.setString(key, v.s); break;
    }
  }
  values_ = std::move(built.values_);
  return true;
}

bool JsonObject::getInt(std::string_view key, int64_t& out) const {
  auto it = values_.find(key);
  if (it == values_.end() || it->second.kind != Kind::Int) return false;
  out = it->second.i;
  return true;
}

bool JsonObject::getBool(std::string_view key, bool& out) const {
  auto it = values_.find(key);
  if (it == values_.end() || it->second.kind != Kind::Bool) return false;
  out = it->second.b;
  return true;
}

bool JsonObject::getString(std::string_view key, std::string& out) const {
  auto it = values_.find(key);
  if (it == values_.end() || it->second.kind != Kind::String) return false;
  out = it->second.s;
  return true;
}

void JsonObject::setInt(std::string_view key, int64_t v) {
  Value& slot = values_[std::string(key)];
  slot = Value{};
  slot.kind = Kind::Int;
  slot.i = v;
}

void JsonObject::setBool(std::string_view key, bool v) {
  Value& slot = values_[std::string(key)];
  slot = Value{};
  slot.kind = Kind::Bool;
  slot.b = v;
}

void JsonObject::setString(std::string_view key, std::string_view v) {
  Value& slot = values_[std::string(key)];
  slot = Value{};
  slot.kind = Kind::String;
  slot.s = std::string(v);
}

std::string JsonObject::dump() const {
  if (values_.empty()) return "{}\n";
  std::string out = "{\n";
  std::size_t left = values_.size();
  for (const auto& [key, v] : values_) {  // std::map iterates sorted by key
    out += "  ";
    appendEscaped(key, out);
    out += ": ";
    switch (v.kind) {
      case Kind::Int: out += std::to_string(v.i); break;
      case Kind::Bool: out += v.b ? "true" : "false"; break;
      case Kind::String: appendEscaped(v.s, out); break;
    }
    out += (--left == 0) ? "\n" : ",\n";
  }
  out += "}\n";
  return out;
}

}  // namespace reader
