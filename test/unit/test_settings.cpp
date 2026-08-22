#include <cstdint>
#include <string>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/json.h"
#include "reader/settings.h"

using namespace reader;

namespace {

// Puts `text` at kSettingsPath, bypassing saveSettings so a test can plant a
// file no writer would produce.
void plant(FakeFileSystem& fs, const std::string& text) {
  REQUIRE(fs.writeAll(kSettingsPath, text));
}

// Defaults, spelled out. If one of these changes the behaviour of a device with
// no settings file changes with it, which is worth a failing test.
void checkIsDefaults(const Settings& s) {
  CHECK(s == Settings{});
  CHECK(s.sleepAfterMs == 5u * 60u * 1000u);
  CHECK(s.fullRefreshEvery == 0);
  CHECK(s.fullOnTransition == true);
}

}  // namespace

TEST_CASE("a filesystem with no settings file yields defaults, and says so") {
  FakeFileSystem fs;
  Settings s;
  s.sleepAfterMs = 1;  // whatever the caller had in there is discarded
  s.fullRefreshEvery = 99;
  s.fullOnTransition = false;
  CHECK_FALSE(loadSettings(fs, s));
  checkIsDefaults(s);
}

TEST_CASE("an unmounted filesystem yields defaults rather than a hang or a lie") {
  FakeFileSystem fs;
  REQUIRE(saveSettings(fs, Settings{}));
  fs.setMounted(false);
  Settings s;
  CHECK_FALSE(loadSettings(fs, s));
  checkIsDefaults(s);
}

TEST_CASE("save then load round-trips every field") {
  FakeFileSystem fs;
  Settings out;
  out.sleepAfterMs = 12u * 60u * 1000u;
  out.fullRefreshEvery = 15;
  out.fullOnTransition = false;
  REQUIRE(saveSettings(fs, out));

  Settings in;
  REQUIRE(loadSettings(fs, in));
  CHECK(in.sleepAfterMs == 12u * 60u * 1000u);
  CHECK(in.fullRefreshEvery == 15);
  CHECK_FALSE(in.fullOnTransition);
  CHECK(in == out);
}

TEST_CASE("saving unchanged defaults and loading them back is a fixed point") {
  FakeFileSystem fs;
  REQUIRE(saveSettings(fs, Settings{}));
  const std::string first = *fs.peek(kSettingsPath);

  Settings loaded;
  REQUIRE(loadSettings(fs, loaded));
  checkIsDefaults(loaded);

  REQUIRE(saveSettings(fs, loaded));
  CHECK(*fs.peek(kSettingsPath) == first);  // byte-identical, no drift

  Settings again;
  REQUIRE(loadSettings(fs, again));
  CHECK(again == loaded);
}

TEST_CASE("the file records the version, and it is readable JSON") {
  FakeFileSystem fs;
  REQUIRE(saveSettings(fs, Settings{}));
  JsonObject o;
  REQUIRE(o.parse(*fs.peek(kSettingsPath)));
  int64_t version = 0;
  REQUIRE(o.getInt("version", version));
  CHECK(version == kSettingsVersion);
}

TEST_CASE("saveSettings creates /.reader when it is absent") {
  FakeFileSystem fs;
  REQUIRE_FALSE(fs.exists("/.reader"));
  REQUIRE(saveSettings(fs, Settings{}));
  CHECK(fs.exists("/.reader"));
  CHECK(fs.exists(kSettingsPath));
}

TEST_CASE("saveSettings reports a refused write rather than claiming success") {
  FakeFileSystem fs;
  fs.setFailWrites(true);
  CHECK_FALSE(saveSettings(fs, Settings{}));
  CHECK_FALSE(fs.exists(kSettingsPath));

  fs.setFailWrites(false);
  fs.setMounted(false);
  CHECK_FALSE(saveSettings(fs, Settings{}));
}

