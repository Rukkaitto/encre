// THE PERCENT-ESCAPING THE TWO NVS RECORDS SHARE. It has its own file so a
// change to it fails with the primitive's name on it rather than twice, in the
// session record's suite and the Wi-Fi store's -- which is the rule that keeps
// those two suites about their own content.
//
// It was extracted when the Wi-Fi store became the second caller. Before that
// it lived in session_record.cpp's anonymous namespace and was tested only
// through a Library path, which is a narrower input than an SSID: a path cannot
// contain a NUL and an SSID can, because 802.11 says an SSID is 0-32 arbitrary
// octets.
#include <initializer_list>
#include <string>

#include "doctest.h"
#include "reader/wire_escape.h"

using namespace reader;

namespace {
bool decode(const std::string& in, std::string& out) {
  return decodeWireField(in.data(), in.size(), out);
}
}  // namespace

TEST_CASE("the three separators and the control bytes are escaped, nothing else") {
  CHECK(escapeWireField("plain") == "plain");
  CHECK(escapeWireField("%") == "%25");
  CHECK(escapeWireField(";") == "%3B");
  CHECK(escapeWireField(":") == "%3A");
  CHECK(escapeWireField(std::string(1, '\x01')) == "%01");
  CHECK(escapeWireField(std::string(1, '\x7F')) == "%7F");
  CHECK(escapeWireField(std::string(1, '\x1F')) == "%1F");
  // 0x20 is a space and is NOT a control byte, so it passes through -- a
  // record with spaces in it is still readable, which is the point.
  CHECK(escapeWireField(" ") == " ");
}

TEST_CASE("UTF-8 PASSES THROUGH, which is why the format is not base64") {
  // `nvs_get encre_sess stack str` printing `library:7:/books/Le Fléau` is what
  // makes a record diagnosable, and a record a person can read is one they can
  // report.
  CHECK(escapeWireField("Le Fléau") == "Le Fléau");
  CHECK(escapeWireField("日本語") == "日本語");
  std::string out;
  REQUIRE(decode("Le Fléau", out));
  CHECK(out == "Le Fléau");
}

TEST_CASE("everything round-trips, including every single byte") {
  for (int b = 1; b < 256; ++b) {  // 0 is refused; see below
    const std::string raw(1, static_cast<char>(b));
    std::string out;
    const std::string wire = escapeWireField(raw);
    CAPTURE(b);
    REQUIRE(decode(wire, out));
    CHECK(out == raw);
  }
}

TEST_CASE("THE THREE REFUSALS") {
  std::string out;
  // Empty: both encoders write no field at all rather than an empty one, so an
  // empty field means a trailing separator and a malformed record.
  CHECK_FALSE(decode("", out));
  // A malformed escape.
  for (const char* bad : {"%", "%2", "%ZZ", "a%", "a%1", "a%GG"}) {
    CAPTURE(bad);
    CHECK_FALSE(decode(bad, out));
  }
  // An escaped NUL. Unreachable from a path and REACHABLE FROM AN SSID, and
  // carrying one would truncate the record's own C string the next time it is
  // written.
  CHECK_FALSE(decode("%00", out));
  CHECK_FALSE(decode("a%00b", out));
  // A raw separator is an extra field rather than content: the escape puts
  // both beyond the parser's reach.
  CHECK_FALSE(decode(":", out));
  CHECK_FALSE(decode(";", out));
  CHECK_FALSE(decode("a:b", out));
  CHECK_FALSE(decode("a;b", out));
}

TEST_CASE("a refusal leaves the output empty rather than half-decoded") {
  std::string out = "stale";
  CHECK_FALSE(decode("good%ZZ", out));
  CHECK(out.empty());
}

TEST_CASE("both hex cases decode, and the encoder emits upper") {
  std::string lower, upper;
  REQUIRE(decode("%3b", lower));
  REQUIRE(decode("%3B", upper));
  CHECK(lower == ";");
  CHECK(upper == ";");
  CHECK(escapeWireField(";") == "%3B");
}
