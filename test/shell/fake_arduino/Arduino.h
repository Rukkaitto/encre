#pragma once
// THE ARDUINO SURFACE shell/ REACHES, AND NO MORE. Enumerated from the source
// rather than from the real header: millis, micros, delay, pinMode, digitalRead,
// two pull constants, Serial, ESP, HWCDC::isPlugged, and two task macros.
//
// SIGNATURES ARE CHECKED AND SEMANTICS ARE NOT. These headers sit on ONE target's
// include path; `make firmware` compiles the same main.cpp against the real ones on
// every PR, so a fake whose SIGNATURE drifts is a build failure. Nothing checks that
// the behaviour still matches, which is the honest limit of the whole directory.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "harness_state.h"

// The virtual clock. Advanced only by the panel model and explicit script steps.
inline unsigned long millis() { return static_cast<unsigned long>(harness::clock_().ms); }
inline unsigned long micros() { return static_cast<unsigned long>(harness::clock_().ms * 1000); }

// A delay MOVES THE CLOCK and does not sleep. A harness that really slept would
// turn setup()'s CDC wait into two and a half seconds of test runtime for nothing.
inline void delay(unsigned long ms) {
  harness::clock_().advance(ms);
  harness::record("<time> delay %lums", ms);
}

constexpr int INPUT = 0x0;
constexpr int OUTPUT = 0x1;
constexpr int INPUT_PULLUP = 0x5;
constexpr int INPUT_PULLDOWN = 0x9;
constexpr int LOW = 0x0;
constexpr int HIGH = 0x1;

inline void pinMode(uint8_t pin, uint8_t mode) {
  harness::record("<gpio> pinMode pin=%u mode=%u", pin, mode);
}

// THE WAKE GATE'S ONLY INPUT. requireHeldPowerButtonOrSleepAgain reads this in a
// dwell loop, so a scenario that holds the button and one that taps it differ by
// this value and the clock alone.
inline int digitalRead(uint8_t pin) {
  const bool down = harness::powerButtonDown();
  // Active-LOW on both Xteink profiles: pressed reads 0.
  (void)pin;
  return down ? LOW : HIGH;
}
inline void digitalWrite(uint8_t pin, uint8_t value) {
  harness::record("<gpio> digitalWrite pin=%u value=%u", pin, value);
}

// SERIAL IS THE TRANSCRIPT SINK, which is what makes main.cpp's own logf the
// record rather than something written alongside it. Every [stage], [boot],
// [paint], [i] and [card] line the firmware already emits becomes an assertion
// for free.
class HardwareSerial {
 public:
  void begin(unsigned long baud) { harness::record("<serial> begin %lu", baud); }
  void setTxBufferSize(size_t n) { harness::record("<serial> txbuf %zu", n); }
  void flush() {}
  // A host having the port open. setup() leaves its CDC wait the moment this is
  // true; isPlugged() below is the other half of that decision.
  explicit operator bool() const { return harness::usbHostPresent(); }
  static bool isPlugged() { return harness::usbPlugged(); }

  size_t write(const uint8_t* data, size_t n) {
    append(reinterpret_cast<const char*>(data), n);
    return n;
  }
  size_t write(uint8_t c) {
    const char ch = static_cast<char>(c);
    append(&ch, 1);
    return 1;
  }
  int printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) append(buf, static_cast<size_t>(n));
    return n;
  }

 private:
  // LINE-BUFFERED, because the firmware writes a line in several calls and a
  // transcript of fragments would pin the CALL BOUNDARIES rather than the output.
  void append(const char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      if (p[i] == '\n') {
        harness::transcript().push_back(pending_);
        pending_.clear();
      } else if (p[i] != '\r') {
        pending_ += p[i];
      }
    }
  }
  std::string pending_;
};
inline HardwareSerial Serial;

// `HWCDC` is what ARDUINO_USB_CDC_ON_BOOT=1 makes `Serial`, and main.cpp names the
// type to ask isPlugged().
using HWCDC = HardwareSerial;

// A SCRIPTED HEAP. getFreeHeap cannot see the largest allocation this firmware
// makes, which is why the [alive] line carries min and block as well -- all three
// are modelled so the branches that read them are reachable.
struct EspClass {
  uint32_t getFreeHeap() const { return harness::heap().free_; }
  uint32_t getMinFreeHeap() const { return harness::heap().min_; }
  uint32_t getMaxAllocHeap() const { return harness::heap().block_; }
};
inline EspClass ESP;

// A FILE-SCOPE MACRO WITH A TRAILING SEMICOLON at main.cpp:631, so it has to
// expand to something a declaration terminator is legal after.
#define SET_LOOP_TASK_STACK_SIZE(x) static_assert(true, "loop task stack: " #x)

// RTC_DATA_ATTR survives a deep sleep on the device and is re-initialised by any
// other reset -- including the ESP_RST_USB a host causes by attaching, which is
// why the wake-refusal COUNT rides it and the diagnostic record does not. On the
// desktop it is ordinary storage; a scenario that wants the reset semantics drives
// them itself.
#define RTC_DATA_ATTR

using StackType_t = uint32_t;
// The smallest free space the loop task has ever had. Scripted: the real figure is
// a fact about FreeRTOS on the part and nothing here is evidence about it.
inline uint32_t uxTaskGetStackHighWaterMark(void*) { return 4096; }
inline uint32_t getArduinoLoopTaskStackSize() { return 16 * 1024; }
