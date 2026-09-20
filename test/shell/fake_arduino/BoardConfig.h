#pragma once
#include <cstdint>
#include "harness_state.h"

// FIVE FIELDS OF A 1,715-LINE HEADER, which is all shell/ reaches: the controller,
// the gauge address, the power pin and its polarity. Enumerated from the source.
//
// ACTIVE IS MUTABLE AND IS WRITTEN THEN READ BACK. detectAndSelectBoard selects the
// X3 profile, runs a display-bus probe that MUTATES ACTIVE.displayController, and
// reads it back to decide the UC8279 promotion. A fake without that write-then-read
// would leave the promotion branch unreachable.
//
// NOTHING CHECKS THESE FIELD NAMES. If the SDK renames one, this keeps compiling
// and the desktop keeps passing -- only `make firmware` catches it, which is why
// every PR must be green on both builds.
struct BatteryGaugeConfig {
  uint8_t gaugeAddr = 0x55;  // BQ27220 on the X3; 0 = no I2C gauge, use the ADC
};

struct InputPins {
  int8_t power = 3;           // a real GPIO, unlike the six ADC-ladder front buttons
  bool powerActiveHigh = false;  // active-LOW on both Xteink profiles
};

class BoardConfig {
 public:
  // NAMES AND VALUES FROM THE REAL HEADER, not invented. The first draft of this
  // file spelled them XTEINK_X3 / UC8253 and main.cpp would not compile -- the
  // method census counted CALLS and could not see enumerator spellings.
  //
  // Only the three Xteink entries matter here; the rest of the real list is other
  // people's boards. XteinkX3Uc8279 is the newer production run -- same board and
  // glass, different controller -- which is what the display-bus probe promotes to.
  enum class Board : uint8_t { XteinkX4, XteinkX3, XteinkX3Uc8279 };
  enum class DisplayController : uint8_t {
    SSD1677 = 0,
    UC8253 = 2,
    ED2208 = 3,
    LgfxEpd = 4,
    IT8951 = 5,
    UC8279 = 6,
    UC8179 = 7
  };

  struct Profile {
    Board board = Board::XteinkX3;
    DisplayController displayController = DisplayController::UC8253;
    BatteryGaugeConfig batteryGauge{};
    InputPins input{};
  };

  static Profile ACTIVE;

  static bool selectDevice(Board b) {
    ACTIVE.board = b;
    ACTIVE.displayController = b == Board::XteinkX3Uc8279 ? DisplayController::UC8279
                               : b == Board::XteinkX4     ? DisplayController::SSD1677
                                                          : DisplayController::UC8253;
    harness::record("<board> selectDevice %d", static_cast<int>(b));
    return true;
  }
};

inline BoardConfig::Profile BoardConfig::ACTIVE{};
