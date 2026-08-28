#pragma once
#include <cstdint>

#include "reader/layout.h"  // kBodyPpem, kBodyLeadEm

namespace reader {
class FileSystem;

// Where settings live (spec §5).
inline constexpr char kSettingsPath[] = "/.reader/settings.json";

// Bumped when a field's MEANING changes, not when one is added: an added field
// simply takes its default from an older file. An unrecognised version is a bad
// file and gets replaced by defaults.
inline constexpr int kSettingsVersion = 1;

// The ranges validate() enforces. Public because the Settings screen (2C-3) has
// to know what it is allowed to offer, and a second copy of these numbers would
// drift from these ones.
//
// A zero sleep timeout means NEVER sleep (IdleTimer's own contract), so it is
// not clamped up to the floor -- the floor only applies to a nonzero value, and
// exists because a two-second timeout would put the device to sleep faster than
// a page turn.
inline constexpr uint32_t kSleepAfterMsMin = 10u * 1000u;
inline constexpr uint32_t kSleepAfterMsMax = 60u * 60u * 1000u;
// 0 is RefreshPolicy::kNever. Beyond this many refreshes between FULLs the
// cadence is indistinguishable from never, so anything larger is a bad number
// rather than a preference.
inline constexpr int kFullRefreshEveryMax = 255;

// --- THE TYPOGRAPHY STEPS ----------------------------------------------------
//
// Public for the reason kSleepAfterMsMin is: the Typography screen has to know
// what it is allowed to offer, and a second copy of these numbers would drift
// from these ones. Ascending, which validate() and the stepper both rely on.
//
// EVERY DEFAULT BELOW IS TODAY'S BEHAVIOUR TO THE PIXEL. That is what keeps
// every reader, sleep and book-details golden where it is -- a golden that
// moves because of this feature is a bug in it.
//
// SIZE: ppem, labelled in points as the chrome ramp is (pt = ppem * 72 / 150,
// truncated). 27->12, 32->15, 38->18, 42->20, 46->22, all clean.
//
//   * 32 is the default because design/Reader.dc.html says `font-size: 32px`.
//   * 38 is on the list because 18 PT is the Typography board's own stated
//     value, and roadmap:1269 has held "is the reader under-sized by its own
//     spec" open since 2A-2 with the instruction to decide it ON THE PANEL.
//     This does not decide it; it makes it decidable by pressing a button.
//   * 46 is the top because the glyph cache is thrash-free to ppem ~46 (see
//     CLAUDE.md, The glyph cache). Past it the arena stops holding the
//     alphabet's union and every page re-rasterises at ~3,794 us a glyph.
inline constexpr int kBodyPpemSteps[] = {27, 32, 38, 42, 46};
// MARGINS: the reader column's side padding in px. 18 is design/Reader.dc.html's
// own, which is why it is the middle step and carries the board's own label.
inline constexpr int kMarginSteps[] = {10, 18, 30};
// LINE SPACING: em x 1000, as PageMetrics::leadEm1000 is. 1700 is the board's
// `line-height: 1.7`.
inline constexpr int kLineSpacingSteps[] = {1400, 1550, 1700, 1850, 2000};

// Every knob the shell hardcoded through 2B. Defaults here are the values that
// were compiled in, so behaviour is unchanged until a user changes something.
struct Settings {
  uint32_t sleepAfterMs = 5u * 60u * 1000u;
  // Periodic FULL refresh cadence. 0 = never; see RefreshPolicy::kNever.
  int fullRefreshEvery = 0;
  // A screen change takes the FULL waveform so the outgoing screen cannot ghost
  // through. Diverges from the reference firmware deliberately -- see the
  // roadmap. Costs ~825 ms against ~520 ms.
  bool fullOnTransition = true;

  // TEE THE SERIAL LOG ONTO THE CARD. Off by default, and hand-edited into
  // /.reader/settings.json rather than put on the Settings screen -- it is a
  // diagnostic, not a preference, and a row for it would be a board change for
  // something nobody but a developer wants.
  //
  // IT EXISTS BECAUSE THE CABLE CHANGES THE DEVICE. Serial write and flush
  // short-circuit when no host is attached and BLOCK when one is, so every timing
  // taken over USB is inflated by the cable -- and attaching after a sleep can
  // reset the chip, turning the wake being investigated into a cold boot. A fault
  // that only happens unplugged is therefore not observable over the wire at all,
  // which is the whole reason this is here.
  bool logToCard = false;

  // --- Typography (design/Typography.dc.html) --------------------------------
  //
  // Four fields, no `font`: one body face is vendored, so a font field's only
  // value would be its default -- a second spelling of a constant, which this
  // project has a rule about. The row reads a constant instead.
  //
  // kSettingsVersion is NOT bumped. An added field takes its default from an
  // older file, which is the rule stated at the top of this header, and this is
  // the case it was written for: a card carrying today's file loads and behaves
  // identically.
  int bodyPpem = kBodyPpem;
  int margins = 18;
  int lineSpacing = kBodyLeadEm;
  bool justify = true;

  // Clamps every field into a sane range, returning false if anything had to be
  // clamped. A file that needs clamping is a file to distrust, but clamping and
  // carrying on beats refusing to boot.
  bool validate();

  bool operator==(const Settings&) const = default;
};

// Reads kSettingsPath. Returns false when the file is absent, unreadable,
// unparseable, or carries an unknown version -- in every one of those cases
// `out` is left holding DEFAULTS, so a caller that ignores the return value
// still gets a working device. Callers that want to warn check the result.
//
// Two softer failures also return false, and they are the reason the result is
// not simply "the file loaded":
//
//  - a field whose value is out of range is CLAMPED (the file is otherwise
//    fine, and refusing to boot over one bad number is worse)
//  - a field whose value is the wrong JSON type keeps its default, the rest of
//    the file still loads
//
// A field that is simply ABSENT, or an unrecognised extra field, is not a
// failure at all: that is how an older file loads under a newer firmware and
// the other way round. Those return true.
bool loadSettings(FileSystem& fs, Settings& out);

// Writes kSettingsPath, creating /.reader if needed. The bytes are stable: two
// saves of equal Settings produce byte-identical files, so a load/save round
// trip is a fixed point rather than a slow drift.
//
// A VALIDATED COPY is what gets written, so the file on the card is never the
// thing loadSettings has to clamp. Refusing to save instead would throw away
// every other setting the caller changed in the same breath.
bool saveSettings(FileSystem& fs, const Settings& in);

}  // namespace reader
