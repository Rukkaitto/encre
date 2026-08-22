#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/json.h"

using namespace reader;

namespace {

// A fixed-seed xorshift32. Tests must be reproducible, so the fuzz loop below
// gets its bytes from here rather than from rand() or a clock.
class Xorshift {
 public:
  explicit Xorshift(uint32_t seed) : s_(seed ? seed : 1u) {}
  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }
  uint32_t below(uint32_t n) { return next() % n; }

 private:
  uint32_t s_;
};

// Every rejection case owes the same two things: false, and an object left
// empty rather than half-populated.
void checkRejected(std::string_view text) {
  INFO("input: " << std::string(text));
  JsonObject o;
  o.setInt("pre-existing", 42);
  CHECK_FALSE(o.parse(text));
  CHECK(o.size() == 0);
  int64_t i = 0;
  CHECK_FALSE(o.getInt("pre-existing", i));
}

}  // namespace

TEST_CASE("a round trip preserves every type") {
  JsonObject o;
  o.setInt("count", 7);
  o.setBool("enabled", true);
  o.setBool("disabled", false);
  o.setString("name", "Encre");

  const std::string text = o.dump();
  CHECK(text == o.dump());  // stable across calls

  JsonObject back;
  REQUIRE(back.parse(text));
  CHECK(back.size() == 4);
  int64_t i = 0;
  REQUIRE(back.getInt("count", i));
  CHECK(i == 7);
  bool b = false;
  REQUIRE(back.getBool("enabled", b));
  CHECK(b);
  b = true;
  REQUIRE(back.getBool("disabled", b));
  CHECK_FALSE(b);
  std::string s;
  REQUIRE(back.getString("name", s));
  CHECK(s == "Encre");

  CHECK(back.dump() == text);  // ...and re-dumping is byte-identical
}

TEST_CASE("keys come out sorted, whatever order they went in") {
  JsonObject o;
  o.setInt("zeta", 1);
  o.setInt("alpha", 2);
  o.setInt("mid", 3);
  const std::string text = o.dump();
  const size_t a = text.find("alpha"), m = text.find("mid"), z = text.find("zeta");
  CHECK(a < m);
  CHECK(m < z);

  JsonObject other;
  other.setInt("mid", 3);
  other.setInt("zeta", 1);
  other.setInt("alpha", 2);
  CHECK(other.dump() == text);  // insertion order cannot leak into the file
}

TEST_CASE("negative and zero integers, and the int64 extremes") {
  JsonObject o;
  o.setInt("neg", -42);
  o.setInt("zero", 0);
  o.setInt("min", INT64_MIN);
  o.setInt("max", INT64_MAX);
  JsonObject back;
  REQUIRE(back.parse(o.dump()));
  int64_t v = 1;
  REQUIRE(back.getInt("neg", v));
  CHECK(v == -42);
  REQUIRE(back.getInt("zero", v));
  CHECK(v == 0);
  REQUIRE(back.getInt("min", v));
  CHECK(v == INT64_MIN);
  REQUIRE(back.getInt("max", v));
  CHECK(v == INT64_MAX);
}

TEST_CASE("string escapes survive both directions") {
  JsonObject o;
  o.setString("quoted", "say \"hi\"");
  o.setString("slashed", "a\\b");
  o.setString("lines", "one\ntwo\ttab\r\n");
  o.setString("empty", "");
  o.setString("utf8", "caf\xc3\xa9 \xe2\x80\x94 dash");
  const std::string text = o.dump();
  JsonObject back;
  REQUIRE(back.parse(text));
  std::string s;
  REQUIRE(back.getString("quoted", s));
  CHECK(s == "say \"hi\"");
  REQUIRE(back.getString("slashed", s));
  CHECK(s == "a\\b");
  REQUIRE(back.getString("lines", s));
  CHECK(s == "one\ntwo\ttab\r\n");
  REQUIRE(back.getString("empty", s));
  CHECK(s.empty());
  REQUIRE(back.getString("utf8", s));
  CHECK(s == "caf\xc3\xa9 \xe2\x80\x94 dash");
}

