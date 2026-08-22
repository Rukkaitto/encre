#include "session.h"

#include <Arduino.h>
#include <Preferences.h>

#include <string>
#include <vector>

#include "reader/session_record.h"

namespace {

// See session.h for what these are and how to read them off a device. NVS caps a
// key at 15 characters and a namespace at 15 too, so they are all short; the
// names are also what a future `nvs_get` invocation needs, which is why they live
// in one place rather than inline at each call.
constexpr const char* kNamespace = "encre_sess";
constexpr const char* kKeySlept = "slept";
constexpr const char* kKeyVersion = "ver";
constexpr const char* kKeyStack = "stack";

// Bumped when the record's SHAPE changes, not when a value does. An unknown
// version is "no session" rather than a best-effort decode, on the same principle
// as the settings file: replacing a record we do not understand beats trusting it.
//
// 1 -> 2: `scr` was the raw reader::ScreenId ORDINAL and became a stable STRING.
//   2C-2 had inserted three screens into the middle of that enum, so a version-1
//   record saying "Settings" by writing a 2 would have been read as an actions
//   overlay for a book the user never chose.
// 2 -> 3: `focus` went from uint16 to int16, so that -1 -- a real position, and on
//   Home the CONTINUE block -- stopped being flattened to row 0.
// 3 -> 4: `scr` and `focus` became one `stack` string holding the WHOLE stack.
//   See session.h for why, and for what the single-screen form could not restore.
//
// Each bump discards the records before it, which costs exactly one wake per
// device -- the first after this firmware lands -- and cannot be misread. The
// alternative, translating an old record, is how a stored 2 becomes the wrong
// screen.
constexpr uint8_t kVersion = 4;

// The last record known to be in NVS, so an unchanged save can skip the store
// entirely. Held in its ENCODED form, because that is the thing that is actually
// in flash and comparing two strings needs no equality operator reaching into
// core/.
//
// File-scope rather than a static inside saveSession because clearSession has to
// be able to invalidate it -- a stale cache would make the save after a clear look
// redundant and silently skip it.
bool gCachedValid = false;
std::string gCached;

}  // namespace

bool loadSession(std::vector<reader::StackEntry>& out) {
  out.clear();

  Preferences prefs;
  // readOnly = true fails when the namespace has never been written.
  //
  // That used to return false in silence, on the reasoning that it is the
  // ordinary first-boot case. It is not only that case, and the silence cost a
  // diagnosis: "the namespace does not exist" and "a save was attempted and did
  // not stick" are the same observation from up here, and both of them look
  // exactly like a wake that came back to Home for no reason. So it says which
  // one it is now -- if this line appears on a wake AFTER a "[session] stored"
  // line from the previous run, the write is the thing that is broken, not the
  // read.
  if (!prefs.begin(kNamespace, true)) {
    Serial.printf("[session] NVS namespace %s does not exist: nothing has ever been "
                  "stored in it, or it was cleared and not written since\n",
                  kNamespace);
    Serial.flush();
    return false;
  }

  // THE VERSION IS READ AND CHECKED BEFORE THE PAYLOAD IS TOUCHED AT ALL, which
  // matters because the payload has changed shape twice: asking for a string
  // under `stack` in a version-3 record is a question about a record we have
  // already decided not to trust. (Preferences does refuse a key of the wrong
  // type, so this is belt and braces -- but relying on that would be relying on a
  // library's error path to enforce our format.)
  const uint8_t version = prefs.getUChar(kKeyVersion, 0);
  if (version != kVersion) {
    prefs.end();
    Serial.printf("[session] record version %u is not %u; staying where the boot path put "
                  "us. An older record stored one screen rather than the whole stack, and "
                  "is discarded rather than decoded -- which is what the bump is for\n",
                  version, kVersion);
    Serial.flush();
    return false;
  }

  // Sized by the format itself rather than by a number kept in step by hand. 0
  // back means absent, the wrong type, or longer than this buffer -- all of which
  // are "not a record this build wrote", and decodeSessionStack refuses an empty
  // string.
  std::vector<char> raw(reader::sessionStackMaxBytes(), '\0');
  prefs.getString(kKeyStack, raw.data(), raw.size());
  prefs.end();

  if (!reader::decodeSessionStack(raw.data(), out)) {
    Serial.printf("[session] stored stack '%s' is not one this build can use; staying where "
                  "the boot path put us\n",
                  raw.data());
    Serial.flush();
    return false;
  }

  Serial.printf("[session] record holds %u screen(s): %s\n", static_cast<unsigned>(out.size()),
                reader::encodeSessionStack(out).c_str());
  Serial.flush();
  return true;
}

bool saveSession(const std::vector<reader::StackEntry>& stack) {
  const std::string wire = reader::encodeSessionStack(stack);

  // Skip an identical rewrite. NVS is wear-levelled and a screen change is a rare
  // event by flash standards, but the record is written on EVERY change to the
  // stack, and the cheapest write is the one not made. (esp-idf's nvs_set_* also
  // compares before writing; this is belt and braces, and it saves opening the
  // namespace at all.)
  if (gCachedValid && gCached == wire) return true;

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.printf("[session] cannot open NVS namespace %s for write\n", kNamespace);
    Serial.flush();
    return false;
  }
  // THE VERSION KEY GOES LAST, because it is the only key loadSession()
  // validates, so a write that dies half way reads back as "no session" rather
  // than as a valid pointer to a stale stack. With one payload key that is a real
  // commit record and not just an ordering preference -- see session.h.
  bool ok = prefs.putString(kKeyStack, wire.c_str()) == wire.size() &&
            prefs.putUChar(kKeyVersion, kVersion) == sizeof(uint8_t);
  // ...and if it did die half way, take the version with it, or a version left
  // over from the PREVIOUS good record would validate this half-written one.
  if (!ok) prefs.remove(kKeyVersion);
  prefs.end();

  if (!ok) {
    gCachedValid = false;  // the store and the cache have diverged; do not trust it
    Serial.printf("[session] NVS write failed for stack %s\n", wire.c_str());
    Serial.flush();
    return false;
  }
  gCached = wire;
  gCachedValid = true;
  return true;
}

bool markSleeping() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    Serial.printf("[session] could not open %s to record the sleep; the next boot will look "
                  "like a cold start and land on Home\n",
                  kNamespace);
    Serial.flush();
    return false;
  }
  const bool ok = prefs.putUChar(kKeySlept, 1) == sizeof(uint8_t);
  prefs.end();
  if (!ok) {
    Serial.printf("[session] the sleep flag did not store; the next boot will land on Home\n");
    Serial.flush();
  }
  return ok;
}

bool takeSleptFlag() {
  Preferences prefs;
  // Read-write, because taking the flag clears it -- see the header. A read-only
  // open here would report a resume on every boot after the first.
  if (!prefs.begin(kNamespace, false)) return false;
  const uint8_t slept = prefs.getUChar(kKeySlept, 0);
  if (slept) prefs.remove(kKeySlept);
  prefs.end();
  return slept != 0;
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
