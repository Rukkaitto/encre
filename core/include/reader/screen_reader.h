#pragma once
#include <memory>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/layout.h"
#include "reader/viewmodel.h"

namespace reader {
class GlyphSource;

// design/Reader.dc.html: the reading page.
//
// IT OWNS A STREAM, NOT A CHAPTER. It used to hold the whole chapter's blocks,
// which is what made a real book unopenable -- Le Fléau's longest chapter is
// 228,849 bytes of them. It holds a ChapterReader instead, which decodes blocks
// from the card as they are wanted and forgets them, so the screen's memory is the
// same for a 1 KB chapter and a 300 KB one.
//
// --- THE PAGE INDEX IS WHAT PAGINATION LEAVES BEHIND -------------------------
//
// One pass over the chapter records the start Cursor of every page -- about 8 bytes
// each, so ~240 bytes for a long chapter -- and discards the lines. That index
// answers the three things a single page cannot:
//
//   * how many pages there are, which the footer's counter needs;
//   * which one this is;
//   * where an earlier one begins.
//
// The pass costs one decode of the chapter, at open. It is the price of being able
// to say "3 / 12" at all.
//
// --- THE INDEX IS BUILT BY READING, NOT BEFORE IT ----------------------------
//
// Paginating the whole chapter before the first page appeared cost ~545 ms on the
// device, which made crossing into a chapter 1.14 s against 575 ms for an ordinary
// page turn -- and a crossing IS a page turn from the reader's side.
//
// So a crossing decodes only as far as page one, and `starts_` grows a cursor at a
// time as pages are passed. Until the chapter's end has been reached the total is
// UNKNOWN, and `ReaderViewModel::pageTotal` is 0, which the footer draws as an em
// dash (design/Reader.dc.html states it).
//
// The full count then happens inside the four-level refinement, which repaints
// anyway -- so the total appears with the upgrade and costs no extra waveform. The
// em dash is therefore visible for one page of each chapter.
//
// --- FORWARD IS FREE; BACKWARD RE-DECODES ------------------------------------
//
// A DEFLATE stream cannot be seeked, and checkpointing one costs 32 KB a
// checkpoint. So the reading position keeps its stream and its PageBuilder alive
// and TURNING FORWARD CONTINUES THEM -- the common case, and it costs one page of
// layout. Going back, or jumping, rewinds and decodes forward to the target: ~100
// to 300 ms on device against a ~520 ms panel refresh.
class ReaderScreen : public Screen {
 public:
  // COUNT A CHAPTER'S PAGES BEFORE THE FIRST PAINT IF IT IS THIS SMALL, and defer
  // otherwise. The number comes from two measurements on this device.
  //
  // Counting costs ~1.79 ms a KB of inflated XHTML (41.1 us/page desktop over 7,968
  // real pages, at this project's ~37x device ratio). So 64 KB is at most ~114 ms --
  // under a fifth of a ~570 ms page turn, and below the run-to-run spread of the
  // render figures themselves.
  //
  // What it buys: over a real book's 92 spine entries, 63% are under 64 KB and get
  // their total the moment the page appears. Median is 53 KB.
  //
  // BOUNDED BY BYTES, NOT BY A PAGE BUDGET, because the bytes are known BEFORE any
  // work is done -- the central directory said so. A page budget would spend the
  // whole budget on a long chapter and then still have no total, which is the worst
  // of both.
  static constexpr uint32_t kEagerCountBytes = 64u * 1024u;

  // A BOOK, not a chapter. `fs` and `body` must outlive the screen.
  //
  // It took a single ChapterLocation and could therefore only ever show one chapter
  // of a book -- which the device found immediately: spine entry 0 of a real EPUB is
  // `Cover.html`, one `<img>` and no body text, so it paginated to NOTHING and the
  // panel showed a blank page reading 0/0. Skipping to the first chapter with text
  // would only have moved the dead end to the bottom of that chapter.
  //
  // The screen is not renderable until setMetrics has been called, exactly as
  // Library is not until setVisibleRows: a page count depends on a column height
  // and onGesture has no framebuffer to ask. Before it, the page is empty and the
  // counter reads 0 -- a readable screen rather than an abort.
  ReaderScreen(FileSystem& fs, OpenedBook book, int startChapter, const GlyphSource* body);

