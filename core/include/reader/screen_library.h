#pragma once
#include <string>
#include <vector>

#include "reader/booklist.h"
#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

class FileSystem;
struct ProgressEntry;

// One row the Library holds: what the filesystem knows (BookEntry) plus the
// display fields nothing can derive from a filename.
//
// `author` is still the Phase 3 seam: it lives in the EPUB's OPF, so learning it
// per row would be ~100 ms an archive open and ~20 s for a 203-book library, and
// it is blank on a card today. `progress` is NOT -- it comes from
// `/.reader/state/`, which the Reader has been filling since progress persistence
// landed. The simulator's sample content sets both to the board's own values,
// which is what keeps `make compare` and the goldens testing the RENDERING rather
// than a parser that does not exist yet.
//
// `childBooks` is a folder's own count, for the board's `FOLDER - 6 BOOKS` line,
// and -1 when it was not looked up. It costs one extra directory listing per
// folder at rescan time and nothing after that.
struct LibraryItem {
  BookEntry entry;
  std::string author;
  std::string progress;
  // design/BookDetails.dc.html's fields, which need the same metadata and are
  // therefore blank on a card today. They are held here, beside the row they
  // describe, rather than assembled by the details screen: the details screen has
  // no way to learn them either, and a second place for a book's facts to live is
  // a second place for Phase 3 to have to fill in.
  //
  // Strings rather than numbers because the units are the design's -- `31% - PAGE
  // 78 OF 252` is not a percentage and `AUG 14, 2026` is not a timestamp -- and
  // the screen only ever draws them. A number here would be a promise the theme
  // could format, and nothing can format a page count that does not exist.
  struct Details {
    // The author as the DETAILS board sets it -- sentence case, `James Joyce` --
    // where the Library row's `author` above is the caps run its own board sets,
    // `JAMES JOYCE`. Two fields for one fact, for the same reason `progress`
    // below is two: the two boards state two different runs, and one cannot be
    // derived from the other. Shouting the sentence-case form would need a
    // Unicode case mapping core/ does not carry -- `Charlotte Bronte` with its
    // diaeresis is on the board precisely to keep a non-ASCII glyph in the
    // goldens, and ASCII folding would render it `BRONTe`.
    std::string author;
    // The Progress ROW's value, which is a superset of the Library row's `31%`:
    // the board writes `31% - PAGE 78 OF 252`. Two strings for one fact because
    // the two boards state two different runs, and deriving the long one from the
    // short one is not possible in either direction.
    std::string progress;
    std::string chapter;  // "ARABY"
  } details;
  int childBooks = -1;
};

class LibraryScreen;

// WHO IS HOLDING A POINTER TO THE LIBRARY, so the Library can null it on the way
// out. The one implementer is DemoScreenFactory, which keeps the Library it built
// last so that an overlay can read the row under it (see its library()).
//
// IT IS THE SCREEN THAT NOTIFIES, not whoever destroyed it, and that is the whole
// point of the interface existing. The pointer's safety used to be a RULE written
// in a comment -- "called by whoever destroyed the App that owned it" -- with the
// App-swap sites remembering to call it and the POP path not covered at all, since
// popping the Library destroys it while the App lives on. A rule stated in prose
// is a mechanism not yet written: this one holds for every way a Library can die,
// including one held by nothing but a unique_ptr in a test.
//
// An interface rather than a callback for the reason ScreenFactory and
// SettingsSink are ones: -fno-exceptions makes a std::function's allocation an
// abort() with no diagnostic, and this has to work on the way out of a screen.
class LibraryWatcher {
 public:
  virtual ~LibraryWatcher() = default;
  // `which` is being destroyed NOW. Drop the pointer only if it is the one you
  // hold: a watcher may already have been handed a NEWER Library, and clearing
  // unconditionally would null a live pointer.
  virtual void libraryGone(const LibraryScreen* which) = 0;
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
class LibraryScreen : public FocusScreen {
 public:
  LibraryScreen(FileSystem& fs, std::string root);
  explicit LibraryScreen(std::vector<LibraryItem> sample);
  ~LibraryScreen() override;

  // NEITHER COPYABLE NOR MOVABLE, because an instance registers ITSELF with a
  // watcher: a copy would leave two screens claiming one registration and a move
  // would leave the watcher naming the husk. Same reasoning as FileHandle's, and
  // nothing copies a screen -- the App owns them through unique_ptr.
  LibraryScreen(const LibraryScreen&) = delete;
  LibraryScreen& operator=(const LibraryScreen&) = delete;

