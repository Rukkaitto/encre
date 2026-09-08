#pragma once
#include <string>
#include <vector>

#include "reader/app.h"  // reader::ScreenId, reader::StackEntry, App::kMaxDepth

namespace reader {

// THE WAKE RECORD'S WIRE FORMAT: `home:-1;library:7:/books;item-actions:1`, root
// first. An entry is `name:focus` or `name:focus:place`.
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
//
// THE THIRD FIELD IS Screen::place() -- WHAT THE FOCUS IS AN INDEX INTO -- AND IT
// IS WHY THE RECORD WENT TO VERSION 5 (#14). The Library can be listing a
// SUBFOLDER of /books and the record could not say which, so sleeping in
// /books/Classics on row 3 woke on /books row 3: a plausible-looking wrong row,
// which is worse than losing the position. It is written only for a screen that
// has one, so every other entry is byte-for-byte the two-field form this replaces
// -- and a version-4 record still PARSES here. It is discarded on its version
// anyway, deliberately: an old record's `library:7` means "row 7 of some
// directory", and honouring it is exactly the wrong row this format exists to
// stop claiming. The cost is one wake per device, the first after this firmware
// lands, which starts at Home.
//
// core/ NEVER LEARNS WHAT A PLACE IS. The Library's is a directory path; this
// file knows only that it is bytes, and what may not appear in them raw.
//
// PERCENT-ESCAPED, BECAUSE A FILENAME MAY LEGALLY CONTAIN WHAT THE FORMAT USES.
// `%`, `;` and `:` become `%25`, `%3B` and `%3A`, and so does any control byte --
// `;` and `%` are both legal in a FAT long name, and a wire format a legal
// filename can break is not a fix. EVERYTHING ELSE PASSES THROUGH, UTF-8
// INCLUDED, because `nvs_get encre_sess stack str` printing `library:7:/books/Le
// Fléau` is the same property that made the screen a NAME rather than an ordinal:
// a record a person can read off a device is one they can diagnose. (`:` cannot
// occur in a FAT or exFAT name at all, so the only escapes a real card produces
// are `%` and `;`.)
//
// A MALFORMED ESCAPE REFUSES THE WHOLE RECORD, on the unknown-name rule: a place
// that cannot be decoded is a record written by something that is not this
// format, and the entries around it may not mean what they say either. A place
// too LONG is a different question and has a different answer -- see
// encodeSessionStack.

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
//
// A PLACE IS BOUNDED THE SAME WAY AND FOR THE SAME REASON, and this is the one
// place the bound is enforced: a place whose ESCAPED form does not fit
// kPlaceMaxBytes is DROPPED WHOLE -- never truncated, because a truncated path
// addresses a different directory rather than none, which is the reasoning
// Xml::kMaxAttrBytes reached from the other side ("an attribute too long to hold
// reads as absent"). The bound is on the escaped form so it holds whatever the
// path contains.
//
// AND THE ENTRY'S FOCUS GOES WITH IT, written as -1. A row index without the
// folder it indexes is the whole of #14, so the two cannot be dropped separately
// -- and -1 is not a special marker but the real position every focused screen
// already accepts, "nothing selected", which clamps to the top of whatever list
// the restore does build.
std::string encodeSessionStack(const std::vector<StackEntry>& stack);

// True when `raw` is a record this build can use, and `out` then holds it. False
// leaves `out` EMPTY, and means exactly one thing to the caller: start where the
// boot path put you. Refused: null or empty, an unknown screen name, a missing or
// non-numeric focus, an empty entry, a trailing separator, an empty or malformed
// place, and a stack deeper than App::kMaxDepth -- which could only ever be
// half-restored.
//
// NO LENGTH LIMIT IS APPLIED TO A PLACE ON THE WAY IN, and that is not an
// oversight: the bound exists so a CALLER's read buffer can be derived
// (sessionStackMaxBytes), and anything that fit that buffer is by definition
// within it. A longer place could only come from a build with a bigger cap, and
// the screen it is handed to is the thing entitled to refuse it -- the Library
// refuses a path that is not under the root it was given, whatever its length.
bool decodeSessionStack(const char* raw, std::vector<StackEntry>& out);

// The longest string encodeSessionStack can produce, plus its terminator, so a
// caller's read buffer is derived rather than a number kept in step by hand.
size_t sessionStackMaxBytes();

}  // namespace reader
