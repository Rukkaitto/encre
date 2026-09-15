#include "reader/screen_wallabag_account.h"

#include "reader/theme.h"

namespace reader {
namespace {
std::string plural(int n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}
}  // namespace

WallabagAccountScreen::WallabagAccountScreen(Facts facts)
    : FocusScreen(kRowCount, kRowCount), facts_(std::move(facts)) {
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // THE FOCUS STARTS ON THE FIRST ROW THAT CAN ACT, not on row 0. Four value
  // rows precede it and the gated walk is what steps past them; setFocus clamps
  // rather than wrapping, so this cannot land anywhere else.
  setFocus(kKeepOffline);
  syncVm();
}

bool WallabagAccountScreen::focusable(int index) const {
  switch (index) {
    case kKeepOffline:
      return true;
    case kRemove:
      // AN UNCONFIGURED DEVICE HAS NOTHING TO REMOVE, so the row that would open
      // a destructive confirmation is unreachable. Derived from `configured`
      // rather than tabulated, which is Settings' `Cover fit` precedent: the
      // answer changes with the card and there is nothing to invalidate.
      return facts_.configured;
    default:
      // The four value rows and the section header. An inert row is DRAWN
      // exactly as an unfocused focusable one -- the flag is about input, and a
      // theme that dimmed on it would be inventing a design decision.
      return false;
  }
}

void WallabagAccountScreen::syncVm() {
  vm_.title = "WALLABAG";
  vm_.bandValue = facts_.configured ? "SIGNED IN" : "NOT SET UP";
  vm_.rows.clear();
  vm_.rows.resize(kRowCount);

  vm_.rows[kAccount] = {"Account", facts_.username, false, false, 0, false};
  vm_.rows[kUnread] = {"Unread", plural(facts_.unread, "ARTICLE", "ARTICLES"), false, false, 0,
                       false};
  vm_.rows[kLastSync] = {"Last sync", facts_.lastSync, false, false, 0, false};
  vm_.rows[kKeepOffline] = {"Keep offline", "NEWEST " + std::to_string(facts_.keepOffline), false,
                            false, 0, true};
  vm_.rows[kPending] = {"Pending actions", plural(facts_.pending, "TO PUSH", "TO PUSH"), false,
                        false, 0, false};
  vm_.rows[kHeader] = {"ON THIS DEVICE", "", true, false, 0, false};
  // A ROW STATES A QUANTITY OR DISCLOSES A SCREEN, NEVER BOTH -- so this one has
  // no value and carries the chevron. Its ellipsis is the promise the
  // confirmation keeps.
  vm_.rows[kRemove] = {"Remove downloaded articles\xE2\x80\xA6", "", false, true, 0,
                       facts_.configured};

  vm_.focusedRow = focus();
  vm_.firstRow = 0;
  vm_.totalRows = kRowCount;
  vm_.note =
      "THIS FREES CARD SPACE AND TOUCHES NOTHING ON YOUR WALLABAG. THE ACCOUNT ITSELF IS SET IN "
      "/.READER/WALLABAG.JSON ON THE CARD.";

  // THE CONFIRM HINT FOLLOWS THE FOCUSED ROW, which Settings is the precedent
  // for and which this screen needs for the same reason: one row cycles a value
  // in place and one opens a screen, and a Confirm labelled CHANGE that opens a
  // screen is a promise the press does not keep.
  vm_.hints = {"BACK", focus() == kRemove ? "OPEN" : "CHANGE", "UP", "DOWN"};
}

Action WallabagAccountScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      if (focus() == kRemove) {
        // A PUSH, not a latch: nothing has happened yet, and the confirmation is
        // what asks. Its REMOVE slab is where the latch lives.
        return Action::push(ScreenId::ArticlesRemoveConfirm);
      }
      if (focus() == kKeepOffline) {
        // CYCLES IN PLACE, which is Settings' and Typography's mechanism. The
        // value moves HERE so the screen redraws correct immediately; the shell
        // then commits and prunes, which is card work and is not core/'s.
        facts_.keepOffline = nextKeepOfflineStep(facts_.keepOffline);
        chosen_ = Chosen::KeepOffline;
        syncVm();
        return Action::article();
      }
      return Action::none();
    case Gesture::Back:
      return Action::pop();
    default:
      return Action::none();
  }
}

void WallabagAccountScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                   Plane plane) const {
  theme.renderWallabagAccount(fb, fonts, vm_, plane);
}

}  // namespace reader
