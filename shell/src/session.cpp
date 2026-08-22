#include "session.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

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
//
// 1 -> 2: `scr` was the raw reader::ScreenId ORDINAL and is now a short stable
// STRING. The bump is not cosmetic and it is not optional -- see below.
constexpr uint8_t kVersion = 2;

// WHY THE SCREEN IS A STRING AND NOT A NUMBER, and why version 1 had to be
// thrown away rather than translated.
//
// Version 1 stored `static_cast<uint8_t>(s.screen)`, so the enum's DECLARATION
// ORDER was part of a persisted format. 2C-2 then inserted ItemActions,
// DeleteConfirm and BookDetails after Library, which moved Settings 2 -> 3,
// InputMonitor 3 -> 4 and SdMissing 4 -> 5. A record written by the firmware
// currently on a device says "Settings" by writing 2, and this build would have
// read that 2 as ItemActions: the user sleeps on Settings and wakes into an
// actions overlay for a book they never chose. It happens to fail safely today
// only because the factory refuses to build an overlay with no live Library
// under it -- which is luck, not design, and the luck runs out the moment a newly
// inserted id happens to be constructible.
//
// The version bump fixes THIS instance (an old record now reads as "no session"
// and the device starts at Home, exactly as a first boot does). The string fixes
// the CLASS: with a name on the wire, where an id sits in the enum stops being
// information anybody stored, so inserting a screen -- and V1 has two more phases
// of screens to add -- cannot silently rename an old record's contents again.
//
// An explicit enum -> integer table appended to by new members would also have
// removed the coupling. The string wins on the thing this project keeps needing,
// which is reading a device rather than reasoning about one: `nvs_get encre_sess
// scr str` prints `library`, and nobody has to hold a number-to-screen table in
// their head or in a doc that can go stale. It costs a handful of bytes of NVS
// and one strcmp loop per boot.
//
// The names are NOT screenName()'s. That is a log label and is free to be
// reworded for a human reading serial output; this is a storage format and must
// not move when someone improves a log line. Two spellings for one thing is the
// point.
struct WireName {
  reader::ScreenId id;
  const char* name;
};

// STABLE FOREVER. A row may be added; a row's `name` may never be edited, and a
// row must not be removed while any device might still hold a record naming it
// (removing one is a version bump, and then it is just a record we do not
// understand -- which is a defined outcome).
//
// Every name fits NVS's 15-character value-free budget comfortably and is
// lower-case-with-hyphens so it is unambiguous to type at an idf console.
constexpr WireName kWireNames[] = {
    {reader::ScreenId::Home, "home"},
    {reader::ScreenId::Library, "library"},
    {reader::ScreenId::ItemActions, "item-actions"},
    {reader::ScreenId::DeleteConfirm, "delete-confirm"},
    {reader::ScreenId::BookDetails, "book-details"},
    {reader::ScreenId::Settings, "settings"},
    {reader::ScreenId::InputMonitor, "input-monitor"},
    {reader::ScreenId::SdMissing, "sd-missing"},
};

// Longest name plus its terminator, so the read buffer below is derived rather
// than a number someone keeps in step by hand.
constexpr size_t kMaxWireBytes = sizeof("delete-confirm");

// The wire name for `id`, or nullptr for an id this build will not store.
//
// A SWITCH AND NOT A LOOKUP, deliberately, and this is the only reason it is not
// a lambda over kWireNames: adding a member to reader::ScreenId makes -Wswitch
// point the compiler at this function, which is the prompt to add the row above.
// Forgetting the row anyway is the safe direction -- the new screen is simply not
// restorable, and saveSession says so out loud -- but nobody should have to
// discover that from a serial log.
const char* wireNameOf(reader::ScreenId id) {
  switch (id) {
    case reader::ScreenId::Home: return kWireNames[0].name;
    case reader::ScreenId::Library: return kWireNames[1].name;
    case reader::ScreenId::ItemActions: return kWireNames[2].name;
    case reader::ScreenId::DeleteConfirm: return kWireNames[3].name;
    case reader::ScreenId::BookDetails: return kWireNames[4].name;
    case reader::ScreenId::Settings: return kWireNames[5].name;
    case reader::ScreenId::InputMonitor: return kWireNames[6].name;
    case reader::ScreenId::SdMissing: return kWireNames[7].name;
  }
  return nullptr;
}

