#pragma once
#include <cstddef>
#include <cstdint>

#include "reader/font.h"

namespace reader {

// The chrome type ramp, by role rather than by pixel size. Bitmap glyphs are
// pre-rendered so each size is its own asset; enumerating the roles keeps the
// asset list finite and lets a theme ask for meaning ("this is a value")
// instead of numbers.
// Display tops the ramp: the design gives the reading percentage 44px/700, well
// above the 24px title, so the number -- not the book's name -- is the first
// thing read on Home. Title cannot simply grow to fill that slot: the two sit
// side by side in the same column block and must stay distinguishable.
enum class Role : uint8_t { Meta, Label, Value, Body, Title, Display, Count_ };

// Owns nothing: each Font is a zero-copy view, so every blob passed to load()
// must outlive the FontSet. The caller (shell or simulator) picks which asset
// backs each role, which is where device knowledge such as BoardConfig's
// uiScale belongs — core/ never sees a board profile.
class FontSet {
 public:
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
  loaded_[i] = faces_[i].load(data, size);
  return loaded_[i];
}

inline bool FontSet::ready() const {
  for (size_t i = 0; i < kCount; ++i)
    if (!loaded_[i]) return false;
  return true;
}

}  // namespace reader
