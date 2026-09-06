#include "reader/session_record.h"

#include <cstdlib>
#include <cstring>

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
};

// TIED TO THE ENUM, NOT TO A NAMED MEMBER. Three separate bounds in this feature were
// spelled `<= ScreenId::Peek`, so appending a screen left the table short, the decode
// loop unable to see the new name, and the round-trip test silently not covering it --
// while sessionWireName's fallthrough stored the new screen as `home`. That is the
// same shape as #42 and as the three "reports on less than it claims" checks CLAUDE.md
// records. A count against the enum's end cannot be left behind by an append.
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(ScreenId::Count),
              "a ScreenId was added or removed; give it a row in kNames and a case in"
              " sessionWireName. This names the Count SENTINEL, never a member -- a"
              " named member does not move when a screen is appended, which is how"
              " Typography and then BookEnd each shipped serialising as `home`.");

// Clamped so the encoded length is bounded. -1 is the floor rather than 0 because
// it is a real position: Home's CONTINUE block, an empty Library.
constexpr int kFocusMin = -1;
constexpr int kFocusMax = 32767;

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
  switch (id) {
    case ScreenId::Home: return kNames[0];
    case ScreenId::Library: return kNames[1];
    case ScreenId::ItemActions: return kNames[2];
    case ScreenId::DeleteConfirm: return kNames[3];
    case ScreenId::BookDetails: return kNames[4];
    case ScreenId::Settings: return kNames[5];
    // The shell does not push the Sleep screen today -- sleepNow() paints
    // nothing, because e-ink holds the frame -- so no record can name it. If one
    // ever does, note that a restored SleepScreen takes no input: waking into it
    // would be a screen with no way out. Make it unstorable before pushing it.
    case ScreenId::Sleep: return kNames[6];
    // Reader is NAMEABLE but not restorable, and the two are separate facts. It
    // needs a name so this switch is exhaustive; it is not restorable because a
    // reading position is a block and a line, which `focus` cannot carry -- so a
    // restored Reader would reopen the chapter at page 1 and lose exactly what the
    // session record exists to keep. ScreenFactory refuses to build one, which is
    // what makes the refusal happen at the push rather than silently.
    case ScreenId::Reader: return kNames[7];
    // NAMEABLE, and restorable only as far as the Reader under them is. A wake that
    // restored the menu or the contents would put a panel over a Reader the factory
    // refuses to build without a book -- and App::restore stops at the screen that
    // will not build, so the stack simply lands shorter. They need names so this
    // switch is exhaustive and so a record cannot encode them as something else,
    // which is the failure the name table exists to prevent: an id with no case
    // returned kNames[0] and would have stored Contents as "home".
    case ScreenId::ReaderMenu: return kNames[8];
    case ScreenId::Contents: return kNames[9];
    case ScreenId::SdMissing: return kNames[10];
    // NAMEABLE AND GENUINELY RESTORABLE, unlike the two above it: the panel needs
    // nothing from a book -- its band names none and its specimen is fixed -- so the
    // factory builds it from the settings it already holds, and its focus is a row
    // index that `focus` carries exactly.
    case ScreenId::Typography: return kNames[11];
    // NAMEABLE, and never actually restored: the factory refuses an unprimed peek
    // exactly as it refuses an unprimed Reader or Contents, so App::restore stops
    // short and leaves whatever is under it standing. It needs a name so this
    // switch stays exhaustive and a record naming it cannot decode as something
    // else.
    case ScreenId::Peek: return kNames[12];
    // NAMEABLE, and its restorability is Task 9's question rather than this
    // function's. It needs a name for the reason every id above does: without a
    // case it fell through to `return kNames[0]` and stored the end-of-book
    // screen as "home", so a reader who idle-slept on it woke on Home -- which is
    // the failure the note on ReaderMenu above says this table exists to prevent,
    // having already happened once to Contents.
    case ScreenId::BookEnd: return kNames[13];
    // NOT A SCREEN, so it has no name and must never reach the fall-through below,
    // which is what silently made a missing case read as `home`.
    case ScreenId::Count: break;
  }
  return kNames[0];
}

std::string encodeSessionStack(const std::vector<StackEntry>& stack) {
  std::string out;
  for (const StackEntry& e : stack) {
    if (!out.empty()) out += ';';
    out += sessionWireName(e.screen);
    out += ':';
    const int focus = e.focus < kFocusMin ? kFocusMin : e.focus > kFocusMax ? kFocusMax : e.focus;
    out += std::to_string(focus);
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
    StackEntry e;
    if (!decodeName(p, static_cast<size_t>(colon - p), e.screen) ||
        !decodeFocus(colon + 1, static_cast<size_t>(entryEnd - colon - 1), e.focus)) {
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
  // The longest name, a colon, "-32768"'s worth of digits and a separator, times
  // the deepest stack, plus a terminator.
  size_t longest = 0;
  for (const char* n : kNames) longest = std::max(longest, std::strlen(n));
  return App::kMaxDepth * (longest + 1 + 6 + 1) + 1;
}

}  // namespace reader
