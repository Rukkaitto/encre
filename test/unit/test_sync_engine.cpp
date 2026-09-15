#include <map>
#include <memory>
#include <string>

#include "doctest.h"
#include "fake_fs.h"
#include "fake_http_transport.h"
#include "reader/sync_engine.h"

using namespace reader;

namespace {

struct FakeTokens : TokenStore {
  std::string access = "AT", refresh = "RT";
  bool present = true;
  bool load(std::string& a, std::string& r) override {
    if (!present) return false;
    a = access;
    r = refresh;
    return true;
  }
  bool save(std::string_view a, std::string_view r) override {
    access = a;
    refresh = r;
    present = true;
    return true;
  }
  void clear() override { present = false; }
};

// A sink factory that WRITES TO THE CARD on finish, which is what the shell's
// card sink does (`.part`, then a rename).
//
// IT HAS TO. The first version kept the bytes in a map and never touched the
// filesystem -- so `store.hasEpub` stayed false, and the idempotence case below
// caught it at once by trying to download everything a second time. A fake that
// is not the shape of the real thing tests the fake.
struct Sinks : SinkFactory {
  struct Held : BodySink {
    Sinks* owner;
    int id;
    std::string buf;
    Held(Sinks* o, int i) : owner(o), id(i) {}
    bool write(const uint8_t* p, size_t n) override {
      buf.append(reinterpret_cast<const char*>(p), n);
      return true;
    }
    bool finish() override {
      owner->bytes[id] = buf;
      owner->finished[id] = true;
      // Only a FINISHED download reaches the card, which is the whole of why a
      // cancelled one leaves no half file.
      return owner->fs->writeAll(owner->store->epubPath(id), buf);
    }
  };
  FakeFileSystem* fs = nullptr;
  ArticleStore* store = nullptr;
  std::map<int, std::string> bytes;
  std::map<int, bool> finished;
  std::vector<int> discarded;
  int refuse = -1;  // an id this factory will not take

  std::unique_ptr<BodySink> forArticle(int id) override {
    if (id == refuse) return nullptr;
    return std::make_unique<Held>(this, id);
  }
  void discard(int id) override {
    discarded.push_back(id);
    bytes.erase(id);
    finished.erase(id);
    if (fs != nullptr && store != nullptr) fs->remove(store->epubPath(id));
  }
};

WallabagCredentials creds() {
  WallabagCredentials c;
  c.server = "http://w.lan";
  c.clientId = "id";
  c.clientSecret = "sec";
  c.username = "u";
  c.password = "p";
  return c;
}

const char* kInfo = R"({"appname":"wallabag","version":"2.6.14"})";

std::string entry(int id, const char* updated, bool archived = false, bool starred = false) {
  return "{\"id\":" + std::to_string(id) + ",\"title\":\"A" + std::to_string(id) +
         "\",\"domain_name\":\"x.com\",\"reading_time\":5,\"is_archived\":" +
         (archived ? "true" : "false") + ",\"is_starred\":" + (starred ? "true" : "false") +
         ",\"updated_at\":\"" + updated + "\",\"tags\":[]}";
}

std::string page(const std::string& items, int pageNo = 1, int pages = 1) {
  return "{\"page\":" + std::to_string(pageNo) + ",\"pages\":" + std::to_string(pages) +
         ",\"total\":9,\"_embedded\":{\"items\":[" + items + "]}}";
}

struct Rig {
  FakeFileSystem fs;
  FakeHttpTransport http;
  FakeTokens tokens;
  Sinks sinks;
  ArticleStore store{fs};
  WallabagClient client{http, tokens, creds()};
  SyncEngine engine{client, store, sinks, 50};

  Rig() {
    sinks.fs = &fs;
    sinks.store = &store;
  }

  void run(int guard = 64) {
    REQUIRE(engine.begin());
    for (int i = 0; i < guard && engine.state() != SyncState::Done; ++i) {
      http.step();
      engine.poll();
    }
  }
};

}  // namespace

TEST_CASE("a full sync: info, push, listing, downloads, watermark") {
  Rig r;
  // One thing already owed, so the ORDER can be asserted.
  REQUIRE(r.store.writeMeta({5, "Old", "x.com", 5, false, false, "2026-08-01T10:00:00Z"}));
  REQUIRE(r.fs.writeAll(r.store.epubPath(5), "PK"));
  REQUIRE(r.store.queueStar(5, true));

  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200);  // the PATCH
  r.http.scriptOk(200, page(entry(7, "2026-09-03T10:00:00Z") + "," +
                            entry(8, "2026-09-02T10:00:00Z")));
  r.http.scriptOk(200, "EPUB7");
  r.http.scriptOk(200, "EPUB8");
  r.run();

  CHECK(r.engine.outcome() == SyncOutcome::New);
  CHECK(r.engine.fetched() == 2);
  CHECK(r.engine.toFetch() == 2);

  // THE ORDER IS PUSH BEFORE PULL: what the reader did on the device is the
  // newer fact, and pulling first would hand back an entry as unstarred and
  // overwrite it.
  REQUIRE(r.http.requests() == 5);
  CHECK(r.http.log()[0].path == "/api/info");
  CHECK(r.http.log()[1].method == "PATCH");
  CHECK(r.http.log()[1].path == "/api/entries/5?starred=1");
  CHECK(r.http.log()[2].path.find("/api/entries?") == 0);
  CHECK(r.http.log()[3].path == "/api/entries/7/export.epub");
  CHECK(r.http.log()[4].path == "/api/entries/8/export.epub");

  CHECK(r.store.pendingCount() == 0);
  CHECK(r.sinks.bytes[7] == "EPUB7");
  CHECK(r.sinks.finished[7]);

  SyncWatermark w;
  REQUIRE(r.store.loadWatermark(w));
  CHECK(w.lastOutcome == "new:2");
  // THE LARGEST updated_at SEEN, which is the server's clock handed back.
  CHECK(w.since == "2026-09-03T10:00:00Z");
}

