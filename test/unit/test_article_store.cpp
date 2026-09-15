#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/article_store.h"
#include "reader/reading_position.h"
#include "reader/reading_store.h"

using namespace reader;

namespace {

ArticleMeta meta(int id, const std::string& updated, bool starred = false,
                 bool archived = false) {
  return {id, std::string("Article ") + std::to_string(id), "LONGREADS", 12, starred, archived,
          updated};
}

void put(ArticleStore& s, FakeFileSystem& fs, const ArticleMeta& m, bool withEpub = true) {
  REQUIRE(s.writeMeta(m));
  if (withEpub) REQUIRE(fs.writeAll(s.epubPath(m.id), "PK\x03\x04"));
}

// A reading sidecar for an article, which is what makes it STARTED -- and with
// `finished` what makes it READ. Written through the real store, so this test
// cannot drift from the format the reader writes.
void markRead(FakeFileSystem& fs, const std::string& path, bool finished) {
  ReadingPosition p;
  p.bookPath = path;
  p.bookBytes = 4;
  p.spine = 0;
  p.block = 1;
  p.finished = finished;
  REQUIRE(savePosition(fs, p) != SaveResult::Failed);
}

}  // namespace

TEST_CASE("list returns one entry per sidecar with an epub, newest first") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00+0000"));
  put(s, fs, meta(2, "2026-09-03T10:00:00+0000"));
  put(s, fs, meta(3, "2026-09-02T10:00:00+0000"));

  const std::vector<ArticleMeta> got = s.list();
  REQUIRE(got.size() == 3);
  // A STRING COMPARISON ON ISO-8601, which is chronological and needs no clock --
  // this device has none (#132) and the ordering is the server's own stamp.
  CHECK(got[0].id == 2);
  CHECK(got[1].id == 3);
  CHECK(got[2].id == 1);
  CHECK(got[0].title == "Article 2");
  CHECK(got[0].domain == "LONGREADS");
}

TEST_CASE("a sidecar with no epub is SKIPPED and COUNTED, never listed") {
  // A download that died between the two writes must show in the log rather than
  // as a row that will not open.
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00+0000"));
  put(s, fs, meta(2, "2026-09-02T10:00:00+0000"), /*withEpub=*/false);

  int strays = -1;
  const std::vector<ArticleMeta> got = s.list(&strays);
  CHECK(got.size() == 1);
  CHECK(got[0].id == 1);
  CHECK(strays == 1);
}

TEST_CASE("a corrupt sidecar costs ONE row, which is why there is a file per article") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00+0000"));
  put(s, fs, meta(2, "2026-09-02T10:00:00+0000"));
  REQUIRE(fs.writeAll(s.metaPath(2), "{not json"));
  CHECK(s.list().size() == 1);
}

TEST_CASE("a sidecar whose stored id disagrees with its name is refused") {
  // reading_store's collision refusal one directory over: trusting the field
  // would make this sidecar name another article's .epub.
  FakeFileSystem fs;
  ArticleStore s(fs);
  ArticleMeta m = meta(1, "2026-09-01T10:00:00+0000");
  REQUIRE(s.writeMeta(m));
  REQUIRE(fs.writeAll(s.epubPath(1), "x"));
  std::string text;
  REQUIRE(fs.readAll(s.metaPath(1), text));
  REQUIRE(fs.writeAll(s.metaPath(1), "{\"id\":99,\"title\":\"t\",\"updatedAt\":\"z\"}"));
  ArticleMeta out;
  CHECK_FALSE(s.readMeta(1, out));
  CHECK(s.list().empty());
}

TEST_CASE("unreadCount asks reading_store rather than re-deriving") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00+0000"));
  put(s, fs, meta(2, "2026-09-02T10:00:00+0000"));
  put(s, fs, meta(3, "2026-09-03T10:00:00+0000", /*starred=*/false, /*archived=*/true));
  CHECK(s.unreadCount() == 2);

  // FINISHED is not unread; merely STARTED still is.
  markRead(fs, s.epubPath(1), /*finished=*/true);
  CHECK(s.unreadCount() == 1);
  markRead(fs, s.epubPath(2), /*finished=*/false);
  CHECK(s.unreadCount() == 1);
}

TEST_CASE("queueing an archive removes the local files at once") {
  // The reader pressed Archive: the article leaves their device now and the
  // server learns later. Leaving the file until the push would keep the row the
  // press was for removing.
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00+0000"));
  markRead(fs, s.epubPath(1), /*finished=*/false);
  REQUIRE(fs.exists(statePathFor(s.epubPath(1))));

  CHECK(s.queueArchive(1));
  CHECK_FALSE(fs.exists(s.epubPath(1)));
  CHECK_FALSE(fs.exists(s.metaPath(1)));
  CHECK_FALSE(fs.exists(statePathFor(s.epubPath(1))));
  CHECK(s.list().empty());
  CHECK(s.pendingCount() == 1);
  CHECK(s.pending()[0].kind == PendingKind::Archive);
  CHECK(s.pending()[0].id == 1);
}

TEST_CASE("starring then unstarring owes the server ONE action, not two") {
  // Marker FILES make that a remove rather than a read-modify-write, which is
  // the whole reason the queue is a directory.
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00+0000"));

  CHECK(s.queueStar(1, true));
  REQUIRE(s.pendingCount() == 1);
  CHECK(s.pending()[0].kind == PendingKind::Star);
  ArticleMeta m;
  REQUIRE(s.readMeta(1, m));
  CHECK(m.starred);

  CHECK(s.queueStar(1, false));
  REQUIRE(s.pendingCount() == 1);
  CHECK(s.pending()[0].kind == PendingKind::Unstar);
  REQUIRE(s.readMeta(1, m));
  CHECK_FALSE(m.starred);
}

