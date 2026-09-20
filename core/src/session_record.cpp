#include "reader/session_record.h"

#include <cstdlib>
#include <cstring>

#include "reader/wire_escape.h"

namespace reader {
namespace {

// A SWITCH AND NOT A TABLE LOOKUP, deliberately: adding a member to ScreenId
// makes -Wswitch point the compiler at this function, which is the prompt to give
// the new screen a name. Forgetting is the one way a screen silently never
// restores, and test_session_record.cpp asks for every id's name so a forgotten
// one fails on the desktop rather than on a device.
// IN ENUM ORDER: decodeName indexes this by ScreenId's ordinal. That coupling is
// local and checked -- test_session_record.cpp asks every id for its name and
// asserts they are all distinct -- and it is not the coupling version 1 got wrong,
// which was putting the ordinal ON THE WIRE.
// IN ENUM ORDER, because decodeName maps an index straight back to ScreenId(i).
// Appending a name rather than inserting it at its enum position silently maps
// every id after the insertion point to the wrong name -- which encodes a
// Reader as "sd-missing" and restores an SdMissing as a Reader.
constexpr const char* kNames[] = {
    "home", "library", "item-actions", "delete-confirm",
    "book-details", "settings", "sleep", "reader", "reader-menu",
    "contents", "sd-missing", "typography", "peek", "book-end",
    "book-error", "battery-empty",
    // The V1.1 connect flow. Hyphenated like their neighbours, and STABLE
    // FOREVER from here: a name may never be edited while any device might
    // hold a record containing it.
    "wifi-settings", "wifi-picker", "wifi-password", "wifi-connect",
    "wifi-error", "wifi-network-actions",
    // Articles over wallabag, and stable forever from here for the reason above.
    "articles", "article-actions", "article-end",
    "wallabag-account", "wallabag-connecting", "wallabag-error",
    "articles-remove-confirm",
};

// TIED TO THE SENTINEL, NOT TO A NAMED MEMBER, AND THAT IS #42's WHOLE POINT.
// This line used to read `BookEnd + 1`, so appending BatteryEmpty left both
// sides equal and it said NOTHING -- the same silence test_focus_restore.cpp's
// guard produced for Typography and then BookEnd. Three bounds in this file
// were once spelled `<= ScreenId::Peek`, which left the table short, the decode
// loop unable to see the new name, and sessionWireName's fall-through storing
// the new screen as `home`.
//
// It works: appending the six connect-flow screens failed this assert before a
// line of them was written, which is the guard doing its job at the moment it
// was written for rather than one append later.
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(ScreenId::Count),
              "a ScreenId was added or removed; give it a row in kNames, IN ENUM"
              " ORDER -- sessionWireName and decodeName both index this table by"
              " ordinal, so a name at the wrong position encodes one screen as"
              " another. This names the Count SENTINEL, never a member: a named"
              " member does not move when a screen is appended, which is how"
              " Typography and then BookEnd each shipped serialising as `home`.");

// Clamped so the encoded length is bounded. -1 is the floor rather than 0 because
// it is a real position: Home's CONTINUE block, an empty Library.
constexpr int kFocusMin = -1;
constexpr int kFocusMax = 32767;

// WHAT A PLACE MAY COST THE RECORD, in ESCAPED bytes, so the bound holds whatever
// the path contains rather than whatever it happens to be made of.
//
// It bounds the record and nothing else: eight entries of a name, a focus, a
// place and their separators is 1,209 bytes, which is a derived read buffer in
// shell/src/session.cpp and comfortably inside NVS's 4,000-byte cap for a string.
// A cap large enough for every path a card can hold would not be -- one FAT long
// name is 255 characters and a path is components of them -- so this is a real
// limit and it is stated as one in the header: the deep folder loses its row, not
// its stack.
//
// 128 IS SIZED FROM WHAT THE LIBRARY ACTUALLY LISTS, not from the filesystem's
// worst case. The Library starts at `/books` and descends by folder, and the
// deepest arrangement a real card carries is a publisher-or-Calibre shaped
// `/books/<author>/<title>` -- ~60 bytes, and 120 characters of path is already
// generous for one. Nothing on the device produces a longer one today: `/books`
// itself is 6.
constexpr size_t kPlaceMaxBytes = 128;

// THE PERCENT-ESCAPING MOVED TO reader/wire_escape.h, because the Wi-Fi store
// needs byte-for-byte the same rules on an SSID -- same separators, same three
// refusals, same reason for wanting a record `nvs_get` can print. It was
// written here for a Library path and this is the second caller, which is
// where this project extracts rather than copies.
//
// What the place-specific reasoning was, and still is, now that the code is
// shared: a place that cannot be decoded refuses the WHOLE record, on the
// unknown-name rule -- it was written by something that is not this format, so
// the entries around it may not mean what they say either. An EMPTY place is
// refused too, because encodeSessionStack writes no third field at all for a
// screen with no place, so `library:7:` is a record with a field and nothing in
// it, which is the trailing-separator rule.

bool decodeName(const char* start, size_t len, ScreenId& out) {
  if (len == 0) return false;
  for (int i = 0; i < static_cast<int>(ScreenId::Count); ++i) {
    const char* n = kNames[i];
    if (std::strlen(n) == len && std::strncmp(n, start, len) == 0) {
      out = static_cast<ScreenId>(i);
      return true;
    }
  }
  return false;
}

// strtol over a bounded, non-terminated span, refusing anything it does not
// consume whole. `text` is inside the caller's buffer, so it is copied out first;
// a focus is at most six characters plus a sign.
bool decodeFocus(const char* start, size_t len, int& out) {
  char buf[12];
  if (len == 0 || len >= sizeof(buf)) return false;
  std::memcpy(buf, start, len);
  buf[len] = '\0';
  char* end = nullptr;
  const long v = std::strtol(buf, &end, 10);
  if (end != buf + len) return false;  // trailing junk, or no digits at all
  if (v < kFocusMin || v > kFocusMax) return false;
  out = static_cast<int>(v);
  return true;
}

}  // namespace

