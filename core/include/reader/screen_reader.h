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
// layout. Going back, or jumping, rewinds and decodes forward to the target.
//
// --- ...AND A RING OF THE PAGES ALREADY LAID OUT -----------------------------
//
// THAT RE-DECODE WAS MEASURED AT ~376 ms ON THE DEVICE, against 20-33 ms for a
// forward turn -- so paging back cost 1055 ms end to end against 634 ms forward.
// This file used to dismiss it as "33.9 ms desktop against a ~520 ms panel
// refresh", which is the ~37x render ratio applied to a path that is not
// render-bound: the rewind is SdFat reads on the display's SPI bus plus an inflate
// on a part with no FPU, and the real desktop-to-device ratio there is ~11x. The
// same mistake this project made once already with the eager page count.
//
// So the last few pages LAID OUT are kept, keyed on the chapter and the page's own
// start Cursor, and the dominant pattern -- turning back to the page you just came
// from -- needs no decode at all. Two things about it that are not obvious:
//
//   * A HIT LEAVES NO LIVE BUILDER, because nothing was decoded. That is the same
//     state a spent stream leaves and Gesture::Next already handles it, so the
//     forward turn after a run of cached ones re-establishes the stream then. The
//     TOTAL cost of back-then-forward is therefore unchanged; what changes is where
//     it falls -- the presses the reader makes in a burst become instant, and the
//     decode is paid once when they read on past what is cached.
//   * A LIVE BUILDER STILL WINS over a cache hit on a forward turn (see
//     Gesture::Next), because advancing the stream is ~20 ms and cheaper than the
//     decode the reset would eventually cost.
//
// The pages are COPIES, and they have to be: LaidLine::text is owned exactly so a
// Page can outlive the blocks it was laid from, and a cache of views would resurrect
// the lifetime bug that rule exists to prevent. Measured cost is in
// kPageCacheDepth's comment.
class ReaderScreen : public Screen {
 public:
  // A PREDICATE THE PAGE COUNT ASKS AS IT WALKS, so that counting a chapter can be
  // GIVEN UP ON rather than blocking the main loop for seconds.
  //
  // The device reported `[index] pages=315 in 3605ms`, and for the whole of those
  // 3.6 s no button did anything: two presses in one run waited 1304 ms and 1962 ms
  // to be looked at, one of which then drew nothing at all. An ordinary chrome
  // interaction on this device is 505-550 ms, so the count was an order of magnitude
  // worse than everything it sat between.
  //
  // A FUNCTION POINTER PLUS A CONTEXT, not std::function: this is -fno-exceptions
  // embedded code and std::function allocates. And a predicate rather than a
  // millisecond budget, because `core/` has no clock and must not acquire one -- the
  // shell's answer is "is there a raw input sample queued", which is a better
  // question than any deadline anyway.
  using StopFn = bool (*)(void*);

  // HOW MANY RECENTLY LAID-OUT PAGES ARE KEPT. See the class comment for what the
  // ring buys; this is what it costs.
  //
  // MEASURED, not guessed -- test_page_cache.cpp sums the heap a Page's lines really
  // hold (every LaidLine's string capacity and emphasis vector, plus the line vector
  // itself) over every page of a chapter at BOTH panel geometries, and asserts a
  // ceiling so the figure cannot drift:
  //
  //   X4, 444px column: 1,471 bytes a page.  X3, 492px: 1,512.
  //   Three of them: 4,536 bytes.
  //
  // THE FLOOR IT IS SPENT AGAINST IS 42,152 BYTES -- minimum free heap on the device
  // with a book open -- so this is 10.8% of the margin, spent on the one page turn
  // that is 18x slower than its counterpart.
  //
  // WHY THREE AND NOT ONE: the page CURRENTLY ON GLASS takes a slot, because it is
  // stored as it is produced -- which is what makes the page count's restore leg free
  // as well (see completeIndex). So depth 3 holds the current page plus TWO behind
  // it, which is the "turning back to the page you just left, twice" that a reader
  // skimming back actually does. Depth 2 would hold only one page back.
  //
  // WHY NOT MORE: the cost is linear and the value is not -- a third page back is
  // rare -- and what has to fit beside it is a 32 KB inflate window plus a page turn's
  // own transient (a block copy of up to 4,406 bytes, a wrap, and the new Page).
  // Raising it is one constant and the test above prices it.
  static constexpr int kPageCacheDepth = 3;

