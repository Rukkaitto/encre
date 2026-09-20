#pragma once
#include "harness_state.h"

// THE RESET REASONS THE BOOT PATH DISTINGUISHES. `[boot] reset reason=...` is what
// separates a real resume (DEEPSLEEP) from a host having reset the chip (USB) from
// a first-ever boot (POWERON) -- and on battery a resume is a POWERON, which is the
// whole reason the `slept` flag exists.
typedef enum {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON = 1,
  ESP_RST_EXT = 2,
  ESP_RST_SW = 3,
  ESP_RST_PANIC = 4,
  ESP_RST_INT_WDT = 5,
  ESP_RST_TASK_WDT = 6,
  ESP_RST_WDT = 7,
  ESP_RST_DEEPSLEEP = 8,
  ESP_RST_BROWNOUT = 9,
  ESP_RST_SDIO = 10,
  ESP_RST_USB = 11,
} esp_reset_reason_t;

inline esp_reset_reason_t esp_reset_reason() {
  return static_cast<esp_reset_reason_t>(harness::resetReason());
}

// THROWS RATHER THAN RETURNING, so the branches that call it are observable
// instead of fatal -- handleRetry's restart-after-a-pull and restartIfHeapSpent.
// The desktop build has exceptions; the firmware's -fno-exceptions comes from the
// Arduino framework, not from CMakeLists.txt. This lives entirely in the fake, and
// if it ever leaks into shell/src/ the firmware build fails, which is the check.
struct HarnessRestarted {};
[[noreturn]] inline void esp_restart() {
  harness::record("<power> esp_restart");
  throw HarnessRestarted{};
}
