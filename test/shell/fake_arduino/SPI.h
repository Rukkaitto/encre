#pragma once
#include <cstdint>
#include "harness_state.h"

// RECORDED AND NOTHING MORE. main.cpp calls SPI.begin() once inside
// detectAndSelectBoard, before the driver owns the pins, and the card manager
// begins the same bus again later -- an ORDER that is a fact about this silicon.
// The transcript makes the order visible; it cannot make it checkable.
class SPIClass {
 public:
  void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1) {
    harness::record("<spi> begin sck=%d miso=%d mosi=%d ss=%d", sck, miso, mosi, ss);
  }
  void end() { harness::record("<spi> end"); }
};
inline SPIClass SPI;