  // ...AND THE CEILING WHEN THE HEAP CAN AFFORD MORE. The depth above is the safe
  // default, sized for the worst floor this device has: 42,152 bytes free, which is
  // what a Reader opened THROUGH THE LIBRARY leaves, because 203 books sit resident
  // underneath it at ~59 KB. Come in through Home's CONTINUE instead and the same
  // book leaves 76,476 -- a 34 KB difference that depends on nothing but which
  // button was pressed.
  //
  // A fixed depth has to be sized for the worse case, so it is the shell that sets
  // this: it is the only layer that can ask the allocator, and it already knows
  // whether the Library is on the stack. core/ takes a number and never a policy.
  static constexpr int kPageCacheMaxDepth = 8;

  // How many pages the ring may hold. Clamped into [1, kPageCacheMaxDepth]; the
  // excess is dropped immediately rather than at the next insertion, so shrinking
  // gives the heap back at the moment the caller asked for it.
  void setPageCacheDepth(int pages);
  int pageCacheDepth() const { return pageCacheDepth_; }

  // How many pages BELOW the current one the ring holds without a gap. This is the
  // number of backward turns that will not touch the card, and it is what the idle
  // warm below is gated on -- a rewind is worth doing early only when the headroom
  // it would restore has actually been spent.
  int backwardHeadroom() const;

  // REFILL THE RING WHILE NOBODY IS WAITING. The reader's one remaining slow
  // interaction is a backward turn that misses: it rewinds and decodes from the
  // chapter start, which costs what page you are ON -- ~1010 ms at page 99 of a
  // 248 KB chapter, and ~3 s deep in one. Nothing can make that cheaper, because a
  // DEFLATE stream cannot be seeked and a second one is a 32 KB window against a
  // 42 KB floor. What it CAN do is happen when the user is reading rather than when
  // they have just pressed a button.
  //
  // So this is the same rewind, taken early: it walks to the current page, caching
  // the depth's worth of pages that end there, and leaves the builder live exactly
  // where it found it. Nothing visible changes -- `page_` and `at_` are untouched
  // on every path, including the abandoned one.
  //
  // Returns false when there was nothing to do, when the walk was abandoned, or
  // when it could not run. `stop` is the same shape completeIndex takes and for the
  // same reason: the shell answers it from the input queue, so a press interrupts
  // the warm at the next block rather than seconds later.
  //
  // THE ONE COST OF ABANDONING is the live builder, which the rewind spends and
  // cannot rebuild HERE -- so the next FORWARD turn pays a seekTo unless a later
  // quiet window has put it back. That is what restreamAtCurrentPage below is for,
  // and it is why this one still wants a long window where that one does not.
  bool warmPageRing(StopFn stop = nullptr, void* ctx = nullptr);

  // IS THERE A STREAM POSITIONED AFTER THE PAGE ON SCREEN. A forward turn with one
  // is ~20 ms; without one it is a full rewind, ~376 ms at page 38 and ~1010 ms at
  // page 99. It is the shell's gate for the restream below, and it is how a test
  // observes the property directly rather than inferring it from a decode count.
  bool hasLiveStream() const { return pb_ != nullptr; }

