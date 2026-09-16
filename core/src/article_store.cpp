#include "reader/article_store.h"

#include <algorithm>
#include <cstdint>

#include "reader/json.h"
#include "reader/reading_position.h"
#include "reader/reading_store.h"

namespace reader {
namespace {

// A NUMERIC LEAF NAME, so the id round-trips exactly. The hash the reading
// sidecars use exists because a BOOK PATH is not a filename; an article's id is
// an integer the server gave us and needs no hashing -- and a name a person can
// read off a card is one they can diagnose, which is the same property the
// session record's screen NAMES buy.
std::string leaf(int id, const char* ext) { return std::to_string(id) + ext; }

bool parseId(std::string_view name, const char* ext, int& out) {
  const std::string suffix(ext);
  if (name.size() <= suffix.size()) return false;
  if (name.substr(name.size() - suffix.size()) != suffix) return false;
  const std::string_view digits = name.substr(0, name.size() - suffix.size());
  if (digits.empty()) return false;
  int v = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') return false;
    // A server id past int is not a real id; refuse rather than wrapping, which
    // would name a DIFFERENT article's files.
    if (v > (2147483647 - (c - '0')) / 10) return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

const char* kindExt(PendingKind k) {
  switch (k) {
    case PendingKind::Archive: return ".archive";
    case PendingKind::Star: return ".star";
    case PendingKind::Unstar: return ".unstar";
  }
  return ".archive";
}

}  // namespace

std::string ArticleStore::epubPath(int id) const {
  return std::string(kArticlesDir) + "/" + leaf(id, ".epub");
}

std::string ArticleStore::metaPath(int id) const {
  return std::string(kArticlesDir) + "/" + leaf(id, ".json");
}

bool ArticleStore::hasEpub(int id) const { return fs_.exists(epubPath(id)); }

bool ArticleStore::writeMeta(const ArticleMeta& m) {
  JsonObject o;
  o.setInt("id", m.id);
  o.setString("title", m.title);
  o.setString("domain", m.domain);
  o.setInt("readingTime", m.readingTime);
  o.setBool("starred", m.starred);
  o.setBool("archived", m.archived);
  o.setString("updatedAt", m.updatedAt);
  fs_.mkdirs(kArticlesDir);
  return fs_.writeAll(metaPath(m.id), o.dump());
}

bool ArticleStore::readMeta(int id, ArticleMeta& out) const {
  std::string text;
  if (!fs_.readAll(metaPath(id), text)) return false;
  JsonObject o;
  if (!o.parse(text)) return false;
  ArticleMeta m;
  int64_t v = 0;
  // THE ID COMES FROM THE FILENAME, not from the file. A sidecar whose stored id
  // disagrees with its name is a corrupt file, and trusting the field would make
  // it name another article's .epub -- which is reading_store's own collision
  // refusal one directory over.
  m.id = id;
  if (o.getInt("id", v) && static_cast<int>(v) != id) return false;
  o.getString("title", m.title);
  o.getString("domain", m.domain);
  if (o.getInt("readingTime", v)) m.readingTime = static_cast<int>(v);
  o.getBool("starred", m.starred);
  o.getBool("archived", m.archived);
  o.getString("updatedAt", m.updatedAt);
  out = m;
  return true;
}

std::vector<ArticleMeta> ArticleStore::list(int* strays) const {
  std::vector<ArticleMeta> out;
  if (strays != nullptr) *strays = 0;
  std::vector<DirEntry> entries;
  if (!fs_.list(kArticlesDir, entries)) return out;
  for (const DirEntry& e : entries) {
    if (e.isDir) continue;
    int id = 0;
    if (!parseId(e.name, ".json", id)) continue;
    ArticleMeta m;
    // A CORRUPT SIDECAR COSTS ONE ROW, which is the whole reason there is a file
    // per article rather than one list.
    if (!readMeta(id, m)) continue;
    if (!hasEpub(id)) {
      if (strays != nullptr) ++*strays;
      continue;
    }
    out.push_back(std::move(m));
  }
  // NEWEST FIRST, by the server's own string. Stable, so two articles the server
  // stamped identically keep the directory's order rather than swapping between
  // renders.
  std::stable_sort(out.begin(), out.end(),
                   [](const ArticleMeta& a, const ArticleMeta& b) {
                     return a.updatedAt > b.updatedAt;
                   });
  return out;
}

int ArticleStore::unreadCount() const {
  std::vector<ProgressEntry> progress;
  loadProgressIndex(fs_, progress);
  int n = 0;
  for (const ArticleMeta& m : list()) {
    if (m.archived) continue;
    const ProgressEntry* p = progressFor(progress, epubPath(m.id));
    // UNREAD IS `NEVER OPENED`, which is what the bullet draws -- so the count
    // and the marks answer one question. An article started and put down half
    // way is neither unread nor READ, and the list says so by saying nothing.
    if (openedFromProgress(p)) continue;
    ++n;
  }
  return n;
}

bool ArticleStore::queueArchive(int id) {
  fs_.mkdirs(kArticlesQueueDir);
  const std::string marker = std::string(kArticlesQueueDir) + "/" + leaf(id, ".archive");
  if (!fs_.writeAll(marker, "")) return false;
  // THE FILES GO NOW. The reader pressed Archive; the article leaves their
  // device, and the server learns on the next sync. Leaving it until the push
  // would keep a row the press was for removing.
  //
  // The results are not branched on: remove() reports the END STATE, so a false
  // means the file is still there, and the list the reader lands on already says
  // which it was.
  fs_.remove(epubPath(id));
  fs_.remove(metaPath(id));
  fs_.remove(statePathFor(epubPath(id)));
  return true;
}

bool ArticleStore::queueStar(int id, bool starred) {
  fs_.mkdirs(kArticlesQueueDir);
  // THE OPPOSITE MARKER GOES FIRST, so a reader who stars and then unstars owes
  // the server ONE action and not two contradictory ones. Marker files make that
  // a remove rather than a rewrite.
  const std::string other =
      std::string(kArticlesQueueDir) + "/" + leaf(id, starred ? ".unstar" : ".star");
  fs_.remove(other);
  const std::string marker =
      std::string(kArticlesQueueDir) + "/" + leaf(id, starred ? ".star" : ".unstar");
  if (!fs_.writeAll(marker, "")) return false;
  // The sidecar carries the new state at once, so the list and the overlay
  // redraw correct before any radio comes up.
  ArticleMeta m;
  if (readMeta(id, m)) {
    m.starred = starred;
    writeMeta(m);
  }
  return true;
}

std::vector<PendingAction> ArticleStore::pending() const {
  std::vector<PendingAction> out;
  std::vector<DirEntry> entries;
  if (!fs_.list(kArticlesQueueDir, entries)) return out;
  for (const DirEntry& e : entries) {
    if (e.isDir) continue;
    int id = 0;
    for (const PendingKind k : {PendingKind::Archive, PendingKind::Star, PendingKind::Unstar}) {
      if (parseId(e.name, kindExt(k), id)) {
        out.push_back({id, k});
        break;
      }
    }
  }
  // OLDEST FIRST BY ID, which is the order they were created in: a server id
  // only grows, so this needs no timestamp and no clock. The engine pushes in
  // this order so a star and a later archive of the same article arrive the way
  // the reader did them.
  std::stable_sort(out.begin(), out.end(), [](const PendingAction& a, const PendingAction& b) {
    return a.id < b.id;
  });
  return out;
}

int ArticleStore::pendingCount() const { return static_cast<int>(pending().size()); }

bool ArticleStore::ack(int id, PendingKind kind) {
  return fs_.remove(std::string(kArticlesQueueDir) + "/" + leaf(id, kindExt(kind)));
}

bool ArticleStore::loadWatermark(SyncWatermark& out) const {
  out = SyncWatermark{};
  std::string text;
  if (!fs_.readAll(kArticlesWatermarkPath, text)) return false;
  JsonObject o;
  if (!o.parse(text)) return false;
  o.getString("since", out.since);
  std::string outcome;
  if (o.getString("lastOutcome", outcome) && !outcome.empty()) out.lastOutcome = outcome;
  return true;
}

bool ArticleStore::saveWatermark(const SyncWatermark& w) {
  JsonObject o;
  o.setString("since", w.since);
  o.setString("lastOutcome", w.lastOutcome);
  fs_.mkdirs(kArticlesDir);
  return fs_.writeAll(kArticlesWatermarkPath, o.dump());
}

int ArticleStore::prune(int keep) {
  if (keep < 0) return 0;
  std::vector<ProgressEntry> progress;
  loadProgressIndex(fs_, progress);
  const std::vector<ArticleMeta> all = list();
  if (static_cast<int>(all.size()) <= keep) return 0;

  int removed = 0;
  // OLDEST FIRST, which is the end of a newest-first list.
  for (size_t i = all.size(); i-- > 0;) {
    if (static_cast<int>(all.size()) - removed <= keep) break;
    const ArticleMeta& m = all[i];
    // STARRED AND STARTED ARE BOTH KEPT, and the two reasons are different: a
    // star is the reader saying so, and a reading position is attention they
    // have already spent. Taking either away to make room for something they
    // have not opened is the wrong trade.
    if (m.starred) continue;
    if (progressFor(progress, epubPath(m.id)) != nullptr) continue;
    fs_.remove(epubPath(m.id));
    fs_.remove(metaPath(m.id));
    ++removed;
  }
  return removed;
}

int ArticleStore::removeAll() {
  int removed = 0;
  for (const ArticleMeta& m : list()) {
    fs_.remove(epubPath(m.id));
    fs_.remove(metaPath(m.id));
    fs_.remove(statePathFor(epubPath(m.id)));
    ++removed;
  }
  return removed;
}

std::string ArticleStore::outcomeLabel(const std::string& lastOutcome) {
  if (lastOutcome == "upToDate") return "NO NEW";
  if (lastOutcome == "failed") return "FAILED";
  if (lastOutcome.rfind("new:", 0) == 0) {
    const std::string n = lastOutcome.substr(4);
    // A COUNT OF NOTHING IS `NO NEW`, not `0 NEW`. The engine writes `upToDate`
    // for that, so this only fires on a malformed watermark -- and stating the
    // honest word beats printing a zero somebody has to interpret.
    if (n.empty() || n == "0") return "NO NEW";
    return n + " NEW";
  }
  // `never`, and anything a future firmware wrote that this one does not know.
  // A watermark from a newer build must not make a screen say something false,
  // and "no sync has completed on this card" is the safe reading of a word we
  // cannot parse: it is what the reader sees before their first sync.
  return "NEVER";
}

std::string ArticleStore::homeMenuValue(bool configured, int unread) {
  // THREE STATES, TWO SHAPES. Never set up and nothing unread both answer with
  // an empty value -- which on Home's menu is the chevron SETTINGS already draws
  // -- and only a real number takes the slot. design/Main.dc.html carries the
  // argument: the right slot states a count only when there IS one, and zero is
  // not one.
  //
  // `0 UNREAD` SHIPPED AND WAS REPORTED OFF THE DEVICE. A reader who has read
  // everything was told so on the screen they see most often, every time, and
  // the words were longer the less there was to say.
  //
  // LIBRARY DIFFERS ON PURPOSE and the distinction is worth keeping: its count
  // is how many books are ON the card, a fact about a shelf, where this is how
  // many are WAITING, a fact about a queue. An empty shelf is worth stating; an
  // empty queue has nothing to report rather than a zero to report.
  if (!configured || unread <= 0) return std::string();
  return std::to_string(unread) + " UNREAD";
}

}  // namespace reader
