#pragma once
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/booklist.h"
#include "reader/scrollwindow.h"
#include "reader/viewmodel.h"

namespace reader {

class FileSystem;

// One row the Library holds: what the filesystem knows (BookEntry) plus the
// display fields nothing can derive from a filename.
//
// `author` and `progress` are the Phase 3 seam. An author needs the EPUB's zip
// container and its OPF; a real percentage needs `/.reader/state/`, which has
// nothing to record until the Reader exists. So on the device today the author
// is blank and every book reads NEW -- and the simulator's sample content sets
// both to the board's own values, which is what keeps `make compare` and the
// goldens testing the RENDERING rather than a parser that does not exist yet.
//
// `childBooks` is a folder's own count, for the board's `FOLDER - 6 BOOKS` line,
// and -1 when it was not looked up. It costs one extra directory listing per
// folder at rescan time and nothing after that.
struct LibraryItem {
  BookEntry entry;
  std::string author;
  std::string progress;
  int childBooks = -1;
};

// The Library (spec 4.1), from design/Library.dc.html.
//
// Movement is ScrollWindow's -- focus plus first-visible, clamped, scrolling by
// the overflow rather than by a page -- and this screen is what turns the window
// into the slice of rows the theme draws. It never hands the theme the whole
// directory: `vm_.rows` is exactly what is on glass.
//
// Two ways to build one, and the difference is whether there is a card:
//
//   over a FileSystem -- the device, and the simulator's `--root`. rescan()
//     reads the directory, Confirm descends into a folder, and a delete removes
//     a real file.
//   over sample items -- the design comparison and the goldens. There is no
//     filesystem, so a rescan keeps what it was given and a delete refuses. The
//     board's own content is what makes a golden a test of the rendering.
class LibraryScreen : public Screen {
 public:
  LibraryScreen(FileSystem& fs, std::string root);
  explicit LibraryScreen(std::vector<LibraryItem> sample);

  ScreenId id() const override { return ScreenId::Library; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const LibraryViewModel& vm() const { return vm_; }

  // How many rows are on glass. Told rather than derived here, because the
  // answer is the theme's box model and this screen has no framebuffer when an
  // event arrives -- see Theme::libraryVisibleRows. Zero is legitimate (a panel
  // with no room for a row): the window goes inert and nothing is drawn.
  void setVisibleRows(int n);
  int visibleRows() const { return window_.visibleRows(); }

  // The focus as an index into the WHOLE list, which is what a session record
  // stores -- not the index into the visible slice the view-model carries.
  int focus() const { return window_.focus(); }
  bool setFocus(int index);
  int itemCount() const { return static_cast<int>(items_.size()); }

  // The item the focus is on, or null for an empty list. This is what the
  // actions overlay acts on, and it is read through the parent screen rather
  // than copied into the overlay so that a rescan under an open overlay cannot
  // leave the two disagreeing about which book is which.
  const LibraryItem* focusedItem() const;

  // The directory being listed, so an overlay can address the focused file. It
  // is the root for a Library that has not descended into anything.
  const std::string& path() const { return path_; }

  // Re-reads the directory and pulls the focus back into range. False when there
  // is no filesystem or the directory could not be read -- in which case the list
  // is EMPTY rather than stale, because BookList::scan clears its output either
  // way and a screen showing the previous card's books would be worse.
  bool rescan();

  // Removes the focused item's file and rescans. Files only: a folder is not
  // deletable here, because FileSystem::remove is files-only by contract and
  // recursively deleting a directory the user pointed at once is not a V1
  // decision. Reading progress is deliberately untouched (spec 4.0: delete
  // "never erases reading progress") -- a book that comes back should still know
  // where you were.
  bool deleteFocused();

 private:
  Action moveFocus(int delta);
  bool descend();
  bool ascend();
  // Rebuilds the view-model from `items_` and the window. One place, called
  // after every change to either, rather than building it inside render(): a
  // const render would need mutable state to do it, and the vm would then be
  // rebuilt once per paint for a list that had not changed.
  void syncVm();
  std::string join(std::string_view leaf) const;

  FileSystem* fs_ = nullptr;  // null = sample content
  std::string root_;          // where the Library starts; Back at this level pops
  std::string path_;          // what is being listed now
  std::vector<LibraryItem> items_;
  ScrollWindow window_;
  LibraryViewModel vm_;
};

}  // namespace reader
