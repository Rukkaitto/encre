#pragma once
// A host stack ceiling, per host compiler -- and per SANITIZER, because an
// instrumented build is a third compiler as far as a stack frame is concerned.
//
// THE CEILING CANNOT BE ONE NUMBER, because the two host compilers lay frames
// out differently and the difference is not small: the same stb inflate chain
// measures 7,348 bytes under clang/libc++ and 12,212 under x86-64 gcc, and the
// streaming decoder 3,072 against 6,824. The ceilings shipped calibrated on
// clang alone, so the first Linux CI run failed both -- with nothing having
// regressed.
//
// ADDRESSSANITIZER IS THE THIRD, AND IT IS NOT A SMALL EFFECT EITHER. ASan puts
// a redzone around every stack object and spaces the locals out to make room,
// so a frame inflates without a line of the code under test changing. Measured
// on this tree -- Apple clang 21, arm64, `-fsanitize=address
// -fno-omit-frame-pointer`, Debug -- the stb chain wants 9,808 bytes against
// 7,348 uninstrumented, and the streaming decoder 9,648 against 2,120. That is
// 1.33x for the one and 4.55x for the other, because the redzone is a
// per-OBJECT cost: the small frame pays the larger multiple.
//
// SO RAISING THE CLANG CEILING TO COVER ASAN IS THE FIX THIS REFUSES, for the
// reason the gcc branch already existed. 13,312 over the streaming decoder's
// real clang appetite of 2,120 is a 6.3x ceiling -- a table could move onto
// that stack twice over without tripping it. Three branches keep each number
// meaning what it says.
//
// WHAT ASAN COSTS THE TEST IS THE RATIO, which is worth knowing before reading
// an instrumented figure. Uninstrumented, the streaming decoder's whole point
// is that it is a FRACTION of stb's stack, 2,120 against 7,348. Instrumented
// the two land within 2% of each other, because the redzones dominate both.
// Under ASan these assertions are a regression bound and nothing more, and the
// ratio claim is only readable on an uninstrumented run.
//
// THE ASAN FIGURES ARE THE ISOLATED ONES, WHICH IS THE WORST CASE AND NOT THE
// ONE CI WOULD SEE. The streaming assertion reports 6,656 in a full-suite run
// and 9,648 when its test case is the only one selected: lazy initialisation a
// full run has already paid for on the main thread happens inside the probe
// thread instead. Calibrated on the larger, so running one test case by name is
// green too -- a ceiling that only holds when the whole binary runs would fail
// for the person bisecting.
//
// MEASURED ON CLANG ONLY. gcc's ASan is unmeasured here -- there is no gcc on
// the host this was calibrated on -- and gcc's uninstrumented frames are
// already ~1.7x clang's, so a gcc ASan run may well want more than 13,312. The
// branch is keyed on "instrumented" rather than on which compiler did the
// instrumenting, so that case surfaces as a failure naming the real number
// instead of as a number nobody measured. Whether an ASan job belongs in
// `ci.yml` at all is a separate question -- it roughly doubles the `test` job --
// and if one ever lands on a gcc runner, this is the line to re-measure.
//
// NEITHER HOST FIGURE IS THE DEVICE'S, and that is worth stating plainly so the
// number is not read as a device budget. The device builds with
// riscv32-esp-elf-gcc, so its frames are gcc-shaped -- but on 32-bit pointers,
// where x86-64 gcc's are 64-bit and overstate it. The device's real figure is
// the `[stack]` serial line (uxTaskGetStackHighWaterMark), on hardware.
//
// So what this guards is a REGRESSION: a vendored-library bump or a refactor
// that moves a table onto the stack. That only works if the ceiling stays tight
// on whichever compiler is running, which is why there are three of them rather
// than one raised to cover them all -- a single 16 KB ceiling would need
// clang's appetite to more than double before it tripped, and clang is where
// nearly all of this project's development happens.
#include <cstddef>

// Both spellings, because neither compiler answers to the other's. clang has
// `__has_feature`, gcc defines `__SANITIZE_ADDRESS__`; the nested form is
// required because gcc before 14 does not have `__has_feature` at all, and an
// unguarded call to it is a preprocessor error rather than a false.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define STACKCEIL_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(STACKCEIL_ADDRESS_SANITIZER)
#define STACKCEIL_ADDRESS_SANITIZER 1
#endif

namespace stackceil {

// The margin over each measurement is ~1.35x on all three, which is the ratio
// the clang ceilings already used (10240 over 7,348). Enough to absorb a
// compiler point release, not enough to hide a table moving onto the stack. The
// two ASan ceilings round to the same 13 KiB from 9,808 (1.36x) and 9,648
// (1.38x) -- that is the redzones flattening two very different frames, not one
// number copied into both call sites.
constexpr std::size_t pick(std::size_t clangBytes, std::size_t gccBytes,
                           std::size_t asanBytes) {
  // INSTRUMENTED IS TESTED FIRST, because an ASan build of clang defines
  // `__clang__` as well: a compiler-first order would hand back the
  // uninstrumented ceiling and fail with nothing wrong.
#if defined(STACKCEIL_ADDRESS_SANITIZER)
  (void)clangBytes;
  (void)gccBytes;
  return asanBytes;
#elif defined(__clang__)
  (void)gccBytes;
  (void)asanBytes;
  return clangBytes;
#else
  (void)clangBytes;
  (void)asanBytes;
  return gccBytes;
#endif
}

}  // namespace stackceil
