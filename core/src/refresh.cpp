#include "reader/refresh.h"

namespace reader {

RefreshMode RefreshPolicy::next(bool transition) {
  if (transition || cadence_ <= 1) {
    sinceFull_ = 0;
    return RefreshMode::Full;
  }
  if (++sinceFull_ >= cadence_) {
    sinceFull_ = 0;
    return RefreshMode::Full;
  }
  return RefreshMode::Fast;
}

}  // namespace reader
