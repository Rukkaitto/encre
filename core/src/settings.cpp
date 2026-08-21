#include "reader/settings.h"

#include <cstdint>
#include <string>

#include "reader/filesystem.h"
#include "reader/json.h"

namespace reader {
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

void readBool(const JsonObject& o, const char* key, bool& field, bool& ok) {
  bool v = false;
  if (o.getBool(key, v)) {
    field = v;
    return;
  }
  // 1 is not true and "yes" is not true: keep the default and tell the caller.
  if (presentAsAnything(o, key)) ok = false;
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
  // writeAll creates /.reader on the way past, so there is no mkdirs here.
  return fs.writeAll(kSettingsPath, o.dump());
}

}  // namespace reader
