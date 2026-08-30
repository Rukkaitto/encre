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

// The whole struct by ==, plus the three values worth naming. BOTH HALVES DO A
// JOB, and they are not the same job: `s == Settings{}` covers every field and
// catches one that FAILED TO DEFAULT, but it compares against those same
// defaults, so it cannot see a default VALUE that changed -- both sides move
// together and it still passes. Only a spelled-out literal catches that, which
// is why the list exists and why it is literals rather than the constants the
// struct is built from. The typography defaults are pinned the same way, in
// their own case below.
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

  // THE TYPOGRAPHY FIELDS TOO, WHICH IS THE ONLY THING readSteppedInt'S LAYER
  // CONTRIBUTES that validate() does not. Both numbers are 2^32 plus a value
  // that is ON the table, so a cast to int would truncate them to exactly their
  // own default and be accepted in silence -- 4294967328 -> 32 and 4294968696 ->
  // 1400. Clamping to the table's top step instead is what makes the file
  // report CORRECTED.
  plant(fs, "{\"version\":1,\"bodyPpem\":4294967328,"
            "\"lineSpacing\":4294968696}");
  Settings u;
  CHECK_FALSE(loadSettings(fs, u));
  CHECK(u.bodyPpem == 46);
  CHECK(u.lineSpacing == 2000);
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

TEST_CASE("the typography fields default to today's behaviour") {
  // THE PROPERTY THE WHOLE FEATURE RESTS ON. Every reader golden is pinned at
  // these values, so a default that moved would re-bless nine goldens and
  // silently change what every book looks like.
  // LITERALS, NOT reader::kBodyPpem. Written against the constant this read
  // `kBodyPpem == kBodyPpem` and could not fail: moving layout.h's kBodyPpem
  // 32->38 and kBodyLeadEm 1700->1850 passed the whole suite, which is the
  // opposite of what the comment above claims. settings.cpp's static_asserts are
  // what tie these to layout.h now; these say what the numbers are.
  const reader::Settings s;
  CHECK(s.bodyPpem == 32);        // design/Reader.dc.html `font-size: 32px`
  CHECK(s.margins == 18);         // its `padding` either side of the column
  CHECK(s.lineSpacing == 1700);   // its `line-height: 1.7`
  CHECK(s.justify);
}

TEST_CASE("validate snaps the typography fields to an offered value") {
  // SNAPPED, not range-clamped. The stepper indexes a list, so a value that is
  // in range but not ON the list would be a value the user could never leave.
  SUBCASE("a size between two steps snaps") {
    reader::Settings s;
    s.bodyPpem = 35;  // between 32 and 38
    CHECK_FALSE(s.validate());
    CHECK(s.bodyPpem == 38);  // nearest; ties go up
  }
  SUBCASE("a size below the smallest step") {
    reader::Settings s;
    s.bodyPpem = 4;
    CHECK_FALSE(s.validate());
    CHECK(s.bodyPpem == 25);
  }
  SUBCASE("a size above the largest step") {
    reader::Settings s;
    s.bodyPpem = 200;
    CHECK_FALSE(s.validate());
    CHECK(s.bodyPpem == 46);
  }
  SUBCASE("margins and line spacing snap the same way") {
    reader::Settings s;
    s.margins = 25;       // between 18 and 30
    s.lineSpacing = 1900; // between 1850 and 2000
    CHECK_FALSE(s.validate());
    CHECK(s.margins == 30);
    CHECK(s.lineSpacing == 1850);  // 1900 is 50 from 1850 and 100 from 2000
  }
  SUBCASE("a lead below the old bottom step snaps to one of the two new ones") {
    // ADDED WITH 1.0 AND 1.2. Before them 1400 was the floor, so ANY tighter value
    // snapped up to it and this whole region of the table was one answer; a test
    // written then would have kept passing over two steps it could not reach.
    reader::Settings s;
    s.lineSpacing = 1100;  // between 1000 and 1200, 100 from each
    CHECK_FALSE(s.validate());
    CHECK(s.lineSpacing == 1200);  // ties go UP, which is what ascending() protects
    reader::Settings t;
    t.lineSpacing = 800;  // below the whole table now
    CHECK_FALSE(t.validate());
    CHECK(t.lineSpacing == 1000);  // and NOT 1400, which is what it used to be
  }
  SUBCASE("a value already on the list is left alone and reports ok") {
    reader::Settings s;
    s.bodyPpem = 42;
    s.margins = 10;
    s.lineSpacing = 1400;
    s.justify = false;
    CHECK(s.validate());
    CHECK(s.bodyPpem == 42);
    CHECK(s.margins == 10);
    CHECK(s.lineSpacing == 1400);
    CHECK_FALSE(s.justify);
  }
}

