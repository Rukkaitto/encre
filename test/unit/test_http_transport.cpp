#include "doctest.h"
#include "fake_http_transport.h"

using namespace reader;

TEST_CASE("BufferSink REFUSES past its cap rather than truncating") {
  // json.h's own "large is malformed" rule. A listing that overran would parse
  // as a valid PREFIX and silently drop entries, which is the
  // reports-on-less-than-it-claims shape -- a refusal is one failed page the
  // sync can name.
  BufferSink s(8);
  const uint8_t six[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  CHECK(s.write(six, 6));
  CHECK(s.body().size() == 6);
  CHECK_FALSE(s.write(six, 6));
  // And nothing partial was kept: a half-written body is not a shorter body.
  CHECK(s.body().size() == 6);
  CHECK(s.write(six, 2));
  CHECK(s.body() == "abcdefab");
}

TEST_CASE("a sink that refuses fails the request as SinkRefused, not as a network error") {
  // OUR side and the network's are different events and the log has to tell them
  // apart: a full card is not an unreachable server.
  FakeHttpTransport t;
  t.scriptOk(200, "0123456789");
  BufferSink small(4);
  HttpRequest r;
  r.path = "/api/info";
  REQUIRE(t.begin(r, small));
  t.step();
  CHECK(t.state() == HttpState::Failed);
  CHECK(t.failure() == HttpFailure::SinkRefused);
}

TEST_CASE("the fake stays Running until the test steps it") {
  // Which is what lets a poll loop be asserted MID-FLIGHT -- the state a cancel
  // has to be tested in, and the state the fetching counter is drawn from.
  FakeHttpTransport t;
  t.scriptOk(200, "{}");
  BufferSink sink(64);
  HttpRequest r;
  r.path = "/api/info";
  REQUIRE(t.begin(r, sink));
  CHECK(t.state() == HttpState::Running);
  t.poll();
  CHECK(t.state() == HttpState::Running);
  t.step();
  CHECK(t.state() == HttpState::Done);
  CHECK(t.status() == 200);
  CHECK(sink.body() == "{}");
}

TEST_CASE("the request log records what was asked for, verbatim") {
  // The client's whole job is building six exact requests, and this is the only
  // way to check a query string nothing else reads.
  FakeHttpTransport t;
  t.scriptOk(200);
  NullSink sink;
  HttpRequest r;
  r.method = "PATCH";
  r.path = "/api/entries/7?archive=1";
  r.headers = {{"Authorization", "Bearer abc"}};
  REQUIRE(t.begin(r, sink));
  REQUIRE(t.requests() == 1);
  CHECK(t.last().method == "PATCH");
  CHECK(t.last().path == "/api/entries/7?archive=1");
  CHECK(t.last().hasHeader("Authorization", "Bearer abc"));
  CHECK_FALSE(t.last().hasHeaderNamed("Cookie"));
}

TEST_CASE("an unscripted request is counted rather than crashing the test") {
  // So a test that under-scripts fails on the assertion it meant to make rather
  // than on a vector bound.
  FakeHttpTransport t;
  NullSink sink;
  HttpRequest r;
  r.path = "/api/info";
  REQUIRE(t.begin(r, sink));
  t.step();
  CHECK(t.unscripted() == 1);
  CHECK(t.state() == HttpState::Failed);
}

TEST_CASE("NullSink counts a body nobody asked for") {
  FakeHttpTransport t;
  t.scriptOk(200, "unexpected");
  NullSink sink;
  HttpRequest r;
  r.path = "/api/entries/1?starred=1";
  REQUIRE(t.begin(r, sink));
  t.step();
  CHECK(t.state() == HttpState::Done);
  CHECK(sink.bytes() == 10);
}

TEST_CASE("cancel stops the request in flight and does NOT finish the sink") {
  // Which is what leaves a partial body the caller's to discard -- for the card
  // sink, removing a `.part`, and is why a cancelled sync leaves no half file.
  struct CountingSink : BodySink {
    bool write(const uint8_t*, size_t) override { return true; }
    bool finish() override {
      ++finished;
      return true;
    }
    int finished = 0;
  } sink;

  FakeHttpTransport t;
  t.scriptOk(200, "body");
  HttpRequest r;
  r.path = "/api/entries/1/export.epub";
  REQUIRE(t.begin(r, sink));
  t.cancel();
  CHECK(t.state() == HttpState::Failed);
  CHECK(t.cancelled());
  CHECK(sink.finished == 0);
}

TEST_CASE("a non-2xx completes the request and does NOT finish the sink") {
  // THE CASE THAT MAKES THE CARD SINK SAFE, and it covered nothing until the
  // clause existed: a 401 is a request that COMPLETED -- the refresh ladder is
  // built on that -- so a rule saying "finish on completion" renames wallabag's
  // JSON error body over the article's real name. The row then opens onto an
  // error message and is indistinguishable from a corrupt EPUB.
  struct CountingSink : BodySink {
    bool write(const uint8_t*, size_t n) override {
      bytes += n;
      return true;
    }
    bool finish() override {
      ++finished;
      return true;
    }
    size_t bytes = 0;
    int finished = 0;
  };

  SUBCASE("401 -- the body arrives, the finish does not") {
    CountingSink sink;
    FakeHttpTransport t;
    // Named, so the length below is the literal's own and not a hand count --
    // the first version of this line said 24 and the body is 25.
    const std::string kError = "{\"error\":\"invalid_grant\"}";
    t.scriptOk(401, kError);
    HttpRequest r;
    r.path = "/api/entries/7/export.epub";
    REQUIRE(t.begin(r, sink));
    t.step();
    // DONE rather than Failed: the round trip worked and the ANSWER was no,
    // which is the distinction the whole refresh ladder reads.
    CHECK(t.state() == HttpState::Done);
    CHECK(t.status() == 401);
    CHECK(sink.bytes == kError.size());
    CHECK(sink.finished == 0);
  }

  SUBCASE("200 -- both") {
    CountingSink sink;
    FakeHttpTransport t;
    t.scriptOk(200, "PK\x03\x04");
    HttpRequest r;
    r.path = "/api/entries/7/export.epub";
    REQUIRE(t.begin(r, sink));
    t.step();
    CHECK(t.state() == HttpState::Done);
    CHECK(sink.finished == 1);
  }

  SUBCASE("204 is 2xx, and a PATCH is the reason to check") {
    // Every archive and star answers 204 with no body. Reading the clause as
    // "200" rather than "2xx" would leave those sinks unfinished for ever.
    CountingSink sink;
    FakeHttpTransport t;
    t.scriptOk(204, "");
    HttpRequest r;
    r.method = "PATCH";
    r.path = "/api/entries/7";
    REQUIRE(t.begin(r, sink));
    t.step();
    CHECK(t.state() == HttpState::Done);
    CHECK(sink.finished == 1);
  }
}
