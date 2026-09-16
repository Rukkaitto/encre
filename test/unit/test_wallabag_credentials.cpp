#include "doctest.h"
#include "fake_fs.h"
#include "reader/json.h"
#include "reader/wallabag_credentials.h"

using namespace reader;

namespace {
const char* kPath = "/.reader/wallabag.json";

std::string five(const char* server = "http://wallabag.lan", const char* pw = "hunter2") {
  return std::string("{\"server\":\"") + server +
         "\",\"clientId\":\"12_abc\",\"clientSecret\":\"3qd1\",\"username\":\"lucasg\""
         ",\"password\":\"" + pw + "\"}";
}
}  // namespace

TEST_CASE("a missing file is Absent, which is not an error") {
  // A device nobody has set up is the NORMAL state, and it is what the Articles
  // list's not-set-up variant draws. Reporting it as a failure would raise a
  // dialog about a file the reader has never seen.
  FakeFileSystem fs;
  WallabagCredentials c;
  std::string why;
  CHECK(loadWallabagCredentials(fs, c, why) == CredentialsResult::Absent);
  CHECK_FALSE(c.configured());
  CHECK(why.empty());
}

TEST_CASE("the seeded file loads as Unconfigured, also not an error") {
  FakeFileSystem fs;
  REQUIRE(seedWallabagCredentials(fs));
  WallabagCredentials c;
  std::string why;
  CHECK(loadWallabagCredentials(fs, c, why) == CredentialsResult::Unconfigured);
  CHECK_FALSE(c.configured());
}

TEST_CASE("ANY empty value is Unconfigured, not just all five") {
  // A half-finished edit is the same state as a seed to the reader: they have
  // not finished. Five subcases, because "all five are non-empty" is satisfied
  // by any four plus a coincidence.
  const char* keys[] = {"server", "clientId", "clientSecret", "username", "password"};
  for (const char* blank : keys) {
    CAPTURE(blank);
    FakeFileSystem fs;
    JsonObject o;
    for (const char* k : keys) o.setString(k, std::string(k) == blank ? "" : "x");
    REQUIRE(fs.writeAll(kPath, o.dump()));
    WallabagCredentials c;
    std::string why;
    CHECK(loadWallabagCredentials(fs, c, why) == CredentialsResult::Unconfigured);
  }
}

TEST_CASE("all five present is Ok and configured") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(kPath, five()));
  WallabagCredentials c;
  std::string why;
  REQUIRE(loadWallabagCredentials(fs, c, why) == CredentialsResult::Ok);
  CHECK(c.configured());
  CHECK(c.server == "http://wallabag.lan");
  CHECK(c.clientId == "12_abc");
  CHECK(c.username == "lucasg");
  CHECK(c.password == "hunter2");
}

TEST_CASE("a malformed file is its OWN answer, with a reason for the log") {
  // Distinct from Absent and from Unconfigured, because the reader's edit is the
  // only copy of itself: this is the one case that has to reach the log, and the
  // one case the seed must refuse to overwrite.
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(kPath, "not json at all"));
  WallabagCredentials c;
  std::string why;
  CHECK(loadWallabagCredentials(fs, c, why) == CredentialsResult::Malformed);
  CHECK_FALSE(why.empty());
}

TEST_CASE("a \\uXXXX escape reads as malformed, which is why the header says to type UTF-8") {
  // The flat parser does not decode one. Stated rather than discovered, because
  // the file is hand-edited and an accented password is the obvious thing to
  // escape.
  FakeFileSystem fs;
  REQUIRE(fs.writeAll(kPath,
                      "{\"server\":\"http://h\",\"clientId\":\"a\",\"clientSecret\":\"b\""
                      ",\"username\":\"c\",\"password\":\"caf\\u00e9\"}"));
  WallabagCredentials c;
  std::string why;
  CHECK(loadWallabagCredentials(fs, c, why) == CredentialsResult::Malformed);
}

TEST_CASE("the server is normalised: a trailing slash goes, a missing scheme is added") {
  struct Case { const char* in; const char* out; };
  const Case cases[] = {
      {"http://wallabag.lan/", "http://wallabag.lan"},
      {"http://wallabag.lan///", "http://wallabag.lan"},
      // The LAN case, which #139 says is the common one.
      {"wallabag.lan", "http://wallabag.lan"},
      {"wallabag.lan/", "http://wallabag.lan"},
      // AND AN EXPLICIT https IS KEPT. A reader who typed it has said something
      // this code must not quietly undo.
      {"https://app.wallabag.it", "https://app.wallabag.it"},
      {"https://app.wallabag.it/", "https://app.wallabag.it"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.in);
    FakeFileSystem fs;
    REQUIRE(fs.writeAll(kPath, five(c.in)));
    WallabagCredentials got;
    std::string why;
    REQUIRE(loadWallabagCredentials(fs, got, why) == CredentialsResult::Ok);
    CHECK(got.server == c.out);
  }
}

TEST_CASE("the seed writes only when the file is absent, and never over one that exists") {
  // loadAndApplySettings' rule: a device that rewrites a file it could not parse
  // destroys the thing it was meant to help fix.
  FakeFileSystem fs;
  CHECK(seedWallabagCredentials(fs));
  CHECK_FALSE(seedWallabagCredentials(fs));

  FakeFileSystem edited;
  REQUIRE(edited.writeAll(kPath, five()));
  CHECK_FALSE(seedWallabagCredentials(edited));
  std::string after;
  REQUIRE(edited.readAll(kPath, after));
  CHECK(after == five());

  // AND NOT OVER A MALFORMED ONE EITHER, which is the case that matters: that
  // file is a reader's edit with a typo in it, and it is the only copy.
  FakeFileSystem broken;
  REQUIRE(broken.writeAll(kPath, "{oops"));
  CHECK_FALSE(seedWallabagCredentials(broken));
  std::string kept;
  REQUIRE(broken.readAll(kPath, kept));
  CHECK(kept == "{oops");
}

TEST_CASE("the seed creates /.reader on a fresh card") {
  FakeFileSystem fs;
  REQUIRE(seedWallabagCredentials(fs));
  CHECK(fs.exists(kPath));
}
