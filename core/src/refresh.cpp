#include "reader/refresh.h"

namespace reader {

RefreshMode RefreshPolicy::next(bool transition) {
  if ((transition && fullOnTransition_) || cadence_ == 1) {
    sinceFull_ = 0;
    return RefreshMode::Full;
  }
  // Cadence disabled: never schedule a periodic FULL. Still counted, so
  // sinceFull() keeps reporting how long it has been for the [paint] log.
  if (cadence_ <= kNever) {
    ++sinceFull_;
    return RefreshMode::Fast;
  }
  // Falling through here with `transition` set is deliberate: the transition is
  // counted as the ordinary refresh it now is, rather than resetting sinceFull_.
  if (++sinceFull_ >= cadence_) {
    sinceFull_ = 0;
    return RefreshMode::Full;
  }
  return RefreshMode::Fast;
}

}  // namespace reader
