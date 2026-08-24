#include "reader/screen_reader.h"

#include <cstdio>
#include <new>

#include "reader/book.h"
#include "reader/glyphsource.h"
#include "reader/theme.h"

namespace reader {
namespace {

// A runaway guard on the pagination walk, not a design limit. Every page consumes
// at least one line of a chapter already capped at kMaxBlocks blocks, so a real
// chapter cannot approach this -- it exists because the walk's termination depends
// on the builder advancing, and a guard is cheaper than trusting that from here.
constexpr int kMaxPages = 4096;

// IS `a` STRICTLY BEFORE `b` in the document. Local rather than an operator< on
// Cursor, because ordering two cursors is only meaningful WITHIN one chapter at one
// layout -- a comparison operator on the type would invite comparing cursors from
// two chapters, which is a question with no answer.
bool earlier(const Cursor& a, const Cursor& b) {
  return a.block != b.block ? a.block < b.block : a.line < b.line;
}

}  // namespace

ReaderScreen::ReaderScreen(FileSystem& fs, OpenedBook book, int startChapter,
                           const GlyphSource* body)
    : body_(body),
      bookTitle_(book.title),
      fs_(&fs),
      book_(std::move(book)),
      chapterAt_(startChapter) {
  // Nothing is opened here: a chapter cannot be paginated without a column height,
  // and setMetrics is the first moment one exists. So the constructor is cheap and
  // the work is in one place rather than half in each.
  updateChapterLabel();
  syncVm();
}

ReaderScreen::ReaderScreen(std::string_view xhtml, std::string bookTitle, std::string chapter,
                           const GlyphSource* body)
    : body_(body),
      bookTitle_(std::move(bookTitle)),
      chapter_label_(std::move(chapter)) {
  chapter_.beginBuffer(xhtml);
  syncVm();
}

ReaderScreen::~ReaderScreen() = default;

void ReaderScreen::setMetrics(const PageMetrics& m) {
  metrics_ = m;
  if (fs_ != nullptr && !book_.path.empty()) {
    // The expensive call: locating the chapter and decoding it once to index its
    // pages, plus however many empty spine entries have to be skipped to reach
    // text.
    openChapterAt(chapterAt_, false);
    return;
  }
  // The in-memory chapter, through the SAME lazy landing and the SAME size threshold
  // the card path takes. Two paths that paginate differently would mean the goldens
  // and the simulator testing something the device does not do -- and this project
  // has been bitten by a desktop path that diverged from the device's before.
  // A restore target wins over the counting choice, exactly as on the card path.
  if (startAt_ != Cursor{}) {
    const Cursor want = startAt_;
    startAt_ = Cursor{};
    openAtCursor(want);
    syncVm();
    return;
  }
  // The same two-pass-or-one choice the card path makes, for the same reason.
  if (chapter_.sizeBytes() > 0 && chapter_.sizeBytes() <= kEagerCountBytes) {
    buildIndex();
    at_ = 0;
    if (!starts_.empty()) seekTo(0);
  } else {
    openFirstPage();
  }
  syncVm();
}

void ReaderScreen::setChapterNames(std::vector<TocEntry> toc) {
  names_ = std::move(toc);
  updateChapterLabel();  // whatever is open now gets its name immediately
  syncVm();
}

void ReaderScreen::updateChapterLabel() {
  // THE CHAPTER'S NAME when the book's contents supply one. This composed a SPINE
  // POSITION for two phases, and said so: "without a table of contents -- which is
  // design/Contents.dc.html and is not built -- the position is the only thing honestly
  // known". It is built.
  //
  // THE LAST entry naming this spine, which is tocIndexForSpine's rule: where several
  // entries point into one file the later ones are further into it, so the last is the
  // closest thing to "where you are" that a spine-granular position can name.
  const int at = tocIndexForSpine(names_, chapterAt_);
  if (at >= 0 && !names_[static_cast<size_t>(at)].label.empty()) {
    chapter_label_ = names_[static_cast<size_t>(at)].label;
    return;
  }
  // THE POSITION IS STILL THE FALLBACK, for a book with no contents and for a chapter
  // its contents does not mention -- spine entry 0 of a real book is its cover, and
  // nothing names that.
  char buf[16];
  std::snprintf(buf, sizeof(buf), "CH. %02d", chapterAt_ + 1);
  chapter_label_.assign(buf);
}

bool ReaderScreen::openChapterAt(int c, bool atEnd) {
  // A FAILED TURN MUST LEAVE THE SCREEN WHERE IT WAS. The walk below opens each
  // candidate before it can know whether that candidate has any pages, so running
  // off either end of the book left `chapterAt_` on the last thing tried and
  // `starts_` empty -- the device reported "spine 0, page 1/7" for a spine entry
  // with no pages at all, with a stale page still on the panel.
  // NOTHING TO PAGE INTO, so nothing may be disturbed. The in-memory constructor has
  // no book behind it, and without this the moved-out index below was never put back
  // -- pressing past the last page of the demo chapter left the screen reporting
  // zero pages.
  if (fs_ == nullptr || book_.path.empty() || body_ == nullptr) return false;

  const int wasAt = chapterAt_;
  const int wasPage = at_;
  // MOVED OUT, not copied: the index can be thousands of cursors and this runs on a
  // device with ~46 KB of headroom. A move is a pointer swap, and walkToChapter
  // clears `starts_` anyway.
  std::vector<Cursor> wasStarts = std::move(starts_);
  const bool wasComplete = indexComplete_;
  starts_.clear();

  if (walkToChapter(c, atEnd)) return true;
  if (!starts_.empty() && chapterAt_ == wasAt) return false;  // nothing was disturbed

  // Put back exactly what was showing, INDEX INCLUDED. Restoring through the forward
  // landing was the first attempt and it threw the count away: paging back off the
  // front of the book left a chapter that had been counted reading `1 / —` again.
  // One extra chapter decode, once, at the book's edge.
  if (!reopenChapter(wasAt)) {
    // Even a failed restore must leave the index intact: the page on glass is still
    // the one it describes.
    starts_ = std::move(wasStarts);
    return false;
  }
  starts_ = std::move(wasStarts);
  indexComplete_ = wasComplete;
  if (starts_.empty()) return false;
  at_ = wasPage < static_cast<int>(starts_.size()) ? wasPage
                                                   : static_cast<int>(starts_.size()) - 1;
  seekTo(at_);
  updateChapterLabel();
  syncVm();
  return false;
}

bool ReaderScreen::reopenChapter(int c) {
  // The stream alone -- no index, no page. For putting a chapter back exactly as it
  // was after a walk that failed.
  if (fs_ == nullptr || book_.path.empty() || c < 0 || c >= book_.chapterCount())
    return false;
  const ChapterLocation where = book_.locate(c);
  if (where.compressedSize == 0) return false;
  if (!chapter_.begin(*fs_, where)) return false;
  chapterAt_ = c;
  return true;
}

bool ReaderScreen::walkToChapter(int c, bool atEnd) {
  if (fs_ == nullptr || book_.path.empty() || body_ == nullptr) return false;
  const int dir = atEnd ? -1 : +1;

  // Bounded by the spine's own length: every step moves one entry, so this cannot
  // loop even if every chapter were empty.
  for (int guard = 0; guard <= book_.chapterCount(); ++guard) {
    if (c < 0 || c >= book_.chapterCount()) return false;

    // A ROW LOOKUP, not an archive parse. This called openBook per candidate, and
    // each call re-read the central directory and re-inflated the OPF -- ~32 KB
    // transient and ~140 ms, three times over, just to reach this book's first
    // chapter with text. The offsets were read once when the book was opened.
    const ChapterLocation where = book_.locate(c);
    if (where.compressedSize == 0) {
      // The spine named an entry the archive does not contain. Skip it exactly as a
      // chapter with no text is skipped.
      c += dir;
      continue;
    }
    if (!chapter_.begin(*fs_, where)) return false;

    chapterAt_ = c;
    // GOING FORWARD, ONLY PAGE ONE IS DECODED -- the count follows later, inside the
    // refinement. Going BACKWARD needs the last page, and there is no way to know
    // which that is without counting, so that direction still pays.
    const bool landed = atEnd ? [&] {
      buildIndex();
      if (starts_.empty()) return false;
      at_ = static_cast<int>(starts_.size()) - 1;
      return seekTo(at_);
    }() : [&] {
      // A RESTORED POSITION LANDS WHERE IT LEFT OFF, before either counting choice
      // below is considered: the walk to the cursor records the boundaries it passes,
      // so it already leaves the index in the state those branches would build.
      //
      // THE CURSOR IS SPENT ON THE FIRST CANDIDATE. The walk starts at the spine
      // entry the position was saved in, so that entry is the one the cursor is
      // about; a later candidate is only reached because this one paginated to
      // nothing, and a cursor into a chapter with no pages says nothing about the
      // next chapter.
      if (startAt_ != Cursor{}) {
        const Cursor want = startAt_;
        startAt_ = Cursor{};
        return openAtCursor(want);
      }
      // THE COUNT DECIDES BEFORE LANDING, NOT AFTER. Landing first and then counting
      // decodes page one, then the whole chapter, then page one AGAIN -- three passes
      // where two will do, and the wasted one is the reason a small chapter felt as
      // slow to open as it did before any of this.
      //
      // The size is known the moment the stream is begun (the central directory said
      // so), so the choice costs nothing to make here.
      const uint32_t bytes = chapter_.sizeBytes();
      if (bytes > 0 && bytes <= kEagerCountBytes) {
        buildIndex();
        if (starts_.empty()) return false;
        at_ = 0;
        return seekTo(0);
      }
      return openFirstPage();
    }();
    if (landed) {
      updateChapterLabel();
      syncVm();
      return true;
    }
    // Nothing on this one -- a cover, a title page. Keep going the way we were
    // heading rather than stopping on a blank page.
    c += dir;
  }
  return false;
}

void ReaderScreen::buildIndex() {
  starts_.clear();
  indexComplete_ = false;
  pb_.reset();
  if (body_ == nullptr || !chapter_.ok()) return;
  if (!chapter_.rewind()) return;

  PageBuilder pb(*body_, metrics_);
  if (!pb.viable()) return;
  // The lines of every page in the chapter would be built and immediately dropped;
  // all this pass keeps is one cursor per page.
  pb.countOnly();

  // The start of the page currently being filled. Pushed when that page completes,
  // so a cursor is only recorded once there is really a page at it -- otherwise a
  // chapter ending exactly on a boundary would leave a page in the index with no
  // lines behind it.
  Cursor pending = pb.pageStart();
  Block b;
  int i = 0;
  for (int guard = 0; guard < kMaxPages * 4; ++guard) {
    if (!chapter_.next(b)) break;
    pb.add(b, i++);
    b = Block{};  // dropped: the whole point of streaming
    while (pb.ready() && static_cast<int>(starts_.size()) < kMaxPages) {
      starts_.push_back(pending);
      pb.take();
      pending = pb.pageStart();
    }
  }
  // ASKED OF THE BUILDER, not inferred from the page it returns: in counting mode
  // that page has no lines whether or not there was one, and reading emptiness off
  // it dropped every trailing partial page -- so a chapter that fits on one page
  // indexed to nothing at all.
  const bool trailing = pb.pageHasContent();
  pb.finish();
  if (trailing && static_cast<int>(starts_.size()) < kMaxPages) starts_.push_back(pending);
  // The whole chapter was walked, so `starts_.size()` really is the page count.
  indexComplete_ = true;
}

bool ReaderScreen::openFirstPage() {
  // Page one starts where the chapter does -- the one cursor that is known without
  // counting anything. seekTo then decodes only far enough to fill it.
  starts_.clear();
  starts_.push_back(Cursor{0, 0});
  indexComplete_ = false;
  at_ = 0;
  if (!seekTo(0) || page_.lines.empty()) {
    // A CHAPTER WITH NO TEXT AT ALL -- a cover, a title page. The provisional cursor
    // has to go with it, or the screen reports one page and shows nothing, which is
    // the blank-page-reading-0/0 defect the device found once already. Nought pages
    // is a KNOWN count, so the index is complete.
    starts_.clear();
    indexComplete_ = true;
    return false;
  }
  return true;
}

Cursor ReaderScreen::currentCursor() const {
  if (at_ < 0 || at_ >= static_cast<int>(starts_.size())) return Cursor{};
  return starts_[static_cast<size_t>(at_)];
}

bool ReaderScreen::openAtCursor(Cursor want) {
  // THE TOP OF THE CHAPTER IS NOT A WALK. It is also what a Rebound restore asks for
  // (the book's bytes changed, so only the spine survived), so this is the common
  // case rather than a corner of one.
  if (want == Cursor{}) return openFirstPage();

  starts_.clear();
  indexComplete_ = false;
  pb_.reset();
  page_ = Page{};
  if (body_ == nullptr || !chapter_.ok() || !chapter_.rewind()) return false;

  PageBuilder pb(*body_, metrics_);
  if (!pb.viable()) return false;
  // Boundaries, not pages: the lines of every page before the target would be built
  // and dropped. Same reason buildIndex counts this way.
  pb.countOnly();

  // The start of the page being filled, pushed once that page completes -- the same
  // one-behind bookkeeping buildIndex does, and for the same reason.
  Cursor pending = pb.pageStart();
  Block b;
  int i = 0;
  bool found = false;
  for (int guard = 0; guard < kMaxPages * 4 && !found; ++guard) {
    if (!chapter_.next(b)) break;
    pb.add(b, i++);
    b = Block{};  // dropped: the whole point of streaming
    while (pb.ready()) {
      starts_.push_back(pending);
      pb.take();
      pending = pb.pageStart();
      // `want` is on the page just recorded exactly when the NEXT page starts after
      // it. Strictly after: a cursor EQUAL to the next page's start belongs to that
      // next page, not to this one.
      if (earlier(want, pending) || static_cast<int>(starts_.size()) >= kMaxPages) {
        found = true;
        break;
      }
    }
  }

  if (!found) {
    // THE CHAPTER ENDED BEFORE THE CURSOR DID. A shorter chapter at the same path, or
    // a record written against a shorter column so its block index runs past this
    // layout's last block. The end of the chapter is the closest honest answer to
    // "past the end of the chapter" -- and the whole chapter really was walked, so
    // the count is known.
    if (pb.pageHasContent() && static_cast<int>(starts_.size()) < kMaxPages)
      starts_.push_back(pending);
    pb.finish();
    indexComplete_ = true;
  }

  if (starts_.empty()) {
    // No pages at all: a cover or a title page. Nought is a KNOWN count, exactly as
    // openFirstPage treats it.
    indexComplete_ = true;
    return false;
  }
  // The target is the last boundary recorded, in both branches: the loop stops having
  // just pushed the page that holds the cursor, and the fallback stops having just
  // pushed the chapter's last.
  return seekTo(static_cast<int>(starts_.size()) - 1);
}

bool ReaderScreen::seekTo(int p) {
  page_ = Page{};
  pb_.reset();
  if (body_ == nullptr || p < 0 || p >= static_cast<int>(starts_.size())) return false;
  if (!chapter_.rewind()) return false;

  pb_.reset(new (std::nothrow) PageBuilder(*body_, metrics_));
  if (pb_ == nullptr || !pb_->viable()) {
    pb_.reset();
    return false;
  }
  // Everything before the target is decoded and thrown away. That is what a
  // backward turn costs on a stream that cannot be seeked.
  pb_->startAt(starts_[static_cast<size_t>(p)]);

  fed_ = 0;
  Block b;
  for (int guard = 0; guard < kMaxPages * 4; ++guard) {
    if (!chapter_.next(b)) break;
    pb_->add(b, fed_++);
    b = Block{};
    if (pb_->ready()) {
      page_ = pb_->take();
      at_ = p;
      page_.lastPage = (p + 1 >= static_cast<int>(starts_.size()));
      return true;
    }
  }
  page_ = pb_->finish();
  at_ = p;
  page_.lastPage = true;
  pb_.reset();  // spent: a forward turn from here has nothing to continue
  return true;
}

bool ReaderScreen::advance() {
  if (pb_ == nullptr) return false;
  // Where the page about to be produced BEGINS. Captured before it is taken, because
  // take() immediately moves pageStart() on to the following one -- and this cursor
  // is what the index records.
  const Cursor thisStart = pb_->pageStart();

  Block b;
  for (int guard = 0; guard < kMaxPages * 4; ++guard) {
    if (pb_->ready()) break;
    if (!chapter_.next(b)) break;
    pb_->add(b, fed_++);
    b = Block{};
  }

  Page produced;
  if (pb_->ready()) {
    produced = pb_->take();
  } else {
    // The blocks ran out. Whatever is on the part-built page is the chapter's last,
    // and either way its end has now been seen -- so the count is known.
    const bool trailing = pb_->pageHasContent();
    Page last = pb_->finish();
    pb_.reset();
    indexComplete_ = true;
    if (!trailing) return false;  // there was no next page: the chapter is finished
    produced = std::move(last);
  }

  ++at_;
  // The index grows by reading. A page reached for the first time appends its start;
  // one revisited after a backward turn is already there.
  if (at_ >= static_cast<int>(starts_.size()) && at_ < kMaxPages) starts_.push_back(thisStart);
  page_ = std::move(produced);
  page_.lastPage = indexComplete_ && at_ + 1 >= static_cast<int>(starts_.size());
  return true;
}

bool ReaderScreen::goToChapter(int spine) {
  if (spine < 0 || spine >= book_.chapterCount()) return false;
  if (spine == chapterAt_) return true;  // already there; a jump to here is a no-op
  return openChapterAt(spine, /*atEnd=*/false);
}

bool ReaderScreen::indexPending() const {
  // Not gated on having a book: an in-memory chapter is counted the same way, so the
  // simulator and the goldens exercise the same path the device does.
  return !indexComplete_ && body_ != nullptr && !starts_.empty();
}

bool ReaderScreen::completeIndex() {
  if (!indexPending()) return false;
  const int wasPage = at_;
  buildIndex();  // rewinds and counts the whole chapter
  if (starts_.empty()) return false;
  at_ = wasPage < static_cast<int>(starts_.size()) ? wasPage
                                                   : static_cast<int>(starts_.size()) - 1;
  seekTo(at_);
  syncVm();
  return true;
}

void ReaderScreen::syncVm() {
  vm_.bookTitle = bookTitle_;
  vm_.chapter = chapter_label_;
  const int known = static_cast<int>(starts_.size());
  // ZERO UNTIL THE CHAPTER'S END HAS BEEN SEEN. `starts_.size()` is pages KNOWN, and
  // reporting it as the total would count up as the reader advanced -- "1 / 1",
  // "2 / 2" -- which is worse than admitting it is not known. The footer draws an em
  // dash for 0; see design/Reader.dc.html.
  vm_.pageTotal = indexComplete_ ? known : 0;
  vm_.page = known == 0 ? 0 : at_ + 1;
  // Rounded once, and off the page just READ rather than the one about to be: the
  // board's 53 of 890 is 5.955%, shown as 6%, so the number is the position reached
  // and not the position started from. Unknown while the total is.
  vm_.progressPercent = vm_.pageTotal == 0 ? 0 : (vm_.page * 100 + vm_.pageTotal / 2) / vm_.pageTotal;
}

Action ReaderScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next: {
      // A HELD button turns pages one at a time here, unlike a list. `steps` is
      // what accelerates a Library scroll; on a panel that costs ~520 ms a repaint
      // and a page that has to be decoded, a repeat that skipped four pages would
      // be four pages the reader never saw.
      // THE STREAM DECIDES WHETHER THERE IS A NEXT PAGE, not the index -- because
      // with the count still unknown the index cannot say. advance() returns false
      // only when the chapter's blocks are exhausted.
      //
      // A backward turn spends the builder, so re-establish it first: seekTo(at_)
      // re-renders the page being read and leaves the stream positioned to continue.
      if (pb_ == nullptr && !seekTo(at_)) return Action::none();
      if (advance()) {
        syncVm();
        return Action::redraw();
      }
      // OFF THE END OF THE CHAPTER IS THE NEXT CHAPTER, which is what makes this a
      // reader rather than a chapter viewer.
      if (!openChapterAt(chapterAt_ + 1, false)) return Action::none();
      return Action::redraw();
    }
    case Gesture::Prev: {
      // Where the page index earns itself: the stream only goes forward, so an
      // earlier page means rewinding and decoding to its recorded cursor. Without
      // the index there would be no cursor to decode TO.
      if (at_ <= 0) {
        // And back off the top is the PREVIOUS chapter's LAST page, so paging
        // backwards through a book is continuous rather than stopping at each
        // chapter's start.
        if (!openChapterAt(chapterAt_ - 1, true)) return Action::none();
        return Action::redraw();
      }
      if (!seekTo(at_ - 1)) return Action::none();
      syncVm();
      return Action::redraw();
    }
    case Gesture::Back:
      return Action::pop();
    // ACTIVATE OPENS THE MENU. It answered none() while ReaderMenu.dc.html was not
    // built -- listed as a known no-op rather than left to be discovered, because a
    // button that does nothing is a defect this project has shipped twice. It is built.
    case Gesture::Activate:
      return Action::push(ScreenId::ReaderMenu);
    default:
      return Action::none();
  }
}

void ReaderScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                          Plane plane) const {
  if (body_ == nullptr) return;
  theme.renderReader(fb, fonts, *body_, vm_, page_, plane);
}

}  // namespace reader