TEST_CASE("a missing key keeps that field's default and still loads") {
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"fullRefreshEvery\":20}");
  Settings s;
  REQUIRE(loadSettings(fs, s));  // an older file is not a bad file
  CHECK(s.fullRefreshEvery == 20);
  CHECK(s.sleepAfterMs == Settings{}.sleepAfterMs);
  CHECK(s.fullOnTransition == Settings{}.fullOnTransition);
}

TEST_CASE("an unknown extra key loads fine") {
  // Forward compatibility: a newer firmware's file must not brick an older one.
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"sleepAfterMs\":60000,\"themeName\":\"quiet\","
            "\"futureKnob\":42,\"futureFlag\":false}");
  Settings s;
  REQUIRE(loadSettings(fs, s));
  CHECK(s.sleepAfterMs == 60000u);
}

TEST_CASE("an unknown version is a bad file: defaults, and false") {
  FakeFileSystem fs;
  for (const char* text : {"{\"version\":2,\"sleepAfterMs\":60000}",
                           "{\"version\":0,\"sleepAfterMs\":60000}",
                           "{\"version\":-1,\"sleepAfterMs\":60000}",
                           "{\"sleepAfterMs\":60000}",          // no version at all
                           "{\"version\":\"1\",\"sleepAfterMs\":60000}"}) {
    INFO("file: " << std::string(text));  // a bare const char* prints as a pointer
    plant(fs, text);
    Settings s;
    CHECK_FALSE(loadSettings(fs, s));
    checkIsDefaults(s);  // and NOT the 60000 that sat next to the bad version
  }
}

TEST_CASE("a malformed file yields complete defaults, never a half-populated out") {
  FakeFileSystem fs;
  for (const char* text : {"",
                           "{",
                           "{\"version\":1,\"sleepAfterMs\":60000",  // truncated
                           "{\"version\":1,\"sleepAfterMs\":60000,}",
                           "{\"version\":1,\"sleepAfterMs\":600.0}",
                           "not json at all",
                           "{\"version\":1,\"nested\":{\"a\":1}}"}) {
    INFO("file: " << std::string(text));  // a bare const char* prints as a pointer
    plant(fs, text);
    Settings s;
    CHECK_FALSE(loadSettings(fs, s));
    checkIsDefaults(s);
  }
}

TEST_CASE("every prefix of a real settings file leaves a working device") {
  // The power-loss-mid-write case, end to end: whatever prefix is on the card,
  // the device comes up with either that file's settings or complete defaults.
  FakeFileSystem fs;
  REQUIRE(saveSettings(fs, Settings{600000u, 15, false}));
  const std::string full = *fs.peek(kSettingsPath);

  int accepted = 0;
  for (size_t n = 0; n <= full.size(); ++n) {
    INFO("prefix length " << n);
    plant(fs, full.substr(0, n));
    Settings s;
    if (loadSettings(fs, s)) {
      ++accepted;
      CHECK(s == Settings{600000u, 15, false});
    } else {
      checkIsDefaults(s);  // complete defaults, not a mixture
    }
  }
  CHECK(accepted >= 1);
}

TEST_CASE("a wrong-typed value keeps the default for that field and reports it") {
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"sleepAfterMs\":\"soon\",\"fullRefreshEvery\":true,"
            "\"fullOnTransition\":1}");
  Settings s;
  CHECK_FALSE(loadSettings(fs, s));  // the file is wrong, and the caller is told
  checkIsDefaults(s);                // ...and nothing was converted
}

TEST_CASE("a wrong-typed value does not throw away the fields that are fine") {
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"sleepAfterMs\":60000,\"fullOnTransition\":\"yes\"}");
  Settings s;
  CHECK_FALSE(loadSettings(fs, s));
  CHECK(s.sleepAfterMs == 60000u);                              // kept
  CHECK(s.fullOnTransition == Settings{}.fullOnTransition);      // defaulted
}

