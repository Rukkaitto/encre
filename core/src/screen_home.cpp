#include "reader/screen_home.h"

#include "reader/text.h"
#include "reader/theme.h"

namespace reader {

// design/HomeMissing.dc.html's own sentence, with the board's CURLY quotes --
// U+201C/U+201D, which fontc.py's subset carries along with the dashes, the
// ellipsis and the guillemets, so this is a real glyph and not a notdef box.
//
// THE QUOTES ARE THEIR OWN LITERALS, on this repo's twice-paid rule: a C++ hex
// escape is UNBOUNDED, so `"\x9C"` followed by a hex digit is one escape and not
// two characters -- clang rejects it and the ESP32's GCC accepts it and emits a
// byte that is not the one meant. Nothing here can grow a hex digit after the
// quote, but the split costs nothing and the trap has bitten twice.
//
// SHOUTED, because the board shouts it: this run is metadata about the book at
// `--t-meta` with 0.1em, the same treatment the author line beside it gets, and
// upperLatin1 is what makes an accented title come out `LE FLEAU` rather than
// `LE FLeAU`.
std::string missingBookNote(std::string_view title) {
  return std::string("\xE2\x80\x9C") + upperLatin1(title) + "\xE2\x80\x9D" +
         " IS GONE FROM THE SD CARD.";
}

// WithNone: -1 is the CONTINUE block, a place the user can be, not the absence
// of a selection.
//
// EXCEPT WHERE THE SCREEN DRAWS NO CONTINUE BLOCK: its first hint slot is empty
// because there is nothing to read, so a focus on -1 would be a selection on an
// invisible row with a blank action. Building the ring Noneless is the model
// being right, and it closes both ways in at once -- Up from LIBRARY, which was
// reachable before lists wrapped, and Down off the last menu row, which wrapping
// added.
//
// `offersContinue()` RATHER THAN `!nothingToContinue`, because there are two ways
// to have no block now: nothing to continue at all, and a pointer naming a book
// the card no longer has (design/HomeMissing.dc.html). One predicate, asked here
// and by the Back gesture below and by the theme -- see HomeViewModel.
HomeScreen::HomeScreen(HomeViewModel vm, std::vector<ScreenId> targets)
    : FocusScreen(static_cast<int>(vm.menu.size()), static_cast<int>(vm.menu.size()),
                  vm.offersContinue() ? Focus::WithNone : Focus::Noneless),
      vm_(std::move(vm)),
      targets_(std::move(targets)) {
  // The view-model may arrive with a focus already set -- the goldens author one
  // -- so it is adopted rather than reset; the mirror is then re-asserted
  // unconditionally, because setFocus reports "moved" and an unmoved adoption
  // still has to leave the vm and the window agreeing.
  setFocus(vm_.focusedMenuIndex);
  syncVm();
  // From whatever view model arrived, because Home's hints are built outside the
  // screen (demoHomeVm, homeVmForCard). Home binds no hold today and the empty
  // variant binds none either, so this is zero -- but it is DERIVED from the bar
  // rather than assumed, which is the point: the day a Home hint grows a ring, the
  // binding follows it without anyone remembering to add one.
  declareHints(vm_.holds);
}

// The mirror is the whole of what this screen still does about focus: the range,
// the clamp and the "did anything move" answer are all FocusScreen's.
void HomeScreen::syncVm() { vm_.focusedMenuIndex = focus(); }

Action HomeScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Activate: {
      const int i = vm_.focusedMenuIndex;
      // CONTINUE, which is focus -1. It answers Action::open() -- "open the book I
      // have selected" -- because opening a book is reading a file off the card and
      // storage is not core/'s; the shell resolves WHICH book from the same pointer
      // that filled this reading column in.
      //
      // It cannot fire on a variant that draws no block: those build the focus ring
      // Noneless, so -1 is unreachable there. The model prevents it rather than a
      // guard here, which is why there is no second check.
      if (i < 0) return Action::open();
      if (i >= static_cast<int>(targets_.size())) return Action::none();
      return Action::push(targets_[static_cast<size_t>(i)]);
    }
    // HOME'S BOARD BINDS BACK TO `READ` -- spec 4.1: there is nothing to go back to
    // from the root, so the slot carries the one action worth a shortcut. Same
    // action as CONTINUE, from a button instead of a selection.
    //
    // Gated on the screen drawing a CONTINUE block, because every variant that does
    // not also draws an EMPTY first hint slot: a bar that promises nothing must not
    // do something, and the converse -- a bar promising READ over a book that is
    // gone -- is the missing-book state's own version of it. The shell would resolve
    // it from the same pointer, find the same missing file and paint nothing.
    case Gesture::Back:
      return vm_.offersContinue() ? Action::open() : Action::none();
    default:
      return Action::none();
  }
}

void HomeScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderHome(fb, fonts, vm_, plane);
}

void HomeScreen::setBattery(int percent, bool charging) {
  vm_.batteryPercent = percent;
  vm_.batteryCharging = charging;
}

}  // namespace reader
