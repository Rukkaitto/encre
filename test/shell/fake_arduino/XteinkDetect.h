#pragma once
#include <cstdint>
#include "BoardConfig.h"
#include "harness_state.h"

// THE I2C FINGERPRINT AND THE DISPLAY-BUS PROBE. Scripted, because the real answer
// is a fact about the silicon in front of you: the panel controller varies by
// production BATCH, and skipping the probe drives a UC8279 with the UC8253 driver
// -- the power-on handshake succeeds and the refresh wait then dies at its
// 30-second timeout.
enum class XteinkVerdict : uint8_t { Unknown, X3, X4 };

struct XteinkDisplayProbeDiag {
  bool ran = false;
  bool promoted = false;
  uint8_t score1 = 0;
  uint8_t score2 = 0;
};

namespace harness {
inline XteinkVerdict& verdict() {
  static XteinkVerdict v = XteinkVerdict::X3;
  return v;
}
// Whether the bus probe promotes the profile to UC8279 -- the dev device's case.
inline bool& promoteToUc8279() {
  static bool p = true;
  return p;
}
inline XteinkDisplayProbeDiag& probeDiag() {
  static XteinkDisplayProbeDiag d;
  return d;
}
}  // namespace harness

inline XteinkVerdict detectXteinkVerdict(uint8_t* score1 = nullptr, uint8_t* score2 = nullptr) {
  if (score1 != nullptr) *score1 = 3;
  if (score2 != nullptr) *score2 = 0;
  harness::record("<board> detect verdict=%s",
                  harness::verdict() == XteinkVerdict::X3   ? "X3"
                  : harness::verdict() == XteinkVerdict::X4 ? "X4"
                                                            : "UNKNOWN");
  return harness::verdict();
}

inline bool applyXteinkDisplayController() {
  harness::probeDiag().ran = true;
  harness::probeDiag().promoted = harness::promoteToUc8279();
  if (harness::promoteToUc8279())
    BoardConfig::ACTIVE.displayController = BoardConfig::DisplayController::UC8279;
  harness::record("<board> displayProbe promoted=%d", harness::promoteToUc8279() ? 1 : 0);
  return harness::promoteToUc8279();
}

inline const XteinkDisplayProbeDiag& getXteinkDisplayProbeDiag() { return harness::probeDiag(); }
