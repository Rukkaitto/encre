// utf8Next is the only decoder in the reader, so anything it accepts becomes a
// glyph lookup. It must reject the whole malformed set -- overlongs,
// surrogates, out-of-range values, bad continuation bytes -- and, crucially,
// must not swallow the valid character that follows a broken sequence.
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "reader/font.h"

namespace {

struct Decoded {
  char32_t cp;
  size_t consumed;
};

Decoded decodeFirst(std::string_view s) {
  size_t i = 0;
  const char32_t cp = reader::utf8Next(s, i);
  return {cp, i};
}

std::vector<char32_t> decodeAll(std::string_view s) {
  std::vector<char32_t> out;
  for (size_t i = 0; i < s.size();) out.push_back(reader::utf8Next(s, i));
  return out;
}

constexpr char32_t kRepl = 0xFFFD;

}  // namespace

TEST_CASE("utf8Next decodes well-formed sequences") {
  CHECK(decodeFirst("A").cp == U'A');
  CHECK(decodeFirst("A").consumed == 1);
  CHECK(decodeFirst("\x7F").cp == 0x7F);

  CHECK(decodeFirst("\xC2\xA9").cp == 0xA9);  // (c)
  CHECK(decodeFirst("\xC2\xA9").consumed == 2);
  CHECK(decodeFirst("\xC2\x80").cp == 0x80);  // shortest 2-byte form

  CHECK(decodeFirst("\xE2\x80\x94").cp == 0x2014);  // em dash
  CHECK(decodeFirst("\xE2\x80\x94").consumed == 3);
  CHECK(decodeFirst("\xE0\xA0\x80").cp == 0x800);  // shortest 3-byte form

  CHECK(decodeFirst("\xF0\x9F\x98\x80").cp == 0x1F600);  // emoji
  CHECK(decodeFirst("\xF0\x9F\x98\x80").consumed == 4);
  CHECK(decodeFirst("\xF0\x90\x80\x80").cp == 0x10000);   // shortest 4-byte form
  CHECK(decodeFirst("\xF4\x8F\xBF\xBF").cp == 0x10FFFF);  // highest legal cp
}

TEST_CASE("utf8Next rejects overlong encodings") {
  // Masking continuation bytes without a range check made C0 80 decode as
  // U+0000 and E0 80 AF as U+002F ('/') -- a classic path-traversal encoding.
  CHECK(decodeFirst("\xC0\x80").cp == kRepl);
  CHECK(decodeFirst("\xC1\xBF").cp == kRepl);      // overlong U+007F
  CHECK(decodeFirst("\xE0\x80\xAF").cp == kRepl);  // overlong U+002F
  CHECK(decodeFirst("\xE0\x9F\xBF").cp == kRepl);  // overlong U+07FF
  CHECK(decodeFirst("\xF0\x80\x80\x80").cp == kRepl);
  CHECK(decodeFirst("\xF0\x8F\xBF\xBF").cp == kRepl);  // overlong U+FFFF
}

TEST_CASE("utf8Next rejects surrogates") {
  CHECK(decodeFirst("\xED\xA0\x80").cp == kRepl);  // U+D800
  CHECK(decodeFirst("\xED\xAF\xBF").cp == kRepl);  // U+DBFF
  CHECK(decodeFirst("\xED\xB0\x80").cp == kRepl);  // U+DC00
  CHECK(decodeFirst("\xED\xBF\xBF").cp == kRepl);  // U+DFFF
  CHECK(decodeFirst("\xEE\x80\x80").cp == 0xE000);  // just past the block: legal
}

TEST_CASE("utf8Next rejects code points above U+10FFFF") {
  CHECK(decodeFirst("\xF4\x90\x80\x80").cp == kRepl);  // U+110000
  CHECK(decodeFirst("\xF7\xBF\xBF\xBF").cp == kRepl);  // U+1FFFFF
}

TEST_CASE("utf8Next rejects impossible lead bytes") {
  CHECK(decodeFirst("\x80").cp == kRepl);  // bare continuation byte
  CHECK(decodeFirst("\xBF").cp == kRepl);
  CHECK(decodeFirst("\xF8\x80\x80\x80\x80").cp == kRepl);  // 5-byte form
  CHECK(decodeFirst("\xFC\x80\x80\x80\x80\x80").cp == kRepl);
  CHECK(decodeFirst("\xFE").cp == kRepl);
  CHECK(decodeFirst("\xFF").cp == kRepl);
}

TEST_CASE("utf8Next does not swallow the character after a broken sequence") {
  // The old decoder masked whatever followed a lead byte, so "\xE0AB" ate
  // three bytes and both good characters vanished. A bad continuation byte
  // must advance exactly one byte past the lead.
  CHECK(decodeAll("\xE0"
                  "AB") == std::vector<char32_t>{kRepl, U'A', U'B'});
  CHECK(decodeAll("\xC2"
                  "A") == std::vector<char32_t>{kRepl, U'A'});
  CHECK(decodeAll("\xF0"
                  "xyz") == std::vector<char32_t>{kRepl, U'x', U'y', U'z'});
  // A rejected overlong must also yield the following character.
  CHECK(decodeAll("\xC0\x80"
                  "A") == std::vector<char32_t>{kRepl, kRepl, U'A'});
  // A malformed lead followed by a valid multi-byte character.
  CHECK(decodeAll("\x80\xE2\x80\x94") == std::vector<char32_t>{kRepl, 0x2014});
}

TEST_CASE("utf8Next consumes a sequence truncated at end of input") {
  // Pre-existing behaviour, kept: no bytes remain, so stop at the end.
  CHECK(decodeFirst("\xE2\x80").cp == kRepl);
  CHECK(decodeFirst("\xE2\x80").consumed == 2);
  CHECK(decodeFirst("\xF0\x9F").cp == kRepl);
  CHECK(decodeFirst("\xF0\x9F").consumed == 2);
  CHECK(decodeFirst("\xC2").consumed == 1);
  CHECK(decodeAll("A\xE2\x80") == std::vector<char32_t>{U'A', kRepl});
}

TEST_CASE("utf8Next walks a mixed string exactly once") {
  const std::string s = "a\xC3\xA9\xE2\x80\x94\xF0\x9F\x98\x80z";
  CHECK(decodeAll(s) == std::vector<char32_t>{U'a', 0xE9, 0x2014, 0x1F600, U'z'});
}