  // PUT BACK THE STREAM A QUIET-WINDOW WALK SPENT, ALSO IN A QUIET WINDOW.
  //
  // Three things rewind the ChapterReader the builder reads from, so all three drop
  // it: completeIndex, warmPageRing, and a backward turn's seekTo. Until this
  // existed none of them could put it back, and the next FORWARD turn paid the
  // rewind -- on the press, where the user is waiting.
  //
  // AND IT IS NOT ONLY THE ABANDONED COUNT THAT DOES THIS, which is what the shell's
  // kCountQuietMs comment had wrong. completeIndex ends in seekTo(at_); `at_` is by
  // definition the page the ring is most certain to hold, so the restore leg takes
  // the cache hit -- and a hit leaves `pb_` null, deliberately, because nothing was
  // decoded. So a count that COMPLETES spends the stream too, and that is the common
  // path: every deferred chapter, every time its total lands.
  //
  // WHY THIS DOES NOT MERELY MOVE THE COST. The rewind is the same length either
  // way. What changes is that ABANDONING THIS ONE IS FREE, and it is the only one of
  // the three of which that is true: it runs only when `pb_` is ALREADY null, so it
  // has no live builder to spend and an interrupted walk leaves exactly the state it
  // found -- builder still null, ring unchanged or richer, nothing visible moved. So
  // the trade is one-sided rather than balanced:
  //
  //   * it finishes  -> the next forward turn is free;
  //   * it is cut    -> the next forward turn pays what it pays today.
  //
  // There is no third case, which is what lets it run on a SHORT window where
  // completeIndex and warmPageRing need a long one. Both of those spend something
  // real before they walk; this one cannot.
  //
  // It is the same walk warmPageRing makes -- one private rewalk with two gates,
  // not two copies of it -- so a restream that lands also leaves the backward
  // headroom a warm would have left, and the warm after it correctly finds nothing
  // to do.
  //
  // Returns false when the stream already stands (a live builder beats
  // re-establishing one and must never be thrown away for this), when there is no
  // page to walk to, or when the walk was abandoned or failed.
  bool restreamAtCurrentPage(StopFn stop = nullptr, void* ctx = nullptr);

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
  // The anchor -- the high-water mark of this reading -- for the shell to persist and
  // for a test to inspect. Const access only: the one transition belongs to a
  // movement, and a caller that could raise it directly is a second place that decides
  // the rule. Ask `anchor().aheadOf(here())` for "is there a way back"; `isSet()` is
  // for persistence and says only that a mark exists.
  const ReturnAnchor& anchor() const { return anchor_; }
  // A RESTORED anchor, from the sidecar. Not a transition -- the record already holds
  // a mark -- so this is the one path that sets it without a movement, and the only
  // reason `anchor_` is not otherwise writable from outside. It runs BEFORE the
  // landing (the factory calls it ahead of setMetrics), so a record behind where the
  // book reopens is raised by the landing's own note().
  void restoreAnchor(const AnchorPos& a) {
    anchor_.set(a);
    syncAnchorLabel();
  }
  // Where the reader is, as the anchor spells a page.
  AnchorPos here() const;

