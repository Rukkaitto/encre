// THE PANEL CONTRACT, RUN AGAINST THE FAKE.
//
// The other runner is shell/src/panel_selftest.cpp, which drives the SAME clauses
// against the real EInkDisplay on a real panel. That pairing is the whole point:
// `make firmware` checks the fake's signatures on every PR and nothing checks its
// semantics, so this is the part of the semantics that can be checked at all.
//
// IT LIVES IN test/shell/ RATHER THAN test/unit/, because it needs the fake include
// path ahead of everything -- a header named Arduino.h on the unit suite's path
// would be a surprise nobody asked for.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <EInkDisplay.h>

#include <string>
#include <vector>

#include "harness_state.h"
#include "reader/panel_contract.h"

namespace {

// Collects rather than aborts, so one clause's failure does not hide the next's --
// and keeps the notes, which are the half a human reads rather than asserts.
class Collecting : public reader::PanelContractReport {
 public:
  void check(bool passed, const char* expr) override {
    ++checks_;
    if (!passed) failures_.emplace_back(expr);
  }
  void note(const char* what, long value) override {
    notes_.emplace_back(std::string(what) + "=" + std::to_string(value));
  }
  int checks() const { return checks_; }
  const std::vector<std::string>& failures() const { return failures_; }
  const std::vector<std::string>& notes() const { return notes_; }

 private:
  int checks_ = 0;
  std::vector<std::string> failures_;
  std::vector<std::string> notes_;
};

}  // namespace

TEST_CASE("the fake panel satisfies every contract clause") {
  harness::resetAll();
  EInkDisplay display(8, 10, 21, 4, 5, 6);
  // AFTER begin(), because the framebuffer clause asserts a real allocation and the
  // real driver has none before it -- a clause run too early would be asserting the
  // opposite of the truth.
  display.begin();

  Collecting report;
  size_t n = 0;
  const auto* clauses = reader::panelContractClauses<EInkDisplay>(n);
  REQUIRE(n == 3);
  for (size_t i = 0; i < n; ++i) {
    INFO("clause: ", clauses[i].name);
    clauses[i].run(display, report);
  }

  for (const std::string& f : report.failures()) FAIL_CHECK("clause failed: " << f);
  CHECK(report.failures().empty());
  // EXACT, NOT A FLOOR. A contract that ran no checks would pass on having done
  // nothing -- the 0/0 shape this project has been bitten by more than once -- and a
  // floor only catches the whole clause disappearing. An exact count also catches
  // one quietly losing an assertion, which is the likelier edit. Five from geometry,
  // two from the framebuffer, three from the capabilities.
  //
  // It has to be updated when a clause gains a check, and that is the point: the
  // number is the thing being pinned.
  CHECK(report.checks() == 10);
  // ...and the notes are the half a human reads off a device rather than asserts.
  CHECK(report.notes().size() == 6);
}

TEST_CASE("a refresh leaves the frame where core/ is looking at it") {
  harness::resetAll();
  EInkDisplay display(8, 10, 21, 4, 5, 6);
  display.begin();
  Collecting report;
  // SEPARATE FROM THE CLAUSE TABLE because it spends a waveform: ~700 ms on glass,
  // which is why the device-side runner may decline it and the desktop always takes
  // it.
  reader::panel_contract::refreshPreservesFrame(display, report);
  for (const std::string& f : report.failures()) FAIL_CHECK("clause failed: " << f);
  CHECK(report.failures().empty());
}

TEST_CASE("the contract's geometry clause refuses a buffer sized for the wrong panel") {
  // PROVING THE CLAUSE BITES, without a mutation: a panel whose buffer is sized for
  // the OTHER Xteink is exactly the confusion this clause exists to catch, and the
  // product is SMALLER rather than larger -- 800x480/8 is 48,000 against 792x528/8's
  // 52,272 -- so a naive "is it big enough" test would pass it in one direction.
  struct WrongSize {
    enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
    uint16_t getDisplayWidth() const { return 792; }
    uint16_t getDisplayHeight() const { return 528; }
    uint32_t getBufferSize() const { return 800u * 480u / 8u; }  // the X4's
    uint8_t* getFrameBuffer() const { return nullptr; }
    bool supportsStripGrayscale() const { return true; }
    bool supportsBusyGrayscaleStaging() const { return false; }
    bool combinesGrayscaleBase() const { return false; }
  } wrong;
  Collecting report;
  reader::panel_contract::geometry(wrong, report);
  CHECK_FALSE(report.failures().empty());
}
