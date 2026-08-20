#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>

#include "reader/font.h"

namespace reader {

// --- The chrome type ramp ---------------------------------------------------
//
// By role rather than by number: bitmap glyphs are pre-rendered so each face is
// its own asset, and enumerating the roles keeps that list finite while letting
// a theme ask for meaning ("this is a value") instead of arithmetic.
//
// A role names a size *and* a weight, and it says both out loud. The ramp used
// to name only the size -- Meta, Label, Value, Body, Title, Display -- with a
// weight baked silently into each asset, chosen by counting the boards' runs and
// taking the majority. That conflates two axes the design varies
// independently, and it cannot be right: across the 46 boards a single size is
// set at two or three different weights.
//
//   --t-meta     21px   188 runs at 400,  17 at 500,   4 at 700
//   --t-label    23px    49 runs at 500,   8 at 400,   2 at 700
//   --t-value    25px   122 runs at 700,  61 at 500,   8 at 400
//   --t-body     29px    24 runs at 500,  11 at 400,   4 at 700
//   --t-title    42px     6 runs at 700
//   --t-display  67px     2 runs at 700
//
// (Counted from design/*.dc.html; a run with no font-weight is CSS default 400.)
// With one face per size the minority runs were simply drawn wrong, and the
// error was invisible in review because nothing in the code said what weight a
// role carried. Home's author line is `--t-body` with no weight declared -- 400
// -- and was drawn in the 500 face, measuring 19% over the board's ink.
//
// So Body is two roles, because the design genuinely sets 29px at two weights.
// The sizes whose second weight no implemented screen reaches yet (Value at
// 500, Meta at 500, Label at 400) are deliberately not shipped: an unused face
// is ~15-20KB of flash. Adding one when its screen lands is an enum entry, a
// line in the Makefile's `fonts` target and a line in each loader -- and
// FontSet::load will then refuse to bind it to the wrong asset. What must never
// happen again is a screen quietly drawing 400 in a 500 face.
enum class Role : uint8_t {
  Meta400,
  Label500,
  Value700,
  Body400,
  Body500,
  Title700,
  Display700,
  Count_
};

// What a role IS: the nominal pixel size (ppem = pt * 150 / 72, resolved by
// FreeType and recorded in the asset) and the weight its variation axis was
// pinned to. This is the ramp's contract with its assets, and FontSet::load
// enforces it -- see below.
struct RoleSpec {
  int ppem;
  int weight;
  int pt;  // the design's unit; ppem is what it resolved to at 150 DPI
};

constexpr RoleSpec roleSpec(Role r) {
  switch (r) {
    case Role::Meta400:    return {21, 400, 10};
    case Role::Label500:   return {23, 500, 11};
    case Role::Value700:   return {25, 700, 12};
    case Role::Body400:    return {29, 400, 14};
    case Role::Body500:    return {29, 500, 14};
    case Role::Title700:   return {42, 700, 20};
    case Role::Display700: return {67, 700, 32};
    case Role::Count_:     break;
  }
  return {0, 0, 0};
}

// Owns nothing: each Font is a zero-copy view, so every blob passed to load()
// must outlive the FontSet. The caller (shell or simulator) picks which asset
// backs each role, which is where device knowledge such as BoardConfig's
// uiScale belongs — core/ never sees a board profile.
class FontSet {
 public:
  // Refuses a blob that is not the face the role names. The assets declare
  // their own size and weight (Font::ppem/weight), so this is a check against
  // the generator's intent rather than against a second table of numbers: hand
  // it the 500 asset for Role::Body400 and it returns false instead of drawing
  // the wrong weight for the next six screens.
  bool load(Role r, const uint8_t* data, size_t size);
  const Font& operator[](Role r) const { return faces_[static_cast<size_t>(r)]; }
  bool ready() const;

 private:
  static constexpr size_t kCount = static_cast<size_t>(Role::Count_);
  Font faces_[kCount];
  bool loaded_[kCount] = {};
};

inline bool FontSet::load(Role r, const uint8_t* data, size_t size) {
  const size_t i = static_cast<size_t>(r);
  if (i >= kCount) return false;
  loaded_[i] = false;
  Font f;
  if (!f.load(data, size)) return false;
  const RoleSpec spec = roleSpec(r);
  // A v1 asset declares neither, and cannot be trusted to be the right face; the
  // ramp's assets are all v2 (tools/fontc.py writes it), so this is not a
  // migration path, it is a refusal to guess.
  if (f.ppem() != spec.ppem || f.weight() != spec.weight) return false;
  faces_[i] = std::move(f);
  loaded_[i] = true;
  return true;
}

inline bool FontSet::ready() const {
  for (size_t i = 0; i < kCount; ++i)
    if (!loaded_[i]) return false;
  return true;
}

}  // namespace reader