TEST_CASE("every offered typography value survives validate") {
  // The screen may only offer values validate accepts, or a step would be
  // undone by the save that follows it. Asserted over the whole table rather
  // than sampled, because one bad entry is one row the user cannot select.
  for (const int p : reader::kBodyPpemSteps) {
    reader::Settings s;
    s.bodyPpem = p;
    CHECK(s.validate());
    CHECK(s.bodyPpem == p);
  }
  for (const int m : reader::kMarginSteps) {
    reader::Settings s;
    s.margins = m;
    CHECK(s.validate());
    CHECK(s.margins == m);
  }
  for (const int l : reader::kLineSpacingSteps) {
    reader::Settings s;
    s.lineSpacing = l;
    CHECK(s.validate());
    CHECK(s.lineSpacing == l);
  }
}

TEST_CASE("the typography fields survive a save/load round trip") {
  FakeFileSystem fs;
  reader::Settings out;
  out.bodyPpem = 46;
  out.margins = 10;
  out.lineSpacing = 2000;
  out.justify = false;
  REQUIRE(reader::saveSettings(fs, out));

  reader::Settings back;
  CHECK(reader::loadSettings(fs, back));
  CHECK(back.bodyPpem == 46);
  CHECK(back.margins == 10);
  CHECK(back.lineSpacing == 2000);
  CHECK_FALSE(back.justify);
  // The whole struct, so a field that round-tripped by accident of its default
  // cannot pass: this is the fixed-point property saveSettings documents.
  CHECK(back == out);
}

TEST_CASE("a settings file from before typography loads with the defaults") {
  // THE BACK-COMPAT CASE, and the reason kSettingsVersion did not move. This is
  // byte-for-byte a file today's firmware writes.
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"fullOnTransition\":true,"
            "\"fullRefreshEvery\":0,\"logToCard\":false,"
            "\"sleepAfterMs\":300000}");
  reader::Settings s;
  CHECK(reader::loadSettings(fs, s));  // TRUE: an absent field is not a failure
  CHECK(s.bodyPpem == 32);  // literals, for the reason the defaults case gives
  CHECK(s.margins == 18);
  CHECK(s.lineSpacing == 1700);
  CHECK(s.justify);
}

TEST_CASE("an off-table size in a hand-edited file is corrected and reported") {
  FakeFileSystem fs;
  plant(fs, "{\"version\":1,\"bodyPpem\":35,\"sleepAfterMs\":300000}");
  reader::Settings s;
  CHECK_FALSE(reader::loadSettings(fs, s));  // CORRECTED, not DEFAULTED
  CHECK(s.bodyPpem == 38);
  // ...and the rest of the file still loaded, which is the whole reason a bad
  // value clamps instead of failing the load.
  CHECK(s.sleepAfterMs == 300000u);
}

// --- The sleep screen (design/Settings.dc.html's SLEEP SCREEN section) --------

