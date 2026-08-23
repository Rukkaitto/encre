#pragma once
#include <string>
#include <string_view>

#include "reader/book.h"
#include "reader/filesystem.h"
#include "reader/reading_position.h"

namespace reader {

// READING PROGRESS ON THE CARD: one sidecar a book, plus one pointer at the book
// last open. In `core/` rather than the shell for the reason everything else here
// is -- `shell/` has no test harness, five bugs have hidden in it, and "it needs a
// filesystem" is not a reason to be untestable when FileSystem is an interface and
// fake_fs.h implements it, write failures included.
//
// --- WHAT `last.json` IS FOR ---------------------------------------------------
//
// Home's CONTINUE block needs a title, an author and a percentage. Getting those
// from the book itself means opening the EPUB -- a central directory, an OPF parse,
// ~100 ms and ~32 KB of transient -- at boot, before anything is on screen, for a
// block the user may not even be looking at.
//
// So the pointer CACHES them. It is written when the reader leaves a book, which is
// the moment they are all known for free, and read at boot as plain JSON. The cost
// is that it can go stale (a book deleted on a computer, a card swapped); that is
// what `HomeMissing.dc.html` is for, and staleness is detectable by asking whether
// the path still exists, which is one cheap call.
struct LastRead {
  std::string bookPath;
  std::string title;
  std::string author;
  int percent = 0;    // 0..100, by BYTES through the book -- see progressPercent
  int spine = 0;      // for the `CH. 03` label, which is a spine position
  int spineCount = 0;
};

// --- Reading ------------------------------------------------------------------
//
// False is a NORMAL answer, not an error: no file, a malformed or truncated one, a
// record from another version, or a hash collision all mean "no position for this
// book", and the caller's response to every one of them is the same -- start at the
// beginning. Distinguishing them would be a distinction with no consequence.
bool loadPosition(FileSystem& fs, std::string_view bookPath, ReadingPosition& out);
bool loadLastRead(FileSystem& fs, LastRead& out);

// --- Writing ------------------------------------------------------------------
//
// THREE ANSWERS, NOT TWO, and the middle one is why. `Unchanged` means the bytes on
// the card already say this, so nothing was written -- which is the common case when
// a save fires on leaving a book the reader did not move in, and a card write is the
// expensive and wear-bearing thing here. It rests on serialise() sorting its keys so
// an unchanged record is byte-identical.
//
// `Failed` MUST NOT BE TREATED AS FATAL by the caller, and this is the one hazard in
// the whole feature. A card can be readable and not writable -- SD cards have a
// physical write-protect tab -- and on the device a failed writeAll calls
// noteCardGone(), which the presence poll turns into an App rooted at
// SdMissingScreen. So a save that failed would throw the reader out of a book they
// can still perfectly well read. The shell logs it and carries on; nothing here
// retries, because a card that refused one write will refuse the next.
enum class SaveResult : uint8_t { Written, Unchanged, Failed };

SaveResult savePosition(FileSystem& fs, const ReadingPosition& p);
SaveResult saveLastRead(FileSystem& fs, const LastRead& l);

// Removes the pointer, for a book that is gone. Not the per-book sidecars: spec 4.0
// says deleting a book "never erases reading progress", so a book that comes back
// should still know where the reader was.
bool forgetLastRead(FileSystem& fs);

// --- Progress -----------------------------------------------------------------
//
// A PERCENTAGE OF THE BOOK'S BYTES, not of its pages, and that is what makes it
// affordable. A page-based percentage needs the book's total page count, which is a
// count of every chapter: 6.94 MB of inflated XHTML for one real novel, ~49 s of
// decode on this device at the measured 7.2 ms/KB. The byte layout is already known
// -- `openBook` read every chapter's uncompressedSize into ChapterSpan -- so this
// costs a sum over 92 integers.
//
// It interpolates WITHIN the open chapter when its page count is known, and reports
// only the chapters behind when it is not. So the number can move slightly when a
// deferred count lands, which is honest: it got more precise.
//
// Pages are 1-based, as the view model reports them. `pageTotal <= 0` means unknown.
int progressPercent(const OpenedBook& book, int spine, int page, int pageTotal);

// Where the pointer lives. The per-book sidecars are statePathFor's.
inline constexpr const char* kLastReadPath = "/.reader/last.json";

}  // namespace reader