TEST_CASE("an escape a hand-editor writes is read back") {
  JsonObject o;
  REQUIRE(o.parse("{\"a\":\"q\\\"q\",\"b\":\"back\\\\slash\",\"c\":\"sol\\/idus\","
                  "\"d\":\"\\b\\f\\n\\r\\t\"}"));
  std::string s;
  REQUIRE(o.getString("a", s));
  CHECK(s == "q\"q");
  REQUIRE(o.getString("b", s));
  CHECK(s == "back\\slash");
  REQUIRE(o.getString("c", s));
  CHECK(s == "sol/idus");
  REQUIRE(o.getString("d", s));
  CHECK(s == "\b\f\n\r\t");
}

TEST_CASE("whitespace anywhere it is allowed is tolerated") {
  JsonObject o;
  REQUIRE(o.parse("  \n\t {\n  \"a\"  :  1 ,\n\t\"b\"\n:\ntrue\n}  \n"));
  CHECK(o.size() == 2);
  int64_t i = 0;
  REQUIRE(o.getInt("a", i));
  CHECK(i == 1);
  bool b = false;
  REQUIRE(o.getBool("b", b));
  CHECK(b);
}

TEST_CASE("an empty object is valid and empty") {
  JsonObject o;
  o.setInt("stale", 1);
  REQUIRE(o.parse("{}"));
  CHECK(o.size() == 0);
  REQUIRE(o.parse("{   \n }"));
  CHECK(o.size() == 0);
  CHECK(o.dump() == JsonObject().dump());
}

TEST_CASE("a key with no value in the file simply is not there") {
  JsonObject o;
  REQUIRE(o.parse("{\"a\":1}"));
  int64_t i = 99;
  CHECK_FALSE(o.getInt("absent", i));
  CHECK(i == 99);  // out untouched
  bool b = true;
  CHECK_FALSE(o.getBool("absent", b));
  std::string s = "keep";
  CHECK_FALSE(o.getString("absent", s));
  CHECK(s == "keep");
}

TEST_CASE("a getter for the wrong type refuses rather than converting") {
  JsonObject o;
  REQUIRE(o.parse("{\"i\":1,\"b\":true,\"s\":\"1\"}"));
  int64_t i = 99;
  bool b = false;
  std::string s = "keep";

  CHECK_FALSE(o.getBool("i", b));   // 1 is not true
  CHECK_FALSE(o.getString("i", s));
  CHECK_FALSE(o.getInt("b", i));    // true is not 1
  CHECK_FALSE(o.getString("b", s));
  CHECK_FALSE(o.getInt("s", i));    // "1" is not 1
  CHECK_FALSE(o.getBool("s", b));

  CHECK(i == 99);
  CHECK_FALSE(b);
  CHECK(s == "keep");
}

TEST_CASE("setting a key again replaces its type as well as its value") {
  JsonObject o;
  o.setInt("k", 1);
  o.setString("k", "now a string");
  CHECK(o.size() == 1);
  int64_t i = 0;
  CHECK_FALSE(o.getInt("k", i));
  std::string s;
  REQUIRE(o.getString("k", s));
  CHECK(s == "now a string");
}

TEST_CASE("duplicate keys: the last one wins") {
  // RFC 8259 leaves this undefined; this parser picks last-wins, the same rule
  // JSON.parse uses. Pinned here so it cannot drift silently.
  JsonObject o;
  REQUIRE(o.parse("{\"a\":1,\"a\":2}"));
  CHECK(o.size() == 1);
  int64_t i = 0;
  REQUIRE(o.getInt("a", i));
  CHECK(i == 2);

  // ...including when the duplicate changes the type.
  REQUIRE(o.parse("{\"a\":1,\"b\":9,\"a\":\"two\"}"));
  CHECK(o.size() == 2);
  CHECK_FALSE(o.getInt("a", i));
  std::string s;
  REQUIRE(o.getString("a", s));
  CHECK(s == "two");
  REQUIRE(o.getInt("b", i));
  CHECK(i == 9);
}

