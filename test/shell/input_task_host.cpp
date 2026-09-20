// THE INPUT TASK'S DESKTOP TWIN.
//
// input_task.h is already a five-function seam, so this replaces it at that seam
// rather than faking FreeRTOS underneath it -- the rule at the top of
// fake_arduino/harness_state.h, and the single largest saving in the directory.
// No task, no queue, no freertos/ header enters this build.
//
// THE COST, STATED. The SDK's debounce and the real queue's 32-deep drops are NOT
// modelled, so rawSamplesDropped() is SCRIPTED rather than emergent: a scenario can
// drive the loop's dropped-edge branch, and the desktop can never DISCOVER a drop.
// That is a fact about what this twin can be evidence for.
#include "input_task.h"

#include <deque>

#include "harness_state.h"

namespace harness {

std::deque<RawSample>& rawSamples() {
  static std::deque<RawSample> q;
  return q;
}
uint32_t& droppedSamples() {
  static uint32_t n = 0;
  return n;
}

// A scenario queues a press as two transitions, because that is what the real task
// queues and what PressRecognizer needs: the DOWN edge is what a Short fires on,
// and the release edge is what classifies a press made entirely inside a repaint.
void queueButton(uint8_t button, bool down, uint32_t atMs) {
  rawSamples().push_back(RawSample{button, down, atMs});
}

}  // namespace harness

void startInputTask(InputManager& input) {
  (void)input;
  harness::record("<input> startInputTask");
}

bool popRawSample(RawSample& out) {
  if (harness::rawSamples().empty()) return false;
  out = harness::rawSamples().front();
  harness::rawSamples().pop_front();
  return true;
}

uint32_t rawSamplesDropped() { return harness::droppedSamples(); }

size_t rawSamplesPending() { return harness::rawSamples().size(); }

bool waitForRawSample(uint32_t timeoutMs) {
  // A PEEK, NOT A POP -- the sample is left in the queue, which is the contract the
  // header states. With nothing waiting this advances the virtual clock by the full
  // timeout, so the loop's idle behaviour is visible in the transcript rather than
  // being instantaneous and therefore untestable.
  if (!harness::rawSamples().empty()) return true;
  harness::clock_().advance(timeoutMs);
  return false;
}
