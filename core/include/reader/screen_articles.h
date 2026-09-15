#pragma once
#include <string>
#include <vector>

#include "reader/filesystem.h"
#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// One article as this screen holds it -- the sidecar's fields, plus the id the
// server knows it by.
//
// `id` IS THE SERVER'S INTEGER and it is what every outcome carries: the shell
// archives, stars and opens by it, and the store names both of an article's two
// files with it. Nothing here addresses a FILE, which is LibraryRow's rule one
// list over: a path is storage's, and a view-model that carried one would be the
// seam through which layout learned about the card.
struct ArticleItem {
  int id = 0;
  std::string title;
  std::string domain;
  int readingMinutes = 0;
  // A reading sidecar EXISTS for it -- the article has been started. Drives the
  // bullet's absence.
  bool opened = false;
  // ...and that sidecar says the reader reached the end. Drives `. READ`.
  bool finished = false;
  bool starred = false;  // the overlay's second row reads `Unstar` when true

  // So a reload can say whether anything MOVED, which is what decides a
  // waveform. Every field, because any of them changes a row on the glass.
  bool operator==(const ArticleItem& o) const {
    return id == o.id && title == o.title && domain == o.domain &&
           readingMinutes == o.readingMinutes && opened == o.opened &&
           finished == o.finished && starred == o.starred;
  }
  bool operator!=(const ArticleItem& o) const { return !(*this == o); }
};

// design/Articles.dc.html, with design/ArticlesSetup.dc.html and
// design/SyncDone.dc.html as VARIANTS of it -- one ScreenId, one view-model, one
// renderer. HomeEmpty's rule.
//
// THE FOCUS RING HAS ONE MORE POSITION THAN THE LIST, AND IT IS -1. The board
// fixes the `Sync now` row between the band and the list, so it does not scroll
// and cannot be a member of the ScrollWindow that does. `Focus::WithNone` already
// has a position outside the list for exactly this, and Home spends it the same
// way on its CONTINUE block -- the only difference being that Home's sits BELOW
// the first item and this one sits above it, which is a fact about where the
// theme draws it rather than about the ring.
//
// Two ways to build one, and the difference is whether the card has credentials:
//
//   with rows and a stamp -- a configured device. The list, the sync row, and
//     `N UNREAD` in the band. An EMPTY row list is still this one: a reader who
//     has just filled the file in and pressed nothing must not be told to go and
//     fill the file in.
//   with neither -- no /.reader/wallabag.json, or one with a value missing. The
//     list and the sync row are replaced by a centred block, and every gesture
//     but Back is refused, because there is nothing to sync and nothing to read.
class ArticlesScreen : public FocusScreen {
 public:
  // WHAT THE LAST PRESS ASKED FOR, read by the shell off the screen still on top
  // -- Action::article() pops nothing precisely so there is something to ask.
  enum class Chosen { None, Sync };

  // The configured screen. `stamp` is the sync row's whole right-hand run,
  // composed by the caller: it is an OUTCOME and never an age, and the four
  // values are design/Articles.dc.html's.
  ArticlesScreen(std::vector<ArticleItem> items, std::string stamp);
  // The not-set-up variant.
  ArticlesScreen();
  // OVER A FileSystem -- the device, and the simulator's card. Which of the two
  // above it becomes is read off the CARD: no credentials (absent, or a value
  // missing) is the not-set-up variant, and anything else is the list, EMPTY OR
  // NOT. A reader who has just filled the file in must not be told to go and
  // fill the file in.
  //
  // LibraryScreen's two-constructor shape, and for its reason: the goldens and
  // the comparison sheet need a screen built from the BOARD's content, which is
  // what makes a golden a test of the rendering.
  explicit ArticlesScreen(FileSystem& fs);

  ScreenId id() const override { return ScreenId::Articles; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const ArticlesViewModel& vm() const { return vm_; }

  void setVisibleRows(int n);
  int visibleRows() const { return window().visibleRows(); }
  int itemCount() const { return static_cast<int>(items_.size()); }

  // The focused ARTICLE, or nullptr on the sync row and on the not-set-up
  // variant. Both callers -- the shell's open and the actions overlay's Facts --
  // have to handle the null, which is what keeps a press on the sync row from
  // acting on whichever article happened to be first.
  const ArticleItem* focusedItem() const;
  int focusedId() const;
  std::string focusedTitle() const;

  Chosen chosen() const { return chosen_; }

  // design/SyncDone.dc.html's status block and the stamp it comes with. Set
  // together by the shell when a sync lands; the block is the ONE place a push
  // count is stated, so a list reached any other way must not carry one.
  void setStamp(std::string stamp);
  void setStatusLine(std::string line);

  // Re-read the directory. LibraryScreen's pair, and the same division of
  // labour: `rescan` is for when the FILES changed (a sync landed, an archive
  // removed one) and `refreshProgress` for when only /.reader/state did (the
  // reader finished one), which costs no listing of the articles directory.
  //
  // Both are no-ops without a filesystem, so a screen built from fixtures is
  // unaffected -- the goldens' content cannot be washed away by a rescan.
  bool rescan();
  bool refreshProgress();

 protected:
  void syncVm() override;

 private:
  // Read the card into `items_` and the stamp, and answer whether anything
  // changed -- so a caller can decide whether to spend a waveform.
  bool load();

  std::vector<ArticleItem> items_;
  ArticlesViewModel vm_;
  FileSystem* fs_ = nullptr;
  Chosen chosen_ = Chosen::None;
};

}  // namespace reader
