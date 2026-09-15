#include "reader/screen_articles.h"

#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

// U+00B7, as its own two-byte literal. A C++ hex escape is UNBOUNDED, so
// "\xB7 MIN" parses `\xB7M` as one escape -- clang rejects it and the ESP32's
// GCC accepts it and emits a byte that is not a middle dot. Every other site in
// this firmware that draws one spells it this way for this reason.
constexpr const char* kMiddot = "\xC2\xB7";

std::string metaFor(const ArticleItem& a) {
  std::string s = a.domain;
  s += " ";
  s += kMiddot;
  s += " " + std::to_string(a.readingMinutes) + " MIN";
  // `READ` is a third field on the board rather than a different row treatment,
  // and the bullet carries the same fact as a shape. Two spellings of one fact,
  // which the board asks for: the mark is legible at a glance and the word is
  // unambiguous.
  if (a.read) {
    s += " ";
    s += kMiddot;
    s += " READ";
  }
  return s;
}

const std::array<std::string, 4> kHints{"BACK", "READ", "UP", "DOWN"};
// BACK and three empty slots. An empty slot is 36px and not zero
// (kHintEmptySlotW), which the board authors as a spacer div -- measuring one as
// nothing draws the live slot in the wrong place.
const std::array<std::string, 4> kSetupHints{"BACK", "", "", ""};

}  // namespace

ArticlesScreen::ArticlesScreen(std::vector<ArticleItem> items, std::string stamp)
    : FocusScreen(static_cast<int>(items.size()), static_cast<int>(items.size()),
                  Focus::WithNone),
      items_(std::move(items)) {
  vm_.title = "ARTICLES";
  vm_.syncLabel = "Sync now";
  vm_.syncStamp = std::move(stamp);
  vm_.hints = kHints;
  // A HOLD ON CONFIRM AND NOTHING ELSE, and the two movers repeat. `setAutoRepeat`
  // enforces the exclusion rather than documenting it, so this pair cannot
  // overlap by accident.
  vm_.holds = {false, true, false, false};
  declareHints(vm_.holds);
  declareRepeat(static_cast<ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down)));
  syncVm();
}

ArticlesScreen::ArticlesScreen()
    : FocusScreen(0, 0, Focus::Noneless) {
  vm_.title = "ARTICLES";
  vm_.notSetUp = true;
  // The board's own words. They live on the model rather than in the theme for
  // the reason every other string here does: the board owns the copy, and a
  // sentence in a renderer is a copy change that needs a code change.
  vm_.setupTitle = "READ IT LATER";
  vm_.setupProse =
      "Put the SD card in your computer and fill in your wallabag details in its "
      "/.reader/wallabag.json file.";
  vm_.setupNote = "THE FILE IS ALREADY ON THE CARD.";
  vm_.hints = kSetupHints;
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  syncVm();
}

void ArticlesScreen::setVisibleRows(int n) {
  window().setVisibleRows(n);
  syncVm();
}

const ArticleItem* ArticlesScreen::focusedItem() const {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(items_.size())) return nullptr;
  return &items_[static_cast<size_t>(f)];
}

int ArticlesScreen::focusedId() const {
  const ArticleItem* a = focusedItem();
  return a == nullptr ? 0 : a->id;
}

std::string ArticlesScreen::focusedTitle() const {
  const ArticleItem* a = focusedItem();
  return a == nullptr ? std::string() : a->title;
}

void ArticlesScreen::setStamp(std::string stamp) {
  vm_.syncStamp = std::move(stamp);
}

void ArticlesScreen::setStatusLine(std::string line) {
  vm_.statusLine = std::move(line);
}

void ArticlesScreen::syncVm() {
  if (vm_.notSetUp) {
    vm_.bandValue = "NOT SET UP";
    vm_.rows.clear();
    vm_.focusedRow = -1;
    vm_.firstRow = 0;
    vm_.totalRows = 0;
    return;
  }

  int unread = 0;
  for (const ArticleItem& a : items_)
    if (!a.read) ++unread;
  vm_.bandValue = std::to_string(unread) + " UNREAD";

  const ScrollWindow::Slice s = window().slice();
  vm_.rows.clear();
  vm_.rows.reserve(static_cast<size_t>(s.count));
  for (int i = 0; i < s.count; ++i) {
    const ArticleItem& a = items_[static_cast<size_t>(s.first + i)];
    vm_.rows.push_back({a.title, metaFor(a), a.read});
  }
  vm_.focusedRow = s.focused;
  vm_.firstRow = s.first;
  vm_.totalRows = static_cast<int>(items_.size());
}

Action ArticlesScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  if (vm_.notSetUp) {
    // Back is navigation and pops itself; everything else is refused, because
    // there is nothing here to act on. A screen that latched from this state
    // would be a press with no work behind it, which is the dead-button shape
    // this project has shipped twice.
    return g.what == Gesture::Back ? Action::pop() : Action::none();
  }

  if (g.what == Gesture::Secondary) {
    // ON AN ARTICLE ROW ONLY. From the sync row there is nothing to act on, and
    // an overlay captioned with whichever article happened to be first would be
    // acting on a row the reader did not choose.
    if (focusedItem() == nullptr) return Action::none();
    return Action::push(ScreenId::ArticleActions);
  }

  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      if (focusedItem() == nullptr) {
        // The sync row. A LATCH rather than a push: bringing the radio up and
        // driving the engine is the shell's, and the dialog it pushes is the
        // shell's to choose -- a device with no saved network gets an error
        // shape rather than a connecting one, and this screen cannot know which.
        chosen_ = Chosen::Sync;
        return Action::article();
      }
      // Action::open() carries no id, for its own stated reason: the shell reads
      // it off this screen, which is still on top.
      return Action::open();
    case Gesture::Back:
      return Action::pop();
    default:
      return Action::none();
  }
}

void ArticlesScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderArticles(fb, fonts, vm_, plane);
}

}  // namespace reader
