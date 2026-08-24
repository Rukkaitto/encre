#include "reader/progress.h"

namespace reader {

Progress::Fn Progress::fn_ = nullptr;
void* Progress::ctx_ = nullptr;

void Progress::install(Fn f, void* ctx) {
  fn_ = f;
  ctx_ = ctx;
}

}  // namespace reader
