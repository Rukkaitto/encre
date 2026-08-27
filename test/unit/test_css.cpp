// Which class names mean italic, read off a stylesheet.
//
// The measurement that made this exist: one chapter of the user's own copy of
// `Le Fleau` carries 609 classed inline tags and NOT ONE <em>, <i> or <cite>. Its
// italics are entirely a publisher stylesheet, which is what a converted EPUB
// usually emits -- so without this, most books on a real card render no italics at
// all and do it silently.
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/css.h"

using reader::classAttrIsItalic;
using reader::collectItalicClasses;

namespace {
std::vector<std::string> italics(const std::string& css) {
  std::vector<std::string> out;
  collectItalicClasses(css, out);
  return out;
}
bool has(const std::vector<std::string>& v, const std::string& s) {
  for (const std::string& x : v)
    if (x == s) return true;
  return false;
}
}  // namespace

TEST_CASE("a class whose rule asks for italics is collected") {
  const auto v = italics(".ital { font-style: italic; }");
  REQUIRE(v.size() == 1);
  CHECK(v[0] == "ital");
}

TEST_CASE("the shape a real book uses") {
  // A publisher sheet: many rules, one of them italic, class names of its own.
  const auto v = italics(
      "body { margin: 0 }\n"
      ".lattes { font-family: serif; }\n"
      "p.lattes-i { font-style: italic; color: #000 }\n"
      ".x, .y { font-weight: bold }\n");
  CHECK(has(v, "lattes-i"));
  CHECK_FALSE(has(v, "lattes"));
  CHECK_FALSE(has(v, "x"));
}

TEST_CASE("every class in a selector list gets it") {
  const auto v = italics("span.a, .b > em, div .c { font-style: oblique }");
  CHECK(has(v, "a"));
  CHECK(has(v, "b"));
  CHECK(has(v, "c"));
}

TEST_CASE("a property that merely MENTIONS italic is not a request for it") {
  // `font-family: "Italic Garamond"` names both words and asks for neither. The
  // colon after the property is what separates a declaration from a name.
  CHECK(italics(".a { font-family: 'Italic Garamond' }").empty());
  CHECK(italics(".a { font-style: normal }").empty());
}

TEST_CASE("case: the property is case-insensitive and the class name is not") {
  CHECK(has(italics(".Ital { FONT-STYLE: ITALIC }"), "Ital"));
  const auto v = italics(".Ital { font-style: italic }");
  CHECK(has(v, "Ital"));
  CHECK_FALSE(has(v, "ital"));  // HTML class names are case-sensitive
}

TEST_CASE("a commented-out rule is not a rule") {
  // Without skipping comments the `{` inside one closes a block that never opened,
  // and everything after it is misread.
  CHECK(italics("/* .a { font-style: italic } */ .b { font-weight: bold }").empty());
  CHECK(has(italics("/* c */ .b { font-style: italic }"), "b"));
}

TEST_CASE("a class inside an at-rule is still collected") {
  // Over-matching, knowingly: see the header. Under-matching is invisible, which is
  // exactly the failure being fixed.
  CHECK(has(italics("@media screen { .a { font-style: italic } }"), "a"));
}

TEST_CASE("element and id selectors are ignored") {
  // `em { font-style: italic }` is every book, and `p { ... }` would italicise a
  // whole chapter. Only classes name a run.
  CHECK(italics("em { font-style: italic }").empty());
  CHECK(italics("#note { font-style: italic }").empty());
}

TEST_CASE("the cap holds, and malformed input terminates") {
  std::string css;
  for (int i = 0; i < 300; ++i)
    css += ".c" + std::to_string(i) + " { font-style: italic }\n";
  CHECK(italics(css).size() == reader::kMaxItalicClasses);
  // Unterminated everything: it must return rather than run off the end.
  CHECK(italics(".a { font-style: italic").empty());
  CHECK(italics("/* unterminated").empty());
  CHECK(italics("").empty());
}

TEST_CASE("a class attribute may name several classes") {
  const std::vector<std::string> set = {"ital"};
  CHECK(classAttrIsItalic("ital", set));
  CHECK(classAttrIsItalic("calibre3 ital", set));
  CHECK(classAttrIsItalic("  ital  x ", set));
  CHECK_FALSE(classAttrIsItalic("italic", set));   // not a prefix match
  CHECK_FALSE(classAttrIsItalic("italx", set));
  CHECK_FALSE(classAttrIsItalic("", set));
  CHECK_FALSE(classAttrIsItalic("ital", {}));
}
