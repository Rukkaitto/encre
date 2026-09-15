#pragma once
#include <string>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// FORWARD-DECLARED RATHER THAN INCLUDED. `openedFromProgress` only compares the
// pointer against null, so the definition is not needed here -- and
// `reading_store.h` would be a header edge bought for one inline body, which is
// the coupling `settings.h` already refuses at `bodyPpem`.
struct ProgressEntry;

// The article directory: /.reader/articles/, and everything the device knows
// about an article that is not its text.
//
// TWO FILES PER ARTICLE, NAMED BY THE SERVER'S INTEGER ID: `<id>.epub` and
// `<id>.json`. One object per file, through the flat parser, which is the same
// shape as /.reader/state/<hash>.json and for the same two reasons: the parser
// cannot hold an array, and one corrupt file costs one row rather than the list.
//
// ARTICLES ARE NEVER IN THE LIBRARY. They live here and the Library lists
// /books, so an archived article's file is removed by a sync and the Library's
// delete never sees it. That is not tidiness: the two lists have different
// lifetimes, and a file the server may take away must not sit where a reader
// expects their own books to stay.
struct ArticleMeta {
  int id = 0;
  std::string title;
  std::string domain;
  int readingTime = 0;
  bool starred = false;
  bool archived = false;
  // THE SERVER'S OWN STRING, STORED VERBATIM. This device has no clock (#132),
  // so every ordering and every `since` is the SERVER's clock handed back
  // unchanged. wallabag's ISO-8601 sorts chronologically as a string, which is
  // what lets `list()` order by it without parsing a date.
  std::string updatedAt;
};

// What the queue owes the server. Marker FILES rather than a list in one file,
// because a list is a read-modify-write that a power cut can lose whole, where a
// marker is one create and one delete and the directory IS the queue.
enum class PendingKind { Archive, Star, Unstar };

struct PendingAction {
  int id = 0;
  PendingKind kind = PendingKind::Archive;
};

// The watermark: what the last sync asked for, and what it did.
struct SyncWatermark {
  // The largest `updated_at` any sync has seen, sent back as `since`. The
  // server's clock, never ours.
  std::string since;
  // `never`, `upToDate`, `new:<n>`, `failed`. The four values the Articles
  // list's stamp and the account screen's `Last sync` row BOTH draw -- one
  // watermark, so the two screens cannot disagree about what the last sync did.
  std::string lastOutcome = "never";
};

inline constexpr const char* kArticlesDir = "/.reader/articles";
inline constexpr const char* kArticlesQueueDir = "/.reader/articles/queue";
inline constexpr const char* kArticlesWatermarkPath = "/.reader/articles/sync.json";

// EVERY FUNCTION TAKES THE FileSystem, none holds one. That is reading_store.h's
// shape and it is deliberate: this is the layer between the screens and the
// card, and an object holding a reference would have to be constructed
// somewhere, which is one more thing for the shell to remember.
class ArticleStore {
 public:
  explicit ArticleStore(FileSystem& fs) : fs_(fs) {}

  std::string epubPath(int id) const;
  std::string metaPath(int id) const;
  bool hasEpub(int id) const;

  bool writeMeta(const ArticleMeta& m);
  bool readMeta(int id, ArticleMeta& out) const;

  // Every article whose sidecar AND .epub both exist, NEWEST FIRST by
  // `updatedAt` -- a string comparison, which is chronological on wallabag's
  // ISO-8601 and needs no clock.
  //
  // ONE LISTING, and the sidecars are read from it. A sidecar with no .epub is
  // SKIPPED and counted by `strays` instead, because a download that died
  // between the two writes must show in the log rather than as a row that will
  // not open.
  std::vector<ArticleMeta> list(int* strays = nullptr) const;

  // Not archived, and not finished according to /.reader/state. THE STORE ASKS
  // reading_store RATHER THAN RE-DERIVING: a second answer to "has this been
  // read" is a second answer the two screens could disagree about.
  int unreadCount() const;

