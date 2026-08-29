#pragma once
// A host stack ceiling, per host compiler.
//
// THE CEILING CANNOT BE ONE NUMBER, because the two host compilers lay frames
// out differently and the difference is not small: the same stb inflate chain
// measures 7,348 bytes under clang/libc++ and 12,212 under x86-64 gcc, and the
// streaming decoder 3,072 against 6,824. The ceilings shipped calibrated on
// clang alone, so the first Linux CI run failed both -- with nothing having
// regressed.
//
// NEITHER HOST FIGURE IS THE DEVICE'S, and that is worth stating plainly so the
// number is not read as a device budget. The device builds with
// riscv32-esp-elf-gcc, so its frames are gcc-shaped -- but on 32-bit pointers,
// where x86-64 gcc's are 64-bit and overstate it. The device's real figure is
// the `[stack]` serial line (uxTaskGetStackHighWaterMark), on hardware.
//
// So what this guards is a REGRESSION: a vendored-library bump or a refactor
// that moves a table onto the stack. That only works if the ceiling stays tight
// on whichever compiler is running, which is why there are two of them rather
// than one raised to cover both -- a single 16 KB ceiling would need clang's
// appetite to more than double before it tripped, and clang is where nearly all
// of this project's development happens.
#include <cstddef>

namespace stackceil {

// The margin over each measurement is ~1.35x on both, which is the ratio the
// clang ceilings already used (10240 over 7,348). Enough to absorb a compiler
// point release, not enough to hide a table moving onto the stack.
constexpr std::size_t pick(std::size_t clangBytes, std::size_t gccBytes) {
#if defined(__clang__)
  (void)gccBytes;
  return clangBytes;
#else
  (void)clangBytes;
  return gccBytes;
#endif
}

}  // namespace stackceil
