#pragma once
#include <cstdint>

namespace reader {

enum class PowerAction : uint8_t { None, Sleep };

// Idle-to-sleep. Fed a millisecond clock and told when the user did something.
//
// Sleep fires ONCE per idle period, not on every tick after the timeout: on the
// device the action is a deep sleep that never returns, but the simulator and
// the tests call tick() in a loop, and a timer that keeps saying Sleep hides
// whether the shell is honouring the first one.
class IdleTimer {
 public:
  // `timeoutMs` of 0 disables sleeping entirely (a setting the user can pick,
  // and the right behaviour while a transfer is running).
  explicit IdleTimer(uint32_t timeoutMs) : timeoutMs_(timeoutMs) {}

  void setTimeout(uint32_t timeoutMs) { timeoutMs_ = timeoutMs; }
  uint32_t timeout() const { return timeoutMs_; }

  // The user pressed something. Also re-arms a timer that already fired.
  void noteActivity(uint32_t ms);

  PowerAction tick(uint32_t ms);

 private:
  uint32_t timeoutMs_;
  uint32_t lastActivity_ = 0;
  bool armed_ = true;
};

}  // namespace reader
