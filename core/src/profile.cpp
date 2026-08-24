#include "reader/profile.h"

namespace reader {

Profile::Clock Profile::clock_ = nullptr;
uint32_t Profile::micros_[kPhaseCount] = {};
uint32_t Profile::calls_[kPhaseCount] = {};

void Profile::install(Clock c) {
  clock_ = c;
  reset();
}

void Profile::reset() {
  for (int i = 0; i < kPhaseCount; ++i) {
    micros_[i] = 0;
    calls_[i] = 0;
  }
}

const char* Profile::name(Phase p) {
  switch (p) {
    case Phase::Fill: return "fill";
    case Phase::Veil: return "veil";
    case Phase::Dither: return "dither";
    case Phase::Glyph: return "glyph";
    case Phase::Icon: return "icon";
    case Phase::Count_: break;
  }
  return "?";
}

}  // namespace reader
