#include "reader/heapguard.h"

namespace reader {
namespace {

// ASK, THEN GIVE IT STRAIGHT BACK. `new (std::nothrow)` is the only question this
// code can put to the allocator that has an answer rather than an `abort()`, and
// the block is released at once so the reserve that follows can take it.
//
// It is a poor substitute for a container that could report failure, and it is here
// rather than in a comment on a crash report.
bool realProbe(size_t bytes) {
  if (bytes == 0) return true;
  char* p = new (std::nothrow) char[bytes];
  const bool ok = p != nullptr;
  delete[] p;
  return ok;
}

}  // namespace

Heap::Probe Heap::probe_ = &realProbe;

void Heap::install(Probe p) { probe_ = p != nullptr ? p : &realProbe; }

}  // namespace reader
