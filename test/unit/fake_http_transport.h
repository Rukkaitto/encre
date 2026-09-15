#pragma once
#include <string>
#include <vector>

#include "reader/http_transport.h"

namespace reader {

// A scripted transport, on fake_wifi_radio.h's model.
//
// IT DOES NOT COMPLETE ON ITS OWN. `begin` leaves the request Running and
// `step()` moves it to Done or Failed under the test's control, which is what
// lets a poll loop be asserted MID-FLIGHT -- the state a cancel has to be tested
// in, and the state the dialog's fetching counter is drawn from.
class FakeHttpTransport : public HttpTransport {
 public:
  struct Scripted {
    int status = 200;
    std::string body;
    HttpFailure failure = HttpFailure::None;
  };

  // What was actually asked for, in order. THE REQUEST LOG IS THE TEST: the
  // client's whole job is building six exact requests, and asserting the paths
  // verbatim is the only way to check a query string nothing else reads.
  struct Recorded {
    std::string method;
    std::string path;
    std::string body;
    std::vector<HttpHeader> headers;

    bool hasHeader(std::string_view name, std::string_view value) const {
      for (const HttpHeader& h : headers)
        if (h.name == name && h.value == value) return true;
      return false;
    }
    bool hasHeaderNamed(std::string_view name) const {
      for (const HttpHeader& h : headers)
        if (h.name == name) return true;
      return false;
    }
  };

  void script(Scripted s) { queue_.push_back(std::move(s)); }
  void scriptOk(int status, std::string body = {}) { script({status, std::move(body), HttpFailure::None}); }
  void scriptFailure(HttpFailure f) { script({0, {}, f}); }

  const std::vector<Recorded>& log() const { return log_; }
  const Recorded& last() const { return log_.back(); }
  int requests() const { return static_cast<int>(log_.size()); }
  // Requests the caller made that the test never scripted -- a count rather than
  // a crash, so a test that under-scripts fails on the assertion it meant to
  // make rather than on a vector bound.
  int unscripted() const { return unscripted_; }

  bool begin(const HttpRequest& request, BodySink& sink) override {
    if (state_ == HttpState::Running) return false;
    log_.push_back({request.method, request.path, request.body, request.headers});
    sink_ = &sink;
    state_ = HttpState::Running;
    if (queue_.empty()) {
      ++unscripted_;
      pending_ = Scripted{0, {}, HttpFailure::Refused};
    } else {
      pending_ = queue_.front();
      queue_.erase(queue_.begin());
    }
    return true;
  }

  // Finish the request in flight. Nothing else moves it, which is the point.
  void step() {
    if (state_ != HttpState::Running) return;
    if (pending_.failure != HttpFailure::None) {
      failure_ = pending_.failure;
      state_ = HttpState::Failed;
      return;
    }
    if (!pending_.body.empty() && sink_ != nullptr) {
      if (!sink_->write(reinterpret_cast<const uint8_t*>(pending_.body.data()),
                        pending_.body.size())) {
        failure_ = HttpFailure::SinkRefused;
        state_ = HttpState::Failed;
        return;
      }
    }
    if (sink_ != nullptr && !sink_->finish()) {
      failure_ = HttpFailure::SinkRefused;
      state_ = HttpState::Failed;
      return;
    }
    status_ = pending_.status;
    failure_ = HttpFailure::None;
    state_ = HttpState::Done;
  }

  // The engine polls; this fake completes only when the test says so, so `poll`
  // is deliberately a no-op. `autoStep` is for the tests that are about
  // something else and do not want to drive every round trip by hand.
  void setAutoStep(bool on) { autoStep_ = on; }
  void poll() override {
    if (autoStep_) step();
  }

  HttpState state() const override { return state_; }
  int status() const override { return status_; }
  HttpFailure failure() const override { return failure_; }
  void cancel() override {
    cancelled_ = true;
    state_ = HttpState::Failed;
    failure_ = HttpFailure::Timeout;
  }
  bool cancelled() const { return cancelled_; }

  // Back to Idle so the next begin() is accepted -- which is what a client does
  // between two requests.
  void release() { state_ = HttpState::Idle; }

 private:
  std::vector<Scripted> queue_;
  std::vector<Recorded> log_;
  Scripted pending_;
  BodySink* sink_ = nullptr;
  HttpState state_ = HttpState::Idle;
  int status_ = 0;
  HttpFailure failure_ = HttpFailure::None;
  bool cancelled_ = false;
  bool autoStep_ = false;
  int unscripted_ = 0;
};

}  // namespace reader
