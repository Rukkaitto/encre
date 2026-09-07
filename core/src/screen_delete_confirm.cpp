#include "reader/screen_delete_confirm.h"

#include <utility>  // std::move -- libc++ pulls it in via <string> and libstdc++ does not

#include "reader/text.h"  // upperLatin1
#include "reader/theme.h"

namespace reader {

DeleteConfirmScreen::DeleteConfirmScreen(Facts facts)
    : FocusScreen(kRowCount, kRowCount), facts_(std::move(facts)) {
  // The board's caption, with the book's name in it: `DELETE "DUBLINERS"?`, in
  // U+201C/U+201D as the board spells them (&ldquo; / &rdquo;).
  //
  // Composed here rather than in the theme because it is copy, and the theme is
  // layout -- and shouted here for the same reason SdMissing's title is: the
  // caption is a caps label, and the title inside it arrives from a filename.
  //
  // A confirmation that does not NAME the thing is one people learn to dismiss
  // without reading, so this is load-bearing rather than decorative.
  const std::string name = upperLatin1(facts_.displayName);
  vm_.title = "DELETE \xE2\x80\x9C" + name + "\xE2\x80\x9D?";
  // The board's own paragraph, verbatim -- and it is a promise the code keeps:
  // nothing here goes near /.reader/state/.
  vm_.message =
      "The file leaves the SD card. Your progress and bookmarks are kept in case it comes back.";
  vm_.cancelLabel = "CANCEL";
  vm_.confirmLabel = "DELETE";
  vm_.hints = {"CANCEL", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // The base's focus starts on the first row, which is kCancel -- the board's
  // filled slab, and where a destructive prompt's focus belongs: a press made
  // before the user has read anything cancels.
  syncVm();
}

void DeleteConfirmScreen::syncVm() { vm_.focusedAction = focus(); }

Action DeleteConfirmScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Back:
      // Back IS cancel here, which is what the board's first hint slot says.
      return Action::pop();
    case Gesture::Activate:
      if (vm_.focusedAction == kCancel) return Action::pop();
      // LATCHED, not done here. The removal's consequences are all the shell's --
      // forgetCardFacts, the Library's rescan, gHomeStale and gLibraryStale -- and
      // core/ has no filesystem. The shell reads the path off this screen while it
      // is still on top, then pops to `facts_.returnTo`.
      //
      // The result is deliberately not branched on: FileSystem::remove reports the
      // END STATE, so a false means the file is still there and the list the reader
      // lands on already says so.
      //
      // THIS RETURNS NO POP, AND THAT IS NOT AN OMISSION. The pop is the shell's,
      // with the same popTo(facts().returnTo) it reads the path from -- because
      // where a completed delete lands depends on how the confirmation was reached,
      // and the actions panel this may have been opened from must go too. Until the
      // shell wires that, confirming leaves this panel standing.
      return Action::del();
    default:
      return Action::none();
  }
}

void DeleteConfirmScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                 Plane plane) const {
  theme.renderDeleteConfirm(fb, fonts, vm_, plane);
}

}  // namespace reader
