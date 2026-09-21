#include "panel_selftest.h"
#if defined(ENCRE_PANEL_SELFTEST) && ENCRE_PANEL_SELFTEST
#include <Arduino.h>
#include <EInkDisplay.h>

#include "reader/panel_contract.h"

namespace {

// Serial, because that is what a device has. The shape is sd_selftest's: one line
// per assertion so a failure names itself, and the notes printed beside them
// because their VALUES are the reason to run this on glass at all.
class SerialReport : public reader::PanelContractReport {
 public:
  void check(bool passed, const char* expr) override {
    ++checks_;
    if (!passed) ++failures_;
    Serial.printf("[panel-contract]   %s  %s\n", passed ? "ok  " : "FAIL", expr);
  }
  void note(const char* what, long value) override {
    // THE HALF A HUMAN READS. supportsStripGrayscale is the one this was written
    // for: the SDK's docs say false for this panel and Uc8279Driver.h returns true,
    // and only the device settles it.
    Serial.printf("[panel-contract]   note %s = %ld\n", what, value);
  }
  int checks() const { return checks_; }
  int failures() const { return failures_; }

 private:
  int checks_ = 0;
  int failures_ = 0;
};

}  // namespace

int runPanelContractSelfTest(freeink::FreeInkDisplay& display) {
  SerialReport r;
  Serial.printf("[panel-contract] running against the real panel\n");

  size_t n = 0;
  const auto* clauses = reader::panelContractClauses<EInkDisplay>(n);
  for (size_t i = 0; i < n; ++i) {
    Serial.printf("[panel-contract] %s\n", clauses[i].name);
    clauses[i].run(display, r);
  }

  // THE ONE THAT COSTS A WAVEFORM, last and named, so a reader of the log knows
  // which flash was the test rather than the boot.
  Serial.printf("[panel-contract] a refresh leaves the frame where core/ looks at it"
                " (this one flashes the panel)\n");
  reader::panel_contract::refreshPreservesFrame(display, r);

  Serial.printf("[panel-contract] %d assertion(s), %d FAILED\n", r.checks(), r.failures());
  // A run that asserted nothing is a failure, not a pass -- the 0/0 shape, and the
  // reason the desktop runner pins its count exactly.
  if (r.checks() == 0) {
    Serial.printf("[panel-contract] NO ASSERTIONS RAN -- that is a failure, not a pass\n");
    return 1;
  }
  return r.failures();
}

#else

// -1 RATHER THAN 0, so a build without the flag cannot be read as a build that
// passed. sd_selftest's rule, and the one that matters in a number nobody looks at
// twice.
int runPanelContractSelfTest(freeink::FreeInkDisplay&) { return -1; }

#endif  // ENCRE_PANEL_SELFTEST
