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
static_assert(Settings{}.lineSpacing == kBodyLeadEm &&
              kLineSpacingSteps[2] == kBodyLeadEm);

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
  int best;
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
  // writeAll creates /.reader on the way past, so there is no mkdirs here.
  return fs.writeAll(kSettingsPath, o.dump());
}

}  // namespace reader
