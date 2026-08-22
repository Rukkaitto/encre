#include "reader/screen_delete_confirm.h"

#include "reader/screen_library.h"
#include "reader/text.h"  // upperAscii
#include "reader/theme.h"

namespace reader {

DeleteConfirmScreen::DeleteConfirmScreen(LibraryScreen& library)
    : FocusScreen(kRowCount, kRowCount), library_(library) {
  // The board's caption, with the book's name in it: `DELETE "DUBLINERS"?`, in
  // U+201C/U+201D as the board spells them (&ldquo; / &rdquo;).
  //
  // Composed here rather than in the theme because it is copy, and the theme is
  // layout -- and shouted here for the same reason SdMissing's title is: the
  // caption is a caps label, and the title inside it arrives from a filename.
  //
  // A confirmation that does not NAME the thing is one people learn to dismiss
  // without reading, so this is load-bearing rather than decorative.
  const LibraryItem* item = library.focusedItem();
  const std::string name = item != nullptr ? upperAscii(item->entry.title()) : std::string();
  vm_.title = "DELETE \xE2\x80\x9C" + name + "\xE2\x80\x9D?";
  // The board's own paragraph, verbatim -- and it is a promise the code keeps:
  // nothing here goes near /.reader/state/.
  vm_.message =
      "The file leaves the SD card. Your progress and bookmarks are kept in case it comes back.";
  vm_.cancelLabel = "CANCEL";
  vm_.confirmLabel = "DELETE";
  vm_.hints = {"CANCEL", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  // The base's focus starts on the first row, which is kCancel -- the board's
  // filled slab, and where a destructive prompt's focus belongs: a press made
  // before the user has read anything cancels.
  syncVm();
}

void DeleteConfirmScreen::syncVm() { vm_.focusedAction = focus(); }

Action DeleteConfirmScreen::onEvent(const InputEvent& ev) {
  if (ev.kind != PressKind::Short) return Action::none();

  switch (ev.button) {
    case Button::Down:
      return moveFocus(+1);
    case Button::Up:
      return moveFocus(-1);
    case Button::Back:
      // Back IS cancel here, which is what the board's first hint slot says.
      return Action::pop();
    case Button::Confirm:
      if (vm_.focusedAction == kCancel) return Action::pop();
      // The delete itself, and the rescan that follows it, are the Library's:
      // it owns the path and the list. This screen owns the confirmation.
      //
      // The result is deliberately not branched on. `FileSystem::remove` reports
      // the END STATE, so a false means the file is still there -- and the
      // rescan the Library just did has already told the user which it was, on
      // the list they are about to be looking at. An error panel here would be a
      // screen with no board saying something the Library already shows.
      library_.deleteFocused();
      // Back to the Library, not back one: the actions panel this was opened
      // from acted on a book that no longer exists, so it goes too. One Action,
      // one screen change, however deep the flow was.
      return Action::popTo(ScreenId::Library);
    default:
      return Action::none();
  }
}

void DeleteConfirmScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                 Plane plane) const {
  theme.renderDeleteConfirm(fb, fonts, vm_, plane);
}

}  // namespace reader
