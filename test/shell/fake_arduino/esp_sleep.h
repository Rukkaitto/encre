#pragma once
#include <cstdint>
#include "harness_state.h"

typedef enum {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_ALL = 1,
  ESP_SLEEP_WAKEUP_EXT0 = 2,
  ESP_SLEEP_WAKEUP_EXT1 = 3,
  ESP_SLEEP_WAKEUP_TIMER = 4,
  ESP_SLEEP_WAKEUP_GPIO = 7,
} esp_sleep_wakeup_cause_t;

inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() {
  return static_cast<esp_sleep_wakeup_cause_t>(harness::wakeCause());
}

typedef enum { ESP_GPIO_WAKEUP_GPIO_LOW = 0, ESP_GPIO_WAKEUP_GPIO_HIGH = 1 } esp_deepsleep_gpio_wake_up_mode_t;

// LEVEL-TRIGGERED, WHICH IS WHY THE HOLD IS ENFORCED IN SOFTWARE. The SoC resumes
// the instant the line reaches its active level; there is no dwell anywhere on this
// path and no way to ask for one, so `HOLD POWER TO WAKE` is made true AFTER the
// wake by requireHeldPowerButtonOrSleepAgain.
inline int esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, esp_deepsleep_gpio_wake_up_mode_t mode) {
  harness::record("<power> armGpioWakeup mask=0x%llx mode=%d", (unsigned long long)mask, (int)mode);
  return 0;
}
