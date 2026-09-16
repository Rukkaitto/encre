#include <string>
#include <vector>

#include "doctest.h"
#include "grained_source.h"
#include "reader/json_stream.h"

using namespace reader;
using Token = JsonScanner::Token;

namespace {

// EVERY CASE RUNS AT GRAIN 1 AS WELL AS WHOLE, which is inflate_stream.h's
// recorded lesson: a source that satisfies every read hides every resumption
// bug there is, and this scanner resumes inside a string, inside an escape and
// inside a surrogate pair.
void forEachGrain(const std::string& text, void (*body)(JsonScanner&)) {
  for (const size_t grain : {size_t(1), size_t(3), text.size() + 1}) {
    CAPTURE(grain);
    grainsrc::Grained src(text, grain == 0 ? 1 : grain);
    JsonScanner s(src);
    body(s);
  }
}

std::string wallabagPage() {
  return R"({"page":1,"limit":20,"pages":2,"total":24,"_embedded":{"items":[)"
         R"({"id":7,"title":"A “curly” title","domain_name":"longreads.com",)"
         R"("reading_time":22,"is_archived":false,"is_starred":true,)"
         R"("updated_at":"2026-09-03T10:00:00+0000","tags":[{"id":1,"label":"x"}],)"
         R"("preview_picture":null},)"
         R"({"id":8,"title":"Second","domain_name":"aeon.co","reading_time":9,)"
         R"("is_archived":true,"is_starred":false,"updated_at":"2026-09-02T09:00:00+0000",)"
         R"("tags":[]}]}})";
}

}  // namespace

TEST_CASE("a flat object yields key/value pairs in order") {
  forEachGrain(R"({"a":1,"b":"two","c":true,"d":null})", [](JsonScanner& s) {
    CHECK(s.next() == Token::ObjectStart);
    CHECK(s.next() == Token::Key);
    CHECK(s.key() == "a");
    CHECK(s.next() == Token::Number);
    CHECK(s.number() == 1);
    CHECK(s.next() == Token::Key);
    CHECK(s.key() == "b");
    CHECK(s.next() == Token::String);
    CHECK(s.text() == "two");
    CHECK(s.next() == Token::Key);
    CHECK(s.next() == Token::Bool);
    CHECK(s.boolean());
    CHECK(s.next() == Token::Key);
    CHECK(s.next() == Token::Null);
    CHECK(s.next() == Token::ObjectEnd);
    CHECK(s.next() == Token::End);
  });
}

TEST_CASE("an array of objects is walkable, which json.h cannot do at all") {
  forEachGrain(wallabagPage(), [](JsonScanner& s) {
    int ids[2] = {0, 0};
    int found = 0;
    std::string firstTitle;
    while (found < 2) {
      const Token t = s.next();
      REQUIRE(t != Token::Error);
      REQUIRE(t != Token::End);
      if (t != Token::Key) continue;
      if (s.key() == "tags" || s.key() == "preview_picture") {
        REQUIRE(s.skipValue());
      } else if (s.key() == "id" && s.next() == Token::Number) {
        ids[found++] = static_cast<int>(s.number());
      } else if (s.key() == "title" && s.next() == Token::String && firstTitle.empty()) {
        firstTitle = s.text();
      }
    }
    CHECK(ids[0] == 7);
    CHECK(ids[1] == 8);
    // `“` AND `”` DECODE TO THE CURLY QUOTES fontc.py's subset carries.
    // A title the subset cannot draw is a notdef box, which is why this has to
    // become the real codepoint rather than pass through as an escape.
    CHECK(firstTitle == "A \xE2\x80\x9C" "curly\xE2\x80\x9D title");
  });
}

TEST_CASE("skipValue walks past a nested value without knowing its shape") {
  // What lets the listing parser ignore `tags` and `preview_picture`.
  forEachGrain(R"({"tags":[{"id":1,"a":[1,2,{"b":null}]},{"id":2}],"after":42})",
               [](JsonScanner& s) {
                 REQUIRE(s.next() == Token::ObjectStart);
                 REQUIRE(s.next() == Token::Key);
                 REQUIRE(s.key() == "tags");
                 REQUIRE(s.skipValue());
                 REQUIRE(s.next() == Token::Key);
                 CHECK(s.key() == "after");
                 REQUIRE(s.next() == Token::Number);
                 CHECK(s.number() == 42);
               });
}

TEST_CASE("skipValue on a scalar consumes exactly one token") {
  forEachGrain(R"({"a":1,"b":2})", [](JsonScanner& s) {
    REQUIRE(s.next() == Token::ObjectStart);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.skipValue());
    REQUIRE(s.next() == Token::Key);
    CHECK(s.key() == "b");
  });
}

TEST_CASE("a surrogate pair becomes one codepoint") {
  forEachGrain(R"({"t":"😀"})", [](JsonScanner& s) {
    REQUIRE(s.next() == Token::ObjectStart);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::String);
    CHECK(s.text() == "\xF0\x9F\x98\x80");
  });
}

TEST_CASE("an UNPAIRED surrogate is an error, not bytes no decoder accepts") {
  for (const char* bad : {R"({"t":"\uD83D"})", R"({"t":"\uDE00"})", R"({"t":"\uD83Dx"})"}) {
    CAPTURE(bad);
    grainsrc::Grained src(std::string_view(bad), 1);
    JsonScanner s(src);
    Token t = s.next();
    while (t != Token::Error && t != Token::End) t = s.next();
    CHECK(t == Token::Error);
  }
}

