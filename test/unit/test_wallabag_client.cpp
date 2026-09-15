#include <string>

#include "doctest.h"
#include "fake_http_transport.h"
#include "grained_source.h"
#include "reader/wallabag_client.h"

using namespace reader;

namespace {

struct FakeTokens : TokenStore {
  std::string access, refresh;
  bool present = false;
  int saves = 0, clears = 0;
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
    ++saves;
    return true;
  }
  void clear() override {
    present = false;
    access.clear();
    refresh.clear();
    ++clears;
  }
};

WallabagCredentials creds() {
  WallabagCredentials c;
  c.server = "http://w.lan";
  c.clientId = "12_abc";
  c.clientSecret = "sec&ret";
  c.username = "lucasg";
  c.password = "p@ss w/rd";
  return c;
}

const char* kGrant = R"({"access_token":"AT1","refresh_token":"RT1","expires_in":3600})";

// Drive a client to Done, stepping the transport for each round trip it makes.
//
// NO `release()` BETWEEN ITERATIONS. `poll()` may START the next request -- a
// refresh, a grant, a retry -- and releasing afterwards would put the transport
// back to Idle and wipe the request the client had just begun. The first version
// of this helper did exactly that and made the whole refresh ladder look broken.
void run(WallabagClient& c, FakeHttpTransport& t) {
  for (int guard = 0; guard < 16 && c.state() != WallabagState::Done; ++guard) {
    t.step();
    c.poll();
  }
}

std::string page(const std::string& items, int pageNo = 1, int pages = 1,
                 int total = 1) {
  return std::string("{\"page\":") + std::to_string(pageNo) + ",\"limit\":20,\"pages\":" +
         std::to_string(pages) + ",\"total\":" + std::to_string(total) +
         ",\"_embedded\":{\"items\":[" + items + "]}}";
}

}  // namespace