TEST_CASE("validate clamps out-of-range values and says it had to") {
  SUBCASE("a sleep timeout below the floor, but not zero") {
    Settings s;
    s.sleepAfterMs = 1;
    CHECK_FALSE(s.validate());
    CHECK(s.sleepAfterMs == kSleepAfterMsMin);
  }
  SUBCASE("zero means never sleep and is left alone") {
    Settings s;
    s.sleepAfterMs = 0;
    CHECK(s.validate());
    CHECK(s.sleepAfterMs == 0u);
  }
  SUBCASE("an absurd sleep timeout") {
    Settings s;
    s.sleepAfterMs = 0xFFFFFFFFu;
    CHECK_FALSE(s.validate());
    CHECK(s.sleepAfterMs == kSleepAfterMsMax);
  }
  SUBCASE("a negative cadence folds into never") {
    Settings s;
    s.fullRefreshEvery = -5;
    CHECK_FALSE(s.validate());
    CHECK(s.fullRefreshEvery == 0);
  }
  SUBCASE("an absurd cadence") {
    Settings s;
    s.fullRefreshEvery = 100000;
    CHECK_FALSE(s.validate());
    CHECK(s.fullRefreshEvery == kFullRefreshEveryMax);
  }
  SUBCASE("the defaults are valid, and validating twice is idempotent") {
    Settings s;
    CHECK(s.validate());
    CHECK(s == Settings{});
    Settings bad;
    bad.fullRefreshEvery = -1;
    bad.sleepAfterMs = 3;
    CHECK_FALSE(bad.validate());
    const Settings clamped = bad;
    CHECK(bad.validate());  // ...and now it is clean
    CHECK(bad == clamped);
  }
  SUBCASE("the extremes of the range are in range") {
    Settings s;
    s.sleepAfterMs = kSleepAfterMsMin;
    s.fullRefreshEvery = kFullRefreshEveryMax;
    CHECK(s.validate());
    s.sleepAfterMs = kSleepAfterMsMax;
    s.fullRefreshEvery = 1;
    CHECK(s.validate());
  }
}

TEST_CASE("a file with out-of-range values is clamped, loaded, and reported") {
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"sleepAfterMs\":2,\"fullRefreshEvery\":-3,"
            "\"fullOnTransition\":true}");
  Settings s;
  CHECK_FALSE(loadSettings(fs, s));  // reported...
  CHECK(s.sleepAfterMs == kSleepAfterMsMin);  // ...but usable, not refused
  CHECK(s.fullRefreshEvery == 0);
  CHECK(s.fullOnTransition);
}

TEST_CASE("a number too large for the field is clamped, not wrapped") {
  FakeFileSystem fs;
  // Beyond uint32_t entirely, and beyond int for the cadence: a silent wrap
  // here would give a plausible-looking small timeout.
  plant(fs, "{\"version\":1,\"sleepAfterMs\":4294967296,"
            "\"fullRefreshEvery\":9223372036854775807}");
  Settings s;
  CHECK_FALSE(loadSettings(fs, s));
  CHECK(s.sleepAfterMs == kSleepAfterMsMax);
  CHECK(s.fullRefreshEvery == kFullRefreshEveryMax);

  plant(fs, "{\"version\":1,\"sleepAfterMs\":-1,"
            "\"fullRefreshEvery\":-9223372036854775808}");
  Settings t;
  CHECK_FALSE(loadSettings(fs, t));
  CHECK(t.sleepAfterMs == kSleepAfterMsMin);  // a negative is not 4 billion ms
  CHECK(t.fullRefreshEvery == 0);
}

TEST_CASE("a saved file is always valid, so a bad in-memory Settings cannot poison it") {
  FakeFileSystem fs;
  Settings bad;
  bad.sleepAfterMs = 1;
  bad.fullRefreshEvery = -7;
  REQUIRE(saveSettings(fs, bad));
  Settings in;
  CHECK(loadSettings(fs, in));  // what came back needs no clamping
  CHECK(in.sleepAfterMs == kSleepAfterMsMin);
  CHECK(in.fullRefreshEvery == 0);
}
