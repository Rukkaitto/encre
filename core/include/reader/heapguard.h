#pragma once
#include <cstddef>
#include <new>
#include <utility>

namespace reader {

// WHETHER THE HEAP CAN SERVE ONE CONTIGUOUS BLOCK, ASKED BEFORE A GROWTH THAT
// CANNOT REFUSE.
//
// Under `-fno-exceptions` a `std::vector` or `std::string` that cannot allocate is
// `abort()` with no message and no stack, and the reboot lands the reader back on
// Home -- which reads as a navigation bug rather than as out of memory. This
// project has now had that reported three times, twice as "opening a book goes
// back to Home" and once as a book that crashed the firmware on the first press and
// opened normally on the second.
//
// `new (std::nothrow)` answers with a null instead, and every hand-rolled buffer in
// the EPUB path already uses it. What it cannot cover is a CONTAINER: there is no
// nothrow spelling of `reserve` or `push_back`, and the open path grows five of
// them from numbers a FILE states -- a zip's entry count, a manifest's length, a
// spine's length, an NCX's entry count, a stylesheet's size.
//
// So the question is asked FIRST, of the allocator, with a block of exactly the
// size the growth will want; the growth that follows takes the block the probe just
// released. That is reliable here because the device is single-threaded and the
// reserve is the next statement -- the same argument `Zip`'s own probe has made
// since 3A, and `CoverFitter::begin`'s.
//
// IT ASKS FOR A BLOCK, NOT FOR A TOTAL, and that is the whole point: every one of
// these allocations is a single contiguous buffer, and a heap with 60 KB free in
// 12 KB pieces cannot serve a 20 KB entry list. `getFreeHeap()` would answer the
// wrong question. Note also that the probe runs while the container's OLD buffer is
// still held, which is exactly the state a reallocation is in.
//
// WHAT IT IS NOT: a budget, a cap or a reservation. It says what the heap could do
// at one instant. A caller that gets `true` and then allocates something else first
// has learned nothing.
class Heap {
 public:
  // Bytes in, "the allocator would serve that" out. A zero-byte block is always
  // available.
  using Probe = bool (*)(size_t bytes);

  static bool hasBlock(size_t bytes) { return probe_(bytes); }

  // FOR TESTS, and there is no other way to have any. The desktop cannot be made to
  // fail an 8 KB allocation naturally -- 64-bit hosts have gigabytes and the OOM
  // killer is not a return value -- so failure is INJECTED, exactly as `Profile`
  // injects a clock that `core/` must not acquire for itself. `install(nullptr)`
  // puts the real probe back, and a test that forgets to is a test that has changed
  // every case after it.
  static void install(Probe p);

 private:
  static Probe probe_;
};

// ROOM FOR `count` ELEMENTS, OR FALSE.
//
// GEOMETRIC, because the callers include an append loop: reserving exactly what is
// asked for each time would turn a 32 KB stylesheet read into 64 reallocations, and
// this function exists to stop an abort rather than to introduce a quadratic.
//
// AND IT FALLS BACK TO THE EXACT SIZE WHEN DOUBLING IS REFUSED, which is the part
// worth not removing: doubling asks for twice what is needed, so near the limit it
// would refuse a book that fits. That is the shape of the defect this project
// records as "the cap protected nothing" -- a bound that fires on the wrong
// question -- and here it would fire in the other direction, refusing a book on
// memory it never needed. Two probes at most, and only on the path that is about to
// refuse anyway.
template <class C>
bool ensureRoom(C& c, size_t count) {
  if (count <= c.capacity()) return true;
  constexpr size_t kElem = sizeof(typename C::value_type);
  size_t want = c.capacity() > count / 2 ? c.capacity() * 2 : count;
  if (want < count) want = count;
  if (!Heap::hasBlock(want * kElem)) {
    if (want == count || !Heap::hasBlock(count * kElem)) return false;
    want = count;
  }
  c.reserve(want);
  return true;
}

// `push_back` that refuses instead of aborting. The value is left alone on a
// refusal, so a caller may report and return without having lost it.
template <class C, class T>
bool pushOrRefuse(C& c, T&& v) {
  if (!ensureRoom(c, c.size() + 1)) return false;
  c.push_back(std::forward<T>(v));
  return true;
}

// `append` that refuses instead of aborting. Templated on the string so this header
// stays a leaf -- `<string>` is 20,000-odd preprocessed lines and every caller
// already has it.
template <class S>
bool appendOrRefuse(S& s, const char* p, size_t n) {
  if (!ensureRoom(s, s.size() + n)) return false;
  s.append(p, n);
  return true;
}

}  // namespace reader
