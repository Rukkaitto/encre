#include "session.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

// See session.h for what these are and how to read them off a device. NVS caps a
// key at 15 characters and a namespace at 15 too, so they are all short; the
// names are also what a future `nvs_get` invocation needs, which is why they live
// in one place rather than inline at each call.
constexpr const char* kNamespace = "encre_sess";
constexpr const char* kKeyVersion = "ver";
constexpr const char* kKeyScreen = "scr";
constexpr const char* kKeyFocus = "focus";

// Bumped when the record's SHAPE changes, not when a value does. An unknown
// version is "no session" rather than a best-effort decode, on the same principle
// as the settings file: replacing a record we do not understand beats trusting it.
constexpr uint8_t kVersion = 1;

// NEVER cast the stored integer to a ScreenId and hand it out. NVS survives a
// firmware update, the enum gained SdMissing this phase and will gain more, and a
// record written by a newer build can name a screen this one cannot construct --
// which would be a null screen pushed onto the stack.
//
// A switch and not a range check, deliberately: it enumerates the ids a session
// may name, so adding a value to ScreenId makes the compiler point at this
// function (-Wswitch) instead of silently opting the new screen into being
// restorable. Casting to the enum first is well-defined here because ScreenId has
// a fixed underlying type (uint8_t), so every byte is a valid representation.
bool decodeScreenId(uint8_t raw, reader::ScreenId& out) {
  const auto id = static_cast<reader::ScreenId>(raw);
  switch (id) {
    case reader::ScreenId::Home:
    case reader::ScreenId::Library:
    case reader::ScreenId::Settings:
    case reader::ScreenId::InputMonitor:
    case reader::ScreenId::SdMissing:
      out = id;
      return true;
  }
  return false;
}

// The last record known to be in NVS, so an unchanged save can skip the store
// entirely. File-scope rather than a static inside saveSession because
// clearSession has to be able to invalidate it -- a stale cache would make the
// save after a clear look redundant and silently skip it.
bool gCachedValid = false;
Session gCached{};

}  // namespace

bool loadSession(Session& out) {
  Preferences prefs;
  // readOnly = true fails when the namespace has never been written.
  //
  // That used to return false in silence, on the reasoning that it is the
  // ordinary first-boot case. It is not only that case, and the silence cost a
  // diagnosis: "the namespace does not exist" and "a save was attempted and did
  // not stick" are the same observation from up here, and both of them look
  // exactly like a wake that came back to Home for no reason. So it says which
  // one it is now -- if this line appears on a wake AFTER a "[session] stored
  // screen=..." line from the previous run, the write is the thing that is
  // broken, not the read.
  if (!prefs.begin(kNamespace, true)) {
    Serial.printf("[session] NVS namespace %s does not exist: nothing has ever been "
                  "stored in it, or it was cleared and not written since\n",
                  kNamespace);
    Serial.flush();
    return false;
  }

  const uint8_t version = prefs.getUChar(kKeyVersion, 0);
  const uint8_t rawScreen = prefs.getUChar(kKeyScreen, 0xFF);
  const uint16_t focus = prefs.getUShort(kKeyFocus, 0);
  prefs.end();

  if (version != kVersion) {
    Serial.printf("[session] record version %u is not %u; starting at Home\n", version, kVersion);
    Serial.flush();
    return false;
  }
  reader::ScreenId screen;
  if (!decodeScreenId(rawScreen, screen)) {
    Serial.printf("[session] stored screen id %u is not one this build knows; starting at Home\n",
                  rawScreen);
    Serial.flush();
    return false;
  }

  out.screen = screen;
  out.focus = focus;
  Serial.printf("[session] restored screen=%s focus=%u\n", reader::screenName(screen), focus);
  Serial.flush();
  return true;
}

bool saveSession(const Session& s) {
  // Skip an identical rewrite. NVS is wear-levelled and a screen change is a rare
  // event by flash standards, but the record is written on EVERY change to the top
  // of the stack, and the cheapest write is the one not made. (esp-idf's nvs_set_*
  // also compares before writing; this is belt and braces, and it saves opening
  // the namespace at all.)
  if (gCachedValid && gCached.screen == s.screen && gCached.focus == s.focus) return true;

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.printf("[session] cannot open NVS namespace %s for write\n", kNamespace);
    Serial.flush();
    return false;
  }
  // THE VERSION KEY GOES LAST, and that ordering is the whole integrity story
  // for this record: it is the only key loadSession() validates, so writing it
  // after the payload means a write that dies half way leaves a record that reads
  // as "no session" rather than as a valid pointer to a stale screen. Same shape
  // as a commit record, for the same reason.
  bool ok = prefs.putUChar(kKeyScreen, static_cast<uint8_t>(s.screen)) == sizeof(uint8_t) &&
            prefs.putUShort(kKeyFocus, s.focus) == sizeof(uint16_t) &&
            prefs.putUChar(kKeyVersion, kVersion) == sizeof(uint8_t);
  // ...and if it did die half way, take the version with it, or a version left
  // over from the PREVIOUS good record would validate this half-written one.
  if (!ok) prefs.remove(kKeyVersion);
  prefs.end();

  if (!ok) {
    gCachedValid = false;  // the store and the cache have diverged; do not trust it
    Serial.printf("[session] NVS write failed for screen=%s focus=%u\n",
                  reader::screenName(s.screen), s.focus);
    Serial.flush();
    return false;
  }
  gCached = s;
  gCachedValid = true;
  return true;
}

bool clearSession() {
  Preferences prefs;
  // Nothing to clear when the namespace was never created, and that is a success:
  // callers care that there is no session afterwards.
  if (!prefs.begin(kNamespace, false)) {
    gCachedValid = false;
    return true;
  }
  const bool ok = prefs.clear();
  prefs.end();
  // The cache must not outlive the record it describes: leaving it valid would
  // make the first save after a clear look redundant, and it would be skipped.
  gCachedValid = false;
  return ok;
}