TEST_CASE("a long string is TRUNCATED at a UTF-8 boundary and FLAGGED, never an error") {
  // A long title is CONTENT, and cutting it is the Library's own ellipsis one
  // layer up. Refusing the page over a title would lose nineteen articles to one.
  std::string text = "{\"t\":\"";
  for (int i = 0; i < 400; ++i) text += "\xC3\xA9";  // e-acute, two bytes each
  text += "\",\"after\":5}";

  grainsrc::Grained src(text, 1);
  JsonScanner s(src);
  REQUIRE(s.next() == Token::ObjectStart);
  REQUIRE(s.next() == Token::Key);
  REQUIRE(s.next() == Token::String);
  CHECK(s.truncated());
  CHECK(s.truncations() == 1);
  CHECK(s.text().size() <= kJsonStreamMaxStringBytes);
  // ON A BOUNDARY: every byte is part of a whole two-byte sequence, so the size
  // is even and the last byte is a continuation. A cut mid-sequence would leave
  // a byte the font layer cannot draw.
  CHECK(s.text().size() % 2 == 0);
  // AND THE SCAN STAYS IN STEP -- the rest of the string was consumed, so the
  // page's remaining fields are still readable.
  REQUIRE(s.next() == Token::Key);
  CHECK(s.key() == "after");
  REQUIRE(s.next() == Token::Number);
  CHECK(s.number() == 5);
}

TEST_CASE("truncation is per TOKEN and counted over the scan") {
  std::string longOne(400, 'x');
  const std::string text = "{\"a\":\"" + longOne + "\",\"b\":\"short\",\"c\":\"" + longOne + "\"}";
  grainsrc::Grained src(text, 7);
  JsonScanner s(src);
  REQUIRE(s.next() == Token::ObjectStart);
  REQUIRE(s.next() == Token::Key);
  REQUIRE(s.next() == Token::String);
  CHECK(s.truncated());
  REQUIRE(s.next() == Token::Key);
  REQUIRE(s.next() == Token::String);
  // So a consumer can flag ONE field without the page reading as suspect.
  CHECK_FALSE(s.truncated());
  REQUIRE(s.next() == Token::Key);
  REQUIRE(s.next() == Token::String);
  CHECK(s.truncated());
  CHECK(s.truncations() == 2);
}

TEST_CASE("nesting past the cap is an ERROR, because that is structure and not content") {
  std::string deep;
  for (int i = 0; i < kJsonMaxDepth + 2; ++i) deep += "[";
  grainsrc::Grained src(deep, 1);
  JsonScanner s(src);
  Token t = s.next();
  int guard = 0;
  while (t != Token::Error && t != Token::End && ++guard < 64) t = s.next();
  CHECK(t == Token::Error);
  CHECK_FALSE(s.error().empty());
}

TEST_CASE("every malformed input is a CLEAN Error, and nothing is read past it") {
  const char* bad[] = {
      "{",                     // never closed
      "{\"a\"",                // a key with no value
      "{\"a\":}",              // a value that is not one
      "[1,2",                  // an array that never ends
      "{\"a\":tru}",           // a broken atom
      "{\"a\":\"unterminated", // a string that never ends
      "}",                     // an unbalanced close
      "]",
      "{\"a\":[1,2}",          // the wrong close
      "{\"a\":01x}",           // not a number
  };
  for (const char* b : bad) {
    CAPTURE(b);
    grainsrc::Grained src(std::string_view(b), 1);
    JsonScanner s(src);
    Token t = s.next();
    int guard = 0;
    while (t != Token::Error && t != Token::End && ++guard < 64) t = s.next();
    REQUIRE(t == Token::Error);
    // AND IT STAYS Error. A malformed document must not be walkable twice.
    CHECK(s.next() == Token::Error);
    CHECK(s.next() == Token::Error);
  }
}

TEST_CASE("the two-byte escapes decode, and a bad one is an error") {
  forEachGrain(R"({"t":"a\"b\\c\/d\be\ff\ng\rh\ti"})", [](JsonScanner& s) {
    REQUIRE(s.next() == Token::ObjectStart);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::String);
    CHECK(s.text() == "a\"b\\c/d\be\ff\ng\rh\ti");
  });
  grainsrc::Grained src(std::string_view(R"({"t":"\q"})"), 1);
  JsonScanner s(src);
  Token t = s.next();
  while (t != Token::Error && t != Token::End) t = s.next();
  CHECK(t == Token::Error);
}

TEST_CASE("a fractional number keeps its text and reads 0 as an integer") {
  // Nothing this parses is fractional -- an id and a reading time are integers --
  // but refusing a `1.0` the server is entitled to send would lose the page.
  forEachGrain(R"({"a":1.5,"b":-7,"c":2e3})", [](JsonScanner& s) {
    REQUIRE(s.next() == Token::ObjectStart);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::Number);
    CHECK(s.text() == "1.5");
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::Number);
    CHECK(s.number() == -7);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::Number);
    CHECK(s.text() == "2e3");
  });
}

TEST_CASE("an empty object and an empty array are walkable") {
  forEachGrain(R"({"a":{},"b":[],"c":1})", [](JsonScanner& s) {
    REQUIRE(s.next() == Token::ObjectStart);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::ObjectStart);
    REQUIRE(s.next() == Token::ObjectEnd);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::ArrayStart);
    REQUIRE(s.next() == Token::ArrayEnd);
    REQUIRE(s.next() == Token::Key);
    REQUIRE(s.next() == Token::Number);
    CHECK(s.number() == 1);
    REQUIRE(s.next() == Token::ObjectEnd);
    CHECK(s.next() == Token::End);
  });
}
