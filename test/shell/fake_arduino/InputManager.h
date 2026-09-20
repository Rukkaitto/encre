#pragma once
#include <cstdint>
#include "harness_state.h"

// THE SEVEN BUTTON INDICES AND begin(), AND NOTHING ELSE.
//
// update() is never called on the desktop: input_task.h is already a five-function
// seam, so test/shell/input_task_host.cpp implements it over a scripted queue and
// FreeRTOS never enters this build. That is the single largest saving in the
// directory -- and its cost, stated: the SDK's debounce and the 32-deep queue's
// drops are not modelled, so rawSamplesDropped() is SCRIPTED rather than emergent.
// The loop's dropped-edge branch stays drivable; the desktop can never DISCOVER a
// drop.
//
// The names describe the SDK's band order, not this device's panel -- BTN_UP and
// BTN_DOWN are the two SIDE buttons, which is why the shell maps them onto
// reader::Button::Left and Right. Getting that mapping wrong is a recorded defect.
class InputManager {
 public:
  void begin() { harness::record("<input> begin"); }

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};