  void setItalic(const GlyphSource* italic) {
    italic_ = italic;
    metrics_.italic = italic;
    // A second face changes what every line MEASURES, so every page already laid out
    // was laid at a different geometry. See dropPageRing.
    dropPageRing();
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

  // RE-PAGINATE AT THE PAGE THE READER IS ON, for a type or column change.
  //
  // setMetrics cannot serve: on the card path it re-opens the chapter and lands on
  // PAGE ONE, which is not what a reader who changed their type size asked for. This
  // captures where they are first, applies the metrics, and walks back to it.
  //
  // IT LANDS AT THE TOP OF THE BLOCK, dropping the cursor's LINE. A line index is a
  // line within a block at one ppem and one column width, so after a re-layout it
  // names a layout that no longer exists -- reading_position.h grades exactly this as
  // `Relaid` and zeroes the same field for the same reason. Landing on line 9 of a
  // block that now has four lines is a wrong page that looks like a rendering bug.
  //
  // The page ring goes, because every page in it was measured against the old column
  // and face. So does the index, which is rebuilt by the walk -- so the total returns
  // to UNKNOWN and the footer draws its em dash until the deferred count lands, which
  // is what design/Typography.dc.html's footnote promises when it says the book
  // re-paginates in the background.
  //
  // THE FACE IS THE CALLER'S TO RE-INIT, and it is not an argument here: `metrics_`
  // carries no ppem, the line height comes from the GlyphSource this screen was
  // handed, and a ScalableFont is pinned to one pixel size by init(). So the shell
  // re-inits the face in place and then calls this -- two halves of one press, and
  // this is the half that has to know where the reader was.
  //
  // COSTS ONE WALK to the reader's page, which is a chapter crossing's cost rather
  // than a page turn's. That is the honest price and it is paid on the press that
  // LEAVES the panel, where the user is already expecting the screen to change.
  void relayout(const PageMetrics& m);

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

  // THE BOOK THIS SCREEN IS READING, for the one caller that has to ask a question
  // about the whole book rather than about the open chapter: progressPercent, which
  // sums every chapter's uncompressedSize. The peek's band composes a percentage that
  // has to follow the chapter it is showing, and the peek owns one of these -- so
  // without this it would need a second copy of the spans it is already holding.
  //
  // EMPTY FOR THE IN-MEMORY CONSTRUCTOR, which is how a caller tells the two apart:
  // `chapterCount() == 0` means there is no book to ask, and progressPercent answers
  // 0 for one. A reference, so nothing is copied; it lives as long as this screen.
  const OpenedBook& book() const { return book_; }

  // JUMP TO A SPINE ENTRY, for the table of contents. False leaves the screen exactly
  // where it was -- `openChapterAt` restores the previous chapter on failure, which is
  // what makes a refused jump safe rather than a blank page with a stale index.
  //
  // Lands on page ONE of the target, not on a saved position: a reader who picked a
  // chapter from a list asked for its beginning. Skipping an entry that paginates to
  // nothing is openChapterAt's own behaviour and is right here too -- a cover selected
  // from the contents lands on the first thing with text rather than on a blank page.
  bool goToChapter(int spine);

  // --- JUMP TO A POSITION, NOT TO A CHAPTER ---------------------------------
  //
  // WHAT `GO HERE` COMMITS. The three jumps this screen has are deliberately distinct:
  //
  //   goToChapter(spine)      -- page ONE of a spine entry. A chapter picked from a
  //                              list asked for its beginning.
  //   goToAnchor(pos)         -- a cursor: back to the high-water mark.
  //   goToPosition(spine, at) -- a cursor. The reader may have paged several pages
  //                              into the peek before committing, so page one is the
  //                              wrong landing.
  //
  // NONE OF THE THREE TOUCHES THE ANCHOR, and that is the collapse: each lands through
  // syncVm(), which raises the mark if the landing is further through the book than
  // anything before it. A jump forward therefore raises it and a jump back does not,
  // with no case for either.
  //
  // ONE WALK, NOT TWO. It is `openChapterAt` with `startAt_` armed -- the same
  // mechanism a restored reading position lands through -- so the target chapter is
  // decoded ONCE, up to the cursor, with the boundaries it passes recorded on the way.
  // It was a landing on page one followed by a second walk from the top, which on the
  // device is ~380 ms wasted on a median chapter and ~2.3 s on a long one. The page
  // NUMBER falls out of the boundaries that walk recorded, which is what lets the peek
  // be honest about not having one while the commit is exact.
  //
  // BOTH CASES GO THROUGH THE SAME CALL, cross-chapter and same-chapter alike, and
  // that is what makes the sentence below true rather than nearly true.
  //
  // FALSE LEAVES THE SCREEN EXACTLY WHERE IT WAS -- the chapter, the index and its
  // completeness, the page, the label, the view model and the anchor. That is
  // openChapterAt's own restore, and it is why nothing here has a second copy of it.
  // The anchor rides that for free: the mark is raised by the landing's syncVm(), and
  // a refused walk never reaches one.
  //
  // WHAT IT COSTS TO SAY THAT: a jump within the open chapter re-opens the file and
  // re-reads its 30-byte local header, where the old two-call form rewound the handle
  // it already had. Noise against the walk, and the alternative was a second restore
  // path -- see the comment at the definition, which prices both halves.
  bool goToPosition(int spine, Cursor at);

  // --- LETTING GO SO A PEEK CAN HAVE THE HEAP -------------------------------
  //
  // A live chapter peaks at 69,884 bytes with a 36,956-byte single allocation, against
  // a measured 45,840-byte heap floor, so TWO live chapters do not fit -- and a peek
  // is a second live chapter. This is how there is only ever one: the Reader beneath a
  // peek releases its stream while the panel is up.
  //
  // WHAT SURVIVES IS EVERYTHING THE PAINT AND A SAVE READ: page_ (with owned LaidLine
  // text), at_, starts_, chapterAt_, pageBytes_, vm_, anchor_ and the book's spans.
  // ReaderScreen::render reads only page_ and vm_, so App::render draws the veiled page
  // underneath with no decode at all -- which is the property the whole design rests
  // on. A save is safe for a related reason worth stating: chapterBytesRead() is
  // pageBytes_, a plain member, NOT ChapterReader::bytesRead(), which would answer 0
  // with the inflater gone and push progressPercent onto its page/pageTotal fallback --
  // the exact shape of the percentage-going-backwards bug this project shipped once.
  void releaseChapter();

  // TAKE THE STREAM BACK, AND PAY NO seekTo FOR IT.
  //
  // The design spec budgeted closing a peek at "one seekTo -- 33.9 ms desktop for the
  // worst page in a real book", which is this project's own ratio trap: a seekTo
  // rewinds and decodes forward, so it costs WHAT PAGE YOU ARE ON, and the device
  // measured ~376 ms at page 38, ~1010 ms at page 99 and ~3 s deep in a long chapter.
  // On CLOSE that would make discarding a peek cost more than committing one.
  //
  // It is not needed. Nothing visible was disturbed, so the page is already correct;
  // what a seekTo would restore is the live PageBuilder, and `pb_ == nullptr` is an
  // already-handled state whose repair has a home -- restreamAtCurrentPage, in a quiet
  // window, where abandoning it is free. So this re-establishes the stream at the
  // chapter's start and stops, leaving hasLiveStream() false on purpose.
  //
  // False when the chapter cannot be reopened -- a card pulled while the peek was up.
  // The caller is the shell, which has pollCardPresence for that case.
  bool reacquireChapter();

  // Whether a stream is established. An OBSERVATION POINT, not a guard: no caller
  // branches on it. The three quiet-window jobs are each gated on the Reader being on
  // TOP of the stack, so a peek over it stops them by construction -- see
  // docs/superpowers/specs/2026-08-28-peek-overlay-design.md.
  bool hasChapter() const { return chapter_.held(); }

  // Whether the chapter's page count is still unknown. The shell completes it inside
  // the refinement; see the class comment.
  bool indexPending() const;

  // Counts the rest of the chapter and returns to the page being read. ~545 ms for a
  // long chapter and 3.6 s for the longest in a real book, so it belongs in a quiet
  // window rather than in a page turn -- AND IT MUST BE ABANDONABLE, because a quiet
  // window only makes it rarer and does nothing about the seconds of dead buttons
  // when it does fire.
  //
  // `stop` is asked every few blocks and true means give up. What that costs is the
  // work already done, thrown away: this returns false, `indexComplete_` stays false,
  // and the next quiet window starts the count from the beginning. That is the right
  // trade because abandoning is now nearly free -- see the three properties below,
  // which are what make it safe to abandon at all:
  //
  //   * THE INDEX IS BUILT INTO A SCRATCH VECTOR AND COMMITTED ONLY ON COMPLETION.
  //     Counting used to clear `starts_` on its first line, so a half-finished walk
  //     left an index that might not even contain the page being read. Nothing an
  //     abandoned count touched can be seen from outside: `starts_`, `at_` and the
  //     page on glass are exactly as they were.
  //   * THE PAGE ON GLASS IS ALREADY CORRECT. Counting changes one number in the
  //     footer and never the text, which is why a press may cancel it outright.
  //   * THE ONE THING SPENT IS THE LIVE BUILDER, because the walk rewinds the shared
  //     ChapterReader and there is no second one to walk with (a second would be
  //     another 32 KB inflate window against a ~42 KB floor -- the same arithmetic
  //     that keeps the eager count to two passes). A null builder is an
  //     already-handled state: Gesture::Next re-establishes it, and after this change
  //     it usually re-establishes it FROM THE PAGE RING for nothing.
  //
  // Defaults to the uninterruptible form, which is what the simulator and the goldens
  // want: the boards show the settled state, so they complete the index before
  // rendering and must never be given a half-counted one.
  bool completeIndex(StopFn stop = nullptr, void* ctx = nullptr);

  // Why the chapter stopped being readable, or empty. A card pulled mid-book, or a
  // stream that turned out to be corrupt partway through.
  const char* error() const { return chapter_.error(); }

  // WHAT THE PAGE RING ACTUALLY DID, on the same principle as the glyph cache's
  // rasterisation counter: the claim being made is "a backward turn no longer
  // decodes", and a test that only checked the page came out right would pass just as
  // happily with the ring removed. `decodes` counts rewind-and-walk-forward passes,
  // which is the ~376 ms the defect is about; `hits` counts pages served from the
  // ring. Two counters and not one, because "no decode happened" and "the ring
  // answered" are different facts and only the pair pins the mechanism.
  struct RingStats {
    uint32_t hits = 0;
    uint32_t decodes = 0;
    // PAGES LAID OUT, counted where they are handed to the ring -- which is every
    // page any walk completes, so this is the walk's real unit of work. It is what
    // distinguishes a restore that lays the prefix out ONCE from one that lays it
    // out twice, and a green suite cannot tell those apart: both produce the right
    // page. See test_reader_restore.cpp.
    uint32_t stored = 0;
  };
  RingStats ringStats() const { return ring_; }

 private:
  void buildIndex();

  // What a counting walk did. THREE ANSWERS, NOT TWO: `Failed` is a chapter that
  // cannot be walked at all (no face, no stream, a column too short for a line box)
  // and must leave the caller with an empty index, where `Abandoned` is a perfectly
  // good chapter the caller asked to stop counting -- and must leave the caller with
  // the index it already had. Collapsing them into a bool is how an abandoned count
  // would come to look like a chapter with no pages.
  enum class CountOutcome { Failed, Abandoned, Counted };
  // Walks the chapter from its start recording one Cursor per page boundary, into
  // `out` and NOWHERE ELSE: it touches neither `starts_` nor `at_` nor `page_`, which
  // is the whole reason abandoning it is safe. It does rewind the shared
  // ChapterReader, so the caller owns resetting `pb_`.
  CountOutcome countPages(std::vector<Cursor>& out, StopFn stop, void* ctx);
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
  //
  // `needStream` demands the decode even when the ring already holds the page: a
  // caller that is about to call advance() needs the live builder the decode leaves
  // behind, and a cache hit produces a page without one. Getting that round the wrong
  // way is not a slow path but a WRONG one -- Gesture::Next would find `pb_` still
  // null, read advance()'s false as "the chapter ended", and turn to the next chapter
  // in the middle of this one.
  bool seekTo(int p, bool needStream = false);
  // THE WALK BOTH IDLE JOBS MAKE, once. warmPageRing and restreamAtCurrentPage want
  // the identical rewind -- decode from `pageCacheDepth_` pages back up to the page
  // on screen, caching every page passed, and leave the builder live one page past
  // it -- and differ only in the gate that decides whether it is worth making. Two
  // copies of it would be two chances to get the CCW-of-lifetimes wrong: the builder
  // installed one page off is a reader that skips or repeats a page, which both
  // callers' tests would have to catch separately.
  //
  // INVISIBLE ON EVERY PATH. `page_`, `at_`, `starts_`, `indexComplete_` and
  // `pageBytes_` are untouched whether it lands, is abandoned or fails -- the last
  // of those especially, because the reading percentage is made of it and a drift
  // there is a saved position that lies about where the reader was.
  bool rewalkToCurrentPage(StopFn stop, void* ctx);
  // Shows page `p` from the ring, or false if it is not there. Sets `page_` and `at_`
  // and leaves no live builder.
  bool showCached(int p);
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
  std::vector<std::string> italicClasses_;
  // Bytes into the chapter at the end of `page_`. Carried on the ring too, because a
  // page served from it was decoded long ago and the stream has moved since.
  uint32_t pageBytes_ = 0;

 public:
  // THE FACES, so a screen that draws this one's page can draw it with them. The peek
  // is the caller: it renders the inner reader's page into its own panel, and a panel
  // drawn with a different face from the one the page was MEASURED with is the
  // measure/draw disagreement StyledFace exists to prevent.
  const GlyphSource* body() const { return body_; }
  const GlyphSource* italic() const { return italic_; }

  // WHETHER AN ITALIC FACE IS INSTALLED AT ALL. drawTextStyled falls back to the
  // roman when this is null, silently and correctly -- so a book whose emphasis is
  // not rendering has two completely different explanations and they look identical
  // on glass. This is what tells them apart from a log.
  bool hasItalic() const { return italic_ != nullptr; }

  // THE BOOK'S ITALIC CLASS NAMES, from its own stylesheets. Owned here because the
  // screen outlives every chapter it reads and the set is the same for all of them;
  // the ChapterReader below is handed a pointer to it.
  //
  // SET BEFORE setMetrics, exactly as the italic FACE is and for the identical
  // reason: setMetrics lays the chapter out, the wrap measures emphasis, and a set
  // arriving after would leave the first page measured roman and drawn in two faces.
  void setItalicClasses(std::vector<std::string> classes) {
    italicClasses_ = std::move(classes);
    chapter_.setItalicClasses(italicClasses_.empty() ? nullptr : &italicClasses_);
  }
  size_t italicClassCount() const { return italicClasses_.size(); }

  // HOW FAR INTO THE OPEN CHAPTER THE PAGE ON SCREEN SITS, in inflated bytes, or 0
  // when that is not knowable (a stored entry, an in-memory chapter). This is what
  // makes book progress independent of the page count -- see ChapterReader::bytesRead.
  //
  // It is the END of the current page rather than its start, which is the honest
  // reading of "how much have I read": the page in front of you has been read by the
  // time you leave it, and the alternative would report 0% on page one of a chapter
  // you are looking at.
  uint32_t chapterBytesRead() const { return pageBytes_; }

  // How many emphasised runs the page currently laid out carries. Zero means the
  // PARSE found none in this text -- `<em>`, `<i>` and `<cite>` are what document.cpp
  // recognises, and a book that marks its italics with a class and a stylesheet
  // carries none of them. Non-zero means the spans reached layout and the question is
  // downstream of it.
  int pageEmphasisRuns() const {
    int n = 0;
    for (const LaidLine& ln : page_.lines) n += static_cast<int>(ln.emphasis.size());
    return n;
  }

 private:
  // THE FURTHEST THIS READING HAS REACHED. The rule is in return_anchor.h and is
  // tested without a book; this screen only tells it where the reading position ended
  // up, from syncVm() and from nowhere else -- see the note at that definition.
  ReturnAnchor anchor_;
  bool goToAnchor(const AnchorPos& to);
  void syncAnchorLabel();
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

  // THE PAGES ALREADY LAID OUT, most recent first. See kPageCacheDepth.
  //
  // KEYED ON THE PAGE'S START CURSOR, not on its index. A page index is a position in
  // `starts_`, which grows as the chapter is read and is rebuilt outright by a count
  // -- so an index is a name that can come to mean a different page. A start cursor
  // names a block and a line of the document, which is the same identity a saved
  // reading position uses and for the same reason. The chapter is part of the key
  // because a cursor is only meaningful within one spine entry.
  struct CachedPage {
    int chapter = -1;
    Cursor start{};
    Page page{};
    // Where the stream stood when this page was laid out. Without it a ring hit
    // would report the byte position of whatever was decoded LAST, which after a
    // rewind is a different part of the chapter entirely.
    uint32_t bytes = 0;
  };
  std::vector<CachedPage> pageRing_;
  int pageCacheDepth_ = kPageCacheDepth;
  RingStats ring_{};
  const CachedPage* cachedPage(int chapter, Cursor start) const;
  void cachePage(int chapter, Cursor start, const Page& p, uint32_t bytes);
  // EVERY ENTRY IS INVALID THE MOMENT THE LAYOUT CHANGES, because a Page is lines
  // measured at one face and one column. Called from the two places that can change
  // either.
  void dropPageRing() { pageRing_.clear(); }

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
