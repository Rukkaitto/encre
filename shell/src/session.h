#pragma once
#include <cstdint>

#include "reader/app.h"  // reader::ScreenId

// Where the user was, so a wake can put them back.
//
// WHY NVS AND NOT THE CARD: a wake has to work with no card in the slot. That is
// the entire reason the SD-missing screen exists, and a resume pointer that lived
// on the card would be unreadable in exactly the state the firmware most needs to
// behave -- boot with no storage. Spec 5 puts the last-open pointer in NVS for the
// same reason.
//
// Deep sleep is a chip reset, so RAM is gone by the time this is read; NVS is the
// only thing that crosses it.
//
// ON-DEVICE FORENSICS. The record is three keys in one NVS namespace, and NVS keys
// cap at 15 characters, so they are short. Named here so a future reader can dump
// them without going through this code:
//
//   namespace: "encre_sess"   (session only -- settings get their own, so
//                              clearSession() can clear the whole namespace
//                              without collateral damage)
//   keys:      "ver"    uint8   record version, currently 2
//              "scr"    str     the top screen's WIRE NAME: "home", "library",
//                               "settings", ... -- see kWireNames in session.cpp
//              "focus"  uint16  that screen's focus index
//
// e.g. from an esp-idf console: `nvs_get encre_sess scr str`
//
// THE SCREEN IS A NAME, NOT A NUMBER, and version 1 is why. It stored the raw
// reader::ScreenId ordinal, which made the enum's declaration order part of a
// persisted format -- and 2C-2 then inserted three screens after Library, moving
// Settings from 2 to 3. A version-1 record therefore says "Settings" by writing a
// 2 that this build would read as ItemActions. The version bump discards those
// records; the name is what stops the next inserted screen from doing it again.
// session.cpp holds the full argument.
struct Session {
  reader::ScreenId screen = reader::ScreenId::Home;
  // An index into the screen's WHOLE list (Screen::focus()), not into the slice
  // on glass. Unsigned, so a caller with a "nothing selected" -1 must decide what
  // that means before it gets here -- shell/src/main.cpp stores 0.
  uint16_t focus = 0;
};

// True when a usable record was found. False means "no session" -- no namespace
// yet, a version this firmware does not know, or a screen NAME it does not
// recognise -- and `out` is left alone. Callers start at Home on false. There is
// no path that casts an unrecognised wire value into a ScreenId.
bool loadSession(Session& out);

// True when the record is stored, or was already exactly this. Cheap to call on
// every screen change: an unchanged record is not rewritten, so navigating back
// and forth does not grind the NVS partition.
bool saveSession(const Session& s);

// Forgets the session. The cold-boot path calls this: a device that boots into a
// Settings sub-screen after a week off is confusing, so the record is only for a
// genuine wake.
bool clearSession();

// --- "Was that a resume, or a cold start?" -----------------------------------
//
// THE RESET REASON CANNOT ANSWER THAT ON THIS DEVICE, which is what these two
// exist for. Measured: with USB attached the chip deep-sleeps and comes back as
// ESP_RST_DEEPSLEEP, and on battery the same sleep leaves it fully powered down,
// so pressing power produces ESP_RST_POWERON -- indistinguishable from a first-ever
// boot. The restore is gated on "did we wake", so on battery it correctly declined
// every time and then cleared a perfectly good record. The user saw "it always
// comes back to Home", which is the symptom of the gate being right and the
// question being unanswerable.
//
// So the intent is recorded before sleeping rather than inferred afterwards. A
// deliberate sleep sets the flag; the next boot takes it (reading CLEARS it) and
// treats itself as a resume regardless of what the reset reason says.
//
// TAKING IT CLEARS IT, deliberately: a boot that sets out to resume and then
// panics must not resume again on the next boot, and again after that. One flag
// buys exactly one resume.
//
//   key: "slept"  uint8  1 = the last shutdown was a deliberate sleep
bool markSleeping();
bool takeSleptFlag();
