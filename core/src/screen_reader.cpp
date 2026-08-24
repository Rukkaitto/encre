#include "reader/screen_reader.h"

#include <algorithm>
#include <cstddef>
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

// HOW OFTEN A COUNTING WALK ASKS WHETHER TO GIVE UP. Every block, which is as fine
// as this loop can be asked at all.
//
// IT WAS EIGHT FIRST, AND THAT WAS A DESKTOP NUMBER WEARING DEVICE CLOTHES -- the
// mistake this file already records under the eager page count, made again. The
// reasoning was "a block costs 1-3 ms on the device, so eight of them is ~25 ms of
// latency, inside the input poll". The device's own log says otherwise: `[index]
// pages=315 in 3605ms` over a chapter of roughly six hundred blocks is ~6 ms A
// BLOCK, so eight blocks is ~48 ms -- a tenth of an entire interaction, spent for
// nothing.
//
// For nothing, because the check is `uxQueueMessagesWaiting`: a critical section
// and a read, a microsecond or two, against a block that costs thousands. Coarsening
// it saves 0.03% of the walk and multiplies the latency it exists to bound.
//
// A BLOCK IS THE FLOOR, not a choice. `chapter_.next()` is the card read and the
// inflate and `pb.add()` wraps the whole block, and neither can be stopped halfway --
// so the worst case is one block, ~6 ms typically and ~30 ms for the largest block
// measured in a real book (4,406 bytes at the panel's 7.2 ms/KB). Checking per PAGE
// would have been worse still and unevenly so: a page is a dozen blocks of ordinary
// prose and one block of a chapter set in long paragraphs.
constexpr int kStopCheckBlocks = 1;

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
  // THE SIDES PAGE AND THE FRONT ROW DOES NOT, which is unique to this screen and
  // is what makes a fifth binding exist at all: every other screen folds the two
  // movement pairs together, and with them folded all four buttons page here. See
  // Gesture::AltPrev.
  declareSplitMovers();
  updateChapterLabel();
  syncVm();
}

ReaderScreen::ReaderScreen(std::string_view xhtml, std::string bookTitle, std::string chapter,
                           const GlyphSource* body)
    : body_(body),
      bookTitle_(std::move(bookTitle)),
      chapter_label_(std::move(chapter)) {
  // THE SIDES PAGE AND THE FRONT ROW DOES NOT, which is unique to this screen and
  // is what makes a fifth binding exist at all: every other screen folds the two
  // movement pairs together, and with them folded all four buttons page here. See
  // Gesture::AltPrev.
  declareSplitMovers();
  chapter_.beginBuffer(xhtml);
  syncVm();
}

ReaderScreen::~ReaderScreen() = default;

