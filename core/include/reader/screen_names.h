#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/names.h"
#include "reader/viewmodel.h"

namespace reader {

// The names the book has used (design/Names.dc.html, design/NamesEmpty.dc.html).
//
// A FULL SCREEN, not an overlay, on Contents' argument: a list you read and scroll,
// not a question about the page behind it.
//
// EMPTY IS A VARIANT OF THIS SCREEN, not a second one -- same ScreenId, same view
// model, copy where the rows would be. `rows` being empty IS the variant; there is
// no flag, because a flag could disagree with the rows. A book opened at chapter one
// legitimately knows almost nothing, and that is the feature working.
//
// IT IS HANDED GROUPS, NOT THE CARD. Grouping is a batch operation over the whole
// index -- containment, prefix edges, both guards -- and it happens when the screen
// opens, because doing it per chapter would freeze decisions later chapters should
// change. Who reads the card and calls `groupNames` is the shell's business; this
// class takes the answer, which is what keeps it testable without a filesystem.
class NamesScreen : public FocusScreen {
 public:
  // `visibleRows` is a starting guess; the real count depends on how many of the
  // rows are SHORT, which only the screen knows -- see `setMetrics`.
  NamesScreen(std::vector<NameGroup> groups, int visibleRows);

  ScreenId id() const override { return ScreenId::Names; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const NamesViewModel& vm() const { return vm_; }

  // THE THEME REPORTS THE BOX MODEL AND THE SCREEN COUNTS, which is settingsMetrics'
  // split and for its reason: the item table belongs to the screen. Here the two
  // heights interleave by content rather than by a header flag, so only this class
  // can say how many fit from a given first row.
  void setMetrics(int listH, int tallRowH, int shortRowH);

  int rowCount() const { return static_cast<int>(groups_.size()); }

  // The group the focus names, or nullptr. What the shell reads after a press to
  // know whose mentions to open.
  const NameGroup* chosen() const;

 protected:
  void syncVm() override;

 private:
  int rowsFittingFrom(int first) const;
  // Re-tune the window to what actually fits from where it is parked. See the .cpp.
  void retune();

  std::vector<NameGroup> groups_;
  NamesViewModel vm_{};
  int listH_ = 0, tallH_ = 0, shortH_ = 0;
};

}  // namespace reader
