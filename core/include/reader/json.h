#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace reader {

// A deliberately tiny JSON subset: ONE object, keys to numbers, bools and
// strings. No nesting, no arrays, no null, no exponents, no fractions.
//
// That is the whole of what settings need, and it is written here rather than
// vendored because nothing is vendored and there is no network to fetch one.
// Phase 3's per-book state carries a bookmark ARRAY and will outgrow this --
// extend it there, with the consumer in hand.
//
// Parsing is total: any malformed input is a clean `false`, never a partial
// result and never a crash. A settings file is attacker-adjacent only in the
// sense that a user can hand-edit it and a half-finished write can truncate it,
// and both must be survivable. On `false` the object is left EMPTY rather than
// holding whatever was read before the error, so a caller that ignores the
// return value cannot pick up half a file's worth of values.
//
// Decisions the JSON spec leaves open or this subset takes deliberately:
//
//  - DUPLICATE KEYS: the LAST one wins. RFC 8259 leaves it undefined; last-wins
//    is what JSON.parse and essentially every other parser do, so a
//    hand-editor's later line is what takes effect. The alternative considered
//    was rejecting the file outright, which is more paranoid but throws away
//    every other setting in it too -- and a duplicated key is still unambiguous
//    under a stated rule, while every value that comes out of here is range
//    checked by its consumer anyway.
//  - Integers only: 1.5, 1e5 and a leading zero are all malformed. An integer
//    too large for int64_t is malformed rather than saturating.
//  - Strings are raw UTF-8 bytes. The escapes are \" \\ \/ \b \f \n \r \t;
//    \uXXXX is NOT supported and makes the file malformed, because nothing this
//    writes emits it -- a title with an accent is UTF-8 in the file, unescaped.
//  - dump() passes control bytes other than those five through raw, which strict
//    JSON forbids and parse() therefore accepts, so an arbitrary byte string
//    still round-trips.
class JsonObject {
 public:
  // Replaces the contents. True only if `text` is a complete, well-formed
  // object in the subset above; on false the object is empty.
  bool parse(std::string_view text);

  bool getInt(std::string_view key, int64_t& out) const;
  bool getBool(std::string_view key, bool& out) const;
  bool getString(std::string_view key, std::string& out) const;

  void setInt(std::string_view key, int64_t v);
  void setBool(std::string_view key, bool v);
  void setString(std::string_view key, std::string_view v);

  std::size_t size() const { return values_.size(); }
  void clear() { values_.clear(); }

  // Serialised with keys in sorted order, so saving unchanged settings produces
  // a byte-identical file and a diff of two dumps is readable. One key per line,
  // because a settings file is a file a user may open.
  std::string dump() const;

 private:
  enum class Kind : uint8_t { Int, Bool, String };
  struct Value {
    Kind kind = Kind::Int;
    int64_t i = 0;
    bool b = false;
    std::string s;
  };
  std::map<std::string, Value, std::less<>> values_;
};

}  // namespace reader