TEST_CASE("the queue is oldest-id first, and ack removes exactly one marker") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(7, "2026-09-01T10:00:00+0000"));
  put(s, fs, meta(3, "2026-09-02T10:00:00+0000"));
  REQUIRE(s.queueStar(7, true));
  REQUIRE(s.queueArchive(3));

  const std::vector<PendingAction> q = s.pending();
  REQUIRE(q.size() == 2);
  // A server id only grows, so id order IS creation order and needs no clock.
  CHECK(q[0].id == 3);
  CHECK(q[1].id == 7);

  CHECK(s.ack(3, PendingKind::Archive));
  CHECK(s.pendingCount() == 1);
  CHECK(s.pending()[0].id == 7);
  // AND ACKING IS IDEMPOTENT, which falls out of FileSystem::remove reporting the
  // END STATE rather than whether it did anything: "the marker is not there" is
  // exactly what an ack wants to be true. That matters because a PATCH on an id
  // the server no longer has (404) acks too -- the desired state is already
  // true -- so the engine can ack the same marker twice without inventing a
  // failure.
  CHECK(s.ack(3, PendingKind::Archive));
  CHECK(s.pendingCount() == 1);
}

TEST_CASE("the watermark round-trips, and an absent one is `never`") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  SyncWatermark w;
  CHECK_FALSE(s.loadWatermark(w));
  CHECK(w.lastOutcome == "never");
  CHECK(w.since.empty());

  w.since = "2026-09-03T10:00:00+0000";
  w.lastOutcome = "new:3";
  REQUIRE(s.saveWatermark(w));
  SyncWatermark back;
  REQUIRE(s.loadWatermark(back));
  CHECK(back.since == w.since);
  CHECK(back.lastOutcome == "new:3");
}

TEST_CASE("prune removes the oldest, and NEVER one starred or started") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  for (int i = 1; i <= 5; ++i) put(s, fs, meta(i, "2026-09-0" + std::to_string(i) + "T10:00:00Z"));
  // id 1 is the oldest and is STARTED; id 2 is next and is STARRED.
  markRead(fs, s.epubPath(1), /*finished=*/false);
  ArticleMeta m2 = meta(2, "2026-09-02T10:00:00Z", /*starred=*/true);
  REQUIRE(s.writeMeta(m2));

  CHECK(s.prune(3) == 2);
  std::vector<int> left;
  for (const ArticleMeta& m : s.list()) left.push_back(m.id);
  // 3 and 4 went -- the oldest two that are neither starred nor started.
  CHECK(left == std::vector<int>{5, 2, 1});
}

TEST_CASE("prune does nothing when the list already fits") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  for (int i = 1; i <= 3; ++i) put(s, fs, meta(i, "2026-09-0" + std::to_string(i) + "T10:00:00Z"));
  CHECK(s.prune(3) == 0);
  CHECK(s.prune(50) == 0);
  CHECK(s.list().size() == 3);
}

TEST_CASE("prune can be blocked entirely, and says so by removing nothing") {
  // Every article starred or started: `keep` is a target, not a promise, and the
  // reader's own marks outrank it.
  FakeFileSystem fs;
  ArticleStore s(fs);
  for (int i = 1; i <= 4; ++i)
    put(s, fs, meta(i, "2026-09-0" + std::to_string(i) + "T10:00:00Z", /*starred=*/true));
  CHECK(s.prune(1) == 0);
  CHECK(s.list().size() == 4);
}

TEST_CASE("removeAll takes the files and the reading sidecars, and LEAVES the queue") {
  // What the server is still owed is not a downloaded file, and forgetting
  // `since` would make the next sync re-fetch everything the reader has just
  // asked to be rid of.
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00Z"));
  put(s, fs, meta(2, "2026-09-02T10:00:00Z"));
  markRead(fs, s.epubPath(2), /*finished=*/true);
  REQUIRE(s.queueStar(1, true));
  SyncWatermark w;
  w.since = "2026-09-02T10:00:00Z";
  w.lastOutcome = "new:2";
  REQUIRE(s.saveWatermark(w));

  CHECK(s.removeAll() == 2);
  CHECK(s.list().empty());
  CHECK_FALSE(fs.exists(statePathFor(s.epubPath(2))));
  CHECK(s.pendingCount() == 1);
  SyncWatermark after;
  REQUIRE(s.loadWatermark(after));
  CHECK(after.since == "2026-09-02T10:00:00Z");
}

TEST_CASE("Home's ARTICLES value is empty when unconfigured, which draws the chevron") {
  // Main.dc.html's menu note refuses a setup nag on the screen the device boots
  // to, and an empty value is SETTINGS' own mechanism for the chevron.
  CHECK(ArticleStore::homeMenuValue(/*configured=*/false, 0).empty());
  CHECK(ArticleStore::homeMenuValue(/*configured=*/false, 7).empty());
  CHECK(ArticleStore::homeMenuValue(/*configured=*/true, 0) == "0 UNREAD");
  CHECK(ArticleStore::homeMenuValue(/*configured=*/true, 3) == "3 UNREAD");
}

TEST_CASE("a file whose name is not an id is ignored rather than parsed") {
  FakeFileSystem fs;
  ArticleStore s(fs);
  put(s, fs, meta(1, "2026-09-01T10:00:00Z"));
  REQUIRE(fs.writeAll(std::string(kArticlesDir) + "/notes.json", "{}"));
  REQUIRE(fs.writeAll(std::string(kArticlesDir) + "/.json", "{}"));
  REQUIRE(fs.writeAll(std::string(kArticlesDir) + "/12x.json", "{}"));
  CHECK(s.list().size() == 1);
}
