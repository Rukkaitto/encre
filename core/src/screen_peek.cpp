#include "reader/screen_peek.h"

#include <string>
#include <utility>

#include "reader/reading_store.h"
#include "reader/theme.h"

namespace reader {

// --- WHAT THE PEEK DECLARES, AND THE TWO THINGS IT DOES NOT -------------------
//
// Stated once here rather than twice at the two constructors, which differ only in
// where the text comes from: a screen whose INPUT depended on that would be a screen
// the goldens test differently from the device.
//
// THE SIDES PAGE AND THE FRONT ROW DOES NOT, which is what `declareSplitMovers` buys
// and is the Reader's own arrangement -- the peek sits over the reading page and the
// buttons that turn a page there turn a page here. Folding the pairs together would
// make Up and Down page as well, and Up on the page underneath already means "return to
// where I was": one button with two meanings across a single press.
//
// NO declareHints, AND THE ABSENCE IS THE POINT. `holds` is one field driving two
// things -- the hint bar's hollow ring and the long-press binding -- so a screen that
// promises no hold must bind none. PeekViewModel::holds is all false; declaring it here
// would be the same fact spelled twice, and this project's rule is that the declaration
// lives where the view model is built. There is nothing to declare.
//
// NO declareRepeat EITHER, and that one is a decision rather than an omission. Every
// page of this panel costs a decode -- an advance on a live stream at best, a rewind
// proportional to the page index at worst -- so a held side button would run away from
// what the reader can follow and past what they meant to look at. The Reader beneath
// declares none for the same reason; a peek is a shorter excursion, not a laxer one.

PeekScreen::PeekScreen(FileSystem& fs, OpenedBook book, int spine, const GlyphSource* body,
                       Cursor at) {
  declareSplitMovers();
  inner_ = std::make_unique<ReaderScreen>(fs, std::move(book), spine, body);
  // BEFORE setMetrics, WHICH IS THE WHOLE ORDERING RULE. restoreAt arms `startAt_`
  // and `walkToChapter` consumes it on the first candidate it opens; armed after the
  // walk it would be a cursor nothing reads, and the peek would silently land on
  // page one while claiming to land on a sighting. `screens.cpp` already constructs
  // in this order for the same reason.
  //
  // A ZERO CURSOR IS NOT A REQUEST, it is the absence of one: block 0 line 0 IS the
  // start of the chapter, which is what Contents wants and what the walk does
  // anyway. Arming it would cost an openAtCursor walk to reach where it already is.
  if (at.block > 0 || at.line > 0) inner_->restoreAt(at);
  syncVm();
}

PeekScreen::PeekScreen(std::string_view xhtml, std::string chapter, int percent,
                       const GlyphSource* body)
    : percent_(percent) {
  declareSplitMovers();
  // THE BOOK TITLE IS EMPTY BECAUSE THE BAND DOES NOT NAME A BOOK. The Reader's header
  // does; this panel says `PEEK` and where you are looking, and the reader already knows
  // which book they are in -- it is on the glass behind the veil.
  inner_ = std::make_unique<ReaderScreen>(xhtml, std::string(), std::move(chapter), body);
  syncVm();
}

PeekScreen::~PeekScreen() = default;

void PeekScreen::setMetrics(const PageMetrics& m) {
  // ONE PAGE OF RING, AND IT BELONGS HERE RATHER THAN IN THE SHELL. The default depth of
  // 3 is sized for a reader who will be turning pages for an hour; a peek is a short
  // excursion of a few presses, and the heap it is spending is the READER's -- the page
  // ring under the veil, the book's spans, and whatever the panel's own chapter needs.
  // Three pages at ~1,500 bytes each is 4.5 KB of a floor this design is already close
  // to. Depth 1 holds the page on screen and nothing else, which is what a backward turn
  // in a peek has to pay for.
  inner_->setPageCacheDepth(1);
  inner_->setMetrics(m);
  // NO LEAD IS MIRRORED INTO THE VIEW MODEL, and it used to be. The panel's box is
  // fixed (theme.h's kPeekPanelH), so renderPeek needs no typography to place the
  // border -- it is the COLUMN this lead sizes, and PageBuilder already has it.
  syncVm();
}

void PeekScreen::setItalic(const GlyphSource* italic) { inner_->setItalic(italic); }

void PeekScreen::setChapterNames(std::vector<TocEntry> toc) {
  inner_->setChapterNames(std::move(toc));
  // The label the band draws comes from the inner reader's own view model, so it only
  // becomes the book's name for the chapter once the names have arrived.
  syncVm();
}

const Page& PeekScreen::page() const { return inner_->page(); }

int PeekScreen::chosenSpine() const { return inner_->chapterIndex(); }

Cursor PeekScreen::chosenCursor() const {
  // THE BLOCK, AND NOT THE LINE, AND THIS IS WHERE THE TWO MEASURES MEET. The inner
  // reader paginated at the PANEL's ~368px column and `ReaderScreen::goToPosition` will
  // resolve whatever comes back at the reading page's 444 -- and a `Cursor`'s `line` is
  // a line WITHIN A BLOCK AT ONE COLUMN WIDTH. A block is a fact about the document and
  // crosses intact; a line is a fact about a layout the Reader does not share. See #48,
  // and `reading_position.h`, which grades this exact change as `Relaid` and zeroes the
  // same field -- as `ReaderScreen::relayout` drops it for the same reason one layer up.
  // This is the third place that question is asked and was the one answering it
  // differently.
  //
  // IT WAS WRONG FORWARD, WHICH IS THE ONLY DIRECTION THAT MATTERS. The panel is
  // NARROWER, so a block has MORE lines there and panel line L has consumed LESS text
  // than reading line L -- so handing L across landed the reader PAST the passage they
  // pressed GO HERE on. Measured over a 600-word paragraph: a commit from panel page 8
  // landed on reading page 5 with the peeked text on page 4, and one from panel page 18
  // named line 130 of a block with 120 reading lines, which took `openAtCursor`'s
  // documented "the end of the chapter is the closest honest answer" exit and put the
  // reader in the NEXT paragraph. Landing at the top of the block undershoots instead,
  // so the passage is ahead of the reader rather than behind them and one press reaches
  // it -- "the top of the right paragraph beats the front of the book".
  //
  // WHAT IT COSTS, measured rather than asserted: over real prose the two answers are
  // the SAME reading page in 20 of `longChapter`'s 45 panel pages and one page apart in
  // the rest, because a paragraph is four or five panel lines and the disagreement is
  // block-relative. The cost is a long paragraph, where the landing is its top.
  //
  // AND NOT IN goToPosition, THOUGH IT HAS ONE CALLER AND THE EFFECT WOULD BE THE SAME
  // TODAY. That function's contract is "a cursor", and `goToAnchor` and a restored
  // reading position hand it lines measured at the reading column, where the line is
  // exactly right. The fact "my column is not the reader's" belongs to the screen that
  // has the other column -- this is the one place both widths are known.
  //
  // THE ALTERNATIVE WAS RE-MEASURING ON ARRIVAL, and it needs a representation `Cursor`
  // does not have: a within-block offset, which means `layout.h` and `PageBuilder` --
  // the most performance-critical code here, and a `LaidLine` that deliberately owns
  // re-based text and carries no offsets. It would also make the peek the ONLY place in
  // the firmware that resolves a line across two measures, with a bespoke
  // representation and a single caller, while `relayout` and the sidecar went on
  // dropping theirs. Scaling the line by the ratio of the two columns was refused
  // outright: it is a guess wearing a measurement's clothes, it can still overshoot,
  // and nothing would catch it being a few lines out.
  return Cursor{inner_->currentCursor().block, 0};
}

int PeekScreen::percentHere() const {
  // WHERE THE PANEL'S CHAPTER IS IN THE BOOK, recomputed from the chapter actually on
  // screen rather than from the one the panel was opened at.
  //
  // IT WAS FIXED AT CONSTRUCTION, and a comment defended that: "the band's percent does
  // not move -- it is the caller's figure for the CHAPTER". It cannot be defended,
  // because PAGING OFF EITHER END OF THE PEEKED CHAPTER CROSSES INTO THE NEXT ONE and
  // this class's own header lists that as a designed property. The label followed the
  // crossing and the number did not, so the band read `CH. 09 · 4%` with 4% being
  // chapter 8's start -- two halves of one composed run describing different chapters,
  // in the ONLY positional information this panel offers (it has no page number, by
  // design) and the number the reader decides `GO HERE` on.
  //
  // THE CHAPTER'S START FRACTION, not the page's. progressPercent would interpolate
  // within the open chapter if it were handed bytes, and it is deliberately not: the
  // panel says WHICH CHAPTER and where that chapter falls, which is a claim true at
  // any column width -- and this column is not the reader's. A number that crept as
  // the reader paged would be a page position, which is exactly what this panel
  // refuses to state.
  //
  // AND THE IN-MEMORY CONSTRUCTOR KEEPS THE CALLER'S FIGURE, because there is no book
  // behind it to ask -- progressPercent answers 0 for a book with no chapters, and the
  // board's peek is 4%.
  const OpenedBook& b = inner_->book();
  if (b.chapterCount() <= 0) return percent_;
  return progressPercent(b, inner_->chapterIndex(), 1, 0, 0);
}

void PeekScreen::syncVm() {
  // `CH. 01 · 4%`, composed here because the theme does no arithmetic -- and the chapter
  // is whatever the inner reader's header would have said, which is the book's own name
  // for it where its contents supply one and the `CH. NN` position where they do not.
  //
  // THE MIDDLE DOT IS A TRAP THIS PROJECT HAS ALREADY PAID FOR ONCE. A C++ hex escape is
  // UNBOUNDED, so "\xC2\xB7CH." parses `\xB7C` as ONE escape: clang rejects it outright
  // and the ESP32's GCC ACCEPTS it, emitting a byte that is not U+00B7. Adjacent string
  // literals end the escape, which is why the dot is spelled on its own below and never
  // glued to what follows it.
  vm_.where = inner_->vm().chapter + " " "\xC2\xB7" " " + std::to_string(percentHere()) + "%";
}

Action PeekScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    // CLOSE. The reader's page was never disturbed -- the Reader underneath still holds
    // the page it was on, released stream and all -- so leaving costs nothing to undo.
    case Gesture::Back:
      return Action::pop();
    // GO HERE. A Pop like Back's, because this screen cannot move the Reader: it is on
    // the stack UNDERNEATH, and a screen returns one Action. The flag is what the shell
    // reads while this is still on top, before the dispatch pops it away.
    case Gesture::Activate:
      committed_ = true;
      return Action::pop();
    // THE SIDE BUTTONS PAGE, inside the panel, over the peeked chapter -- which is the
    // inner reader's whole job. Its answer is forwarded rather than re-derived: `None`
    // means it could not turn (the end of the book, or a chapter that would not open),
    // and repainting an identical panel would cost ~1.4 s of grayscale for nothing.
    case Gesture::Next:
    case Gesture::Prev: {
      const Action a = inner_->onGesture(g);
      if (a.kind == Action::Kind::None) return Action::none();
      // BOTH HALVES OF THE BAND MOVE, and one of them used to not: paging off either
      // end crosses into the next spine entry, so the label changed and the percent
      // stayed on the chapter the panel was opened at. See percentHere().
      syncVm();
      return Action::redraw();
    }
    // EVERYTHING ELSE, AND THAT INCLUDES THE FRONT ROW. `declareSplitMovers` keeps the
    // pairs apart, so the front row's movers arrive as AltPrev/AltNext -- and they are
    // dead here on purpose: the bar's last two slots are empty, and a bar that promises
    // nothing must not do something. See PeekViewModel::hints.
    default:
      return Action::none();
  }
}

void PeekScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                        Plane plane) const {
  // THE FACES ARE THE INNER READER'S, because the page being drawn is the page IT
  // measured. Drawing a laid-out line with a face other than the one it was measured
  // against is the measure/draw disagreement StyledFace exists to prevent.
  const GlyphSource* body = inner_->body();
  // No face, no text: the screen is constructed before setMetrics and a caller with no
  // body face at all is a readable empty panel rather than an abort. Same call
  // ReaderScreen::render makes.
  if (body == nullptr) return;
  theme.renderPeek(fb, fonts, *body, inner_->italic(), vm_, inner_->page(), plane);
}

}  // namespace reader