void ReaderScreen::setMetrics(const PageMetrics& m) {
  metrics_ = m;
  // A page is lines measured against one column and one face, so a column that
  // changed makes every page already laid out wrong. In practice this fires on an
  // empty ring (metrics arrive once, before the first page) -- it is here so that the
  // rule is a property of the setter rather than of the order of calls.
  dropPageRing();
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

ReaderScreen::CountOutcome ReaderScreen::countPages(std::vector<Cursor>& out, StopFn stop,
                                                   void* ctx) {
  out.clear();
  if (body_ == nullptr || !chapter_.ok()) return CountOutcome::Failed;
  if (!chapter_.rewind()) return CountOutcome::Failed;

  PageBuilder pb(*body_, metrics_);
  if (!pb.viable()) return CountOutcome::Failed;
  // The lines of every page in the chapter would be built and immediately dropped;
  // all this pass keeps is one cursor per page.
  pb.countOnly();

  // RESERVED ONCE rather than grown a page at a time, and what that buys is not the
  // eight bytes an entry -- it is the REALLOCS. This walks beside a live index and a
  // 32 KB inflate window, and a doubling realloc holds the old buffer and the new one
  // at the same time, so the peak is 1.5x the index rather than 1x. 128 entries is
  // 1 KB and covers all but the longest chapter of a real book (315 pages), which
  // then doubles twice from here instead of eight times from nothing.
  out.reserve(128);

  // The start of the page currently being filled. Pushed when that page completes,
  // so a cursor is only recorded once there is really a page at it -- otherwise a
  // chapter ending exactly on a boundary would leave a page in the index with no
  // lines behind it.
  Cursor pending = pb.pageStart();
  Block b;
  int i = 0;
  for (int guard = 0; guard < kMaxPages * 4; ++guard) {
    // ASKED BEFORE THE BLOCK IS FETCHED, not after it is laid: `chapter_.next` is the
    // card read and the inflate, so a check on the far side of it would still commit
    // to the most expensive step of the loop before it could get out of the way.
    if (stop != nullptr && i % kStopCheckBlocks == 0 && i > 0 && stop(ctx))
      return CountOutcome::Abandoned;
    if (!chapter_.next(b)) break;
    pb.add(b, i++);
    b = Block{};  // dropped: the whole point of streaming
    while (pb.ready() && static_cast<int>(out.size()) < kMaxPages) {
      out.push_back(pending);
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
  if (trailing && static_cast<int>(out.size()) < kMaxPages) out.push_back(pending);
  // The whole chapter was walked, so `out.size()` really is the page count.
  return CountOutcome::Counted;
}

void ReaderScreen::buildIndex() {
  // THE UNINTERRUPTIBLE FORM, and the one every path that is REPLACING the index
  // takes: opening a chapter, landing on its last page, the eager count. Each of
  // those has no index worth keeping, so the clear-first shape is right for them --
  // and a failed walk must leave `starts_` empty, because walkToChapter reads exactly
  // that to mean "this spine entry paginates to nothing, try the next".
  starts_.clear();
  indexComplete_ = false;
  pb_.reset();
  std::vector<Cursor> built;
  if (countPages(built, nullptr, nullptr) != CountOutcome::Counted) return;
  starts_ = std::move(built);
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

// WHERE THE READER IS, spelled the way the anchor spells a page. `currentCursor()`
// is the position within the chapter and `chapterAt_` is which chapter, and the
// anchor needs both -- a cursor alone cannot be compared across chapters, which is
// the whole reason the anchor stores a spine.
AnchorPos ReaderScreen::here() const {
  const Cursor c = currentCursor();
  return AnchorPos{chapterAt_, c.block, c.line};
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

  // ONE WALK, NOT TWO, AND THE LINES ARE KEPT. This counted boundaries and then
  // handed the answer to seekTo(), which REWOUND AND WALKED THE WHOLE CHAPTER AGAIN
  // to lay out the one page it wanted -- so restoring a position cost two full
  // decodes of everything before it. On the device that is what CONTINUE was: a
  // saved position on page 99 of a 248 KB chapter measured `post=2269ms`, and the
  // walk on its own is ~1010 ms.
  //
  // The trade is exact and it is the same one the eager page count got wrong in the
  // other direction: counting mode skips building the LINES of every page it passes,
  // which this project measured at ~15% of a walk, and it was buying that 15% at the
  // price of a second whole walk. So the lines stay on, the most recently completed
  // page is held, and when the target is found that page IS the answer.
  //
  // What makes it land rather than merely be cheaper: the builder is left LIVE, sat
  // exactly one page past the target, which is precisely the state a forward turn
  // needs -- the same state seekTo() used to hand back. So this is not "seekTo with
  // its work skipped", it is seekTo's own postcondition reached once.
  pb_.reset(new (std::nothrow) PageBuilder(*body_, metrics_));
  if (pb_ == nullptr || !pb_->viable()) {
    pb_.reset();
    return false;
  }

  // The start of the page being filled, pushed once that page completes -- the same
  // one-behind bookkeeping buildIndex does, and for the same reason.
  Cursor pending = pb_->pageStart();
  Page held;
  Cursor heldStart{};
  bool haveHeld = false;
  Block b;
  fed_ = 0;
  bool found = false;
  for (int guard = 0; guard < kMaxPages * 4 && !found; ++guard) {
    if (!chapter_.next(b)) break;
    pb_->add(b, fed_++);
    b = Block{};  // dropped: the whole point of streaming
    while (pb_->ready()) {
      starts_.push_back(pending);
      heldStart = pending;
      held = pb_->take();
      haveHeld = true;
      pending = pb_->pageStart();
      // EVERY PAGE PASSED GOES INTO THE RING on its way past, which costs nothing --
      // it was laid out anyway and the ring drops all but the newest few. So a
      // restore lands with the pages BEFORE the target already held, and the first
      // backward turns off a resumed position need no decode at all. They were the
      // worst case there was: a restore puts the reader deep in a chapter, which is
      // exactly where a rewind costs most.
      cachePage(chapterAt_, heldStart, held);
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
    if (pb_->pageHasContent() && static_cast<int>(starts_.size()) < kMaxPages) {
      starts_.push_back(pending);
      heldStart = pending;
      held = pb_->finish();
      haveHeld = true;
      cachePage(chapterAt_, heldStart, held);
    } else {
      pb_->finish();
    }
    // SPENT, exactly as seekTo marks it: the builder has been asked for the chapter's
    // end, so there is nothing left for a forward turn to continue and it must
    // re-establish one rather than read false as "no next page".
    pb_.reset();
    indexComplete_ = true;
  }

  if (starts_.empty() || !haveHeld) {
    // No pages at all: a cover or a title page. Nought is a KNOWN count, exactly as
    // openFirstPage treats it.
    pb_.reset();
    indexComplete_ = true;
    return false;
  }
  at_ = static_cast<int>(starts_.size()) - 1;
  page_ = std::move(held);
  page_.lastPage = indexComplete_ && at_ + 1 >= static_cast<int>(starts_.size());
  syncVm();
  return true;
}

// --- THE RING OF LAID-OUT PAGES ------------------------------------------------
//
// A linear scan over at most kPageCacheDepth entries, deliberately: at three entries
// a map is more code, more allocation and slower than three comparisons of two ints.

const Page* ReaderScreen::cachedPage(int chapter, Cursor start) const {
  for (const CachedPage& e : pageRing_)
    if (e.chapter == chapter && e.start == start) return &e.page;
  return nullptr;
}

void ReaderScreen::cachePage(int chapter, Cursor start, const Page& p) {
  // COUNTED BEFORE THE DEPTH GUARD, so the figure is "pages laid out" and not "pages
  // the ring happened to keep" -- the second would go to zero if the ring were ever
  // disabled and would take the restore's cost measurement with it.
  if (!p.lines.empty()) ++ring_.stored;
  if (kPageCacheDepth <= 0) return;
  // An empty page is not worth a slot and is the one thing a hit must never be
  // mistaken for -- a chapter that paginates to nothing takes the same code path.
  if (p.lines.empty()) return;
  for (size_t i = 0; i < pageRing_.size(); ++i) {
    if (pageRing_[i].chapter == chapter && pageRing_[i].start == start) {
      // Already held. Move it to the front rather than re-copying it: the eviction
      // rule is least-recently-USED, and a page revisited is the one most likely to
      // be wanted again.
      const auto at = pageRing_.begin() + static_cast<std::ptrdiff_t>(i);
      if (i > 0) std::rotate(pageRing_.begin(), at, at + 1);
      return;
    }
  }
  if (static_cast<int>(pageRing_.size()) >= kPageCacheDepth) pageRing_.pop_back();
  // THE COPY IS THE COST AND IT IS PAID ON EVERY PAGE TURN: ~1.5 KB and a dozen small
  // allocations (measured -- test_page_cache.cpp). Against a 439 ms panel and a
  // ~376 ms decode it is noise, and it cannot be a move: `page_` is what the theme
  // renders.
  pageRing_.insert(pageRing_.begin(), CachedPage{chapter, start, p});
}

bool ReaderScreen::showCached(int p) {
  if (p < 0 || p >= static_cast<int>(starts_.size())) return false;
  const Page* hit = cachedPage(chapterAt_, starts_[static_cast<size_t>(p)]);
  if (hit == nullptr) return false;
  ++ring_.hits;
  page_ = *hit;
  at_ = p;
  // RECOMPUTED, NOT RESTORED, and it is the one field of a Page that is not a
  // property of the page: `lastPage` is "is there another one after this", which
  // depends on how much of the chapter has been counted SINCE. The expression is
  // seekTo's own, so a cached page and a decoded one answer identically.
  page_.lastPage = (p + 1 >= static_cast<int>(starts_.size()));
  // NOTHING WAS DECODED, so there is no stream positioned after this page. The
  // caller that needs one asks for it (seekTo's `needStream`); Gesture::Next handles
  // a null builder already.
  pb_.reset();
  cachePage(chapterAt_, starts_[static_cast<size_t>(p)], page_);  // freshen its slot
  return true;
}

bool ReaderScreen::seekTo(int p, bool needStream) {
  if (!needStream && showCached(p)) return true;
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
  // backward turn costs on a stream that cannot be seeked -- ~376 ms on the device,
  // and the number the ring exists to avoid paying. Counted here rather than at the
  // call sites so the figure cannot miss one.
  ++ring_.decodes;
  // AND IT STOPS SKIPPING A FEW PAGES EARLY, KEEPING WHAT IT PASSES. The walk goes
  // over those pages either way -- the inflate, the parse and the wrap are already
  // paid for every one of them -- and all `startAt` saves on them is the LINE
  // BUILDING, which this project measured at ~15% of a whole walk. Building three
  // pages of lines instead of one is a fraction of that fraction.
  //
  // What it buys is the case the ring could not reach: a reader going BACKWARDS
  // through new ground. That was one full rewind per page -- ~1010 ms each on the
  // device, deep in a long chapter, and it is what the device reported after the
  // ring landed. It is now one rewind per kPageCacheDepth pages, with the rest free.
  //
  // Only the ring's own depth back, never further: pages older than it can hold
  // would be laid out and immediately evicted, which is the cost with none of the
  // benefit.
  const int from = p >= kPageCacheDepth ? p - (kPageCacheDepth - 1) : 0;
  pb_->startAt(starts_[static_cast<size_t>(from)]);

  fed_ = 0;
  Block b;
  int at = from;
  for (int guard = 0; guard < kMaxPages * 4; ++guard) {
    if (!chapter_.next(b)) break;
    pb_->add(b, fed_++);
    b = Block{};
    while (pb_->ready() && at <= p) {
      Page produced = pb_->take();
      cachePage(chapterAt_, starts_[static_cast<size_t>(at)], produced);
      if (at == p) {
        page_ = std::move(produced);
        at_ = p;
        page_.lastPage = (p + 1 >= static_cast<int>(starts_.size()));
        return true;
      }
      ++at;
    }
  }
  page_ = pb_->finish();
  at_ = p;
  page_.lastPage = true;
  pb_.reset();  // spent: a forward turn from here has nothing to continue
  cachePage(chapterAt_, starts_[static_cast<size_t>(p)], page_);
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
  // KEYED OFF `starts_` RATHER THAN OFF `thisStart`, though the two are equal by
  // construction here: seekTo looks the page up by `starts_[p]`, so a store keyed any
  // other way would be a second spelling of the key, free to disagree with the first.
  if (at_ >= 0 && at_ < static_cast<int>(starts_.size()))
    cachePage(chapterAt_, starts_[static_cast<size_t>(at_)], page_);
  return true;
}

bool ReaderScreen::goToChapter(int spine) {
  if (spine < 0 || spine >= book_.chapterCount()) return false;
  if (spine == chapterAt_) return true;  // already there; a jump to here is a no-op
  // A JUMP OVERWRITES THE ANCHOR UNCONDITIONALLY, with the position being LEFT.
  // Captured before the move for that reason -- and this is the call that makes
  // Contents safe, which shipped without it and took the reader's place with it.
  const AnchorPos from = here();
  if (!openChapterAt(spine, /*atEnd=*/false)) return false;
  anchorJumped(from);
  return true;
}

// Landing on an anchor. A jump in mechanism and NOT in the anchor's sense -- the
// anchor has already been spent by `follow()`, so this must not re-set it.
bool ReaderScreen::goToAnchor(const AnchorPos& to) {
  if (to.spine != chapterAt_) {
    if (!openChapterAt(to.spine, /*atEnd=*/false)) return false;
  }
  if (!openAtCursor(Cursor{to.block, to.line})) return false;
  syncVm();
  return true;
}

bool ReaderScreen::indexPending() const {
  // Not gated on having a book: an in-memory chapter is counted the same way, so the
  // simulator and the goldens exercise the same path the device does.
  return !indexComplete_ && body_ != nullptr && !starts_.empty();
}

bool ReaderScreen::completeIndex(StopFn stop, void* ctx) {
  if (!indexPending()) return false;
  const int wasPage = at_;
  // SPENT BEFORE THE WALK, not after it: countPages rewinds the ChapterReader the
  // builder is reading from, so a builder left standing would be pointing at a stream
  // position that no longer exists. This is the one piece of state an abandoned count
  // does not restore -- see completeIndex's header for why there is no second stream
  // to walk with.
  pb_.reset();

  std::vector<Cursor> built;
  if (countPages(built, stop, ctx) != CountOutcome::Counted) {
    // ABANDONED, OR A CHAPTER THAT CANNOT BE WALKED. Either way nothing was
    // committed: `starts_`, `at_` and the page on glass are exactly as they were, and
    // `indexComplete_` is still false, so the next quiet window tries again from the
    // beginning. The work already done is thrown away rather than resumed -- resuming
    // would mean checkpointing the DEFLATE stream, which is 32 KB a checkpoint, to
    // save a walk that is now interruptible and therefore cheap to repeat.
    return false;
  }
  // A COMPLETED WALK THAT FOUND NO PAGES, over a chapter that was showing one. That
  // is a contradiction rather than a state, and the honest answer to it is to keep
  // what is on glass: the old shape cleared `starts_` first, so this case left the
  // reader with an empty index under a page it was still displaying.
  if (built.empty()) return false;
  starts_ = std::move(built);
  indexComplete_ = true;
  at_ = wasPage < static_cast<int>(starts_.size()) ? wasPage
                                                   : static_cast<int>(starts_.size()) - 1;
  // THE RESTORE LEG IS USUALLY FREE NOW. `at_` is by definition the most recently
  // laid-out page, so it is the page the ring is most certain to be holding -- which
  // takes the second of the count's two full passes off the critical path entirely.
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
  syncAnchorLabel();
}

// THE THREE TRANSITIONS GO THROUGH HERE, and the reason is an ordering bug this
// caught: `openChapterAt` calls `syncVm()` itself, so on the chapter-crossing paths
// the footer label was computed BEFORE the anchor was set and came out empty -- an
// anchor that existed and did not draw, which is the dead-button defect wearing the
// other face. Every transition now re-syncs the label immediately after, in one
// place, rather than at each of the four call sites where one can be missed.
void ReaderScreen::anchorPagedForward(const AnchorPos& from) {
  anchor_.pagedForward(from, here());
  syncAnchorLabel();
}

void ReaderScreen::anchorPagedBackward(const AnchorPos& from) {
  anchor_.pagedBackward(from, here());
  syncAnchorLabel();
}

// A NOTE ON THE SHAPE OF THESE THREE, because one of them shipped as infinite
// recursion and took the device down with a stack-protection fault.
//
// The bug: a scripted edit rewrote the call sites `anchor_.jumped(from, here())` into
// `anchorJumped(from)` with a replace that had NO COUNT -- and this helper's own body
// was character-for-character one of those call sites, because it used the same
// parameter name `from`. So it replaced itself with a call to itself. Its two siblings
// escaped only because their call sites happened to say `fromNext` and `fromPrev`.
//
// NO TEST CAUGHT IT. 873 passed over a function that could only ever recurse, because
// nothing exercised `goToChapter` -- the jump, which is the Contents path. That gap is
// closed now; the shape is kept as a reminder that a replacement matching more than
// you meant is this project's most productive source of defects.
void ReaderScreen::anchorJumped(const AnchorPos& from) {
  anchor_.jumped(from, here());
  syncAnchorLabel();
}

// The footer's third field, or empty when there is nowhere to go.
void ReaderScreen::syncAnchorLabel() {
  vm_.anchorLabel.clear();
  if (!anchor_.isSet()) return;
  const AnchorPos a = anchor_.get();
  if (a.spine == chapterAt_) {
    // SAME CHAPTER, so the page is a lookup and nothing is decoded. The anchor's
    // page is the last page whose start is at or before it -- which is what a page
    // CONTAINING a cursor means, and why this is not a search for an exact match.
    int page = 0;
    for (size_t i = 0; i < starts_.size(); ++i) {
      const AnchorPos start{chapterAt_, starts_[i].block, starts_[i].line};
      if (start <= a) page = static_cast<int>(i) + 1;
    }
    if (page > 0) {
      vm_.anchorLabel = "P. " + std::to_string(page);
      return;
    }
    // The index does not reach it yet -- the count grows by reading. The chapter
    // label is still true, so fall through rather than promising nothing.
  }
  // ACROSS CHAPTERS THE PAGE IS NOT FREE, so this says which chapter. Naming the
  // page would mean paginating the anchor's chapter to count its boundaries.
  //
  // AND IT IS THE POSITION, NOT THE NAME, which this got wrong first. A chapter's
  // name is unbounded -- a real one is `PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT` --
  // and this field has ~90px between the percent and the counter. Eliding it to
  // `PREMIÈRE PA…` says less than nothing.
  //
  // `CH. NN` is bounded at seven characters, is the shorthand the header already
  // falls back to for a book with no contents, and CANNOT BE CONFUSED WITH THE
  // HEADER, which is showing the current chapter's name two hundred pixels above.
  // Which chapter to return to is the useful fact; what it is called is already on
  // screen.
  char buf[16];
  std::snprintf(buf, sizeof(buf), "CH. %02d", a.spine + 1);
  vm_.anchorLabel.assign(buf);
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
      // A backward turn spends the builder, and so does an abandoned page count. Two
      // ways back from that, and the ORDER IS THE POINT:
      //
      //   * THE RING FIRST, because the page after this one is exactly the page the
      //     reader just came back from, which is the pattern the ring exists for. It
      //     costs no decode at all, and it leaves the builder null -- so a reader
      //     bouncing between two pages never decodes either of them again.
      //   * THE DECODE ONLY IF THAT MISSES, and it must be `needStream`: a cache hit
      //     here would leave `pb_` null, advance() would report false, and this
      //     function would read that as the end of the chapter and turn to the next
      //     one from the middle of this.
      //
      // A LIVE BUILDER STILL BEATS BOTH and is why this whole branch is under a null
      // check: advancing the stream is ~20 ms on the device against ~376 ms to
      // re-establish it, so the ring must never be preferred to a stream that stands.
      if (pb_ == nullptr) {
        const AnchorPos fromCached = here();
        if (showCached(at_ + 1)) {
          anchorPagedForward(fromCached);
          syncVm();
          return Action::redraw();
        }
        if (!seekTo(at_, /*needStream=*/true)) return Action::none();
      }
      const AnchorPos fromNext = here();
      if (advance()) {
        // FORWARD NEVER RAISES AN ANCHOR, it only spends one -- reading back up to
        // where you were ends the excursion. The transition runs AFTER the move
        // because it compares against where the reader arrived.
        anchorPagedForward(fromNext);
        syncVm();
        return Action::redraw();
      }
      // OFF THE END OF THE CHAPTER IS THE NEXT CHAPTER, which is what makes this a
      // reader rather than a chapter viewer.
      if (!openChapterAt(chapterAt_ + 1, false)) return Action::none();
      // OFF THE END OF A CHAPTER IS STILL PAGING FORWARD, not a jump. A jump would
      // overwrite the anchor with the position being left, so a reader who paged
      // back and then read on through a chapter boundary would find their anchor
      // silently moved to the boundary.
      anchorPagedForward(fromNext);
      return Action::redraw();
    }
    case Gesture::Prev: {
      // Where the page index earns itself: the stream only goes forward, so an
      // earlier page means rewinding and decoding to its recorded cursor. Without
      // the index there would be no cursor to decode TO.
      const AnchorPos fromPrev = here();
      if (at_ <= 0) {
        // And back off the top is the PREVIOUS chapter's LAST page, so paging
        // backwards through a book is continuous rather than stopping at each
        // chapter's start.
        if (!openChapterAt(chapterAt_ - 1, true)) return Action::none();
        anchorPagedBackward(fromPrev);
        return Action::redraw();
      }
      if (!seekTo(at_ - 1)) return Action::none();
      // PAGING BACK IS HOW A READER LOSES THEIR PLACE, far more often than by
      // jumping -- so this is the transition that makes the anchor appear during
      // ordinary reading. It sets only if unset; one already standing holds still.
      anchorPagedBackward(fromPrev);
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
    // THE FRONT ROW'S LEFT BUTTON, which reaches here only because this screen
    // declares `declareSplitMovers()` -- with the pairs folded together as every
    // other screen has them, all four movement buttons page and there is no free
    // binding at all. See Gesture::AltPrev.
    case Gesture::AltPrev: {
      AnchorPos target{};
      // NOTHING WHEN THERE IS NO ANCHOR, and the footer draws no field then. The
      // absence of the promise is the absence of the affordance -- this project has
      // shipped a dead button twice, so the two are wired to the same fact rather
      // than to two agreeing conditions.
      if (!anchor_.follow(&target)) return Action::none();
      if (!goToAnchor(target)) return Action::none();
      return Action::redraw();
    }
    default:
      return Action::none();
  }
}

void ReaderScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                          Plane plane) const {
  if (body_ == nullptr) return;
  theme.renderReader(fb, fonts, *body_, italic_, vm_, page_, plane);
}

}  // namespace reader