TEST_CASE("nothing new is upToDate, and the stamp says so") {
  Rig r;
  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(""));
  r.run();
  CHECK(r.engine.outcome() == SyncOutcome::UpToDate);
  CHECK(r.engine.fetched() == 0);
  SyncWatermark w;
  REQUIRE(r.store.loadWatermark(w));
  CHECK(w.lastOutcome == "upToDate");
}

TEST_CASE("an entry archived on the SERVER has its local files removed") {
  // This is what the later sync's dropped `archive=0` exists to make possible:
  // without it the entry never comes back and the file sits there for ever.
  Rig r;
  REQUIRE(r.store.writeMeta({3, "Gone", "x.com", 5, false, false, "2026-08-01T10:00:00Z"}));
  REQUIRE(r.fs.writeAll(r.store.epubPath(3), "PK"));

  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(3, "2026-09-01T10:00:00Z", /*archived=*/true)));
  r.run();

  CHECK(r.engine.outcome() == SyncOutcome::UpToDate);
  CHECK_FALSE(r.fs.exists(r.store.epubPath(3)));
  CHECK(r.store.list().empty());
  // AND NOTHING IS QUEUED BACK AT THE SERVER. It already knows -- pushing the
  // archive would be telling it what it told us.
  CHECK(r.store.pendingCount() == 0);
}

TEST_CASE("a 404 on a PATCH acks, because the desired state is already true") {
  Rig r;
  REQUIRE(r.store.writeMeta({9, "X", "x.com", 5, false, false, "2026-08-01T10:00:00Z"}));
  REQUIRE(r.fs.writeAll(r.store.epubPath(9), "PK"));
  REQUIRE(r.store.queueArchive(9));
  REQUIRE(r.store.pendingCount() == 1);

  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(404);
  r.http.scriptOk(200, page(""));
  r.run();
  CHECK(r.store.pendingCount() == 0);
  CHECK(r.engine.outcome() == SyncOutcome::UpToDate);
}

TEST_CASE("a 500 on a PATCH does NOT ack, so the next sync tries again") {
  Rig r;
  REQUIRE(r.store.writeMeta({9, "X", "x.com", 5, false, false, "2026-08-01T10:00:00Z"}));
  REQUIRE(r.fs.writeAll(r.store.epubPath(9), "PK"));
  REQUIRE(r.store.queueArchive(9));

  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(500);
  r.http.scriptOk(200, page(""));
  r.run();
  CHECK(r.store.pendingCount() == 1);
}

TEST_CASE("the listing is walked page by page") {
  Rig r;
  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z"), 1, 2));
  r.http.scriptOk(200, page(entry(2, "2026-09-02T10:00:00Z"), 2, 2));
  r.http.scriptOk(200, "E1");
  r.http.scriptOk(200, "E2");
  r.run();
  CHECK(r.engine.outcome() == SyncOutcome::New);
  CHECK(r.engine.fetched() == 2);
  CHECK(r.http.log()[1].path.find("page=1") != std::string::npos);
  CHECK(r.http.log()[2].path.find("page=2") != std::string::npos);
}

TEST_CASE("a download that fails mid-sync keeps what landed and does NOT advance the watermark") {
  // Which is what makes an interrupted sync safe to retry.
  Rig r;
  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z") + "," +
                            entry(2, "2026-09-02T10:00:00Z")));
  r.http.scriptOk(200, "E1");
  r.http.scriptFailure(HttpFailure::Timeout);
  r.run();

  CHECK(r.engine.outcome() == SyncOutcome::Failed);
  CHECK(r.engine.fetched() == 1);
  CHECK(r.sinks.bytes.count(1) == 1);
  // The partial file went.
  CHECK(r.sinks.discarded == std::vector<int>{2});

  SyncWatermark w;
  REQUIRE(r.store.loadWatermark(w));
  CHECK(w.lastOutcome == "failed");
  CHECK(w.since.empty());
}