TEST_CASE("/api/info carries NO Authorization header, which is the whole point of it") {
  // Reachability is answerable BEFORE any credential is used, so "that URL is
  // not a wallabag" is a different message from "those credentials were
  // refused" -- and the reader can act on each differently.
  FakeHttpTransport t;
  FakeTokens tok;
  t.scriptOk(200, R"({"appname":"wallabag","version":"2.6.14"})");
  WallabagClient c(t, tok, creds());
  BufferSink sink(4096);
  REQUIRE(c.beginInfo(sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::Ok);
  REQUIRE(t.requests() == 1);
  CHECK(t.last().method == "GET");
  CHECK(t.last().path == "/api/info");
  CHECK_FALSE(t.last().hasHeaderNamed("Authorization"));
}

TEST_CASE("NotAWallabag is answered from the BODY, not from the status") {
  // A proxy, a router's captive portal and a different application all answer
  // 200 to /api/info. Only the body says which.
  FakeHttpTransport t;
  FakeTokens tok;
  t.scriptOk(200, R"({"hello":"i am a router"})");
  WallabagClient c(t, tok, creds());
  BufferSink sink(4096);
  REQUIRE(c.beginInfo(sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::NotAWallabag);
}

TEST_CASE("a transport failure is Unreachable, distinct from both") {
  FakeHttpTransport t;
  FakeTokens tok;
  t.scriptFailure(HttpFailure::Dns);
  WallabagClient c(t, tok, creds());
  BufferSink sink(4096);
  REQUIRE(c.beginInfo(sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::Unreachable);
}

TEST_CASE("with no token at all, the password grant goes FIRST -- no wasted 401") {
  FakeHttpTransport t;
  FakeTokens tok;
  t.scriptOk(200, kGrant);
  t.scriptOk(200, page(""));
  WallabagClient c(t, tok, creds());
  BufferSink sink(kListingSinkCap);
  REQUIRE(c.beginListing("", 1, sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::Ok);
  REQUIRE(t.requests() == 2);

  CHECK(t.log()[0].method == "POST");
  CHECK(t.log()[0].path == "/oauth/v2/token");
  CHECK(t.log()[0].hasHeader("Content-Type", "application/x-www-form-urlencoded"));
  // FORM-ENCODED, because a password is whatever the reader typed and an `&` in
  // one would otherwise end the field and start a new one.
  CHECK(t.log()[0].body ==
        "grant_type=password&client_id=12_abc&client_secret=sec%26ret"
        "&username=lucasg&password=p%40ss%20w%2Frd");

  CHECK(t.log()[1].hasHeader("Authorization", "Bearer AT1"));
  CHECK(tok.saves == 1);
  CHECK(tok.access == "AT1");
  CHECK(tok.refresh == "RT1");
}

TEST_CASE("a FIRST sync asks archive=0; a LATER one drops it and sends since") {
  // THE LOAD-BEARING HALF IS THE DROP. An entry archived on the SERVER has to
  // come back so its local file can be removed, and `archive=0` would hide
  // exactly those -- leaving a reader with articles their phone archived last
  // week and no way for this device ever to learn it.
  FakeHttpTransport t;
  FakeTokens tok;
  tok.present = true;
  tok.access = "AT0";
  tok.refresh = "RT0";

  t.scriptOk(200, page(""));
  WallabagClient c(t, tok, creds());
  BufferSink sink(kListingSinkCap);
  REQUIRE(c.beginListing("", 1, sink));
  run(c, t);
  CHECK(t.last().path == "/api/entries?detail=metadata&perPage=20&page=1&archive=0");

  t.scriptOk(200, page("", 2, 2, 24));
  BufferSink sink2(kListingSinkCap);
  REQUIRE(c.beginListing("2026-09-03T10:00:00+0000", 2, sink2));
  run(c, t);
  const std::string got = t.last().path;
  CHECK(got.find("&archive=0") == std::string::npos);
  CHECK(got.find("&since=") != std::string::npos);
  CHECK(got.find("page=2") != std::string::npos);
}

TEST_CASE("since is a UNIX timestamp, computed without a clock") {
  // wallabag's `since` is an integer and the watermark stores the server's
  // ISO-8601 string, so one of them has to convert. Days-from-civil is a closed
  // form -- nothing here asks what time it is (#132).
  CHECK(unixTimeFromIso8601("1970-01-01T00:00:00+0000") == 0);
  CHECK(unixTimeFromIso8601("2026-09-03T10:00:00+0000") == 1788429600);
  // An offset shifts it, so a differently-configured server does not move the
  // watermark by hours.
  CHECK(unixTimeFromIso8601("2026-09-03T12:00:00+0200") == 1788429600);
  CHECK(unixTimeFromIso8601("2026-09-03T10:00:00Z") == 1788429600);
  // AN UNPARSEABLE STAMP IS 0, which the caller reads as "ask for everything" --
  // the safe direction, since a sync that re-offers what we have is slower and a
  // sync that skips is a lost article.
  CHECK(unixTimeFromIso8601("") == 0);
  CHECK(unixTimeFromIso8601("not a date") == 0);
  CHECK(unixTimeFromIso8601("2026-13-03T10:00:00Z") == 0);
}

TEST_CASE("a 401 triggers ONE refresh and ONE retry") {
  FakeHttpTransport t;
  FakeTokens tok;
  tok.present = true;
  tok.access = "STALE";
  tok.refresh = "RT0";

  t.scriptOk(401);
  t.scriptOk(200, R"({"access_token":"AT2","refresh_token":"RT2"})");
  t.scriptOk(200, page(""));

  WallabagClient c(t, tok, creds());
  BufferSink sink(kListingSinkCap);
  REQUIRE(c.beginListing("", 1, sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::Ok);
  REQUIRE(t.requests() == 3);
  CHECK(t.log()[0].hasHeader("Authorization", "Bearer STALE"));
  CHECK(t.log()[1].path == "/oauth/v2/token");
  CHECK(t.log()[1].body.find("grant_type=refresh_token") == 0);
  CHECK(t.log()[1].body.find("refresh_token=RT0") != std::string::npos);
  CHECK(t.log()[2].hasHeader("Authorization", "Bearer AT2"));
  CHECK(tok.access == "AT2");
}

TEST_CASE("a 401 on the REFRESH triggers one password grant and one retry") {
  FakeHttpTransport t;
  FakeTokens tok;
  tok.present = true;
  tok.access = "STALE";
  tok.refresh = "EXPIRED";

  t.scriptOk(401);            // the call
  t.scriptOk(401);            // the refresh
  t.scriptOk(200, kGrant);    // the password grant
  t.scriptOk(200, page(""));  // the retry

  WallabagClient c(t, tok, creds());
  BufferSink sink(kListingSinkCap);
  REQUIRE(c.beginListing("", 1, sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::Ok);
  REQUIRE(t.requests() == 4);
  CHECK(t.log()[2].body.find("grant_type=password") == 0);
  CHECK(t.log()[3].hasHeader("Authorization", "Bearer AT1"));
}

TEST_CASE("a 401 on the PASSWORD GRANT is CredentialsRefused, and the tokens are cleared") {
  // The end of the ladder. The server has judged the file and said no, and
  // nothing the reader can press changes the file -- which is why the dialog
  // offers no retry. A stored token the server has stopped honouring is worse
  // than none: it costs a round trip before every call.
  FakeHttpTransport t;
  FakeTokens tok;
  tok.present = true;
  tok.access = "STALE";
  tok.refresh = "EXPIRED";

  t.scriptOk(401);
  t.scriptOk(401);
  t.scriptOk(401);

  WallabagClient c(t, tok, creds());
  BufferSink sink(kListingSinkCap);
  REQUIRE(c.beginListing("", 1, sink));
  run(c, t);
  CHECK(c.result() == WallabagResult::CredentialsRefused);
  CHECK(tok.clears == 1);
  CHECK_FALSE(tok.present);
  // THREE ROUND TRIPS AND NO MORE. A device hammering a server that has already
  // answered is what the one-step-each-rung ladder prevents.
  CHECK(t.requests() == 3);
}

TEST_CASE("the download, archive and star requests are built verbatim") {
  FakeHttpTransport t;
  FakeTokens tok;
  tok.present = true;
  tok.access = "AT";
  WallabagClient c(t, tok, creds());

  t.scriptOk(200, "PK");
  BufferSink epub(4096);
  REQUIRE(c.beginDownload(42, epub));
  run(c, t);
  CHECK(t.last().method == "GET");
  CHECK(t.last().path == "/api/entries/42/export.epub");

  t.scriptOk(200);
  NullSink n1;
  REQUIRE(c.beginArchive(42, n1));
  run(c, t);
  // THE PARAMETERS ARE IN THE BODY AND THE PATH CARRIES NONE, which is what the
  // server actually reads: `patchEntriesAction` takes them off Symfony's
  // `$request->request`, filled by FOSRestBundle from the BODY, and PHP never
  // populates `$_POST` for a PATCH at all. These three asserted the query-string
  // form verbatim and were green while the device pushed nothing -- wallabag
  // answers 200 to a PATCH whose parameters it never saw, the push step acks the
  // queue on any 2xx, and the intent is gone. So the BODY is what is asserted.
  CHECK(t.last().method == "PATCH");
  CHECK(t.last().path == "/api/entries/42");
  CHECK(t.last().body == "archive=1");
  CHECK(t.last().hasHeader("Content-Type", "application/x-www-form-urlencoded"));

  t.scriptOk(200);
  NullSink n2;
  REQUIRE(c.beginStar(42, true, n2));
  run(c, t);
  CHECK(t.last().path == "/api/entries/42");
  CHECK(t.last().body == "starred=1");

  t.scriptOk(200);
  NullSink n3;
  REQUIRE(c.beginStar(42, false, n3));
  run(c, t);
  CHECK(t.last().path == "/api/entries/42");
  // `starred=0` AND NOT AN ABSENT PARAMETER: unstarring has to SAY so, or the
  // server keeps the star and the queue acks anyway.
  CHECK(t.last().body == "starred=0");

  // EVERY AUTHENTICATED CALL CARRIES THE BEARER.
  for (const auto& r : t.log()) CHECK(r.hasHeader("Authorization", "Bearer AT"));
}

TEST_CASE("a 404 on a PATCH is the CALLER's to interpret, not a client failure") {
  // The desired state is already true, so the engine acks it -- this layer must
  // not turn that into a refusal.
  FakeHttpTransport t;
  FakeTokens tok;
  tok.present = true;
  tok.access = "AT";
  t.scriptOk(404);
  WallabagClient c(t, tok, creds());
  NullSink n;
  REQUIRE(c.beginArchive(9, n));
  run(c, t);
  CHECK(c.result() == WallabagResult::Ok);
  CHECK(c.status() == 404);
}

// --- the listing parser ------------------------------------------------------

TEST_CASE("the listing parser reads the named fields and skips everything else") {
  const std::string body = page(
      R"({"id":7,"title":"First","domain_name":"longreads.com","reading_time":22,)"
      R"("is_archived":false,"is_starred":true,"updated_at":"2026-09-03T10:00:00+0000",)"
      R"("tags":[{"id":99,"label":"x"}],"preview_picture":null,"content":"ignored"},)"
      R"({"id":8,"title":"Second","domain_name":null,"reading_time":9,)"
      R"("is_archived":true,"is_starred":false,"updated_at":"2026-09-02T09:00:00+0000","tags":[]})",
      1, 2, 24);

  for (const size_t grain : {size_t(1), size_t(5), body.size() + 1}) {
    CAPTURE(grain);
    grainsrc::Grained src(body, grain);
    ListingPage p;
    REQUIRE(parseListing(src, p));
    CHECK(p.page == 1);
    CHECK(p.pages == 2);
    CHECK(p.total == 24);
    REQUIRE(p.entries.size() == 2);
    // THE `id` INSIDE `tags` IS NOT READ AS AN ARTICLE'S, which is the defect a
    // naive walk has: depth is counted rather than assumed.
    CHECK(p.entries[0].id == 7);
    CHECK(p.entries[0].title == "First");
    CHECK(p.entries[0].domain == "longreads.com");
    CHECK(p.entries[0].readingTime == 22);
    CHECK_FALSE(p.entries[0].archived);
    CHECK(p.entries[0].starred);
    CHECK(p.entries[0].updatedAt == "2026-09-03T10:00:00+0000");
    CHECK(p.entries[1].id == 8);
    // A NULL domain is not a refusal: losing the page over a field nothing needs
    // would be the worst trade in this parser.
    CHECK(p.entries[1].domain.empty());
    CHECK(p.entries[1].archived);
  }
}

TEST_CASE("a title over the cap arrives truncated and FLAGGED, and the page survives") {
  std::string longTitle(400, 'x');
  const std::string body = page(
      "{\"id\":1,\"title\":\"" + longTitle +
      "\",\"domain_name\":\"a.com\",\"reading_time\":1,\"is_archived\":false,"
      "\"is_starred\":false,\"updated_at\":\"2026-09-01T10:00:00Z\"}");
  grainsrc::Grained src(body, 3);
  ListingPage p;
  REQUIRE(parseListing(src, p));
  REQUIRE(p.entries.size() == 1);
  CHECK(p.entries[0].titleTruncated);
  CHECK(p.entries[0].title.size() <= kJsonStreamMaxStringBytes);
}

TEST_CASE("is_archived accepts both a bool and a 0/1, which wallabag has sent across versions") {
  const std::string body = page(
      R"({"id":1,"title":"t","domain_name":"a","reading_time":1,"is_archived":1,)"
      R"("is_starred":0,"updated_at":"2026-09-01T10:00:00Z"})");
  grainsrc::Grained src(body, 1);
  ListingPage p;
  REQUIRE(parseListing(src, p));
  REQUIRE(p.entries.size() == 1);
  CHECK(p.entries[0].archived);
  CHECK_FALSE(p.entries[0].starred);
}

TEST_CASE("a malformed page hands back NOTHING PARTIAL") {
  // Partial entries are worse than none: the engine would advance its watermark
  // past articles it never saw.
  for (const char* bad : {"{\"_embedded\":{\"items\":[{\"id\":1,",
                          "{\"_embedded\":{\"items\":[{\"id\":\"seven\"}]}}",
                          "not json"}) {
    CAPTURE(bad);
    grainsrc::Grained src(std::string_view(bad), 1);
    ListingPage p;
    CHECK_FALSE(parseListing(src, p));
  }
}

TEST_CASE("an empty page is valid and yields no entries") {
  const std::string body = page("", 2, 2, 24);
  grainsrc::Grained src(body, 1);
  ListingPage p;
  REQUIRE(parseListing(src, p));
  CHECK(p.entries.empty());
  CHECK(p.pages == 2);
}

TEST_CASE("a REAL wallabag listing parses, with every field a real one carries") {
  // EVERY FIXTURE IN THIS SUITE IS A HAND-WRITTEN ITEM OF SIX FIELDS, AND A REAL
  // ONE HAS THIRTY. That gap is why a 5,368-byte listing off a real server
  // failed to parse on glass with every desktop test green -- the same shape as
  // "a stream of one block kind is not a chapter", which this project has
  // already paid for once in the pager.
  //
  // This body is wallabag 2.6's `/api/entries?detail=metadata` as it actually
  // comes back: HAL `_links` at the root AND inside every item, `is_archived`
  // and `is_starred` as 0/1 integers, nulls in six nullable fields, a `tags`
  // array of objects, a `headers` object, timestamps with a `+0200` offset
  // rather than `Z`, and a `é` escape where PHP's json_encode puts one.
  const std::string body = R"({
  "page": 1,
  "limit": 30,
  "pages": 1,
  "total": 2,
  "_links": {
    "self": {"href": "https://w.example.com/api/entries?page=1&perPage=30"},
    "first": {"href": "https://w.example.com/api/entries?page=1&perPage=30"},
    "last": {"href": "https://w.example.com/api/entries?page=1&perPage=30"}
  },
  "_embedded": {
    "items": [
      {
        "is_archived": 0,
        "is_starred": 0,
        "user_name": "lucas",
        "user_email": "l@example.com",
        "user_id": 1,
        "tags": [{"id": 3, "label": "tech", "slug": "tech"}],
        "is_public": false,
        "id": 12,
        "uid": null,
        "title": "Un titre accentué",
        "url": "https://example.com/a",
        "hashed_url": "0a1b2c",
        "origin_url": null,
        "given_url": null,
        "hashed_given_url": "3d4e5f",
        "archived_at": null,
        "created_at": "2026-09-01T10:00:00+0200",
        "updated_at": "2026-09-02T11:00:00+0200",
        "published_at": null,
        "published_by": ["Somebody"],
        "starred_at": null,
        "annotations": [],
        "mimetype": "text/html",
        "language": "fr",
        "reading_time": 7,
        "domain_name": "example.com",
        "preview_picture": "https://example.com/p.jpg",
        "http_status": "200",
        "headers": {"content-type": "text/html; charset=UTF-8"},
        "_links": {"self": {"href": "https://w.example.com/api/entries/12"}}
      },
      {
        "is_archived": 1,
        "is_starred": 1,
        "user_name": "lucas",
        "user_id": 1,
        "tags": [],
        "is_public": false,
        "id": 13,
        "uid": null,
        "title": "Second",
        "url": "https://example.com/b",
        "hashed_url": "aaa",
        "origin_url": null,
        "archived_at": "2026-09-03T09:00:00+0200",
        "created_at": "2026-09-01T09:00:00+0200",
        "updated_at": "2026-09-03T09:00:00+0200",
        "published_at": null,
        "published_by": [],
        "starred_at": "2026-09-03T09:00:00+0200",
        "annotations": [],
        "mimetype": "text/html",
        "language": null,
        "reading_time": 2,
        "domain_name": "example.com",
        "preview_picture": null,
        "http_status": "200",
        "headers": {"content-type": "text/html"},
        "_links": {"self": {"href": "https://w.example.com/api/entries/13"}}
      }
    ]
  }
})";

  // AT THREE GRAINS, because the device feeds this 4 KB at a time and a source
  // that satisfies every read hides every resumption bug there is -- the lesson
  // the inflater's own validation is built on.
  for (const size_t grain : {size_t(1), size_t(4096), body.size() + 1}) {
    CAPTURE(grain);
    grainsrc::Grained src(body, grain);
    ListingPage p;
    REQUIRE(parseListing(src, p));
  CHECK(p.pages == 1);
  REQUIRE(p.entries.size() == 2);

  CHECK(p.entries[0].id == 12);
  // The escape is DECODED, not passed through: a reader seeing `accentué`
  // on the glass would read as a rendering fault and be a parsing one.
  CHECK(p.entries[0].title == "Un titre accentu\xC3\xA9");
  CHECK(p.entries[0].domain == "example.com");
  CHECK(p.entries[0].readingTime == 7);
  CHECK_FALSE(p.entries[0].archived);
  CHECK_FALSE(p.entries[0].starred);
  CHECK(p.entries[0].updatedAt == "2026-09-02T11:00:00+0200");

  CHECK(p.entries[1].id == 13);
  CHECK(p.entries[1].archived);
  CHECK(p.entries[1].starred);
  // `domain_name` is present here and `language` is null -- the nullable path
  // has to survive a null it does not read as well as one it does.
  CHECK(p.entries[1].domain == "example.com");
  }
}

TEST_CASE("chunked framing does not parse, which is what a raw socket read gives") {
  // THIS IS A RECORD OF A REAL DEFECT, NOT A HYPOTHETICAL. `HTTPClient` de-chunks
  // only inside `writeToStream()`, so a streaming reader that takes
  // `getStreamPtr()` receives the framing along with the body. On glass a
  // 5,368-byte listing arrived as `14eb\r\n{...}\r\n0\r\n\r\n` and the engine
  // reported "the listing did not parse" about a listing that was perfectly well
  // formed. The shell's transport asks for HTTP/1.0 now, which has no chunked
  // encoding at all.
  //
  // The case lives here because `shell/` has no harness: what can be pinned is
  // that the framing IS fatal to the parser, so anyone who reintroduces a raw
  // stream read meets a named failure instead of a mystery.
  const std::string framed = "14eb\r\n" + std::string(R"({"page":1,"pages":1,"total":1,)"
                                                     R"("_embedded":{"items":[]}})") +
                             "\r\n0\r\n\r\n";
  grainsrc::Grained src(framed, framed.size() + 1);
  ListingPage p;
  CHECK_FALSE(parseListing(src, p));
}

TEST_CASE("a header value of several kilobytes is skipped, not choked on") {
  // THE OTHER THING THE REAL BODY CARRIED. wallabag stores the origin's response
  // headers verbatim, and a Substack page's `content-security-policy-report-only`
  // is ~2.5 KB in ONE string -- inside `headers`, which `parseListing` skips
  // wholesale. A scanner that refused a string longer than its buffer, rather
  // than truncating and moving on, would lose every entry to a field nothing
  // reads. No hand-written fixture would ever have contained one.
  const std::string csp(2600, 'x');
  const std::string body =
      R"({"page":1,"pages":1,"total":1,"_embedded":{"items":[{)"
      R"("id":42,"title":"Fine","domain_name":"x.com","reading_time":3,)"
      R"("is_archived":0,"is_starred":0,"updated_at":"2026-09-15T06:57:26+0000",)"
      R"("headers":{"content-security-policy-report-only":")" +
      csp + R"("},"tags":[],"_links":{"self":{"href":"/api/entries/42"}}}]}})";

  for (const size_t grain : {size_t(1), size_t(4096), body.size() + 1}) {
    CAPTURE(grain);
    grainsrc::Grained src(body, grain);
    ListingPage p;
    REQUIRE(parseListing(src, p));
    REQUIRE(p.entries.size() == 1);
    CHECK(p.entries[0].id == 42);
    CHECK(p.entries[0].title == "Fine");
    CHECK(p.entries[0].readingTime == 3);
  }
}
