#include "reader/screen_mentions.h"

#include "reader/theme.h"

namespace reader {
namespace {

std::string chapterLabelFor(const std::vector<std::string>& names, int spine) {
  // THE TOC'S OWN LABEL WHERE THERE IS ONE, and a bare `CH. NN` where there is not.
  // Four surfaces already draw a chapter from one label and this is the fifth; a
  // book with no NCX reads fine and simply cannot name its chapters, which is the
  // case `fillTocGaps` exists for.
  if (spine >= 0 && static_cast<size_t>(spine) < names.size() &&
      !names[static_cast<size_t>(spine)].empty())
    return names[static_cast<size_t>(spine)];
  std::string s = "CH. ";
  const int n = spine + 1;
  if (n < 10) s += '0';
  s += std::to_string(n);
  return s;
}

}  // namespace

MentionsScreen::MentionsScreen(std::string subject, std::vector<StoredExtract> extracts,
                               std::vector<std::string> chapterNames, int visibleRows)
    : FocusScreen(0, visibleRows, Focus::WithNone), subject_(std::move(subject)) {
  int lastSpine = -1;
  for (StoredExtract& e : extracts) {
    Item it;
    // A HEADER ONLY WHERE THE CHAPTER CHANGES. The extracts arrive oldest first --
    // the index lists them in spine order and each chapter's parts are in block
    // order -- so this is one comparison rather than a grouping pass.
    if (e.spine != lastSpine) {
      it.header = chapterLabelFor(chapterNames, e.spine);
      lastSpine = e.spine;
    }
    it.extract = std::move(e);
    items_.push_back(std::move(it));
  }
  window().setCount(rowCount());
  if (rowCount() > 0) {
    // Noneless once there is something to sit on, exactly as the Library is.
    setFocus(0);
  }
  syncVm();
}

std::vector<std::string> MentionsScreen::extractTexts() const {
  std::vector<std::string> out;
  out.reserve(items_.size());
  for (const Item& it : items_) out.push_back(it.extract.text);
  return out;
}

int MentionsScreen::rowsFittingFrom(int first) const {
  if (listH_ <= 0 || rowH_.empty()) return 0;
  int used = 0, n = 0;
  for (int i = first; i < rowCount(); ++i) {
    // A HEADER IS PART OF ITS ROW, never a unit of its own: one stranded at the foot
    // of the panel with its sighting below the fold would be a label for nothing.
    int h = rowH_[static_cast<size_t>(i)];
    if (!items_[static_cast<size_t>(i)].header.empty()) h += headerH_;
    if (used + h > listH_) break;
    used += h;
    ++n;
  }
  return n;
}

void MentionsScreen::retune() {
  if (listH_ <= 0 || rowH_.empty()) return;
  for (int pass = 0; pass < 3; ++pass) {
    const int fit = rowsFittingFrom(window().firstVisible());
    const int want = fit > 0 ? fit : 1;
    if (want == window().visibleRows()) return;
    window().setVisibleRows(want);
  }
}

void MentionsScreen::setMetrics(int listH, int headerH, const std::vector<int>& rowHeights) {
  listH_ = listH;
  headerH_ = headerH;
  rowH_ = rowHeights;
  rowH_.resize(items_.size(), 0);
  retune();
  syncVm();
}

Action MentionsScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      return Action::pop();
    case Gesture::Prev:
      return moveFocus(-1, g.held);
    case Gesture::Next:
      return moveFocus(+1, g.held);
    case Gesture::Activate:
      // THE SHELL OPENS THE PEEK, not this screen: the peek is pushed after this one
      // pops, which is what Contents already does. So the screen reports the choice
      // and asks to be dismissed.
      if (chosen() == nullptr) return Action::none();
      return Action::pop();
    default:
      return Action::none();
  }
}

const StoredExtract* MentionsScreen::chosen() const {
  const int f = focus();
  if (f < 0 || f >= rowCount()) return nullptr;
  return &items_[static_cast<size_t>(f)].extract;
}

void MentionsScreen::syncVm() {
  retune();
  vm_.title = "MENTIONS";
  vm_.subject = subject_;
  const ScrollWindow::Slice s = window().slice();
  vm_.rows.clear();
  for (int i = 0; i < s.count && s.first + i < rowCount(); ++i) {
    const Item& it = items_[static_cast<size_t>(s.first + i)];
    // A HEADER IS EMITTED AS ITS OWN ROW, but it is not a focusable one and it never
    // arrives without the sighting it labels: the two are one unit in the window's
    // arithmetic, which is why this loop cannot strand it.
    if (!it.header.empty()) vm_.rows.push_back(MentionRow{it.header, true});
    vm_.rows.push_back(MentionRow{it.extract.text, false});
  }
  // THE FOCUS IS AN INDEX INTO THE DRAWN ROWS, headers included, because that is
  // what the renderer walks. Translating it here keeps the theme from having to know
  // that some rows cannot be focused.
  vm_.focusedRow = -1;
  if (s.focused >= 0) {
    int drawn = 0;
    for (int i = 0; i < s.count && s.first + i < rowCount(); ++i) {
      if (!items_[static_cast<size_t>(s.first + i)].header.empty()) ++drawn;
      if (i == s.focused) {
        vm_.focusedRow = drawn;
        break;
      }
      ++drawn;
    }
  }
  vm_.scrollFirst = s.first;
  vm_.scrollCount = s.count;
  vm_.scrollTotal = rowCount();
  vm_.scrollable = vm_.scrollTotal > s.count;
  // PEEK, which is where MENTIONS sits one screen back. The word names the immediate
  // action on every screen that carries it.
  vm_.hints = {"BACK", "PEEK", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
}

void MentionsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderMentions(fb, fonts, vm_, plane);
}

}  // namespace reader
