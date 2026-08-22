#pragma once
#include <cstddef>
#include <cstdint>

class InputManager;

// Raw button transitions, sampled off the main loop.
//
// Why a task and not polling in loop(): an e-ink refresh blocks for 0.4-2 s, and
// the driver's busy-wait yields with delay(), so a press that lands during a
// refresh is simply never sampled. The SDK ships beginAsync() for exactly this,
// but it queues PRESS EDGES ONLY -- no releases, no hold duration -- so it
// cannot support a long press. This is beginAsync's own loop (update() on a
// timer) queuing both edges with a timestamp, which is what the recognizer
// needs.
//
// Only this task may call InputManager::update(); it owns the edge state.

struct RawSample {
  uint8_t button;  // InputManager::BTN_*
  bool down;
  uint32_t ms;
};

// Starts the polling task. Safe to call once; later calls are no-ops.
void startInputTask(InputManager& input);

// Drain one queued transition. False when the queue is empty.
bool popRawSample(RawSample& out);

// Transitions dropped because the queue was full.
uint32_t rawSamplesDropped();

// How many transitions are waiting. For deciding whether to start something the
// panel cannot interrupt: a press already queued means the user is still going.
size_t rawSamplesPending();
