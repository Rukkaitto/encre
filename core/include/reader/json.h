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
// RESOURCE BOUNDS, and they are part of the grammar rather than a safety net.
//
// "Parsing is total" above was true for every MALFORMED input and false for a
// well-formed one that is merely large: nothing bounded the pair count or a
// string's length, so a valid file could ask for more memory than the device
// has. Under -fno-exceptions that is not a failed parse, it is `abort()` with no
// diagnostic -- and it happens DURING the parse, before validity is decided, so
// the DEFAULTED path that exists to replace a bad file never runs and the file
// is still there at the next boot. A persistent boot loop, cleared only by
// pulling the card. The trigger is a user pasting something large into the file
// the boot log advertises as hand-editable; no hostility required.
//
// So exceeding either limit is MALFORMED, which routes it into the refusal path
// the design already has, with the log line that already explains itself.
//
// The numbers: settings needs about four short pairs, and Phase 3's per-book
// state is a different file with a different format (see the note above). The
// headroom is for a hand-editor, not for growth -- extend the format rather than
// these. `kJsonMaxStringBytes` counts DECODED bytes and is checked as the string
// grows, so an escape-heavy input cannot smuggle twice the limit past it.
// 256 rather than 512 because the test that states the composed worst case
// caught 512 as too generous: 64 pairs of two 512-byte strings is 67 KB, and a
// bound this file cannot afford is not a bound. A path or a title fits in 256
// with room, which is the longest string anything here plausibly stores.
inline constexpr std::size_t kJsonMaxPairs = 64;
inline constexpr std::size_t kJsonMaxStringBytes = 256;

class JsonObject {
 public:
  // Replaces the contents. True only if `text` is a complete, well-formed
  // object in the subset above AND within the bounds above; on false the object
  // is empty.
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
