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
//   keys:      "ver"    uint8   record version, currently 1
//              "scr"    uint8   reader::ScreenId of the top of the stack
//              "focus"  uint16  that screen's focus index
//
// e.g. from an esp-idf console: `nvs_get encre_sess scr u8`
struct Session {
  reader::ScreenId screen = reader::ScreenId::Home;
  uint16_t focus = 0;
};

// True when a usable record was found. False means "no session" -- no namespace
// yet, a version this firmware does not know, or a ScreenId it cannot build --
// and `out` is left alone. Callers start at Home on false.
bool loadSession(Session& out);

// True when the record is stored, or was already exactly this. Cheap to call on
// every screen change: an unchanged record is not rewritten, so navigating back
// and forth does not grind the NVS partition.
bool saveSession(const Session& s);

// Forgets the session. The cold-boot path calls this: a device that boots into a
// Settings sub-screen after a week off is confusing, so the record is only for a
// genuine wake.
bool clearSession();
