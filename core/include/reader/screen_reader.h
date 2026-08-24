#pragma once
#include <memory>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/layout.h"
#include "reader/return_anchor.h"
#include "reader/toc.h"
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
  // otherwise. THE NUMBER IS MEASURED ON THE PANEL, and the first version of it was
  // not -- it was 64 KB, derived from a desktop figure times a remembered ratio, and
  // the device then priced a 33 KB chapter at 484 ms where that model predicted 60.
  //
  // The desktop figure was right: 41.1 us/page, and this chapter's count pass really
  // is 1.7-1.9 ms there. THE RATIO WAS WRONG. 37x came from a render measurement, and
  // this path is not render-bound -- it is SD reads through SdFat on the display's SPI
  // bus, plus an inflate on a part with no FPU, neither of which the desktop does at
  // all. Against 3.5 ms of desktop work for two passes the device spent 484 ms: ~135x.
  //
  // So the constant is stated in device milliseconds per KB, from the panel:
  //
  //   484 ms / 32.7 KB = 14.8 ms/KB for the TWO-pass eager open (count, then seek
  //   back to page one). That is ~7.2 ms/KB a pass, which independently matches the
  //   ~545 ms this project measured counting a long chapter.
  //
  // At that price 64 KB is 932 ms -- 163% of a ~570 ms page turn, so the eager count
  // cost MORE than the turn it was meant to hide inside. That is the 1.14 s crossing
  // this whole design exists to avoid, reintroduced at a smaller size.
  //
  // 8 KB is 118 ms, ~21% of a turn, which is the budget the threshold was always
  // supposed to buy. It covers 14% of a real book's 92 chapters -- the front matter a
  // reader lands on when they open the book, where a six-page chapter reading "1 / -"
  // looks like a defect. The median chapter is 53 KB and gets the dash, as designed.
  //
  // TWO PASSES IS INHERENT, not slop. One pass ends at the chapter's END, and a
  // forward turn needs the builder live just after page one -- so the content and the
  // stream position cannot both come from the same walk. Counting on a second
  // ChapterReader would buy one pass for another 32 KB window, against a 45,840-byte
  // heap floor.
  //
  // BOUNDED BY BYTES, NOT BY A PAGE BUDGET, because the bytes are known BEFORE any
  // work is done -- the central directory said so. A page budget would spend the
  // whole budget on a long chapter and then still have no total, which is the worst
  // of both.
  static constexpr uint32_t kEagerCountBytes = 8u * 1024u;

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

  // MUST BE SET BEFORE THE BOOK IS OPENED. It goes into `metrics_`, which the page
  // builder reads at `add()` time, so a face arriving after the first page was laid
  // would measure that page roman and draw it italic.
  // The anchor, for the shell to persist and for a test to inspect. Const access
  // only: every transition belongs to a movement, and a caller that could set it
  // directly is a second place that decides the rule.
  const ReturnAnchor& anchor() const { return anchor_; }
  // A RESTORED anchor, from the sidecar. Not a transition -- the record already holds
  // the result of one -- so this is the one path that sets it without a movement, and
  // the only reason `anchor_` is not otherwise writable from outside.
  void restoreAnchor(const AnchorPos& a) {
    anchor_.set(a);
    syncAnchorLabel();
  }
  // Where the reader is, as the anchor spells a page.
  AnchorPos here() const;

  void setItalic(const GlyphSource* italic) {
    italic_ = italic;
    metrics_.italic = italic;
  }

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

  // WHERE THE READER IS, as the one thing worth saving: the start cursor of the page
  // on screen. A page INDEX would be the obvious thing to store and it is the wrong
  // thing -- page 7 is page 7 only at one type size and one column width, where a
  // cursor names a block and a line of the document itself. See reading_position.h,
  // which grades exactly that difference.
  //
  // {0, 0} when nothing is open, which is also the top of a chapter -- the caller
  // cannot tell those apart from here and does not need to, because both mean "start
  // this chapter at its beginning".
  Cursor currentCursor() const;

  // THE BOOK'S CHAPTER NAMES, so the header can say `LIVRE I` instead of `CH. 08`.
  //
  // The spine gives an ORDER and no names, which is why this screen composed its label
  // from a position for two phases. `toc.h` supplies the names; a book with none, or a
  // chapter its contents does not mention, still falls back to the position -- one slot,
  // the best name available for it.
  //
  // A COPY, and it costs ~1.2 KB for a 96-entry book (measured: 1,161 bytes of labels).
  // The alternative is a reference into something the shell owns for exactly as long as
  // the screen, which is a lifetime rule to enforce across a chapter crossing for a
  // saving smaller than one page of laid-out text.
  void setChapterNames(std::vector<TocEntry> toc);

  // LAND HERE WHEN THE METRICS ARRIVE, instead of on page one. Restoring a saved
  // reading position is the only caller.
  //
  // MUST BE CALLED BEFORE setMetrics, because setMetrics is the landing -- a
  // constructor cannot do it (there is no column height yet) and after the landing
  // it would be too late. The cursor is SPENT by the first chapter the walk lands
  // on, so a spine entry reached by skipping an empty one gets page one.
  void restoreAt(Cursor at) { startAt_ = at; }
  // How many bytes the open chapter inflates to -- what kEagerCountBytes is compared
  // against. Exposed so the shell can report which branch an open actually took;
  // without it the device cannot say, and the eager path has no log line of its own.
  uint32_t chapterBytes() const { return chapter_.sizeBytes(); }
  int chapterCount() const { return book_.chapterCount(); }

  // JUMP TO A SPINE ENTRY, for the table of contents. False leaves the screen exactly
  // where it was -- `openChapterAt` restores the previous chapter on failure, which is
  // what makes a refused jump safe rather than a blank page with a stale index.
  //
  // Lands on page ONE of the target, not on a saved position: a reader who picked a
  // chapter from a list asked for its beginning. Skipping an entry that paginates to
  // nothing is openChapterAt's own behaviour and is right here too -- a cover selected
  // from the contents lands on the first thing with text rather than on a blank page.
  bool goToChapter(int spine);

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
  // Lands on the page CONTAINING `want`, recording every page boundary up to it.
  //
  // That walk is what makes the footer able to say which page this is: a cursor does
  // not carry a page number, and the number is a count of the boundaries before it.
  // It stops at the target rather than counting the whole chapter, so a restore
  // costs a walk to the page being restored and not to the end of the book's longest
  // chapter -- and the total then arrives in the quiet window exactly as it does for
  // any other chapter.
  bool openAtCursor(Cursor want);
  void syncVm();

  ChapterReader chapter_;
  const GlyphSource* body_;
  // The italic face, or null. Held beside `body_` and pushed into `metrics_` so the
  // WRAP measures with it -- setting it after a page has been laid would leave the
  // layout measured in one face and drawn in two, which is the disagreement
  // StyledFace exists to prevent.
  const GlyphSource* italic_ = nullptr;
  // WHERE THE READER WAS BEFORE THEY STOPPED READING LINEARLY. The rule is in
  // return_anchor.h and is tested without a book; this screen only tells it which
  // of the three movements just happened.
  ReturnAnchor anchor_;
  bool goToAnchor(const AnchorPos& to);
  void syncAnchorLabel();
  // Each applies one transition and re-syncs the footer label -- see the note in
  // screen_reader.cpp for the ordering bug that made that one call rather than four.
  void anchorPagedForward(const AnchorPos& from);
  void anchorPagedBackward(const AnchorPos& from);
  void anchorJumped(const AnchorPos& from);
  PageMetrics metrics_{};

  // One cursor per page, in order -- but only as far as has been READ, unless
  // `indexComplete_` says the chapter's end was reached. So `starts_.size()` is
  // "pages known", not "pages total", and only the flag makes it the latter.
  std::vector<Cursor> starts_;
  bool indexComplete_ = false;
  int at_ = 0;

  // A restore target waiting for setMetrics, and cleared the moment it is used.
  // Cursor{} means "no target", which is indistinguishable from "the top of the
  // chapter" and correctly takes the cheaper path for both.
  Cursor startAt_{};

  // The live position: a builder mid-chapter and the index of the next block to
  // feed it. Null when the stream is not positioned for a forward turn.
  std::unique_ptr<PageBuilder> pb_;
  int fed_ = 0;

  Page page_{};
  ReaderViewModel vm_{};
  std::string bookTitle_, chapter_label_;
  std::vector<TocEntry> names_;

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
