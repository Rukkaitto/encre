#include "reader/settings.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "reader/filesystem.h"
#include "reader/json.h"
#include "reader/layout.h"

namespace reader {

// THE TYPOGRAPHY DEFAULTS ARE LAYOUT'S CONSTANTS, ASSERTED HERE RATHER THAN IN
// THE HEADER so settings.h stays a leaf (including layout.h there cost 74,022
// preprocessed lines against 896, for two integers). Each line asserts two
// facts: the
// struct's default equals layout.h's constant, AND that value is on its step
// table -- a default off its own table would be snapped away by the first
// validate() to see it. So a change to either layout constant now fails the
// BUILD instead of silently moving every reader golden.
static_assert(Settings{}.bodyPpem == kBodyPpem && kBodyPpemSteps[1] == kBodyPpem);
// Index 4, and it was 2 until two tighter steps were prepended. THIS ASSERT
// EXISTS TO FAIL HERE: a step added below the default silently moves the default's
// index, and the build stopping is what forces the number to be re-read rather
// than the table to be edited and the pairing to be assumed.
static_assert(Settings{}.lineSpacing == kBodyLeadEm &&
              kLineSpacingSteps[4] == kBodyLeadEm);

namespace {

// JsonObject has no "is this key present?" -- a getter answers "present AND of
// this type" -- so a wrong type is detected by asking for the other two. An
// absent key answers no to all three, which is exactly the distinction that
// matters: absent keeps the default silently, wrong-typed keeps it loudly.
bool presentAsAnything(const JsonObject& o, const char* key) {
  int64_t i = 0;
  bool b = false;
  std::string s;
  return o.getInt(key, i) || o.getBool(key, b) || o.getString(key, s);
}

// Reads an int field, clamping into [lo, hi]. `ok` is cleared when the value had
// to be clamped or was the wrong type. The value is never narrowed by a cast: a
// 64-bit number truncated into an int gives a plausible-looking small one, and
// for a refresh cadence or a timeout that is worse than an obvious absurdity.
void readClampedInt(const JsonObject& o, const char* key, int& field, int lo, int hi,
                    bool& ok) {
  int64_t v = 0;
  if (!o.getInt(key, v)) {
    if (presentAsAnything(o, key)) ok = false;
    return;
  }
  if (v < lo) {
    field = lo;
    ok = false;
  } else if (v > hi) {
    field = hi;
    ok = false;
  } else {
    field = static_cast<int>(v);
  }
}

// The same read, bounded by a STEP TABLE's own ends. It exists because the
// bounds were hand-spelled at three call sites, which reintroduced exactly the
// mis-pairing hazard snapToTable's signature removes: pairing kMarginSteps[0]
// with kBodyPpemSteps' end would be completely invisible, since for any value
// that fits an int the bounds here change nothing that validate() does not then
// redo. Deriving both ends from the one table argument makes the pairing
// unwritable.
template <std::size_t N>
void readSteppedInt(const JsonObject& o, const char* key, int& field,
                    const int (&table)[N], bool& ok) {
  readClampedInt(o, key, field, table[0], table[N - 1], ok);
}

void readBool(const JsonObject& o, const char* key, bool& field, bool& ok) {
  bool v = false;
  if (o.getBool(key, v)) {
    field = v;
    return;
  }
  // 1 is not true and "yes" is not true: keep the default and tell the caller.
  if (presentAsAnything(o, key)) ok = false;
}

// SNAPS to the nearest value in an ascending table, returning false when the
// value was not already on it. Ties go UP: a value exactly between two steps has
// no better answer, and rounding up is the direction that never leaves a reader
// with type smaller than they asked for.
//
// A RANGE CLAMP WOULD NOT DO. The Typography screen steps through the table by
// index, so a value in range but off the table is a value the stepper can never
// leave -- the same trap cycleFocused handles by landing on index 0's successor,
// solved one layer earlier because a nearest-value answer exists here.
// THE DISTANCE IS `long long` BECAUSE `candidate - field` OVERFLOWS. With
// field == INT_MIN, `25 - INT_MIN` is signed overflow (UBSan says so) and the
// wrapped result made INT_MIN snap to 46 -- the largest step, where the nearest
// is obviously the smallest. The card path is shielded by readSteppedInt's
// int64 clamp; the in-memory path is not, and saveSettings validates whatever a
// caller hands it, so the wrong answer would have been persisted.
template <std::size_t N>
bool snapToTable(int& field, const int (&table)[N]) {
  // INITIALISED, although the sentinel below always overwrites it on the first
  // iteration and `N >= 1` is guaranteed by the array-reference signature. The
  // redundant store costs nothing the optimiser keeps, and it is not worth the
  // invariant: this is -fno-exceptions embedded code, so a future edit that
  // reordered the loop would read an uninitialised int with no diagnostic at all.
  int best = table[0];
  long long bestDist = -1;
  for (const int candidate : table) {
    const long long d = static_cast<long long>(candidate) - field;
    const long long dist = d < 0 ? -d : d;
    // `<=` rather than `<`, over an ASCENDING table, is what makes a tie go up.
    // settings.h static_asserts the ascent, because this line is the only thing
    // that depends on it and no test can see the order change.
    if (bestDist < 0 || dist <= bestDist) {
      bestDist = dist;
      best = candidate;
    }
  }
  if (best == field) return true;
  field = best;
  return false;
}

// Resets an enum field to `fallback` when its value is not one of the
// enumerators, returning false when it had to. NOT A CLAMP, and the difference
// is the point: an integer outside an enum has no nearest meaningful neighbour,
// so a `sleepShows` of 47 clamped to the top would silently become `Details` --
// an intent the file never carried. The default is the only honest answer.
//
// The BOUND is `kSleepShowsCount` / `kCoverFitCount`, derived in settings.h from
// the last enumerator rather than written here, so an added mode cannot be
// rejected by a count nobody remembered to move.
template <typename E>
bool resetIfNotAnEnumerator(E& field, int count, E fallback) {
  const int v = static_cast<int>(field);
  if (v >= 0 && v < count) return true;
  field = fallback;
  return false;
}

// Reads an enum field written as its integer index. A value outside the
// enumerators keeps the DEFAULT rather than being clamped, for the reason above,
// and clears `ok` -- the boot log then says CORRECTED, which is the only way a
// user learns their hand-edited file was not applied. A wrong JSON TYPE keeps the
// default and clears `ok` too, which is readBool's rule.
template <typename E>
void readEnum(const JsonObject& o, const char* key, E& field, int count, bool& ok) {
  int64_t v = 0;
  if (!o.getInt(key, v)) {
    if (presentAsAnything(o, key)) ok = false;
    return;
  }
  // Compared as int64 BEFORE any narrowing, exactly as readClampedInt refuses to
  // narrow: `sleepShows: 4294967296` truncates to a perfectly plausible 0.
  if (v < 0 || v >= count) {
    ok = false;
    return;  // `field` still holds the default
  }
  field = static_cast<E>(static_cast<int>(v));
}

}  // namespace

bool Settings::validate() {
  bool ok = true;
  // 0 is "never sleep" and is a legitimate choice, so only a NONZERO timeout
  // meets the floor.
  if (sleepAfterMs != 0 && sleepAfterMs < kSleepAfterMsMin) {
    sleepAfterMs = kSleepAfterMsMin;
    ok = false;
  }
  if (sleepAfterMs > kSleepAfterMsMax) {
    sleepAfterMs = kSleepAfterMsMax;
    ok = false;
  }
  if (fullRefreshEvery < 0) {
    fullRefreshEvery = 0;  // RefreshPolicy::kNever
    ok = false;
  }
  if (fullRefreshEvery > kFullRefreshEveryMax) {
    fullRefreshEvery = kFullRefreshEveryMax;
    ok = false;
  }
  if (!snapToTable(bodyPpem, kBodyPpemSteps)) ok = false;
  if (!snapToTable(margins, kMarginSteps)) ok = false;
  if (!snapToTable(lineSpacing, kLineSpacingSteps)) ok = false;
  // `justify` is a bool: there is no invalid value to snap.
  // The two enums are RESET rather than clamped -- see the helper, and the
  // header's own note on why an enum is a third kind of correction.
  if (!resetIfNotAnEnumerator(sleepShows, kSleepShowsCount, Settings{}.sleepShows))
    ok = false;
  if (!resetIfNotAnEnumerator(coverFit, kCoverFitCount, Settings{}.coverFit)) ok = false;
  return ok;
}

bool loadSettings(FileSystem& fs, Settings& out) {
  // Defaults FIRST and unconditionally. Every failure below returns with `out`
  // holding a complete, working set rather than whatever the caller passed in or
  // whatever the file managed to fill before it went wrong.
  out = Settings{};

  std::string text;
  if (!fs.readAll(kSettingsPath, text)) return false;

  JsonObject o;
  if (!o.parse(text)) return false;

  int64_t version = 0;
  if (!o.getInt("version", version) || version != kSettingsVersion) return false;

  // Built on a copy and committed at the end, so a field that goes wrong
  // half-way through cannot leave `out` part file, part default.
  Settings parsed;
  bool ok = true;

  // sleepAfterMs does not go through readClampedInt: 0 means NEVER SLEEP, so it
  // is a valid value below the floor, while a negative is not "never" -- it is
  // nonsense, and nonsense clamps to the floor rather than silently disabling
  // sleep. validate() applies the nonzero floor afterwards.
  int64_t ms = 0;
  if (o.getInt("sleepAfterMs", ms)) {
    if (ms < 0) {
      parsed.sleepAfterMs = kSleepAfterMsMin;
      ok = false;
    } else if (ms > static_cast<int64_t>(kSleepAfterMsMax)) {
      parsed.sleepAfterMs = kSleepAfterMsMax;
      ok = false;
    } else {
      parsed.sleepAfterMs = static_cast<uint32_t>(ms);
    }
  } else if (presentAsAnything(o, "sleepAfterMs")) {
    ok = false;
  }

  readClampedInt(o, "fullRefreshEvery", parsed.fullRefreshEvery, 0, kFullRefreshEveryMax,
                 ok);
  readBool(o, "fullOnTransition", parsed.fullOnTransition, ok);
  readBool(o, "logToCard", parsed.logToCard, ok);

  // THE TABLE'S ENDS AS A RANGE, AND THE REASON IS THE NARROWING GUARD -- not,
  // as this comment first claimed, that `ok` needs clearing either way.
  // validate() below already clears `ok` for both cases, and sweeping every
  // value from -5 to 250 through both paths found ZERO disagreements. What this
  // layer alone contributes is readClampedInt's refusal to narrow an int64 by
  // cast: `bodyPpem: 4294967328` would otherwise truncate to a perfectly
  // plausible 32 and be accepted in silence, where clamping to the table's top
  // step is an obvious absurdity the boot log reports as CORRECTED. The snap
  // onto an actual step is validate()'s, just below.
  readSteppedInt(o, "bodyPpem", parsed.bodyPpem, kBodyPpemSteps, ok);
  readSteppedInt(o, "margins", parsed.margins, kMarginSteps, ok);
  readSteppedInt(o, "lineSpacing", parsed.lineSpacing, kLineSpacingSteps, ok);
  readBool(o, "justify", parsed.justify, ok);
  readEnum(o, "sleepShows", parsed.sleepShows, kSleepShowsCount, ok);
  readEnum(o, "coverFit", parsed.coverFit, kCoverFitCount, ok);

  if (!parsed.validate()) ok = false;

  out = parsed;
  return ok;
}

bool saveSettings(FileSystem& fs, const Settings& in) {
  Settings valid = in;
  valid.validate();  // the card never holds a value the loader has to clamp

  JsonObject o;
  o.setInt("version", kSettingsVersion);
  o.setInt("sleepAfterMs", static_cast<int64_t>(valid.sleepAfterMs));
  o.setInt("fullRefreshEvery", valid.fullRefreshEvery);
  o.setBool("fullOnTransition", valid.fullOnTransition);
  o.setBool("logToCard", valid.logToCard);
  o.setInt("bodyPpem", valid.bodyPpem);
  o.setInt("margins", valid.margins);
  o.setInt("lineSpacing", valid.lineSpacing);
  o.setBool("justify", valid.justify);
  // The enums go out as their integer index, which is what makes the order in
  // settings.h a stored format rather than a free choice: reordering the
  // enumerators would re-read every card's file as a different setting.
  o.setInt("sleepShows", static_cast<int64_t>(valid.sleepShows));
  o.setInt("coverFit", static_cast<int64_t>(valid.coverFit));
  // writeAll creates /.reader on the way past, so there is no mkdirs here.
  return fs.writeAll(kSettingsPath, o.dump());
}

}  // namespace reader