const char* sessionWireName(ScreenId id) {
  // AN INDEX, NOT A SWITCH, AND THE SWITCH IS WHERE THE FALL-THROUGH LIVED.
  // Twenty-nine `case ScreenId::X: return kNames[N];` lines each carried a
  // hand-written ordinal and ended in `return kNames[0]`, so a member with no
  // case -- which is exactly what an append produces -- serialised as `home`.
  // That happened twice, to Typography and then BookEnd, and `-Wswitch` is not
  // what saves it: CMakeLists.txt:7 declines -Werror on purpose, so an
  // unhandled case is a warning scrolling past.
  //
  // decodeName ALREADY indexes kNames by ordinal, so this is not a new coupling
  // -- it is the same one, spelled the same way in both directions, and the two
  // are now inverse by construction rather than by two lists agreeing. The
  // static_assert above ties the table's length to the SENTINEL, and nothing
  // here has to remember anything.
  //
  // OUT OF RANGE IS "" RATHER THAN A NAME. The sentinel is not a screen and
  // neither is a value from a newer firmware, so there is nothing truthful to
  // return. An empty name makes encodeSessionStack write a field decodeName
  // refuses, which refuses the WHOLE record on the unknown-name rule -- a record
  // that will not decode is the loud form of this, and `home` was the quiet one.
  if (id >= ScreenId::Count) return "";
  return kNames[static_cast<size_t>(id)];
}

std::string encodeSessionStack(const std::vector<StackEntry>& stack) {
  std::string out;
  for (const StackEntry& e : stack) {
    if (!out.empty()) out += ';';
    out += sessionWireName(e.screen);
    out += ':';
    int focus = e.focus < kFocusMin ? kFocusMin : e.focus > kFocusMax ? kFocusMax : e.focus;
    // THE PLACE AND THE FOCUS ARE ONE VALUE HERE. A place too long to hold is
    // dropped whole rather than truncated -- a cut path addresses a different
    // directory, not none -- and the row it indexes goes with it, because a row
    // index without its folder is the defect this field exists to close. -1 is
    // "nothing selected", a real position every focused screen accepts.
    const std::string place = escapeWireField(e.place);
    const bool fits = !place.empty() && place.size() <= kPlaceMaxBytes;
    if (!place.empty() && !fits) focus = kFocusMin;
    out += std::to_string(focus);
    if (fits) {
      out += ':';
      out += place;
    }
  }
  return out;
}

bool decodeSessionStack(const char* raw, std::vector<StackEntry>& out) {
  out.clear();
  if (raw == nullptr || raw[0] == '\0') return false;

  const char* p = raw;
  while (true) {
    const char* sep = std::strchr(p, ';');
    const char* entryEnd = sep != nullptr ? sep : p + std::strlen(p);
    const char* colon = static_cast<const char*>(std::memchr(p, ':', static_cast<size_t>(entryEnd - p)));
    if (colon == nullptr) {
      out.clear();
      return false;
    }
    // The SECOND colon, if the entry has one, ends the focus and begins the
    // place. It cannot be a colon inside the place: escapeWireField put every one of
    // those beyond this search, which is what makes a path with a colon in it
    // impossible rather than ambiguous.
    const char* placeSep = static_cast<const char*>(
        std::memchr(colon + 1, ':', static_cast<size_t>(entryEnd - colon - 1)));
    const char* focusEnd = placeSep != nullptr ? placeSep : entryEnd;

    StackEntry e;
    if (!decodeName(p, static_cast<size_t>(colon - p), e.screen) ||
        !decodeFocus(colon + 1, static_cast<size_t>(focusEnd - colon - 1), e.focus)) {
      out.clear();
      return false;
    }
    if (placeSep != nullptr &&
        !decodeWireField(placeSep + 1, static_cast<size_t>(entryEnd - placeSep - 1), e.place)) {
      out.clear();
      return false;
    }
    // Refused rather than truncated: a record the app cannot hold whole could
    // only ever be half-restored, and half a stack is not a state the user was
    // ever in.
    if (out.size() >= App::kMaxDepth) {
      out.clear();
      return false;
    }
    out.push_back(e);
    if (sep == nullptr) return true;
    p = sep + 1;  // an empty tail here is a trailing ';', which the next pass refuses
  }
}

size_t sessionStackMaxBytes() {
  // The longest name, a colon, "-32768"'s worth of digits, a colon, the longest
  // place the encoder will write and a separator, times the deepest stack, plus a
  // terminator. Every term is the format's own, which is the point: the read
  // buffer in shell/src/session.cpp is derived from this rather than kept in step
  // by hand, and the place is the term that made that matter -- it is eight times
  // larger than everything else in the entry put together.
  size_t longest = 0;
  for (const char* n : kNames) longest = std::max(longest, std::strlen(n));
  return App::kMaxDepth * (longest + 1 + 6 + 1 + kPlaceMaxBytes + 1) + 1;
}

}  // namespace reader
