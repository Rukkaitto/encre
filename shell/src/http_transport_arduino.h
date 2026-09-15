#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "reader/http_transport.h"

// `HttpTransport` OVER `HTTPClient`, chunked per poll.
//
// POLL-SHAPED OVER A BLOCKING LIBRARY IS THIS FILE'S WHOLE JOB, and the seam's
// own header says so: `core/` must not learn what a socket is, and the loop is
// what drives the panel and the buttons on this device.
//
// WHAT `begin()` BLOCKS FOR, STATED RATHER THAN HIDDEN: the connection and the
// TLS handshake, measured at 653-823 ms on an X3 (`docs/notes/wallabag-api.md`
// §8). `HTTPClient` has no non-blocking connect and neither does the socket
// layer under it, so that is not a thing this class can chunk. It is about one
// panel refresh, it happens once per request rather than per chunk, and the
// screen it happens under is a modal that says SYNCING. What `poll()` buys is
// everything after it: the BODY arrives 4 KB at a time with the loop running
// between chunks, which is what makes a cancel land within one chunk rather than
// at the end of an article.
//
// TLS IS THE ONLY SCHEME THAT WORKS AND IT COSTS A RESTART, which is #140's
// answer and not a preference. One handshake takes the largest free block from
// 61,428 bytes to 34,804 and never returns it above 36,852, against an
// `Inflater::begin` window of 36,956 -- so a device that has synced cannot open
// a book until it reboots, and the sync driver restarts it when it finishes.
// The scheme is read off the server URL; a plain `http://` server is served by
// `WiFiClient` and costs no block at all.
//
// THE CERTIFICATE IS CHECKED AGAINST THE BUNDLE arduino-esp32 SHIPS, with no
// per-host pinning. The card already carries the account's password in plain
// text -- a documented trade -- so the token grant puts it on the wire, and an
// unverified handshake would hand it to anything that answers the DNS.
//
// IT COSTS 69,018 BYTES OF FLASH AND NO RAM, measured by forcing a reference to
// the bundle and linking: 2,242,745 -> 2,311,763, which is 1.1% more of a 6.25 MB
// app partition. The probe that priced TLS used `setInsecure()` and therefore
// measured a handshake that parses no chain, so the HEAP cost of verifying is
// still unmeasured -- one more reason the sync ends in a restart rather than in
// an argument about headroom.
class ArduinoHttpTransport : public reader::HttpTransport {
 public:
  // `server` is the scheme, host and optional port with NO trailing slash --
  // `https://wallabag.example.com`. A request's path is joined to it, so nothing
  // in `core/` ever builds a URL.
  explicit ArduinoHttpTransport(std::string server);
  ~ArduinoHttpTransport() override;
  ArduinoHttpTransport(const ArduinoHttpTransport&) = delete;
  ArduinoHttpTransport& operator=(const ArduinoHttpTransport&) = delete;

  bool begin(const reader::HttpRequest& request, reader::BodySink& sink) override;
  void poll() override;
  reader::HttpState state() const override { return state_; }
  int status() const override { return status_; }
  reader::HttpFailure failure() const override { return failure_; }
  void cancel() override;

  // THE LAST REQUEST'S OWN ACCOUNT, for the shell to log. It is kept here rather
  // than printed here because `logf` is static to main.cpp and is the only route
  // that also tees to `/encre.log` -- and a sync that fails unplugged is exactly
  // the case serial cannot see. Empty when the request did not fail.
  const std::string& lastError() const { return lastError_; }
  int lastCode() const { return lastCode_; }
  // HOW MANY REQUESTS HAVE BEEN ACCEPTED, so the shell can log one line per
  // ROUND TRIP. Logging on a changed CODE instead is what hid the second
  // request of the first real sync: it also answered 200, so the line was
  // suppressed and the failure after it had no trail at all. A counter cannot
  // collapse two requests that agree.
  uint32_t requests() const { return requests_; }
  uint32_t heapBefore() const { return heapBefore_; }
  uint32_t heapAfter() const { return heapAfter_; }
  uint32_t heapMin() const { return heapMin_; }
  uint32_t blockBefore() const { return blockBefore_; }
  uint32_t blockAfter() const { return blockAfter_; }

  // What the body actually moved, for the log. A sync that reports success
  // having transferred nothing is the reports-on-less-than-it-claims shape.
  size_t bodyBytes() const { return bodyBytes_; }
  bool secure() const { return secure_; }

  // A READ THAT YIELDS NOTHING FOR THIS LONG IS `Timeout`. Measured against the
  // probe's own numbers: a whole TLS round trip to a real server is under a
  // second, so 15 s is not a tuning constant, it is the point past which the
  // other end has plainly gone.
  static constexpr uint32_t kIdleTimeoutMs = 15000;
  // ONE CHUNK. 4 KB is the probe's, and it is held ONCE for the transport's
  // life rather than taken per poll or per request: a 4 KB frame in a function
  // the loop calls is a stack this project has already panicked (the 6,608-byte
  // inflate frame), and a per-request allocation is 4 KB of churn through a heap
  // that TLS has just been measured fragmenting.
  static constexpr size_t kChunkBytes = 4096;

 private:
  void fail(reader::HttpFailure why);
  void teardown();

  struct Live;
  std::string server_;
  std::unique_ptr<Live> live_;
  reader::BodySink* sink_ = nullptr;
  reader::HttpState state_ = reader::HttpState::Idle;
  reader::HttpFailure failure_ = reader::HttpFailure::None;
  int status_ = 0;
  size_t bodyBytes_ = 0;
  int32_t declaredLen_ = -1;
  uint32_t lastProgressMs_ = 0;
  bool secure_ = false;
  std::string lastError_;
  int lastCode_ = 0;
  uint32_t requests_ = 0;
  uint32_t heapBefore_ = 0;
  uint32_t heapAfter_ = 0;
  uint32_t heapMin_ = 0;
  uint32_t blockBefore_ = 0;
  uint32_t blockAfter_ = 0;
};
