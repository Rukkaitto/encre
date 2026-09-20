#pragma once
#include <cstdint>
#include "harness_state.h"

// THE I2C FINGERPRINT AND THE DISPLAY-BUS PROBE, matching the real header exactly
// -- namespace, enumerator names and the diag struct's fields. The first draft of
// this file guessed at all three and main.cpp would not compile against it, which
// is what PR2 of #179 is for: the surface census counted METHODS and could not see
// shapes.
//
// THE PROBE IS NOT OPTIONAL ON THIS HARDWARE. The panel controller varies by
// production BATCH, so a UC8279 driven with the UC8253 driver takes the power-on
// handshake and then dies at the refresh wait's 30-second timeout.
namespace freeink {

enum class XteinkVerdict : uint8_t { X4Confirmed, X3Confirmed, Inconclusive };
enum class X3DisplayVerdict : uint8_t { Uc8253Assumed, Uc8279Confirmed, Inconclusive };
enum class DisplayControllerVerdict : uint8_t { PrimaryAssumed, Uc81xxConfirmed, Inconclusive };

struct XteinkDisplayProbeDiag {
  bool valid = false;
  uint8_t ver[5] = {0};
  uint8_t flg = 0;
  uint8_t verdict = 0;
  bool promoted = false;
  bool mtpValid = false;
  uint8_t mtp[48] = {0};
};

}  // namespace freeink

namespace harness {
// The dev device is an X3 with a UC8279, so that is the default a scenario gets
// without asking. Both are scripted: on real hardware they are facts about the
// silicon in front of you.
inline freeink::XteinkVerdict& verdict() {
  static freeink::XteinkVerdict v = freeink::XteinkVerdict::X3Confirmed;
  return v;
}
inline bool& promoteToUc8279() {
  static bool p = true;
  return p;
}
inline freeink::XteinkDisplayProbeDiag& probeDiag() {
  static freeink::XteinkDisplayProbeDiag d;
  return d;
}
}  // namespace harness

namespace freeink {

inline XteinkVerdict detectXteinkVerdict(uint8_t* score1 = nullptr, uint8_t* score2 = nullptr) {
  if (score1 != nullptr) *score1 = 3;
  if (score2 != nullptr) *score2 = 0;
  harness::record("<board> detect verdict=%d", static_cast<int>(harness::verdict()));
  return harness::verdict();
}

inline bool detectXteinkIsX3() { return harness::verdict() == XteinkVerdict::X3Confirmed; }

inline X3DisplayVerdict detectX3DisplayController(uint8_t verBytes[5] = nullptr,
                                                  uint8_t* flg = nullptr) {
  (void)verBytes;
  (void)flg;
  return harness::promoteToUc8279() ? X3DisplayVerdict::Uc8279Confirmed
                                    : X3DisplayVerdict::Uc8253Assumed;
}

inline DisplayControllerVerdict detectXteinkDisplayController(uint8_t verBytes[5] = nullptr,
                                                              uint8_t* flg = nullptr) {
  (void)verBytes;
  (void)flg;
  return harness::promoteToUc8279() ? DisplayControllerVerdict::Uc81xxConfirmed
                                    : DisplayControllerVerdict::PrimaryAssumed;
}

const XteinkDisplayProbeDiag& getXteinkDisplayProbeDiag();
bool applyXteinkDisplayController();
bool selectXteinkDevice();

}  // namespace freeink
