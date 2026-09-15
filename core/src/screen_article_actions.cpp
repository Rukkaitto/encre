#include "reader/screen_article_actions.h"

#include "reader/theme.h"

namespace reader {

ArticleActionsScreen::ArticleActionsScreen(Facts facts)
    : FocusScreen(kRowCount, kRowCount), facts_(std::move(facts)) {
  vm_.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  syncVm();
}

void ArticleActionsScreen::syncVm() {
  vm_.caption = facts_.title;
  // NEITHER ROW DISCLOSES: both act in place and neither opens a screen. Drawn
  // from the flag rather than derived from an empty value, which is
  // ListRow::discloses' recorded reason -- deriving it once drew a chevron
  // promising a screen that does not exist.
  vm_.actions = {{"Archive", false}, {facts_.starred ? "Unstar" : "Star", false}};
  vm_.focusedAction = focus();
}

Action ArticleActionsScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      // A LATCH, NOT A POP. Both rows are card writes -- a marker file in the
      // queue, and for Archive the article's own two files going -- and the
      // shell has to ask which. A screen that popped itself and then offered
      // this getter would be offering it about a destroyed object.
      chosen_ = (focus() == kStar) ? Chosen::Star : Chosen::Archive;
      return Action::article();
    case Gesture::Back:
      // Navigation and nothing else, so it pops itself: Action::wifi()'s rule,
      // that a screen latches when the shell has work and pops when it has not.
      return Action::pop();
    default:
      return Action::none();
  }
}

void ArticleActionsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                  Plane plane) const {
  theme.renderArticleActions(fb, fonts, vm_, plane);
}

}  // namespace reader