  // --- the offline queue ---------------------------------------------------
  //
  // QUEUEING AN ARCHIVE REMOVES THE LOCAL FILES IMMEDIATELY. The reader pressed
  // Archive; the article leaves their device now and the server learns on the
  // next sync. Leaving the file until the push would mean the list still showed
  // an article the reader has dealt with, which is the one thing the press was
  // for.
  bool queueArchive(int id);
  bool queueStar(int id, bool starred);
  int pendingCount() const;
  std::vector<PendingAction> pending() const;
  bool ack(int id, PendingKind kind);

  // --- the watermark -------------------------------------------------------
  bool loadWatermark(SyncWatermark& out) const;
  bool saveWatermark(const SyncWatermark& w);

  // Remove the oldest UNREAD, UNSTARTED articles beyond `keep`.
  //
  // NEVER ONE WITH A READING POSITION, which is the whole of why this is not a
  // plain "delete the oldest": a reader part-way through an article has spent
  // attention on it, and taking it away to make room for one they have not
  // opened is the wrong trade. Starred ones are kept for the same reason stated
  // by the reader rather than inferred.
  int prune(int keep);

  // Every .epub and sidecar, and the reading sidecar for each. LEAVES THE QUEUE
  // AND THE WATERMARK ALONE: what the server is still owed is not a downloaded
  // file, and forgetting `since` would make the next sync re-fetch everything
  // the reader has just asked to be rid of.
  int removeAll();

  // THE FOUR WORDS BOTH SCREENS DRAW, from one watermark, so the Articles list's
  // stamp and the account screen's `Last sync` row cannot disagree about what
  // the last sync did: `NEVER`, `NO NEW`, `N NEW`, `FAILED`.
  //
  // THEY ARE OUTCOMES AND NEVER AGES (#132). A deep sleep on battery is a full
  // power-down, so nothing survives to measure elapsed time across one, and the
  // server's `Date` header is the server's clock. What the device holds is what
  // the last sync DID.
  //
  // SHORT BECAUSE THE SYNC ROW IS: design/Articles.dc.html gives its stamp
  // 279.03px beside a `Sync now` that may never elide, and `NEVER SYNCED` and
  // `UP TO DATE` both overflowed it at BOTH geometries. The account screen has
  // room for the longer words and does not get them -- a vocabulary that fits
  // one slot and not the other is two vocabularies.
  static std::string outcomeLabel(const std::string& lastOutcome);

  // Home's ARTICLES row. `N UNREAD` when configured, EMPTY when not -- and the
  // empty value is what draws the chevron, which is Main.dc.html's menu note
  // refusing a setup nag on the screen the device boots to.
  // WHETHER AN ARTICLE HAS BEEN STARTED, which is what the bullet's absence and
  // the UNREAD count both mean. One spelling, because there were two: this rule
  // sat in `unreadCount()` and again in `ArticlesScreen::load()`, and both took
  // the book's meaning.
  //
  // WHICH MADE THE MARK UNREACHABLE. `ProgressEntry::finished` is set by an
  // explicit press -- BookEnd's slab, or the Library's actions overlay -- and an
  // article has neither, so every row stayed solid for ever. Reported off the
  // device as "the little dot on the left of an article never goes away".
  //
  // THE TWO MEANINGS ARE RIGHT FOR THE TWO THINGS. A novel is not read because
  // you opened it, and marking one finished is a fair thing to ask once a book.
  // An article is a single sitting with no second session to come back to, and a
  // list needing a press per item to stay useful is a chore in front of a feature
  // whose point is not being one. design/Articles.dc.html has the argument.
  //
  // THE SIDECAR EXISTING IS THE SIGNAL, which is the same record the percentage
  // comes from: a position is saved on the way out, on a chapter crossing, into
  // sleep, and in the quiet window two seconds after the buttons stop. `finished`
  // is not tested because it implies the record exists -- testing it too would be
  // two conditions where one decides.
  static bool openedFromProgress(const ProgressEntry* p) { return p != nullptr; }

  static std::string homeMenuValue(bool configured, int unread);

 private:
  FileSystem& fs_;
};

}  // namespace reader
