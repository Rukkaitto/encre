#include "http_transport_arduino.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include <new>

using reader::HttpFailure;
using reader::HttpState;

// THE ROOT BUNDLE ESP-IDF EMBEDS, reached by its linker symbol because the
// Arduino wrapper takes bytes rather than attaching the default: passing
// `(nullptr, 0)` to `setCACertBundle` DETACHES instead of using the built-in
// one, which reads like the opposite of what it does. `CONFIG_MBEDTLS_
// CERTIFICATE_BUNDLE_DEFAULT_FULL=y` in the C3 libs, so this is the full set.
extern const uint8_t kCrtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCrtBundleEnd[] asm("_binary_x509_crt_bundle_end");

// Everything Arduino, kept out of the header.
struct ArduinoHttpTransport::Live {
  NetworkClientSecure tls;
  NetworkClient plain;
  HTTPClient http;
  // THE CHUNK LIVES HERE, once per transport rather than once per poll or once
  // per request -- see kChunkBytes in the header for both reasons.
  uint8_t chunk[ArduinoHttpTransport::kChunkBytes];
};

ArduinoHttpTransport::ArduinoHttpTransport(std::string server) : server_(std::move(server)) {
  // A TRAILING SLASH WOULD DOUBLE THE ONE EVERY PATH ALREADY CARRIES, and
  // `//api/info` is a 404 on nginx rather than an error anybody can read. The
  // credentials file is hand-edited, so this is a reader's typo and not a
  // programming error.
  while (!server_.empty() && server_.back() == '/') server_.pop_back();
  secure_ = server_.rfind("https://", 0) == 0;
}

ArduinoHttpTransport::~ArduinoHttpTransport() { teardown(); }

void ArduinoHttpTransport::fail(HttpFailure why) {
  failure_ = why;
  state_ = HttpState::Failed;
  teardown();
}

void ArduinoHttpTransport::teardown() {
  if (live_) {
    live_->http.end();
    live_.reset();
  }
  sink_ = nullptr;
}

bool ArduinoHttpTransport::begin(const reader::HttpRequest& request, reader::BodySink& sink) {
  if (state_ == HttpState::Running) return false;
  if (server_.empty()) return false;

  status_ = 0;
  failure_ = HttpFailure::None;
  bodyBytes_ = 0;
  declaredLen_ = -1;
  lastError_.clear();
  lastCode_ = 0;
  teardown();

  // THE RADIO FIRST, because every other failure below costs a DNS lookup or a
  // handshake to discover, and `NoNetwork` is the one the account screen turns
  // into "no Wi-Fi network is saved" rather than into a server problem.
  if (WiFi.status() != WL_CONNECTED) {
    failure_ = HttpFailure::NoNetwork;
    state_ = HttpState::Failed;
    return false;
  }

  live_ = std::unique_ptr<Live>(new (std::nothrow) Live);
  if (!live_) {
    failure_ = HttpFailure::Refused;
    state_ = HttpState::Failed;
    return false;
  }

  if (secure_) {
    live_->tls.setCACertBundle(kCrtBundleStart,
                               static_cast<size_t>(kCrtBundleEnd - kCrtBundleStart));
    live_->tls.setHandshakeTimeout(kIdleTimeoutMs / 1000);
  }

  const uint32_t heapBefore = ESP.getFreeHeap();
  const uint32_t blockBefore = ESP.getMaxAllocHeap();
  const std::string url = server_ + request.path;
  NetworkClient& client = secure_ ? static_cast<NetworkClient&>(live_->tls) : live_->plain;
  if (!live_->http.begin(client, url.c_str())) {
    fail(HttpFailure::Dns);
    return false;
  }
  // REDIRECTS ARE NOT FOLLOWED, and that is deliberate on an API: every wallabag
  // endpoint here answers directly, so a 3xx means the server is not the one the
  // reader thinks it is -- a plain-HTTP URL against an HTTPS host, or a login
  // page in front of the API. Following it would put an HTML page in a `.part`.
  live_->http.setReuse(false);
  live_->http.setTimeout(kIdleTimeoutMs);
  for (const auto& h : request.headers) {
    live_->http.addHeader(h.name.c_str(), h.value.c_str());
  }

  // ONE const_cast FOR BOTH BODY METHODS, and it is HTTPClient's signature that
  // is wrong rather than this: `POST`/`PATCH` take `uint8_t*` and only ever read
  // through it. Spelled once here so neither call site looks like it might
  // write, and so `request` stays const as the seam declares it.
  uint8_t* const body = reinterpret_cast<uint8_t*>(const_cast<char*>(request.body.data()));
  const size_t bodyLen = request.body.size();

  int code;
  if (request.method == "GET") {
    code = live_->http.GET();
  } else if (request.method == "POST") {
    code = live_->http.POST(body, bodyLen);
  } else if (request.method == "PATCH") {
    code = live_->http.PATCH(body, bodyLen);
  } else {
    fail(HttpFailure::Refused);
    return false;
  }

  // WHAT THE HANDSHAKE COST AND WHY IT FAILED, RECORDED RATHER THAN PRINTED.
  // The first sync on glass failed with nothing in the log but `outcome 3`,
  // which is the reports-on-less-than-it-claims shape this project records for
  // the card probe answered from cache -- so the transport keeps its own
  // account and `pollSync` writes it through `logf`, which is static to
  // main.cpp and is the only route that also reaches `/encre.log`. A sync that
  // fails unplugged is exactly the case serial cannot see.
  //
  // A VERIFIED HANDSHAKE IS THE LARGEST TRANSIENT THIS FIRMWARE MAKES -- ~66 KB
  // measured on glass against the ~56 KB the probe priced with `setInsecure()`
  // -- so the heap either side of it is the first thing anybody debugging this
  // needs and the last thing they can reconstruct afterwards.
  ++requests_;
  heapBefore_ = heapBefore;
  heapAfter_ = ESP.getFreeHeap();
  heapMin_ = ESP.getMinFreeHeap();
  blockBefore_ = blockBefore;
  blockAfter_ = ESP.getMaxAllocHeap();
  lastCode_ = code;

  if (code < 0) {
    // THE CAUSE, WHILE THE CLIENT IS STILL ALIVE TO BE ASKED. `HTTPClient`'s
    // negative codes name the layer and `NetworkClientSecure::lastError` names
    // the mbedTLS failure underneath -- a certificate that did not verify and an
    // allocation that did not happen are both "connection refused" up here, and
    // they need opposite fixes.
    lastError_ = HTTPClient::errorToString(code).c_str();
    if (secure_) {
      char tls[128] = {0};
      live_->tls.lastError(tls, sizeof(tls));
      if (tls[0] != '\0') {
        lastError_ += " | tls: ";
        lastError_ += tls;
      }
    }
    switch (code) {
      case HTTPC_ERROR_CONNECTION_REFUSED:
        fail(secure_ ? HttpFailure::Tls : HttpFailure::Refused);
        break;
      case HTTPC_ERROR_CONNECTION_LOST:
      case HTTPC_ERROR_READ_TIMEOUT:
        fail(HttpFailure::Timeout);
        break;
      default:
        fail(HttpFailure::Refused);
        break;
    }
    return false;
  }

  status_ = code;
  declaredLen_ = live_->http.getSize();
  sink_ = &sink;
  state_ = HttpState::Running;
  lastProgressMs_ = millis();
  return true;
}