TEST_CASE("malformed input is a clean false, never a partial parse") {
  checkRejected("");
  checkRejected("   \n ");
  checkRejected("{");
  checkRejected("}");
  checkRejected("{\"a\":1");
  checkRejected("{\"a\":1,}");           // trailing comma
  checkRejected("{,}");
  checkRejected("{\"a\":1,,\"b\":2}");
  checkRejected("{\"a\" 1}");            // missing colon
  checkRejected("{\"a\"::1}");
  checkRejected("{a:1}");                // unquoted key
  checkRejected("{'a':1}");              // single quotes
  checkRejected("{\"a\":{\"b\":1}}");    // nested object
  checkRejected("{\"a\":[1,2]}");        // array value
  checkRejected("{\"a\":null}");
  checkRejected("{\"a\":True}");
  checkRejected("{\"a\":tru}");
  checkRejected("{\"a\":\"unterminated}");
  checkRejected("{\"unterminated:1}");
  checkRejected("42");                   // bare number outside an object
  checkRejected("\"just a string\"");
  checkRejected("true");
  checkRejected("[]");
  checkRejected("{\"a\":1} trailing");   // trailing junk
  checkRejected("{\"a\":1}{\"b\":2}");   // two objects
  checkRejected("{\"a\":}");
  checkRejected("{\"a\":,}");
  checkRejected("{:1}");
  checkRejected("{\"a\":1:2}");
}

TEST_CASE("numbers outside the subset are malformed rather than guessed at") {
  checkRejected("{\"a\":1.5}");
  checkRejected("{\"a\":1.}");
  checkRejected("{\"a\":.5}");
  checkRejected("{\"a\":1e5}");
  checkRejected("{\"a\":1E5}");
  checkRejected("{\"a\":+1}");
  checkRejected("{\"a\":-}");
  checkRejected("{\"a\":--1}");
  checkRejected("{\"a\":01}");   // leading zero
  checkRejected("{\"a\":-01}");
  checkRejected("{\"a\":0x10}");
  // An integer too big for int64_t: malformed, NOT saturated to INT64_MAX. A
  // clamped value read back as if it were the file's is a lie.
  checkRejected("{\"a\":9223372036854775808}");
  checkRejected("{\"a\":-9223372036854775809}");
  checkRejected("{\"a\":99999999999999999999999999}");
  // ...but the extremes themselves are fine.
  JsonObject o;
  REQUIRE(o.parse("{\"lo\":-9223372036854775808,\"hi\":9223372036854775807}"));
  int64_t v = 0;
  REQUIRE(o.getInt("lo", v));
  CHECK(v == INT64_MIN);
  REQUIRE(o.getInt("hi", v));
  CHECK(v == INT64_MAX);
  REQUIRE(o.parse("{\"a\":-0}"));
  REQUIRE(o.getInt("a", v));
  CHECK(v == 0);
}

TEST_CASE("an unsupported escape is malformed rather than half-decoded") {
  checkRejected("{\"a\":\"\\u00e9\"}");  // \u is not in the subset
  checkRejected("{\"a\":\"\\x41\"}");
  checkRejected("{\"a\":\"\\q\"}");
  checkRejected("{\"a\":\"trailing backslash\\\"}");
  checkRejected("{\"\\u0061\":1}");      // and in a key too
}

TEST_CASE("EVERY prefix of a valid file either parses or fails cleanly") {
  // This is the case that matters most. The realistic corruption on this device
  // is losing power part way through a write, which leaves a prefix of the
  // intended bytes on the card. Every one of them must be survivable: a clean
  // parse or a clean refusal, never a crash and never half a file's values.
  JsonObject src;
  src.setInt("sleepAfterMs", 300000);
  src.setInt("fullRefreshEvery", 0);
  src.setBool("fullOnTransition", true);
  src.setString("theme", "quiet \"q\"\n\t");
  src.setInt("version", 1);
  const std::string full = src.dump();
  REQUIRE(full.size() > 40);

  int accepted = 0;
  for (size_t n = 0; n <= full.size(); ++n) {
    const std::string_view prefix(full.data(), n);
    JsonObject o;
    o.setInt("pre-existing", 7);
    if (o.parse(prefix)) {
      ++accepted;
      // A prefix that parses must be internally consistent: re-dumping and
      // re-parsing it gives the same thing back.
      JsonObject again;
      REQUIRE(again.parse(o.dump()));
      CHECK(again.dump() == o.dump());
    } else {
      CHECK(o.size() == 0);  // nothing half-populated survives a refusal
      int64_t i = 0;
      CHECK_FALSE(o.getInt("pre-existing", i));
      CHECK_FALSE(o.getInt("sleepAfterMs", i));
    }
  }
  // The whole file parses, and so does at least one shorter prefix (the dump
  // ends in a newline) -- but not many: a truncation lands mid-token almost
  // always.
  CHECK(accepted >= 1);
  CHECK(accepted <= 3);
}

