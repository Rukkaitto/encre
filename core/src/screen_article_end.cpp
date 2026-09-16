#include "reader/screen_article_end.h"

#include "reader/theme.h"

namespace reader {
namespace {
// U+00B7 as its own literal: a C++ hex escape is unbounded, so "\xB7 22" would
// parse `\xB72` as one escape. See screen_articles.cpp, which says it once more.
constexpr const char* kMiddot = "\xC2\xB7";

std::vector<ArticleEndScreen::Chosen> slabsFor(const ArticleEndScreen::Facts& f) {
  using C = ArticleEndScreen::Chosen;
  std::vector<C> s{C::Archive, C::Star};
  if (f.hasNext) s.push_back(C::NextArticle);
  s.push_back(C::BackToList);
  return s;
}
}  // namespace

ArticleEndScreen::ArticleEndScreen(Facts facts)
    : FocusScreen(static_cast<int>(slabsFor(facts).size()),
                  static_cast<int>(slabsFor(facts).size())),
      facts_(std::move(facts)),
      slabs_(slabsFor(facts_)) {
  vm_.hints = {"BACK", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  syncVm();
}

ArticleEndScreen::Chosen ArticleEndScreen::chosenAt(int slab) const {
  if (slab < 0 || slab >= static_cast<int>(slabs_.size())) return Chosen::None;
  return slabs_[static_cast<size_t>(slab)];
}

void ArticleEndScreen::syncVm() {
  vm_.title = "ARTICLE FINISHED";
  // EMPTY WHEN NOTHING REMAINS, rather than `0 LEFT`. A count of nothing is not
  // information, and the band's phantom gap cancels for an empty value so the
  // label still lands on the margin exactly -- renderBookEnd's own note.
  vm_.leftValue =
      facts_.unreadRemaining > 0 ? std::to_string(facts_.unreadRemaining) + " LEFT" : std::string();
  vm_.articleTitle = facts_.title;
  vm_.meta = facts_.domain;
  if (!vm_.meta.empty()) {
    vm_.meta += " ";
    vm_.meta += kMiddot;
    vm_.meta += " " + std::to_string(facts_.readingMinutes) + " MIN";
  }
  vm_.actions.clear();
  for (const Chosen c : slabs_) {
    switch (c) {
      case Chosen::Archive: vm_.actions.push_back("ARCHIVE"); break;
      // Names what it will DO, as the actions overlay's second row does.
      case Chosen::Star: vm_.actions.push_back(facts_.starred ? "UNSTAR" : "STAR"); break;
      case Chosen::NextArticle: vm_.actions.push_back("NEXT ARTICLE"); break;
      case Chosen::BackToList: vm_.actions.push_back("BACK TO LIST"); break;
      case Chosen::None: break;
    }
  }
  vm_.focusedAction = focus();
  // EVERY OUTCOME ON THIS SCREEN IS A CARD WRITE THE RADIO FINISHES LATER, and
  // this is the sentence that says so. Archive and Star queue a marker file; the
  // server learns on the next sync.
  vm_.note = "SYNCS ON THE NEXT CONNECTION.";
}

Action ArticleEndScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      chosen_ = chosenAt(focus());
      return Action::article();
    case Gesture::Back:
      // BACK TO THE LAST PAGE, which is BookEnd's rule: this screen is reached
      // by a page turn, so Back is the turn undone rather than leaving the
      // article. `BACK TO LIST` is the slab for leaving, and it latches because
      // the shell decides where the list comes back.
      return Action::pop();
    default:
      return Action::none();
  }
}

void ArticleEndScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                              Plane plane) const {
  theme.renderArticleEnd(fb, fonts, vm_, plane);
}

}  // namespace reader