// NEVER CAST ANYTHING INTO A ScreenId. An unrecognised wire value is "no
// session", not a best-effort decode: NVS survives a firmware update in both
// directions, so the string may have been written by a build that knows screens
// this one cannot construct, or by a build that has since renamed one. Home is
// the answer to both.
bool decodeScreenId(const char* raw, reader::ScreenId& out) {
  if (raw == nullptr || raw[0] == '\0') return false;
  for (const WireName& w : kWireNames) {
    if (std::strcmp(raw, w.name) == 0) {
      out = w.id;
      return true;
    }
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

  // THE VERSION IS READ AND CHECKED BEFORE THE PAYLOAD IS TOUCHED AT ALL, which
  // matters now that `scr` changed type between versions: a version-1 record has
  // a uint8 under that key, and asking for a string there is a question about a
  // record we have already decided not to trust. (Preferences::getString does
  // refuse a key of the wrong type, so this is belt and braces -- but relying on
  // that would be relying on a library's error path to enforce our format.)
  const uint8_t version = prefs.getUChar(kKeyVersion, 0);
  if (version != kVersion) {
    prefs.end();
    Serial.printf("[session] record version %u is not %u; starting at Home. (Version 1 stored "
                  "the screen as a raw enum ORDINAL, and 2C-2 inserted three screens into the "
                  "middle of that enum -- so a version-1 record is discarded rather than "
                  "decoded, which is exactly what the bump is for)\n",
                  version, kVersion);
    Serial.flush();
    return false;
  }
  char rawScreen[kMaxWireBytes] = {0};
  // 0 means absent, wrong type, or longer than this buffer -- all of which are
  // "not a name this build wrote", and decodeScreenId refuses an empty string.
  prefs.getString(kKeyScreen, rawScreen, sizeof(rawScreen));
  const uint16_t focus = prefs.getUShort(kKeyFocus, 0);
  prefs.end();

  reader::ScreenId screen;
  if (!decodeScreenId(rawScreen, screen)) {
    Serial.printf("[session] stored screen '%s' is not a name this build knows; starting at "
                  "Home\n",
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

  // Refuse before opening the namespace rather than writing a name we would not
  // read back. Only reachable if a ScreenId gained a member and kWireNames did
  // not -- the -Wswitch prompt in wireNameOf() was ignored -- so it says so.
  const char* wire = wireNameOf(s.screen);
  if (wire == nullptr) {
    Serial.printf("[session] screen=%s has no stored name in this build, so it cannot be "
                  "restored; add a row to kWireNames in session.cpp\n",
                  reader::screenName(s.screen));
    Serial.flush();
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.printf("[session] cannot open NVS namespace %s for write\n", kNamespace);
    Serial.flush();
    return false;
  }
  // THE VERSION KEY GOES LAST, because it is the only key loadSession()
  // validates, so writing it after the payload means a half-finished write reads
  // as "no session" rather than as a valid pointer to a stale screen.
  //
  // BUT IT IS A COMMIT RECORD ONLY FOR THE FIRST WRITE, and the comment here
  // claimed otherwise until a review pointed it out. On an UPDATE the previous
  // record's version key is already present and valid, so it cannot gate
  // anything: a power cut after putString(scr) and before putUShort(focus)
  // leaves version + NEW screen + OLD focus, which reads back as a perfectly
  // valid mixed record. The failure branch below only covers a put that
  // *returns* an error, not a cut between two of them.
  //
  // Left as-is deliberately. Each nvs_set is individually atomic, so the blast
  // radius is one stale field; the field is a focus index, and a wrong one is
  // clamped by setFocus on restore. A real fix is one blob instead of three
  // keys, which is a storage-format change for a cosmetic symptom.
  bool ok = prefs.putString(kKeyScreen, wire) == std::strlen(wire) &&
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