TEST_CASE("the two sleep-screen fields default to today's behaviour plus a cover") {
  const reader::Settings s;
  CHECK(s.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(s.coverFit == reader::CoverFit::Fill);
}

TEST_CASE("the sleep-screen fields survive a save and load") {
  FakeFileSystem fs;
  reader::Settings in;
  in.sleepShows = reader::SleepShows::Details;
  in.coverFit = reader::CoverFit::Whole;
  REQUIRE(reader::saveSettings(fs, in));

  reader::Settings out;
  REQUIRE(reader::loadSettings(fs, out));
  CHECK(out.sleepShows == reader::SleepShows::Details);
  CHECK(out.coverFit == reader::CoverFit::Whole);
  // The whole struct, so a field that round-tripped by accident of its default
  // cannot pass -- the same reason the typography round trip says so.
  CHECK(out == in);
}

TEST_CASE("a file from before the sleep-screen fields loads and keeps today's behaviour") {
  // kSettingsVersion did NOT move, so an older file must load clean -- not
  // DEFAULTED -- and take the new fields' defaults. Typography set this precedent
  // and this is the case the rule exists for.
  FakeFileSystem fs;
  plant(fs, R"({"version":1,"sleepAfterMs":600000,"fullRefreshEvery":0,)"
            R"("fullOnTransition":true,"logToCard":false,"bodyPpem":32,)"
            R"("margins":18,"lineSpacing":1700,"justify":true})");
  reader::Settings out;
  CHECK(reader::loadSettings(fs, out));  // true: nothing was corrected
  CHECK(out.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(out.coverFit == reader::CoverFit::Fill);
  CHECK(out.sleepAfterMs == 600000u);
}

TEST_CASE("a nonsense value in either sleep-screen field is CORRECTED, not fatal") {
  FakeFileSystem fs;
  plant(fs, R"({"version":1,"sleepShows":47,"coverFit":-3,"sleepAfterMs":600000})");
  reader::Settings out;
  CHECK_FALSE(reader::loadSettings(fs, out));  // false: something was corrected
  CHECK(out.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(out.coverFit == reader::CoverFit::Fill);
  CHECK(out.sleepAfterMs == 600000u);  // the rest of the file still loaded
}

TEST_CASE("a sleep-screen field of the wrong JSON type keeps its default, loudly") {
  // readBool's rule, applied to an enum read as an int: `"COVER"` is not 0, so
  // the field keeps its default and the caller is told the file was not clean.
  FakeFileSystem fs;
  plant(fs, R"({"version":1,"sleepShows":"COVER","sleepAfterMs":600000})");
  reader::Settings out;
  CHECK_FALSE(reader::loadSettings(fs, out));
  CHECK(out.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(out.sleepAfterMs == 600000u);
}

TEST_CASE("every sleep-screen value round-trips, not just the two the cases name") {
  // Walked rather than sampled: the write is an int cast and the read is a
  // bounded one, so an off-by-one at either end of either enum is exactly the
  // shape a two-value sample misses.
  for (const reader::SleepShows shows :
       {reader::SleepShows::Cover, reader::SleepShows::CoverAndDetails,
        reader::SleepShows::Details}) {
    for (const reader::CoverFit fit : {reader::CoverFit::Fill, reader::CoverFit::Whole}) {
      FakeFileSystem fs;
      reader::Settings in;
      in.sleepShows = shows;
      in.coverFit = fit;
      REQUIRE(reader::saveSettings(fs, in));
      reader::Settings out;
      REQUIRE(reader::loadSettings(fs, out));
      CHECK(out.sleepShows == shows);
      CHECK(out.coverFit == fit);
    }
  }
}

TEST_CASE("validate() resets an out-of-enum value handed to it in memory") {
  // THE HOLE A MUTATION FOUND. Deleting validate()'s enum reset outright failed
  // NOTHING: every case above reaches the fields through loadSettings, where
  // readEnum has already refused an out-of-range value, so validate() only ever
  // saw a good one. The in-memory path is not shielded -- saveSettings validates
  // whatever a CALLER hands it, so without this the wrong value would have been
  // written to the card. Exactly the shape snapToTable's INT_MIN note records.
  //
  // The cast is how a caller gets there: a JSON int reinterpreted upstream, or a
  // struct memcpy'd out of a record from a firmware that had a fourth mode.
  reader::Settings s;
  s.sleepShows = static_cast<reader::SleepShows>(47);
  s.coverFit = static_cast<reader::CoverFit>(-3);
  CHECK_FALSE(s.validate());
  CHECK(s.sleepShows == reader::SleepShows::CoverAndDetails);
  CHECK(s.coverFit == reader::CoverFit::Fill);

  // A GOOD value is not touched and does not report a correction -- otherwise the
  // reset above would be indistinguishable from validate() clobbering the field
  // on every save.
  reader::Settings good;
  good.sleepShows = reader::SleepShows::Details;
  good.coverFit = reader::CoverFit::Whole;
  CHECK(good.validate());
  CHECK(good.sleepShows == reader::SleepShows::Details);
  CHECK(good.coverFit == reader::CoverFit::Whole);
}

TEST_CASE("saveSettings never writes an out-of-enum value to the card") {
  // The consequence of the case above, at the layer that matters: a caller with a
  // bogus enum must not persist it, or the next boot reads a file that loadSettings
  // has to correct and the user is told their settings were wrong.
  FakeFileSystem fs;
  reader::Settings bad;
  bad.sleepShows = static_cast<reader::SleepShows>(47);
  REQUIRE(reader::saveSettings(fs, bad));

  reader::Settings back;
  CHECK(reader::loadSettings(fs, back));  // TRUE: the file on the card is clean
  CHECK(back.sleepShows == reader::SleepShows::CoverAndDetails);
}