  // A single chapter already in memory, through the same layers minus the inflate.
  // What the simulator and the goldens render, having no card -- and the reason
  // ChapterReader has a buffer entry point at all. There is no book behind it, so
  // paging past either end simply stops.
  ReaderScreen(std::string_view xhtml, std::string bookTitle, std::string chapter,
               const GlyphSource* body);
  ~ReaderScreen() override;

  ScreenId id() const override { return ScreenId::Reader; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // Body text is the one thing on this device drawn from a runtime-rasterised
  // face, and the whole reason ScalableFont packs its cache in fontc.py's 2bpp
  // format is so its edges can carry grey. Mono would throw that away and
  // hard-threshold every stem of a serif face at 32px.
  Fidelity fidelity() const override { return Fidelity::Grayscale; }

  // The column, from Theme::readerMetrics. Builds the page index and renders the
  // first page, so it is the expensive call: one decode of the chapter.
  void setMetrics(const PageMetrics& m);

  const ReaderViewModel& vm() const { return vm_; }
  const Page& page() const { return page_; }
  // PAGES KNOWN, not pages total: the index grows as the chapter is read, so this
  // equals the chapter's page count only once `indexPending()` is false. The view
  // model reports 0 for an unknown total rather than this number, which would count
  // up as the reader advanced.
  int pageCount() const { return static_cast<int>(starts_.size()); }
  int pageIndex() const { return at_; }
  // Which spine entry is open, and how many there are.
  int chapterIndex() const { return chapterAt_; }
  // How many bytes the open chapter inflates to -- what kEagerCountBytes is compared
  // against. Exposed so the shell can report which branch an open actually took;
  // without it the device cannot say, and the eager path has no log line of its own.
  uint32_t chapterBytes() const { return chapter_.sizeBytes(); }
  int chapterCount() const { return book_.chapterCount(); }

  // Whether the chapter's page count is still unknown. The shell completes it inside
  // the refinement; see the class comment.
  bool indexPending() const;
  // Counts the rest of the chapter and returns to the page being read. ~545 ms for a
  // long chapter, so it belongs in a quiet window rather than in a page turn.
  bool completeIndex();

  // Why the chapter stopped being readable, or empty. A card pulled mid-book, or a
  // stream that turned out to be corrupt partway through.
  const char* error() const { return chapter_.error(); }

 private:
  void buildIndex();
  // Opens spine entry `c` and lands on its first page, or its last when `atEnd`.
  //
  // SKIPS CHAPTERS THAT PAGINATE TO NOTHING, continuing in whichever direction it
  // was already going. Three of the 92 spine entries in one real book do -- a cover
  // and two title pages, each an `<img>` and nothing document.h models. A reader
  // that stopped on one would show a blank page and no way off it.
  bool openChapterAt(int c, bool atEnd);
  // The walk itself. Separate so openChapterAt can undo it on failure.
  bool walkToChapter(int c, bool atEnd);
  // Re-establishes a chapter's stream without touching the index or the page, for
  // undoing a walk that failed.
  bool reopenChapter(int c);
  void updateChapterLabel();
  // Renders page `p` by rewinding and decoding forward to it. The general path.
  bool seekTo(int p);
  // Renders the page after the current one by continuing the live stream. The
  // common path, and the reason the builder is kept alive between turns.
  bool advance();
  // Lands on page one of the chapter already begun, without counting the rest of it.
  bool openFirstPage();
  void syncVm();

  ChapterReader chapter_;
  const GlyphSource* body_;
  PageMetrics metrics_{};

  // One cursor per page, in order -- but only as far as has been READ, unless
  // `indexComplete_` says the chapter's end was reached. So `starts_.size()` is
  // "pages known", not "pages total", and only the flag makes it the latter.
  std::vector<Cursor> starts_;
  bool indexComplete_ = false;
  int at_ = 0;

  // The live position: a builder mid-chapter and the index of the next block to
  // feed it. Null when the stream is not positioned for a forward turn.
  std::unique_ptr<PageBuilder> pb_;
  int fed_ = 0;

  Page page_{};
  ReaderViewModel vm_{};
  std::string bookTitle_, chapter_label_;

  // THE WHOLE BOOK'S GEOMETRY, read once. 12 bytes a spine entry, so 1,104 for a
  // 92-chapter book -- against the ~32 KB transient a re-parse of the central
  // directory and the OPF costs, which is what reaching another chapter used to
  // pay. An empty path means the in-memory constructor was used and there is no
  // book to page into.
  FileSystem* fs_ = nullptr;
  OpenedBook book_{};
  int chapterAt_ = 0;
};

}  // namespace reader
