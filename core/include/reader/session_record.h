#pragma once
#include <string>
#include <vector>

#include "reader/app.h"  // reader::ScreenId, reader::StackEntry, App::kMaxDepth

namespace reader {

// THE WAKE RECORD'S WIRE FORMAT: `home:-1;library:7;item-actions:1`, root first.
//
// IT LIVES IN core/ SO IT CAN BE TESTED. shell/ has no test harness, and this is
// the one piece of the resume path that is pure logic -- the rest is NVS calls
// and a serial log. Everything that decides whether a record is usable is here;
// shell/src/session.cpp does the two Preferences calls around it and nothing else.
//
// THE SCREEN IS A NAME, NOT A NUMBER, and version 1 of the record is why. It
// stored the raw ScreenId ORDINAL, which made the enum's declaration order part
// of a persisted format -- and 2C-2 then inserted ItemActions, DeleteConfirm and
// BookDetails after Library, moving Settings 2 -> 3. A record saying "Settings"
// by writing a 2 would have been read as an actions overlay for a book the user
// never chose. With a name on the wire, where an id sits in the enum stops being
// information anybody stored. It also means `nvs_get encre_sess stack str` prints
// something a person can read off a device, which is the thing this project keeps
// needing.
//
// AN UNRECOGNISED NAME IS "NO SESSION", never a best-effort decode: NVS survives
// a firmware update in both directions, so a name may have been written by a
// build that knows screens this one cannot construct. Nothing here casts an
// integer into a ScreenId.

// The stored name for `id`, never null. STABLE FOREVER: a name may never be
// edited while any device might hold a record containing it (editing one is a
// record version bump, after which it is just a record we do not understand --
// which is a defined outcome). These are NOT screenName()'s strings: that is a
// log label, free to be reworded for a human reading serial output, and this is a
// storage format. Two spellings for one thing is the point.
const char* sessionWireName(ScreenId id);

// Root first. Each entry's focus is clamped to [-1, 32767] so the string has a
// bound -- a focus is a row index, and the record's length must not depend on how
// big one gets. (The clamp predates the string: it used to be there because the
// focus lived in a uint16 NVS key. Same numbers, a different reason.)
std::string encodeSessionStack(const std::vector<StackEntry>& stack);

// True when `raw` is a record this build can use, and `out` then holds it. False
// leaves `out` EMPTY, and means exactly one thing to the caller: start where the
// boot path put you. Refused: null or empty, an unknown screen name, a missing or
// non-numeric focus, an empty entry, a trailing separator, and a stack deeper
// than App::kMaxDepth -- which could only ever be half-restored.
bool decodeSessionStack(const char* raw, std::vector<StackEntry>& out);

// The longest string encodeSessionStack can produce, plus its terminator, so a
// caller's read buffer is derived rather than a number kept in step by hand.
size_t sessionStackMaxBytes();

}  // namespace reader