  // Tell `w` when this screen is destroyed. Called by whatever kept a pointer to
  // it; a Library nobody watches notifies nobody.
  //
  // AND THE LINK IS TWO-WAY, because otherwise this fixes one dangle by creating
  // its mirror: the screen holds a pointer to the watcher, so a watcher that dies
  // first is the same use-after-free the other way round. `stopWatching` is how a
  // watcher lets go -- on its own destruction, or when it has adopted a newer
  // Library and no longer cares about this one. GUARDED ON WHO IS ASKING, so one
  // watcher cannot cancel another's registration; only one watcher at a time is
  // held, which is all the one implementer needs.
  void watchedBy(LibraryWatcher& w) { watcher_ = &w; }
  void stopWatching(const LibraryWatcher& w) {
    if (watcher_ == &w) watcher_ = nullptr;
  }

  ScreenId id() const override { return ScreenId::Library; }
  // The one screen with a list long enough to need it -- 256 rows at the cap, and
  // a row per press is a minute of pressing.
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const LibraryViewModel& vm() const { return vm_; }

  // How many rows are on glass. Told rather than derived here, because the
  // answer is the theme's box model and this screen has no framebuffer when an
  // event arrives -- see Theme::libraryVisibleRows. Zero is legitimate (a panel
  // with no room for a row): the window goes inert and nothing is drawn.
  void setVisibleRows(int n);
  int visibleRows() const { return window().visibleRows(); }

  // focus()/setFocus() are FocusScreen's -- final, one mechanism. The focus is
  // an index into the WHOLE list, which is what a session record stores -- not
  // the index into the visible slice the view-model carries. -1 is an empty
  // directory; setFocus clamps, so a record written before some books were
  // deleted still restores to a row that exists.
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

  // RE-READS ONLY WHAT READING PROGRESS SAYS, over the rows already here: each
  // row's percentage-or-NEW and the two fields Book details reads from the same
  // entry. False when there is no filesystem, or the index could not be read at
  // all -- in which case the rows are left EXACTLY as they were, because "the card
  // did not answer" is not the same as "nothing has been started".
  //
  // WHY THIS EXISTS: the Reader is pushed ON TOP of the Library, so the pop that
  // leaves a book hands back this same screen with the rows it was built with --
  // and a book just read to 31% still read NEW. That was reported off the device.
  //
  // WHY NOT rescan(): the set of books cannot change while the firmware runs (V1
  // transfers by card, so putting a book there means the card is in a computer),
  // and the save that made this stale has just dropped the listing cache -- so a
  // rescan would pay a fresh `/books` listing, ~600 ms on a 203-book card, plus one
  // listing per folder for the counts, on the critical path of a Back. The only
  // directory that changed is `/.reader/state`.
  //
  // IT MOVES NEITHER THE FOCUS NOR THE WINDOW, which is what makes it safe on that
  // press: it re-derives values over `items_` and never touches the list itself. A
  // rescan would be entitled to move both.
  bool refreshProgress();

  // Removes the focused item's file and rescans. Files only: a folder is not
  // deletable here, because FileSystem::remove is files-only by contract and
  // recursively deleting a directory the user pointed at once is not a V1
  // decision. Reading progress is deliberately untouched (spec 4.0: delete
  // "never erases reading progress") -- a book that comes back should still know
  // where you were.
  bool deleteFocused();

 private:
  // WHAT THE CARD SAYS ABOUT ONE ROW'S PROGRESS: its value, and Book details'
  // Progress and Current chapter. ONE SPELLING, because two callers derive it --
  // rescan() when the directory is read and refreshProgress() when a book has been
  // read while this screen stood underneath the Reader. A second copy of
  // `to_string(percent) + "%"` is how a row and the details screen drift into
  // disagreeing about one number.
  void applyProgress(LibraryItem& item, const std::vector<ProgressEntry>& started) const;
  bool descend();
  bool ascend();
  // Rebuilds the view-model from `items_` and the window. One place, called
  // after every change to either, rather than building it inside render(): a
  // const render would need mutable state to do it, and the vm would then be
  // rebuilt once per paint for a list that had not changed.
  void syncVm() override;
  std::string join(std::string_view leaf) const;

  LibraryWatcher* watcher_ = nullptr;
  FileSystem* fs_ = nullptr;  // null = sample content
  std::string root_;          // where the Library starts; Back at this level pops
  std::string path_;          // what is being listed now
  std::vector<LibraryItem> items_;
  LibraryViewModel vm_;
};

}  // namespace reader