TEST_CASE("every prefix of a valid file with a single key is survivable too") {
  JsonObject src;
  src.setInt("version", 1);
  const std::string full = src.dump();
  for (size_t n = 0; n <= full.size(); ++n) {
    JsonObject o;
    const bool ok = o.parse(std::string_view(full.data(), n));
    if (!ok) CHECK(o.size() == 0);
  }
  JsonObject o;
  REQUIRE(o.parse(full));
  CHECK(o.size() == 1);
}

TEST_CASE("deterministic fuzz: parse never crashes and never half-populates") {
  // Fixed seed, no rand() and no clock: the project bans non-deterministic
  // sources in tests, so a failure here is reproducible from the seed alone.
  Xorshift rng(0x51ED2701u);
  // A charset biased towards the tokens the parser cares about, so the bytes
  // reach further in than uniform noise would.
  static const char kChars[] = "{}[]\":,\\ \n\t01239-+.eEtruefalsnabxyz_\x01\x7f";
  const size_t kAlphabet = sizeof(kChars) - 1;

  int parsedOk = 0;
  for (int iter = 0; iter < 600; ++iter) {
    std::string s;
    const uint32_t len = rng.below(48);
    for (uint32_t i = 0; i < len; ++i) {
      // A tenth of the bytes are fully arbitrary, the rest come from the
      // charset.
      s.push_back(rng.below(10) == 0 ? static_cast<char>(rng.below(256))
                                     : kChars[rng.below(kAlphabet)]);
    }
    JsonObject o;
    o.setInt("pre-existing", 1);
    if (o.parse(s)) {
      ++parsedOk;
      JsonObject again;
      REQUIRE_MESSAGE(again.parse(o.dump()), "a dump of a parsed object must parse: " << s);
    } else {
      REQUIRE_MESSAGE(o.size() == 0, "a refused parse must leave nothing behind: " << s);
    }
  }
  // Every one of these is a rejection -- measured, 0 of 600 -- because noise
  // essentially never forms `{"key":value}`. So this case covers the REFUSAL
  // path only, and the mutation fuzz below is what exercises the accepting one.
  CHECK(parsedOk == 0);
}

TEST_CASE("deterministic fuzz: mutating a valid file one byte at a time") {
  // Closer in than random noise: take a real settings dump and corrupt single
  // bytes. This is the shape of a bit-rotted card.
  JsonObject src;
  src.setInt("sleepAfterMs", 300000);
  src.setBool("fullOnTransition", true);
  src.setString("theme", "quiet");
  const std::string full = src.dump();

  Xorshift rng(0x2C0100u);
  int parsedOk = 0;
  for (int iter = 0; iter < 400; ++iter) {
    std::string s = full;
    const int edits = 1 + static_cast<int>(rng.below(3));
    for (int e = 0; e < edits; ++e) {
      const size_t at = rng.below(static_cast<uint32_t>(s.size()));
      switch (rng.below(3)) {
        case 0: s[at] = static_cast<char>(rng.below(256)); break;
        case 1: s.erase(at, 1); break;
        default: s.insert(at, 1, static_cast<char>(rng.below(256))); break;
      }
      if (s.empty()) break;
    }
    JsonObject o;
    o.setInt("pre-existing", 1);
    if (o.parse(s)) {
      ++parsedOk;
      // A corruption that still parses must still be internally consistent.
      JsonObject again;
      REQUIRE_MESSAGE(again.parse(o.dump()), "a dump of a parsed object must parse: " << s);
      CHECK(again.dump() == o.dump());
      int64_t i = 0;
      CHECK_FALSE(o.getInt("pre-existing", i));  // and it replaced, not merged
    } else {
      REQUIRE_MESSAGE(o.size() == 0, "a refused parse must leave nothing behind: " << s);
    }
  }
  // Unlike the noise above, a one-byte edit to a real file often still parses --
  // so this is where the ACCEPTING path gets its fuzz coverage.
  CHECK(parsedOk > 20);
}

