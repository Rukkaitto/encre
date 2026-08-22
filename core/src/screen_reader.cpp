#include "reader/screen_reader.h"

#include "reader/glyphsource.h"
#include "reader/theme.h"

namespace reader {
namespace {

// A runaway guard on the pagination walk, not a design limit. Every page consumes
// at least one line of a document already capped at kMaxBlocks blocks, so a real
// chapter cannot approach this -- it exists because the walk's termination depends
// on layoutPage advancing, and a guard is cheaper than trusting that from here.
constexpr int kMaxPages = 4096;

}  // namespace

ReaderScreen::ReaderScreen(Document doc, std::string bookTitle, std::string chapter,
                           const GlyphSource* body)
    : doc_(std::move(doc)),
      body_(body),
      bookTitle_(std::move(bookTitle)),
      chapter_(std::move(chapter)) {
  syncVm();
}

void ReaderScreen::setMetrics(const PageMetrics& m) {
  metrics_ = m;
  paginate();
  layoutCurrent();
  syncVm();
}

void ReaderScreen::paginate() {
  starts_.clear();
  if (body_ == nullptr || metrics_.columnW <= 0 || metrics_.columnH <= 0) return;

  const int blocks = static_cast<int>(doc_.blocks.size());
  Cursor at{};
  for (int guard = 0; guard < kMaxPages && at.block < blocks; ++guard) {
    starts_.push_back(at);
    const Page p = layoutPage(doc_, *body_, metrics_, at);
    // A column too short for one line box reports a page that does not advance.
    // layout.h says the caller must not loop on that; this is that caller.
    if (p.next == at) break;
    at = p.next;
    if (p.lastPage) break;
  }
  // A re-paginate at a different column height can leave fewer pages than the one
  // being read -- a font size change will, once that setting exists. Landing on
  // the last page is the honest answer: the position is lost either way, and the
  // end of the chapter is where the reader was closest to.
  if (at_ >= static_cast<int>(starts_.size()))
    at_ = starts_.empty() ? 0 : static_cast<int>(starts_.size()) - 1;
}

void ReaderScreen::layoutCurrent() {
  page_ = Page{};
  if (body_ == nullptr) return;
  if (at_ < 0 || at_ >= static_cast<int>(starts_.size())) return;
  page_ = layoutPage(doc_, *body_, metrics_, starts_[static_cast<size_t>(at_)]);
}

void ReaderScreen::syncVm() {
  vm_.bookTitle = bookTitle_;
  vm_.chapter = chapter_;
  const int total = static_cast<int>(starts_.size());
  vm_.pageTotal = total;
  vm_.page = total == 0 ? 0 : at_ + 1;
  // Rounded once, and off the page just READ rather than the one about to be:
  // the board's 53 of 890 is 5.955%, shown as 6%, so the number is the position
  // reached and not the position started from.
  vm_.progressPercent = total == 0 ? 0 : (vm_.page * 100 + total / 2) / total;
}

Action ReaderScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next: {
      // A HELD button turns pages one at a time here, unlike a list. `steps` is
      // what accelerates a Library scroll; on a panel that costs ~520ms a repaint
      // and a page that has to be laid out, a repeat that skipped four pages would
      // be four pages the reader never saw.
      if (at_ + 1 >= static_cast<int>(starts_.size())) return Action::none();
      ++at_;
      layoutCurrent();
      syncVm();
      return Action::redraw();
    }
    case Gesture::Prev: {
      // Where the once-paginated index earns itself: layoutPage only walks
      // forward, so without the list of page starts the previous page would mean
      // re-walking the chapter from its first block.
      if (at_ <= 0) return Action::none();
      --at_;
      layoutCurrent();
      syncVm();
      return Action::redraw();
    }
    case Gesture::Back:
      return Action::pop();
    // Activate opens design/ReaderMenu.dc.html, which is not built. It answers
    // none() rather than doing something approximate, and it is listed as a known
    // no-op rather than left to be discovered -- a button that does nothing is a
    // defect this project has shipped twice.
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
