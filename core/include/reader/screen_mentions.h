#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/name_extracts.h"
#include "reader/viewmodel.h"

namespace reader {

// One name's sightings (design/Mentions.dc.html).
//
// The screen the 2026-08-24 design deliberately did not have. Selecting a name used
// to open the peek directly; it opens this, and a row here opens the peek at that
// spot.
//
// --- THE CHAPTER IS A SECTION HEADER, NOT A FIELD ------------------------------
//
// A name's mentions land in 1.88 chapters on average and 7 at worst, so a chapter
// label on every row would repeat its neighbour's more often than not. Contents'
// shape instead: tracked caps with no rule, shown only where the chapter changes.
//
// A HEADER BELONGS TO THE ROW UNDER IT and is never focusable, which is a stronger
// rule than Contents needs. There the headers are an NCX's own depth-1 entries and a
// screenful can legitimately start with one; here a header stranded at the foot of
// the panel with its sighting below the fold would be a label for nothing, so the
// unit the window measures is header-plus-row.
class MentionsScreen : public FocusScreen {
 public:
  // `subject` is the band's right slot -- the group's fullest form, or its display
  // name where there is nothing longer. It is never empty: with the label naming the
  // screen, that slot is the only thing saying whose mentions these are.
  MentionsScreen(std::string subject, std::vector<StoredExtract> extracts,
                 std::vector<std::string> chapterNames, int visibleRows);

  ScreenId id() const override { return ScreenId::Mentions; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const MentionsViewModel& vm() const { return vm_; }

  // THE THEME HANDS OVER THE BOX AND EVERY ROW'S HEIGHT, and this screen does the
  // counting, because the item table -- which rows carry a chapter header -- is its.
  // `rowHeights` is one per sighting, in order.
  void setMetrics(int listH, int headerH, const std::vector<int>& rowHeights);

  int rowCount() const { return static_cast<int>(items_.size()); }

  // Every sighting's extract text, in order. What a caller passes to the theme to
  // get the heights back.
  std::vector<std::string> extractTexts() const;

  // The sighting the focus names, or nullptr. What the shell reads to know where to
  // open the peek.
  const StoredExtract* chosen() const;

 protected:
  void syncVm() override;

 private:
  // One drawable item: an extract, with the chapter header that precedes it where
  // the chapter changed.
  struct Item {
    StoredExtract extract;
    std::string header;  // empty unless this row opens a new chapter
  };

  int rowsFittingFrom(int first) const;
  void retune();

  std::vector<Item> items_;
  std::vector<int> rowH_;
  int listH_ = 0, headerH_ = 0;
  std::string subject_;
  MentionsViewModel vm_{};
};

}  // namespace reader
