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
  enum class Board : uint8_t { UNKNOWN, XTEINK_X3, XTEINK_X4 };
  enum class DisplayController : uint8_t { UNKNOWN, UC8253, UC8279 };

  struct Profile {
    Board board = Board::XTEINK_X3;
    DisplayController displayController = DisplayController::UC8253;
    BatteryGaugeConfig batteryGauge{};
    InputPins input{};
  };

  static Profile ACTIVE;

  static bool selectDevice(Board b) {
    ACTIVE.board = b;
    ACTIVE.displayController =
        b == Board::XTEINK_X4 ? DisplayController::UC8279 : DisplayController::UC8253;
    harness::record("<board> selectDevice %s", b == Board::XTEINK_X3   ? "X3"
                                               : b == Board::XTEINK_X4 ? "X4"
                                                                       : "UNKNOWN");
    return true;
  }
};

inline BoardConfig::Profile BoardConfig::ACTIVE{};
