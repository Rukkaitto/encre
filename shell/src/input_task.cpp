#include "input_task.h"

#include <Arduino.h>
#include <InputManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr uint32_t kPollMs = 10;
// Short re-poll while a raw change is still inside the SDK's debounce window.
// Its own comment asks for this: a change commits only after two matching
// samples, so a host polling slowly can drop a press that lands in one sample.
constexpr uint32_t kDebouncePollMs = 2;
constexpr int kQueueLen = 32;

QueueHandle_t gQueue = nullptr;
TaskHandle_t gTask = nullptr;
volatile uint32_t gDropped = 0;

void pollTask(void* arg) {
  auto* input = static_cast<InputManager*>(arg);
  static const uint8_t kButtons[] = {
      InputManager::BTN_BACK, InputManager::BTN_CONFIRM, InputManager::BTN_LEFT,
      InputManager::BTN_RIGHT, InputManager::BTN_UP,     InputManager::BTN_DOWN,
      InputManager::BTN_POWER};
  for (;;) {
    input->update();
    const uint32_t now = millis();
    for (const uint8_t b : kButtons) {
      // `gDropped = gDropped + 1` rather than `++gDropped`: C++20 deprecates a
      // compound increment on a volatile object and the toolchain warns on it.
      // A plain load-add-store is the same thing here -- one writer (this task),
      // one reader (the main loop), and an aligned uint32 needs no more.
      if (input->wasPressed(b)) {
        const RawSample s{b, true, now};
        if (xQueueSend(gQueue, &s, 0) != pdTRUE) gDropped = gDropped + 1;
      }
      if (input->wasReleased(b)) {
        const RawSample s{b, false, now};
        if (xQueueSend(gQueue, &s, 0) != pdTRUE) gDropped = gDropped + 1;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(input->isDebouncePending() ? kDebouncePollMs : kPollMs));
  }
}

}  // namespace

void startInputTask(InputManager& input) {
  if (gTask) return;
  gQueue = xQueueCreate(kQueueLen, sizeof(RawSample));
  if (!gQueue) return;
  xTaskCreate(pollTask, "encre_input", 4096, &input, 2, &gTask);
}

bool popRawSample(RawSample& out) {
  if (!gQueue) return false;
  return xQueueReceive(gQueue, &out, 0) == pdTRUE;
}

uint32_t rawSamplesDropped() { return gDropped; }