void ArduinoHttpTransport::poll() {
  if (state_ != HttpState::Running || !live_) return;

  NetworkClient* stream = live_->http.getStreamPtr();
  if (stream == nullptr) {
    fail(HttpFailure::Timeout);
    return;
  }

  const int avail = stream->available();
  if (avail > 0) {
    const size_t want = avail < static_cast<int>(kChunkBytes) ? static_cast<size_t>(avail)
                                                              : kChunkBytes;
    const int n = stream->readBytes(live_->chunk, want);
    if (n > 0) {
      // A NON-2xx BODY IS READ AND DROPPED. It has to be READ, or the connection
      // never closes and this times out on a request that plainly answered; it
      // must not reach the sink, because the sink for a download is a file on
      // the card and wallabag's error JSON is not an article. `finish()` is
      // withheld for the same reason -- see BodySink::finish's 2xx clause.
      const bool wanted = status_ >= 200 && status_ < 300;
      if (wanted && sink_ != nullptr &&
          !sink_->write(live_->chunk, static_cast<size_t>(n))) {
        fail(HttpFailure::SinkRefused);
        return;
      }
      bodyBytes_ += static_cast<size_t>(n);
      lastProgressMs_ = millis();
      // ONE CHUNK PER POLL AND THEN OUT, which is what leaves the loop a turn
      // between chunks. Draining while bytes are available would be a blocking
      // read wearing a poll's clothes.
      return;
    }
  }

  // COMPLETE WHEN THE LENGTH IS SATISFIED, or when the far end has gone with
  // nothing left buffered. Both are needed: a `Content-Length` response can
  // leave the socket open (keep-alive), and a chunked one -- `getSize()` is -1
  // there, which is every wallabag response measured -- can only end the second
  // way.
  const bool lengthDone = declaredLen_ >= 0 && bodyBytes_ >= static_cast<size_t>(declaredLen_);
  if (lengthDone || (!live_->http.connected() && stream->available() == 0)) {
    const bool wanted = status_ >= 200 && status_ < 300;
    if (wanted && sink_ != nullptr && !sink_->finish()) {
      fail(HttpFailure::SinkRefused);
      return;
    }
    state_ = HttpState::Done;
    teardown();
    return;
  }

  if (millis() - lastProgressMs_ > kIdleTimeoutMs) {
    fail(HttpFailure::Timeout);
  }
}

void ArduinoHttpTransport::cancel() {
  if (state_ != HttpState::Running) return;
  // THE SINK IS NOT FINISHED, which is the seam's own contract and what leaves a
  // `.part` for the caller to discard -- and `CardFileSink`'s destructor does
  // that unasked, so a cancelled sync leaves no half file behind.
  teardown();
  failure_ = HttpFailure::None;
  state_ = HttpState::Failed;
}
