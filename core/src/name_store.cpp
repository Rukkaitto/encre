#include "reader/name_store.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "reader/heapguard.h"

namespace reader {
namespace {

// TAB IS A SAFE SEPARATOR AND THAT IS NOT AN ASSUMPTION. `names.cpp` classifies a tab
// as whitespace, so it ends a token and can never appear inside a run -- which is
// what makes a line splittable without escaping. A separator a field could contain is
// a format that corrupts on the first book that uses it.
constexpr char kSep = '\t';

// WHAT THE OUTPUT IS RESERVED AT when there is an old index to carry across. The
// file is not resident any more, so its size is not known without a stat -- and
// asking for one would be a second read of the thing this stopped reading. 20 KB is
// the measured index of a 1,400-page novel with an enormous cast, and `body` grows
// past it if it must: the reserve is there to stop a doubling realloc on a tight
// heap, not to cap the file.
constexpr size_t kBodyGuess = 20 * 1024;

void appendInt(std::string& out, int v) {
  char buf[16];
  const int n = std::snprintf(buf, sizeof buf, "%d", v);
  out.append(buf, static_cast<size_t>(n < 0 ? 0 : n));
}

bool parseInt(std::string_view s, int& out) {
  if (s.empty()) return false;
  int v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    if (v > 1000000000) return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

bool parseU32(std::string_view s, uint32_t& out) {
  if (s.empty()) return false;
  uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

// READS A SORTED FILE ONE LINE AT A TIME, holding a chunk and never the file.
//
// THE MERGE WAS DOCUMENTED AS STREAMING AND WAS NOT. It called `readAll`, so the
// old index and the new one were both resident -- the ~48 KB against a 45,840-byte
// floor that streaming was chosen to avoid, written into the one function whose
// comment said it had been avoided. Reported off glass as the feature standing down
// on a heap with 32,752 bytes free.
class LineReader {
 public:
  LineReader(FileSystem& fs, const std::string& path) : fh_(fs.openRead(path)) {}
  bool ok() const { return fh_ != nullptr; }

  // The next line, or false at the end. The returned view is valid until the next
  // call, which is all a two-way merge needs: it consumes one side at a time.
  bool next(std::string_view& out) {
    for (;;) {
      const size_t nl = buf_.find('\n', at_);
      if (nl != std::string::npos) {
        line_.assign(buf_, at_, nl - at_);
        at_ = nl + 1;
        out = line_;
        return true;
      }
      if (at_ > 0) {
        buf_.erase(0, at_);
        at_ = 0;
      }
      if (fh_ == nullptr) return finish(out);
      char chunk[256];
      const size_t n = fh_->read(chunk, sizeof chunk);
      if (n == 0) {
        fh_.reset();
        return finish(out);
      }
      buf_.append(chunk, n);
    }
  }

  // Skip the header: everything up to and including the blank line.
  void skipHeader() {
    std::string_view l;
    while (next(l)) {
      if (l.empty()) return;
    }
  }

 private:
  bool finish(std::string_view& out) {
    if (buf_.empty()) return false;
    line_ = buf_;
    buf_.clear();
    at_ = 0;
    out = line_;
    return true;
  }

  std::unique_ptr<FileHandle> fh_;
  std::string buf_;
  std::string line_;
  size_t at_ = 0;
};

std::string_view nextLine(std::string_view text, size_t& pos) {
  if (pos >= text.size()) return {};
  const size_t nl = text.find('\n', pos);
  const size_t end = (nl == std::string_view::npos) ? text.size() : nl;
  const std::string_view line = text.substr(pos, end - pos);
  pos = (nl == std::string_view::npos) ? text.size() : nl + 1;
  return line;
}

char hexDigit(int v) { return static_cast<char>(v < 10 ? '0' + v : 'a' + (v - 10)); }

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

// --- The header's bitmap -------------------------------------------------------

bool NameIndexHeader::isScanned(int spine) const {
  if (spine < 0) return false;
  const size_t byte = static_cast<size_t>(spine) / 8;
  if (byte >= scanned.size()) return false;
  return (scanned[byte] >> (spine % 8)) & 1u;
}

void NameIndexHeader::markScanned(int spine) {
  if (spine < 0) return;
  const size_t byte = static_cast<size_t>(spine) / 8;
  if (byte >= scanned.size()) scanned.resize(byte + 1, 0);
  scanned[byte] |= static_cast<uint8_t>(1u << (spine % 8));
}

int NameIndexHeader::scannedCount() const {
  int n = 0;
  for (const uint8_t b : scanned) {
    for (int i = 0; i < 8; ++i) n += (b >> i) & 1u;
  }
  return n;
}

int NameIndexHeader::contiguousPrefix() const {
  int n = 0;
  while (isScanned(n)) ++n;
  return n;
}

int NameIndexEntry::extractCount() const {
  int n = 0;
  for (const At& a : extracts) n += a.count;
  return n;
}

namespace {

// ADD A CHAPTER'S EXTRACTS AND KEEP THE FIRST `cap` IN READING ORDER.
//
// THE SPINE ORDER IS NOT THE ARRIVAL ORDER, and that is the whole of this function.
// Chapters do not arrive in spine order -- a Contents jump reads 40 before 4, and
// backfill fills the gap afterwards -- so appending would keep whichever eight
// happened to be scanned first. The rule is the first eight IN THE BOOK, which keeps
// a name's introduction and keeps its list stable between visits.
//
// So the entry is inserted at its spine position and the TAIL is trimmed. The
// evicted extracts stay in their chapter's write-once part with nothing referencing
// them: dead weight in a file that is read selectively, not corruption, and
// reclaiming it would mean rewriting a chapter file, which is the streaming-write
// problem that forced the parts in the first place.
void addExtracts(NameIndexEntry& e, int spine, int count, int cap) {
  if (count <= 0) return;
  const auto at = std::lower_bound(e.extracts.begin(), e.extracts.end(), spine,
                                   [](const NameIndexEntry::At& a, int s) { return a.spine < s; });
  if (at != e.extracts.end() && at->spine == spine) at->count += count;
  else e.extracts.insert(at, NameIndexEntry::At{spine, count});
  int kept = 0;
  for (size_t i = 0; i < e.extracts.size(); ++i) {
    if (kept >= cap) {
      e.extracts.resize(i);
      return;
    }
    const int room = cap - kept;
    if (e.extracts[i].count > room) e.extracts[i].count = room;
    kept += e.extracts[i].count;
  }
}

}  // namespace

// --- Serialisation -------------------------------------------------------------

std::string serialiseHeader(const NameIndexHeader& h) {
  std::string out = "encre-names\t";
  appendInt(out, NameIndexHeader::kVersion);
  out += '\n';
  out += "path\t";
  out += h.bookPath;
  out += '\n';
  out += "bytes\t";
  appendInt(out, static_cast<int>(h.bookBytes));
  out += '\n';
  out += "admit\t";
  appendInt(out, h.admitMidSentence);
  out += '\n';
  out += "cap\t";
  appendInt(out, h.extractCap);
  out += '\n';
  // HEX, NOT RAW BYTES, because the rest of the file is text and one format is easier
  // to be right about than two. 23 characters for a 92-chapter book.
  out += "scanned\t";
  for (const uint8_t b : h.scanned) {
    out += hexDigit((b >> 4) & 0xF);
    out += hexDigit(b & 0xF);
  }
  out += '\n';
  out += '\n';  // the blank line the body starts after
  return out;
}

bool parseHeader(std::string_view text, NameIndexHeader& out, size_t* bodyOffset) {
  NameIndexHeader h;
  size_t pos = 0;
  bool sawMagic = false, sawPath = false, sawBytes = false;
  while (pos < text.size()) {
    const std::string_view line = nextLine(text, pos);
    if (line.empty()) break;  // the blank line ends the header
    const size_t tab = line.find(kSep);
    if (tab == std::string_view::npos) return false;
    const std::string_view key = line.substr(0, tab);
    const std::string_view val = line.substr(tab + 1);
    if (key == "encre-names") {
      int v = 0;
      // A VERSION MISMATCH IS A HARD REFUSAL, not a best effort. `kPositionVersion`
      // is the precedent: a store from a future version is discarded and rebuilt,
      // which backfill then does without the reader noticing.
      if (!parseInt(val, v) || v != NameIndexHeader::kVersion) return false;
      sawMagic = true;
    } else if (key == "path") {
      h.bookPath.assign(val);
      sawPath = true;
    } else if (key == "bytes") {
      if (!parseU32(val, h.bookBytes)) return false;
      sawBytes = true;
    } else if (key == "admit") {
      if (!parseInt(val, h.admitMidSentence)) return false;
    } else if (key == "cap") {
      if (!parseInt(val, h.extractCap)) return false;
    } else if (key == "scanned") {
      if (val.size() % 2 != 0) return false;
      h.scanned.clear();
      h.scanned.reserve(val.size() / 2);
      for (size_t i = 0; i + 1 < val.size(); i += 2) {
        const int hi = hexValue(val[i]), lo = hexValue(val[i + 1]);
        if (hi < 0 || lo < 0) return false;
        h.scanned.push_back(static_cast<uint8_t>((hi << 4) | lo));
      }
    }
    // AN UNKNOWN KEY IS IGNORED, not refused. The version gate is what catches a
    // format change; a key added in a later version that this one skips is the
    // difference between a store that degrades and one that is thrown away for a
    // field nobody needed.
  }
  if (!sawMagic || !sawPath || !sawBytes) return false;
  out = std::move(h);
  if (bodyOffset != nullptr) *bodyOffset = pos;
  return true;
}

std::string serialiseEntry(const NameIndexEntry& e) {
  std::string out = e.text;
  out += kSep;
  appendInt(out, e.midSentence);
  out += kSep;
  appendInt(out, e.chapterOpening);
  out += kSep;
  appendInt(out, e.total);
  out += kSep;
  for (size_t i = 0; i < e.extracts.size(); ++i) {
    if (i != 0) out += ',';
    appendInt(out, e.extracts[i].spine);
    out += ':';
    appendInt(out, e.extracts[i].count);
  }
  return out;
}

bool parseEntry(std::string_view line, NameIndexEntry& out) {
  NameIndexEntry e;
  size_t pos = 0;
  auto field = [&](std::string_view& f) {
    const size_t tab = line.find(kSep, pos);
    if (tab == std::string_view::npos) return false;
    f = line.substr(pos, tab - pos);
    pos = tab + 1;
    return true;
  };
  std::string_view text, mid, open, total;
  if (!field(text) || !field(mid) || !field(open) || !field(total)) return false;
  if (text.empty()) return false;
  e.text.assign(text);
  if (!parseInt(mid, e.midSentence) || !parseInt(open, e.chapterOpening) ||
      !parseInt(total, e.total))
    return false;
  const std::string_view rest = line.substr(pos);
  size_t i = 0;
  while (i < rest.size()) {
    const size_t comma = rest.find(',', i);
    const std::string_view item =
        rest.substr(i, comma == std::string_view::npos ? std::string_view::npos : comma - i);
    const size_t colon = item.find(':');
    if (colon == std::string_view::npos) return false;
    NameIndexEntry::At a;
    if (!parseInt(item.substr(0, colon), a.spine)) return false;
    if (!parseInt(item.substr(colon + 1), a.count)) return false;
    e.extracts.push_back(a);
    if (comma == std::string_view::npos) break;
    i = comma + 1;
  }
  out = std::move(e);
  return true;
}

// --- The store -----------------------------------------------------------------

std::string nameStoreDirFor(std::string_view bookPath) {
  // FNV-1a, eight lowercase hex, which is `statePathFor`'s naming and for its reason:
  // a book path holds `/` by construction and real cards carry accented 90-character
  // titles, so the path cannot be the filename.
  uint32_t h = 2166136261u;
  for (const char c : bookPath) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  std::string name(8, '0');
  for (int i = 7; i >= 0; --i) {
    name[static_cast<size_t>(i)] = hexDigit(static_cast<int>(h & 0xF));
    h >>= 4;
  }
  return "/.reader/names/" + name;
}

NameStore::NameStore(FileSystem& fs, std::string bookPath, uint32_t bookBytes)
    : fs_(fs), bookPath_(std::move(bookPath)), bookBytes_(bookBytes),
      dir_(nameStoreDirFor(bookPath_)) {}

std::string NameStore::indexPath() const { return dir_ + "/index"; }

bool NameStore::loadHeader(NameIndexHeader& out) const {
  std::unique_ptr<FileHandle> fh = fs_.openRead(indexPath());
  if (fh == nullptr) return false;
  // THE HEADER IS BOUNDED AND THE BODY IS NOT, which is the whole reason this reads a
  // prefix rather than the file: a caller asking "has this chapter been scanned"
  // must not pay ~20 KB to find out, and `readAll` on the device caps at 64 KB
  // anyway.
  char buf[1024];
  const size_t n = fh->read(buf, sizeof buf);
  NameIndexHeader h;
  if (!parseHeader(std::string_view(buf, n), h, nullptr)) return false;
  // THE IDENTITY CHECK, and a mismatch is a discard rather than a repair. An index
  // for a different book is worse than none: the reading position already refuses
  // this way and for this reason.
  if (h.bookPath != bookPath_ || h.bookBytes != bookBytes_) return false;
  out = std::move(h);
  return true;
}

bool NameStore::mergeChapter(int spine, const std::vector<NameScanner::Run>& runs,
                             const std::vector<int>* extractCounts) {
  NameIndexHeader header;
  std::string body;
  bool haveOld = false;
  if (loadHeader(header) && header.admitMidSentence == NameScanner::kAdmitMidSentence)
    haveOld = true;
  if (!haveOld) {
    // A STORE THAT CANNOT BE READ, OR IS FOR ANOTHER BOOK, IS STARTED OVER. It is not
    // repaired and it is not merged into: mixing two populations into one set of
    // counts is the failure the header's thresholds exist to prevent.
    header = NameIndexHeader{};
    header.bookPath = bookPath_;
    header.bookBytes = bookBytes_;
  }
  // RE-READING IS NORMAL, SO THIS IS NOT AN EDGE CASE. Without the refusal a second
  // read of the same chapter doubles every one of its counts and admits runs that had
  // been correctly rejected.
  if (header.isScanned(spine)) return true;

  // THE TWO-WAY MERGE. Both sides are sorted by run text -- the old file by
  // construction, `runs` by NameScanner -- so this is one pass with no sort and no
  // second copy of either side.
  // ONLY THE OUTPUT IS RESIDENT NOW. The old index arrives a chunk at a time, so
  // the peak is one copy of the index rather than two -- which is what the design
  // said all along and what the first implementation did not do.
  //
  // PROBED BEFORE IT IS RESERVED, because a reserve that cannot be served is
  // abort() rather than an error. A merge that cannot allocate REFUSES, and the
  // chapter is simply rescanned: the scanned-spine bit is only set by a merge that
  // completed.
  LineReader in(fs_, indexPath());
  const bool readable = haveOld && in.ok();
  if (readable) in.skipHeader();
  const size_t bodyWant = (haveOld ? kBodyGuess : 0) + runs.size() * 24;
  if (!Heap::hasBlock(bodyWant)) return false;
  body.reserve(bodyWant);
  size_t i = 0;
  std::string_view line;
  if (!readable || !in.next(line)) line = std::string_view();
  // A RUN NOT YET ON THE CARD EARNS ITS SLOT OR IS DROPPED. This is the door, and it
  // is here rather than in the scanner because only the index knows what is already
  // through it.
  auto emitNew = [&](size_t k) {
    if (runs[k].midSentence < header.admitMidSentence) return;
    NameIndexEntry e;
    e.text = runs[k].text;
    e.midSentence = runs[k].midSentence;
    e.chapterOpening = runs[k].chapterOpening;
    e.total = runs[k].total;
    if (extractCounts != nullptr && k < extractCounts->size()) {
      addExtracts(e, spine, (*extractCounts)[k], header.extractCap);
    }
    body += serialiseEntry(e);
    body += '\n';
  };
  while (!line.empty() || i < runs.size()) {
    if (line.empty()) {
      emitNew(i++);
      continue;
    }
    NameIndexEntry oldEntry;
    if (!parseEntry(line, oldEntry)) {
      // A LINE THAT WILL NOT PARSE IS DROPPED, not fatal. `loadProgressIndex` skips
      // unparseable records individually for the same reason: one corrupt line must
      // not cost a reader the other seven hundred.
      if (!in.next(line)) line = std::string_view();
      continue;
    }
    if (i >= runs.size() || oldEntry.text < runs[i].text) {
      body += serialiseEntry(oldEntry);
      body += '\n';
      if (!in.next(line)) line = std::string_view();
    } else if (runs[i].text < oldEntry.text) {
      emitNew(i++);
    } else {
      // ALREADY ON THE CARD: keep accumulating whatever this chapter saw. A run that
      // clears the bar once keeps growing for the rest of the book, which is what
      // admission-at-the-door buys over eviction-by-count.
      oldEntry.midSentence += runs[i].midSentence;
      oldEntry.chapterOpening += runs[i].chapterOpening;
      oldEntry.total += runs[i].total;
      if (extractCounts != nullptr && i < extractCounts->size()) {
        addExtracts(oldEntry, spine, (*extractCounts)[i], header.extractCap);
      }
      body += serialiseEntry(oldEntry);
      body += '\n';
      ++i;
      if (!in.next(line)) line = std::string_view();
    }
  }

  // THE BIT IS SET LAST AND THE WHOLE FILE IS WRITTEN ONCE. An interruption before
  // this leaves a store that never knew about the chapter, so the chapter is simply
  // rescanned -- which costs one redundant walk and cannot leave the index promising
  // extracts that are not there.
  header.markScanned(spine);
  if (!fs_.mkdirs(dir_)) return false;
  return fs_.writeAll(indexPath(), serialiseHeader(header) + body);
}

bool NameStore::quotasFor(const std::vector<NameScanner::Run>& runs, int cap,
                          std::vector<std::string>& wanted, std::vector<int>& quota,
                          std::vector<int>& runIndex) const {
  wanted.clear();
  quota.clear();
  runIndex.clear();
  NameIndexHeader h;
  const bool haveIndex = loadHeader(h);
  const int room = haveIndex ? h.extractCap : cap;
  LineReader in(fs_, indexPath());
  const bool readable = haveIndex && in.ok();
  if (readable) in.skipHeader();
  std::string_view line;
  bool have = readable && in.next(line);
  for (size_t i = 0; i < runs.size(); ++i) {
    const std::string& text = runs[i].text;
    // Advance the file to this run or past it. Both sides are sorted, so the file
    // is walked once over the whole chapter rather than once per run.
    NameIndexEntry e;
    bool onCard = false;
    while (have) {
      if (!parseEntry(line, e)) {
        have = in.next(line);
        continue;
      }
      if (e.text < text) {
        have = in.next(line);
        continue;
      }
      onCard = (e.text == text);
      break;
    }
    // A RUN NOT ON THE CARD MUST EARN ITS SLOT; one already on it keeps growing.
    if (!onCard && runs[i].midSentence < NameScanner::kAdmitMidSentence) continue;
    const int used = onCard ? e.extractCount() : 0;
    const int left = room - used;
    wanted.push_back(text);
    quota.push_back(left > 0 ? left : 0);
    runIndex.push_back(static_cast<int>(i));
    if (onCard) have = in.next(line);
  }
  return true;
}

bool NameStore::loadAll(NameIndexHeader& header, std::vector<NameIndexEntry>& out) const {
  std::string text;
  if (!fs_.readAll(indexPath(), text)) return false;
  size_t bodyOffset = 0;
  if (!parseHeader(text, header, &bodyOffset)) return false;
  if (header.bookPath != bookPath_ || header.bookBytes != bookBytes_) return false;
  out.clear();
  size_t pos = bodyOffset;
  while (pos < text.size()) {
    const std::string_view line = nextLine(text, pos);
    if (line.empty()) continue;
    NameIndexEntry e;
    if (parseEntry(line, e)) out.push_back(std::move(e));
  }
  return true;
}

bool NameStore::remove() {
  // The index first, then the directory. A store whose index is gone is a store, so
  // a half-done removal reads as absent rather than as corrupt.
  fs_.remove(indexPath());
  return fs_.remove(dir_);
}

}  // namespace reader
