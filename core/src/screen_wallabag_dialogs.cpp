#include "reader/screen_wallabag_dialogs.h"

#include "reader/theme.h"

namespace reader {

// --- the sync dialog, two stages ---------------------------------------------

WallabagConnectingScreen::WallabagConnectingScreen(std::string host)
    : FocusScreen(0, 0), host_(std::move(host)) {
  vm_.caption = "CONNECTING\xE2\x80\xA6";
  // THE HOST IS NAMED UNQUOTED, which is WifiConnect's own treatment of an SSID:
  // quotes around a value that may itself contain punctuation read as part of
  // the name. The renderer breaks it `Anywhere`, because a hostname need contain
  // no space and the line is centred.
  vm_.message = "Connecting to " + host_ + ".";
  vm_.note = "THIS CAN TAKE A FEW SECONDS.";
  vm_.hints = {"CANCEL", "", "", ""};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

bool WallabagConnectingScreen::setFetching(int done, int total) {
  // `Fetching N of M.` -- and the word `article` is NOT in it. See the board
  // note: with it, the message reflows to a second line partway through a sync
  // and the panel changes height while the reader watches.
  const std::string caption = "SYNCING\xE2\x80\xA6";
  const std::string message = "Fetching " + std::to_string(done) + " of " + std::to_string(total) +
                              ".";
  const std::string note = "ANYTHING FETCHED IS KEPT.";
  if (vm_.caption == caption && vm_.message == message) return false;
  vm_.caption = caption;
  vm_.message = message;
  vm_.note = note;
  return true;
}

Action WallabagConnectingScreen::onGesture(const GestureEvent& g) {
  if (g.what == Gesture::Back) {
    // A LATCH, NOT A POP, and this is the case Action::wifi()'s rule was written
    // for: a sync is in flight and the engine has to be told. Where the reader
    // lands is the shell's, because it depends on what the cancel interrupted.
    cancelled_ = true;
    return Action::article();
  }
  // Nothing else is bound. The board draws three 36px dead slots, and a dialog
  // with no focus has nothing for a mover to move.
  return Action::none();
}

void WallabagConnectingScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                      Plane plane) const {
  theme.renderWallabagConnecting(fb, fonts, vm_, plane);
}

// --- the failure dialog, three copy shapes -----------------------------------

WallabagErrorScreen::WallabagErrorScreen(Shape shape)
    : FocusScreen(slabCountFor(shape), slabCountFor(shape)), shape_(shape) {
  vm_.hints = {"CANCEL", "OK", "", ""};
  vm_.holds = {false, false, false, false};
  if (shape_ == Shape::Offline) {
    // TWO SLABS IS A CHOICE, so the movers are live and Confirm says SELECT.
    // With one slab `SELECT` would promise a choice and Up and Down would have
    // no second row to reach -- SdMissing's rule, where the Confirm slot is
    // named after the slab it activates.
    vm_.hints = {"CANCEL", "SELECT", "UP", "DOWN"};
  }
  declareHints(vm_.holds);
  syncVm();
}

void WallabagErrorScreen::syncVm() {
  // THE BOARDS' STRINGS VERBATIM. Each was measured in its own board against
  // #76's floor -- see the notes there, which carry the numbers and the cuts
  // that did not survive them.
  switch (shape_) {
    case Shape::SignIn:
      vm_.caption = "COULDN\xE2\x80\x99T SIGN IN";
      vm_.message =
          "wallabag didn\xE2\x80\x99t accept those details. Check /.reader/wallabag.json on the "
          "card.";
      // `TRY AGAIN` IS ABSENT, NOT INERT: a rejected grant is deterministic --
      // the same values out of the same file produce the same answer, and
      // nothing the reader can press changes the file.
      vm_.actions = {"OK"};
      break;
    case Shape::Offline:
      vm_.caption = "COULDN\xE2\x80\x99T CONNECT";
      vm_.message = "Couldn\xE2\x80\x99t reach your wallabag. Nothing on it changed.";
      // THE ONE SHAPE THAT KEEPS `TRY AGAIN`, because a round trip really can
      // fail spuriously and retrying is a real action.
      vm_.actions = {"TRY AGAIN", "CANCEL"};
      break;
    case Shape::NoNetwork:
      vm_.caption = "COULDN\xE2\x80\x99T CONNECT";
      vm_.message = "No Wi-Fi network is saved. Join one in Settings first.";
      // Absent for the sign-in shape's reason reached by another road: the saved
      // list does not change between two presses of a slab either.
      vm_.actions = {"OK"};
      break;
  }
  vm_.focusedAction = focus();
}

Action WallabagErrorScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      // RESOLVED THROUGH THE SLAB LIST, never a fixed index -- the end screen's
      // lesson, and the same reason: the list is the shape, so slab 0 is
      // `TRY AGAIN` on one shape and `OK` on the other two.
      chosen_ = (shape_ == Shape::Offline && focus() == 0) ? Chosen::TryAgain : Chosen::Ok;
      return Action::article();
    case Gesture::Back:
      // Dismissing a failure is not navigation: the shell has a radio to take
      // down and a screen to choose, so this latches like the slabs do.
      chosen_ = Chosen::Ok;
      return Action::article();
    default:
      return Action::none();
  }
}

void WallabagErrorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                 Plane plane) const {
  theme.renderWallabagError(fb, fonts, vm_, plane);
}

// --- the remove-downloads confirmation ---------------------------------------

ArticlesRemoveConfirmScreen::ArticlesRemoveConfirmScreen() : FocusScreen(kRowCount, kRowCount) {
  vm_.hints = {"CANCEL", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  syncVm();
}

void ArticlesRemoveConfirmScreen::syncVm() {
  vm_.title = "REMOVE THE DOWNLOADS?";
  vm_.message =
      "The articles leave the card. Your wallabag still has them, and the next sync brings them "
      "back.";
  vm_.cancelLabel = "CANCEL";
  vm_.confirmLabel = "REMOVE";
  vm_.focusedAction = focus();
}

Action ArticlesRemoveConfirmScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      if (focus() == kRemove) {
        // A LATCH: deleting every article is card work, and where the reader
        // lands afterwards is the shell's -- it replaces the account screen so
        // the counts it states are the counts after the removal.
        chosen_ = Chosen::RemoveAll;
        return Action::article();
      }
      return Action::pop();
    case Gesture::Back:
      return Action::pop();
    default:
      return Action::none();
  }
}

void ArticlesRemoveConfirmScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                         Plane plane) const {
  theme.renderDeleteConfirm(fb, fonts, vm_, plane);
}

}  // namespace reader
