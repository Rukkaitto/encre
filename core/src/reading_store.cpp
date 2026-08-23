#include "reader/reading_store.h"

#include "reader/json.h"

namespace reader {

namespace {

constexpr const char* kKeyVersion = "version";
constexpr const char* kKeyPath = "path";
constexpr const char* kKeyTitle = "title";
constexpr const char* kKeyAuthor = "author";
constexpr const char* kKeyPercent = "percent";
constexpr const char* kKeySpine = "spine";
constexpr const char* kKeySpineCount = "spineCount";

// The pointer's own version, separate from the position record's: they are two files
// with two formats and either can change without the other.
constexpr int kLastReadVersion = 1;

int clampPercent(int64_t v) { return v < 0 ? 0 : (v > 100 ? 100 : static_cast<int>(v)); }

// WRITE ONLY IF THE BYTES WOULD DIFFER. One read to save one write, which is the
// right trade on a card: a read is cheap and a write costs erase cycles and can
// fail. It works because both dumps sort their keys.
SaveResult writeIfChanged(FileSystem& fs, std::string_view path, const std::string& text) {
  std::string existing;
  if (fs.readAll(path, existing) && existing == text) return SaveResult::Unchanged;
  return fs.writeAll(path, text) ? SaveResult::Written : SaveResult::Failed;
}

}  // namespace

bool loadPosition(FileSystem& fs, std::string_view bookPath, ReadingPosition& out) {
  std::string text;
  if (!fs.readAll(statePathFor(bookPath), text)) return false;
  ReadingPosition p;
  if (!parsePosition(text, p)) return false;
  // THE COLLISION CHECK, here as well as in fitOf. The filename is a hash, so a
  // record can legitimately be found under another book's name -- and a caller that
  // forgot to grade the fit would otherwise get a position for the wrong book. Two
  // independent refusals for one hazard, and the tests assert both.
  if (p.bookPath != bookPath) return false;
  out = p;
  return true;
}

SaveResult savePosition(FileSystem& fs, const ReadingPosition& p) {
  // A record with no book is not a record. Refused rather than written, because a
  // sidecar whose stored path is empty can never match a book again -- it would be
  // an unreachable file taking a name a real book might hash to.
  if (p.bookPath.empty()) return SaveResult::Failed;
  return writeIfChanged(fs, statePathFor(p.bookPath), serialise(p));
}

bool loadLastRead(FileSystem& fs, LastRead& out) {
  std::string text;
  if (!fs.readAll(kLastReadPath, text)) return false;
  JsonObject o;
  if (!o.parse(text)) return false;
  int64_t version = 0;
  if (!o.getInt(kKeyVersion, version) || version != kLastReadVersion) return false;

  LastRead l;
  if (!o.getString(kKeyPath, l.bookPath) || l.bookPath.empty()) return false;
  // TITLE AND AUTHOR ARE OPTIONAL. An EPUB is not obliged to carry either, and a
  // book with no author in its OPF is a book, not a broken pointer -- Home draws the
  // line blank rather than refusing to name the book at all.
  o.getString(kKeyTitle, l.title);
  o.getString(kKeyAuthor, l.author);
  int64_t v = 0;
  if (o.getInt(kKeyPercent, v)) l.percent = clampPercent(v);
  if (o.getInt(kKeySpine, v)) l.spine = v < 0 ? 0 : static_cast<int>(v);
  if (o.getInt(kKeySpineCount, v)) l.spineCount = v < 0 ? 0 : static_cast<int>(v);
  out = l;
  return true;
}

SaveResult saveLastRead(FileSystem& fs, const LastRead& l) {
  if (l.bookPath.empty()) return SaveResult::Failed;
  JsonObject o;
  o.setInt(kKeyVersion, kLastReadVersion);
  o.setString(kKeyPath, l.bookPath);
  o.setString(kKeyTitle, l.title);
  o.setString(kKeyAuthor, l.author);
  o.setInt(kKeyPercent, clampPercent(l.percent));
  o.setInt(kKeySpine, l.spine);
  o.setInt(kKeySpineCount, l.spineCount);
  return writeIfChanged(fs, kLastReadPath, o.dump());
}

bool forgetLastRead(FileSystem& fs) {
  // A pointer that is not there is already forgotten, so absence is success -- the
  // caller asked for a state, not for an event.
  if (!fs.exists(kLastReadPath)) return true;
  return fs.remove(kLastReadPath);
}

int progressPercent(const OpenedBook& book, int spine, int page, int pageTotal) {
  const int chapters = book.chapterCount();
  if (chapters <= 0 || spine < 0) return 0;

  uint64_t total = 0;
  for (int c = 0; c < chapters; ++c) total += book.chapters[static_cast<size_t>(c)].uncompressedSize;
  if (total == 0) return 0;

  uint64_t through = 0;
  for (int c = 0; c < chapters && c < spine; ++c)
    through += book.chapters[static_cast<size_t>(c)].uncompressedSize;

  // WITHIN the open chapter, only when its page count is known. Page 1 is zero
  // through it, which is why this is (page - 1): arriving at a chapter has not read
  // any of it yet.
  if (spine < chapters && pageTotal > 0 && page > 0) {
    const uint64_t here = book.chapters[static_cast<size_t>(spine)].uncompressedSize;
    const int within = page - 1 > pageTotal ? pageTotal : page - 1;
    through += here * static_cast<uint64_t>(within) / static_cast<uint64_t>(pageTotal);
  }

  if (through > total) through = total;
  // Rounded, not truncated: a reader four fifths of the way through a book should not
  // be told 79%.
  const uint64_t pct = (through * 100 + total / 2) / total;
  return pct > 100 ? 100 : static_cast<int>(pct);
}

bool loadProgressIndex(FileSystem& fs, std::vector<ProgressEntry>& out) {
  out.clear();
  std::vector<DirEntry> entries;
  // AN ABSENT DIRECTORY IS AN EMPTY INDEX, not a failure: it is the state of a card
  // nothing has been read on, which is every card until the first book is opened.
  if (!fs.exists(kStateDir)) return true;
  if (!fs.list(kStateDir, entries)) return false;

  std::string path(kStateDir);
  path += '/';
  const size_t base = path.size();
  for (const DirEntry& e : entries) {
    if (e.isDir) continue;
    path.resize(base);
    path += e.name;
    std::string text;
    if (!fs.readAll(path, text)) continue;
    ReadingPosition p;
    // SKIPPED INDIVIDUALLY, never fatal. A truncated record -- a save cut by a power
    // loss -- costs that one book its percentage and says nothing about the rest.
    if (!parsePosition(text, p) || p.bookPath.empty()) continue;
    out.push_back(ProgressEntry{p.bookPath, p.percent});
  }
  return true;
}

int percentFor(const std::vector<ProgressEntry>& index, std::string_view bookPath) {
  for (const ProgressEntry& e : index)
    if (e.bookPath == bookPath) return e.percent;
  return -1;
}

}  // namespace reader
