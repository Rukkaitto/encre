#include "reader/power.h"

namespace reader {

void IdleTimer::noteActivity(uint32_t ms) {
  lastActivity_ = ms;
  armed_ = true;
}

PowerAction IdleTimer::tick(uint32_t ms) {
  if (timeoutMs_ == 0 || !armed_) return PowerAction::None;
  // Unsigned subtraction, so a wrapped clock still measures the true interval.
  if (static_cast<uint32_t>(ms - lastActivity_) < timeoutMs_) return PowerAction::None;
  armed_ = false;
  return PowerAction::Sleep;
}

}  // namespace reader
