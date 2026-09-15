#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

// WHERE THE BYTES GO. A sink rather than a returned string, because a response
// can be an EPUB: `readAll` is capped at 64 KB for `-fno-exceptions`' reason (a
// resize that cannot allocate is an abort() with no diagnostic), and an article
// is bigger than that. The card sink in shell/ streams straight to a file and
// the whole body never exists in RAM.
class BodySink {
 public:
  virtual ~BodySink() = default;
  // FALSE FAILS THE REQUEST with `SinkRefused`, which is the one failure the
  // transport does not invent: a full card and a closed connection are different
  // events and the log has to tell them apart.
  virtual bool write(const uint8_t* data, size_t n) = 0;
  // Called once, after the last write, on a request that completed AND whose
  // status is 2xx. False fails it too -- a rename that did not happen is a file
  // that is not there, and reporting success would leave a `.part` the next sync
  // re-fetches while the list shows a row that will not open.
  //
  // THE 2xx CLAUSE IS LOAD-BEARING AND WAS NOT HERE AT FIRST. This said "only on
  // a request that completed", and a 401 IS a request that completed -- the
  // refresh ladder is built on exactly that. So a download whose token had
  // expired would have had wallabag's JSON error body written into the article's
  // `.part` and then RENAMED over the article's real name: a row in the list
  // that opens onto an error message, indistinguishable from a corrupt EPUB. The
  // sink is deliberately dumb about HTTP and must not learn to read a status, so
  // the transport is what withholds the call.
  //
  // A NON-2xx THEREFORE LEAVES THE SINK UNFINISHED, which is the same state a
  // cancel leaves, and the caller discards it the same way.
  virtual bool finish() = 0;
};

// A bounded in-memory sink, for a response that IS small: the token grant, the
// `/api/info` probe, a listing page.
//
// THE CAP IS A REFUSAL, NOT A TRUNCATION, which is json.h's own "large is
// malformed" rule: a listing that overran would parse as a valid prefix and
// silently drop entries, where a refusal is one failed page the sync can report.
class BufferSink : public BodySink {
 public:
  explicit BufferSink(size_t cap) : cap_(cap) {}
  bool write(const uint8_t* data, size_t n) override;
  bool finish() override { return true; }
  const std::string& body() const { return body_; }
  void reset() { body_.clear(); }

 private:
  std::string body_;
  size_t cap_;
};

// 32 KB, which is `perPage=20` x ~1 KB of metadata with `detail=metadata`, plus
// room. The listing is the largest thing this sink ever holds; the token grant
// is ~200 bytes and `/api/info` less.
inline constexpr size_t kListingSinkCap = 32u * 1024u;

// For a response nobody reads -- every PATCH. Counting rather than discarding,
// so a body that arrives where none was expected is visible in a log.
class NullSink : public BodySink {
 public:
  bool write(const uint8_t*, size_t n) override {
    bytes_ += n;
    return true;
  }
  bool finish() override { return true; }
  size_t bytes() const { return bytes_; }

 private:
  size_t bytes_ = 0;
};

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpRequest {
  // GET or PATCH. No POST field: the token grant is a POST and says so here.
  std::string method = "GET";
  // RELATIVE TO THE SERVER, query included -- `/api/entries?detail=metadata`.
  // The transport joins it to the host, so nothing in core/ builds a URL and
  // nothing here has to know whether the scheme is http or https.
  std::string path;
  std::vector<HttpHeader> headers;
  // The token form, ~200 bytes. Nothing else has a body.
  std::string body;
};

enum class HttpState { Idle, Running, Done, Failed };

enum class HttpFailure {
  None,
  NoNetwork,   // the radio is not up
  Dns,         // the host did not resolve
  Refused,     // the connection was refused
  Timeout,     // opened and then went quiet
  Tls,         // a handshake or a certificate
  SinkRefused  // OUR side: a full card, or a write that did not land
};

// THE INJECTED HTTP SEAM, poll-shaped, and it is WifiRadio's argument verbatim:
// the loop is what drives everything on this device, a blocking call would stop
// the panel and the buttons for the length of a round trip, and `core/` must not
// learn what a socket is.
//
// POLL-SHAPED OVER A BLOCKING LIBRARY IS THE SHELL'S PROBLEM, not this
// interface's. `begin` sends the request and `poll` moves at most one chunk, so
// loop() keeps ticking between chunks and a cancel lands within one of them.
//
// EVERY STATE THE FAKE HAS, THE REAL ONE REPORTS. That is what makes a desktop
// test evidence about the device rather than about the fake -- the lesson this
// project records for `FileSystem`, whose contract is driven by two harnesses
// for the same reason.
class HttpTransport {
 public:
  virtual ~HttpTransport() = default;

  // Whether the request was ACCEPTED, not whether it succeeded. `sink` must
  // outlive the request.
  virtual bool begin(const HttpRequest& request, BodySink& sink) = 0;
  virtual void poll() = 0;
  virtual HttpState state() const = 0;
  // The HTTP status, once Done. Meaningless otherwise.
  virtual int status() const = 0;
  virtual HttpFailure failure() const = 0;
  // Stops after the chunk in flight. The sink is NOT finished, so a partial body
  // is the caller's to discard -- which for the card sink means removing a
  // `.part`, and is why a cancelled sync leaves no half file behind.
  virtual void cancel() = 0;
};

}  // namespace reader