TEST_CASE("NotAWallabag and CredentialsRefused end the sync with nothing written") {
  {
    Rig r;
    r.http.scriptOk(200, R"({"hello":"router"})");
    r.run();
    CHECK(r.engine.outcome() == SyncOutcome::NotAWallabag);
    CHECK(r.store.list().empty());
  }
  {
    Rig r;
    r.http.scriptOk(200, kInfo);
    r.http.scriptOk(401);  // the listing
    r.http.scriptOk(401);  // the refresh
    r.http.scriptOk(401);  // the grant
    r.run();
    CHECK(r.engine.outcome() == SyncOutcome::CredentialsRefused);
    CHECK(r.store.list().empty());
  }
}

TEST_CASE("cancel stops after the file in flight, and is NOT reported as a failure") {
  // A reader who cancelled knows why the sync stopped; telling them it failed
  // would be the device inventing a fault.
  Rig r;
  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z") + "," +
                            entry(2, "2026-09-02T10:00:00Z")));
  r.http.scriptOk(200, "E1");
  r.http.scriptOk(200, "E2");

  REQUIRE(r.engine.begin());
  for (int i = 0; i < 64 && r.engine.state() != SyncState::Done; ++i) {
    r.http.step();
    r.engine.poll();
    if (r.engine.fetched() == 1 && r.engine.state() == SyncState::Running) r.engine.cancel();
  }
  CHECK(r.engine.outcome() == SyncOutcome::Cancelled);
  CHECK(r.engine.fetched() == 1);

  // THE WATERMARK IS UNTOUCHED, so the next sync asks for what this one did not
  // get -- and the outcome is NOT `failed`.
  SyncWatermark w;
  REQUIRE(r.store.loadWatermark(w));
  CHECK(w.since.empty());
  CHECK(w.lastOutcome == "never");
}

TEST_CASE("a sink that refuses one article costs that article, never the sync") {
  // One missing beats nineteen not fetched.
  Rig r;
  r.sinks.refuse = 2;
  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z") + "," +
                            entry(2, "2026-09-02T10:00:00Z")));
  r.http.scriptOk(200, "E1");
  r.run();
  CHECK(r.engine.outcome() == SyncOutcome::New);
  CHECK(r.engine.fetched() == 1);
  CHECK(r.sinks.bytes.count(1) == 1);
  CHECK(r.sinks.bytes.count(2) == 0);
}

TEST_CASE("an article already on the card is not downloaded again") {
  Rig r;
  REQUIRE(r.store.writeMeta({1, "Have", "x.com", 5, false, false, "2026-08-01T10:00:00Z"}));
  REQUIRE(r.fs.writeAll(r.store.epubPath(1), "PK"));

  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z")));
  r.run();
  CHECK(r.engine.outcome() == SyncOutcome::UpToDate);
  CHECK(r.engine.toFetch() == 0);
  // ...but its metadata was refreshed, which is what a `since` sync is for.
  ArticleMeta m;
  REQUIRE(r.store.readMeta(1, m));
  CHECK(m.updatedAt == "2026-09-01T10:00:00Z");
}

TEST_CASE("a sync run TWICE against the same server yields an identical store") {
  // IDEMPOTENCE IS WHAT MAKES A CANCELLED SYNC SAFE TO RETRY, which is the
  // property every failure path above rests on.
  auto once = [](Rig& r) {
    r.http.scriptOk(200, kInfo);
    r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z") + "," +
                              entry(2, "2026-09-02T10:00:00Z")));
    r.http.scriptOk(200, "E1");
    r.http.scriptOk(200, "E2");
    r.run();
  };

  Rig r;
  once(r);
  REQUIRE(r.engine.outcome() == SyncOutcome::New);
  const size_t after1 = r.store.list().size();
  std::string meta1;
  REQUIRE(r.fs.readAll(r.store.metaPath(1), meta1));

  // The second run sees the files already there, so it downloads nothing.
  r.http.scriptOk(200, kInfo);
  r.http.scriptOk(200, page(entry(1, "2026-09-01T10:00:00Z") + "," +
                            entry(2, "2026-09-02T10:00:00Z")));
  REQUIRE(r.engine.begin());
  for (int i = 0; i < 64 && r.engine.state() != SyncState::Done; ++i) {
    r.http.step();
    r.engine.poll();
  }
  CHECK(r.engine.outcome() == SyncOutcome::UpToDate);
  CHECK(r.store.list().size() == after1);
  std::string meta2;
  REQUIRE(r.fs.readAll(r.store.metaPath(1), meta2));
  CHECK(meta1 == meta2);
}

TEST_CASE("prune runs on a successful sync and not on a failed one") {
  Rig r;
  // keep is 50, so nothing is pruned here; what is asserted is that a FAILED
  // sync does not touch the card at all beyond its outcome.
  REQUIRE(r.store.writeMeta({1, "A", "x.com", 5, false, false, "2026-08-01T10:00:00Z"}));
  REQUIRE(r.fs.writeAll(r.store.epubPath(1), "PK"));
  r.http.scriptOk(200, kInfo);
  r.http.scriptFailure(HttpFailure::Refused);
  r.run();
  CHECK(r.engine.outcome() == SyncOutcome::Failed);
  CHECK(r.store.list().size() == 1);
}
