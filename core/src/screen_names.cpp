#include "reader/screen_names.h"

#include "reader/theme.h"

namespace reader {

NamesScreen::NamesScreen(std::vector<NameGroup> groups, int visibleRows)
    : FocusScreen(static_cast<int>(groups.size()),
                  groups.empty() ? 0 : visibleRows,
                  groups.empty() ? Focus::WithNone : Focus::Noneless),
      groups_(std::move(groups)) {
  // WithNone ON AN EMPTY LIST, because -1 is a real position there exactly as it is
  // on an empty Library. Noneless would clamp the focus to row 0, which does not
  // exist, and the view model would then name a focused row the renderer never drew.
  syncVm();
}

int NamesScreen::rowsFittingFrom(int first) const {
  if (listH_ <= 0 || shortH_ <= 0) return 0;
  int used = 0, n = 0;
  for (int i = first; i < rowCount(); ++i) {
    const int h = groups_[static_cast<size_t>(i)].fullest.empty() ? shortH_ : tallH_;
    if (used + h > listH_) break;
    used += h;
    ++n;
  }
  return n;
}

void NamesScreen::setMetrics(int listH, int tallRowH, int shortRowH) {
  listH_ = listH;
  tallH_ = tallRowH;
  shortH_ = shortRowH;
  retune();
  syncVm();
}

Action NamesScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      return Action::pop();
    case Gesture::Prev:
      return moveFocus(-1, g.held);
    case Gesture::Next:
      return moveFocus(+1, g.held);
    case Gesture::Activate:
      // NOTHING TO OPEN ON AN EMPTY LIST, and that is not a row that focuses and
      // then ignores Select: there is no row. The hint bar says BACK and nothing
      // else in that state.
      if (chosen() == nullptr) return Action::none();
      return Action::push(ScreenId::Mentions);
    default:
      return Action::none();
  }
}

const NameGroup* NamesScreen::chosen() const {
  const int f = focus();
  if (f < 0 || f >= rowCount()) return nullptr;
  return &groups_[static_cast<size_t>(f)];
}

void NamesScreen::retune() {
  // THE WINDOW IS TOLD WHAT ACTUALLY FITS FROM WHERE IT IS PARKED, and re-told after
  // it moves. The two heights interleave by CONTENT, so a fixed count is wrong in
  // one of two ways and this screen shipped both in one render: a conservative count
  // taken from the tall row scrolled the list a row early AND left a whole row of
  // slack at the foot, because the renderer drew what fits while the window scrolled
  // on a smaller number. An optimistic count is worse -- the focus walks past the
  // last row drawn.
  //
  // TWO PASSES, because setting the count can itself move `first` (the window keeps
  // the focus in view), and the new `first` may fit a different number. It settles
  // immediately in practice; the loop is bounded rather than trusted to converge.
  if (listH_ <= 0 || shortH_ <= 0) return;
  for (int pass = 0; pass < 3; ++pass) {
    const int fit = rowsFittingFrom(window().firstVisible());
    const int want = fit > 0 ? fit : 1;
    if (want == window().visibleRows()) return;
    window().setVisibleRows(want);
  }
}

void NamesScreen::syncVm() {
  retune();
  vm_.title = "NAMES";
  // THE COPY IS design/NamesEmpty.dc.html's, WORD FOR WORD. It says when the list
  // will fill rather than that it is empty: naming the mechanism turns an empty
  // screen into an explained one, which is HomeEmpty's argument.
  vm_.emptyTitle = "NO NAMES YET";
  vm_.emptyBody =
      "Names appear as you read. Keep going and the people and places this book uses "
      "will collect here.";

  const ScrollWindow::Slice s = window().slice();
  vm_.rows.clear();
  for (int i = 0; i < s.count && s.first + i < rowCount(); ++i) {
    const NameGroup& g = groups_[static_cast<size_t>(s.first + i)];
    vm_.rows.push_back(NameRow{g.display, g.fullest});
  }
  const int f = focus();
  vm_.focusedRow = (f >= s.first && f < s.first + static_cast<int>(vm_.rows.size()))
                       ? f - s.first
                       : -1;
  vm_.scrollFirst = s.first;
  vm_.scrollCount = static_cast<int>(vm_.rows.size());
  vm_.scrollTotal = rowCount();
  // ONE CONDITION FOR THE RAIL AND THE GUTTER, read from one place, which is
  // renderLibrary's rule.
  vm_.scrollable = vm_.scrollTotal > vm_.scrollCount;
  if (groups_.empty()) {
    // BACK ONLY, and the other three slots are the boards' 36px dead spacers. The
    // only thing that fills this list is reading, so there is nothing to press.
    vm_.hints = {"BACK", "", "", ""};
  } else {
    // MENTIONS, WHERE THE BOARD ONCE SAID PEEK. Confirm opens the sightings list,
    // not the peek, and this is one of only two places the word reaches the glass.
    vm_.hints = {"BACK", "MENTIONS", "UP", "DOWN"};
  }
  vm_.holds = {false, false, false, false};
}

void NamesScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                         Plane plane) const {
  theme.renderNames(fb, fonts, vm_, plane);
}

}  // namespace reader
