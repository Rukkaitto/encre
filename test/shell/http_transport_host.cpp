// THE HTTP TRANSPORT'S DESKTOP TWIN, at the reader::HttpTransport seam it already
// satisfies -- so no <HTTPClient.h> or <NetworkClientSecure.h> enters the build.
//
// THE FIDELITY LIMIT HERE IS THE SHARPEST IN THE DIRECTORY, and it is worth stating
// where somebody will read it. On the device, ONE TLS handshake takes the largest
// free BLOCK from 61,428 to 34,804 and it never returns above 36,852 -- while
// getFreeHeap RECOVERS every time, which is why nothing saw it for so long. The
// plateau is 104 bytes under the one allocation every book needs, so a device that
// has synced cannot open a book until it restarts.
//
// None of that is reproducible here. A scenario that wants the consequence scripts
// harness::heap() directly; this twin moves bytes and nothing else.
#include "http_transport_arduino.h"

#include <string>

#include "harness_state.h"

namespace harness {
// What the next request answers with. Scripted per scenario.
int& httpStatus() {
  static int s = 200;
  return s;
}
std::string& httpBody() {
  static std::string b;
  return b;
}
}  // namespace harness

// `Live` holds the real transport's WiFiClientSecure and HTTPClient. Nothing here
// needs one, but the declaration is in the header and a unique_ptr to an incomplete
// type needs it defined before the destructor runs.
struct ArduinoHttpTransport::Live {};

ArduinoHttpTransport::ArduinoHttpTransport(std::string server) : server_(std::move(server)) {}
ArduinoHttpTransport::~ArduinoHttpTransport() = default;

bool ArduinoHttpTransport::resolveHost() {
  resolved_ = true;
  return true;
}

void ArduinoHttpTransport::fail(reader::HttpFailure why) {
  failure_ = why;
  state_ = reader::HttpState::Failed;
  harness::record("<http> failed why=%d", static_cast<int>(why));
}

void ArduinoHttpTransport::teardown() { live_.reset(); }

bool ArduinoHttpTransport::begin(const reader::HttpRequest& request, reader::BodySink& sink) {
  ++requests_;
  sink_ = &sink;
  status_ = harness::httpStatus();
  bodyBytes_ = 0;
  failure_ = reader::HttpFailure::None;
  state_ = reader::HttpState::Running;
  harness::record("<http> begin status=%d", status_);
  (void)request;
  // THE HEAP FIGURES ARE THE SCRIPTED ONES, and they are on the transport because
  // this is where the device measures them -- the fragmentation a handshake causes
  // is invisible to getFreeHeap and only the BLOCK shows it.
  heapBefore_ = harness::heap().free_;
  blockBefore_ = harness::heap().block_;
  return true;
}

void ArduinoHttpTransport::poll() {
  if (state_ != reader::HttpState::Running) return;
  const std::string& body = harness::httpBody();
  if (sink_ != nullptr && !body.empty()) {
    if (!sink_->write(reinterpret_cast<const uint8_t*>(body.data()), body.size())) {
      // THE ONE FAILURE THE TRANSPORT DOES NOT INVENT: a full card and a closed
      // connection are different events and the log has to tell them apart.
      fail(reader::HttpFailure::SinkRefused);
      return;
    }
    bodyBytes_ = body.size();
  }
  if (status_ >= 200 && status_ < 300) {
    if (sink_ != nullptr && !sink_->finish()) {
      fail(reader::HttpFailure::SinkRefused);
      return;
    }
    state_ = reader::HttpState::Done;
  } else {
    state_ = reader::HttpState::Failed;
  }
  heapAfter_ = harness::heap().free_;
  blockAfter_ = harness::heap().block_;
  heapMin_ = harness::heap().min_;
  harness::record("<http> done status=%d bytes=%zu", status_, bodyBytes_);
}

void ArduinoHttpTransport::cancel() {
  harness::record("<http> cancel");
  teardown();
  state_ = reader::HttpState::Idle;
}