// --- Resource bounds --------------------------------------------------------
//
// A cold-read review (docs/superpowers/2026-08-22-review-findings.md, finding 1)
// found that "parsing is total" held for every malformed input and not for a
// WELL-FORMED one that is merely large. The integer path was already bounded
// against overflow; the allocation paths were not bounded at all.
//
// Why that was a crash and not a slow parse: the firmware builds with
// -fno-exceptions, so a std::vector or std::string that cannot allocate calls
// abort() with no diagnostic. And it aborts DURING parse, before validity is
// decided -- so loadSettings never returns false, the DEFAULTED path that exists
// precisely to replace a bad file is unreachable, and the file is still on the
// card at the next boot. A persistent boot loop, cleared only by pulling the
// card.
//
// These tests are the reason the caps cannot be quietly raised past what the
// heap allows: they pin the refusal, not the number.

TEST_CASE("a well-formed object with too many pairs is refused, not allocated") {
  std::string text = "{";
  for (size_t i = 0; i <= reader::kJsonMaxPairs; ++i) {
    if (i) text += ",";
    text += "\"k" + std::to_string(i) + "\":0";
  }
  text += "}";

  reader::JsonObject o;
  CHECK_FALSE(o.parse(text));
  CHECK(o.size() == 0);  // and empty, per the header's promise
}

TEST_CASE("exactly the pair limit still parses") {
  std::string text = "{";
  for (size_t i = 0; i < reader::kJsonMaxPairs; ++i) {
    if (i) text += ",";
    text += "\"k" + std::to_string(i) + "\":1";
  }
  text += "}";

  reader::JsonObject o;
  REQUIRE(o.parse(text));
  CHECK(o.size() == reader::kJsonMaxPairs);
}

TEST_CASE("duplicate keys count toward the pair limit") {
  // Last-wins means size() stays 1, so counting DISTINCT keys would let a
  // 64 KB file of one repeated key allocate without bound while looking tiny.
  // The cap is on pairs parsed, which is what actually gets allocated.
  std::string text = "{";
  for (size_t i = 0; i <= reader::kJsonMaxPairs; ++i) {
    if (i) text += ",";
    text += "\"same\":0";
  }
  text += "}";

  reader::JsonObject o;
  CHECK_FALSE(o.parse(text));
}

TEST_CASE("an over-long string is refused, in a key or a value") {
  const std::string big(reader::kJsonMaxStringBytes + 1, 'x');

  reader::JsonObject value;
  CHECK_FALSE(value.parse("{\"a\":\"" + big + "\"}"));
  CHECK(value.size() == 0);

  reader::JsonObject key;
  CHECK_FALSE(key.parse("{\"" + big + "\":0}"));
  CHECK(key.size() == 0);
}

TEST_CASE("exactly the string limit still parses, escapes counted as bytes OUT") {
  const std::string ok(reader::kJsonMaxStringBytes, 'x');
  reader::JsonObject o;
  REQUIRE(o.parse("{\"a\":\"" + ok + "\"}"));
  std::string got;
  REQUIRE(o.getString("a", got));
  CHECK(got.size() == reader::kJsonMaxStringBytes);

  // A cap counted on INPUT bytes would let "\\n" (2 bytes in, 1 out) through at
  // twice the limit, and one counted on output has to be checked as it grows
  // rather than after. This is the escaped form of a string that is exactly at
  // the limit once decoded, so it must parse.
  const std::string escaped(reader::kJsonMaxStringBytes, 'n');
  std::string src = "{\"a\":\"";
  for (char c : escaped) { src += '\\'; src += c; }
  src += "\"}";
  reader::JsonObject esc;
  REQUIRE(esc.parse(src));
  std::string decoded;
  REQUIRE(esc.getString("a", decoded));
  CHECK(decoded.size() == reader::kJsonMaxStringBytes);
}

TEST_CASE("the bounds compose to a total allocation the device can afford") {
  // The point of the caps is a NUMBER, so state it. Two strings per pair at the
  // string cap, plus the pair vector itself. If this figure ever approaches the
  // ~155 KB measured heap floor (CLAUDE.md, Memory), the caps are wrong.
  const size_t worst = reader::kJsonMaxPairs *
                       (2 * reader::kJsonMaxStringBytes +
                        sizeof(std::pair<std::string, int64_t>));
  CHECK(worst < 64u * 1024u);
}
