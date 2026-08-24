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

// Block until a transition is waiting or `timeoutMs` elapses, whichever is first.
// The sample is LEFT IN THE QUEUE -- this is a peek, so the next popRawSample()
// still sees it.
//
// This is what the main loop idles on instead of delay(). The two are identical
// when nothing happens, and the difference is the whole point when something
// does: a delay() sleeps out its full period whatever arrives, so an edge queued
// one millisecond in cost the user the other nine before the loop even looked.
// That is dead time in front of a ~520 ms waveform, and it is free to remove.
bool waitForRawSample(uint32_t timeoutMs);
