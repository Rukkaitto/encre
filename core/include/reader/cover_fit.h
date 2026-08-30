#pragma once

namespace reader {

// HOW A COVER IS FITTED TO A PANEL.
//
// Fill crops to the panel; Whole letterboxes and the caller tints the bands.
//
// THIS HEADER INCLUDES NOTHING, AND THAT IS THE WHOLE REASON IT EXISTS.
// `settings.h` carries the user's choice of fit and is a leaf on purpose: it
// spells layout.h's two body-type constants as literals rather than including
// it, because that include cost 73,976 preprocessed lines against 895 for two
// integers (settings.h says so at the `bodyPpem` field). `imagefit.h` costs the
// same order for the same reason -- it needs `<vector>`, which is 72,845 lines
// by itself -- so including it from settings.h to reach one enum would undo
// that decision exactly. Measured, `clang++ -std=c++20 -E`:
//
//     <cstdint>      368        settings.h        895
//     <memory>    38,447        layout.h       73,976
//     <vector>    72,845        imagefit.h     72,916
//
// This file is 17 preprocessed lines against 8 for an empty translation unit,
// so it costs NINE: settings.h goes 895 -> 904 when Task 11 includes it.
//
// THE ENUM RATHER THAN A BOOL, and here rather than in settings.h: `settings.h`
// storing a `CoverFit` keeps the fit named in the one vocabulary the imaging
// layer and the Settings row share, and putting the enum IN settings.h would
// make an imaging layer depend on a settings header -- the dependency pointing
// the wrong way for the sake of one declaration.
//
// THE MEASUREMENT BEHIND OFFERING BOTH, over the 225 books in the corpus. The
// method, since the tool that repeats it is a later task's: walk every EPUB,
// resolve the cover through the OPF by both routes (`<meta name="cover">` and
// `properties="cover-image"`), and read its dimensions out of the JPEG SOF or
// the PNG IHDR. All 225 declare one; 184 are baseline JPEG, 39 PNG, 2
// progressive.
//
//   * 160 of 225 covers are 2:3 to within half a percent, so on the X3
//     (528x792, 2:3 exactly) Fill loses NOTHING for 71% of books and the
//     setting is a no-op there.
//   * The aspects run 0.558 to 0.901, median 0.667. Only TWO are narrower than
//     the X4's 0.600, so on the X4 a cover is almost always WIDER than the
//     panel and Fill crops its WIDTH: the median 2:3 cover keeps 1260 of its
//     1400 columns, a 10.0% loss.
//   * The tail is what earns the setting -- the squarest corpus cover is
//     877x973 and Fill cuts its title off at both edges, keeping 584 of 877
//     columns: a 33.4% loss.
//
// A MEASUREMENT THAT CANNOT BE REPEATED IS AN ANECDOTE, which is the standard
// `tools/corpus.py` sets, and this one is not repeatable from the repo yet: the
// walk above was a throwaway script. `tools/covers.py` is the task that lands
// it, and it runs the real `decodeCover` rather than a parallel parser -- so
// when it exists, its numbers are the ones to trust and these are the ones to
// re-check against it.
//
// THE AXES ARE THE PART TO READ TWICE, because the plan this came from had them
// transposed and its numbers right. The X4 is 3:5 = 0.600 and a 2:3 cover is
// 0.667, so the cover is RELATIVELY WIDER than the panel: Fill crops width and
// Whole leaves bands ABOVE AND BELOW. (10.0% happens to be correct for either
// axis by coincidence of these two ratios -- 0.600 / 0.667 = 0.9 -- which is
// what let the mistake survive being sanity-checked against its own figure.)
enum class CoverFit { Fill, Whole };

}  // namespace reader
