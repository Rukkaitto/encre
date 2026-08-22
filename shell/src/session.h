#pragma once
#include <cstdint>
#include <vector>

#include "reader/app.h"  // reader::StackEntry

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
// ON-DEVICE FORENSICS. The record is two keys in one NVS namespace, and NVS keys
// cap at 15 characters, so they are short. Named here so a future reader can dump
// them without going through this code:
//
//   namespace: "encre_sess"   (session only -- settings get their own, so
//                              clearSession() can clear the whole namespace
//                              without collateral damage)
//   keys:      "ver"    uint8   record version, currently 4
//              "stack"  str     the whole stack, root first, as
//                               `home:-1;library:7;item-actions:1`
//
// e.g. from an esp-idf console: `nvs_get encre_sess stack str`
//
// IT IS THE WHOLE STACK, NOT THE TOP SCREEN, and version 4 is that change.
// Version 3 and earlier stored one screen and one focus, so Home > Library >
// actions came back as HOME: the restore pushed the overlay onto a fresh app, the
// factory refused it -- correctly, since an overlay reads the focused row of the
// Library under it and there was no Library under it -- and the user landed at the
// root having lost both the screen and the row. Restoring in order fixes the
// refusal as a side effect, because the Library is pushed first and is the thing
// the overlay then finds.
//
// It also deleted the special cases. "The record names Home, which is already the
// root" and "the record says SD-MISSING but the card mounted" were two hand-written
// branches in shell/src/main.cpp, and each screen that was not in that ladder
// silently lost the user's place. Both are now one question -- does the record's
// root match this app's root -- asked by App::restore, which names no screen at
// all.
//
// THE FORMAT, AND EVERY DECISION ABOUT WHETHER A RECORD IS USABLE, LIVE IN core/
// (reader/session_record.h). shell/ has no test harness and that part is pure
// logic, so it is tested on the desktop; what is left here is the Preferences
// calls and the log lines around them.
//
// ONE PAYLOAD KEY IS ALSO WHY THE COMMIT RECORD WORKS NOW. With `scr` and `focus`
// as separate keys, a power cut between them left version + new screen + old
// focus, which read back as a perfectly valid mixed record -- documented as
// accepted, because the blast radius was one index that setFocus would clamp.
// There is nothing to interleave with one payload key: the string is either the
// old one or the new one, and each nvs_set is individually atomic.

// True when a usable record was found, and `out` then holds it root first. False
// means "no session" -- no namespace yet, a version this firmware does not know,
// or a stack it cannot decode -- and `out` is left empty. Callers stay where the
// boot path put them.
bool loadSession(std::vector<reader::StackEntry>& out);

// True when the record is stored, or was already exactly this. Cheap to call on
// every screen change: an unchanged record is not rewritten, so navigating back
// and forth does not grind the NVS partition.
bool saveSession(const std::vector<reader::StackEntry>& stack);

// Forgets the session. The cold-boot path calls this: a device that boots into a
// Settings sub-screen after a week off is confusing, so the record is only for a
// genuine wake. It clears the whole namespace, which is also what finally removes
// the dead `scr` and `focus` keys a device upgraded from version 3 still carries.
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
