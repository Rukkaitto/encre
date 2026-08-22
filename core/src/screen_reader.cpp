#include "reader/screen_reader.h"

#include <new>

#include "reader/glyphsource.h"
#include "reader/theme.h"

namespace reader {
namespace {

// A runaway guard on the pagination walk, not a design limit. Every page consumes
// at least one line of a chapter already capped at kMaxBlocks blocks, so a real
// chapter cannot approach this -- it exists because the walk's termination depends
// on the builder advancing, and a guard is cheaper than trusting that from here.
constexpr int kMaxPages = 4096;

}  // namespace

ReaderScreen::ReaderScreen(FileSystem& fs, const ChapterLocation& where,
                           std::string bookTitle, std::string chapter,
                           const GlyphSource* body)
    : body_(body),
      bookTitle_(std::move(bookTitle)),
      chapter_label_(std::move(chapter)) {
  // The stream is opened here so a book that cannot be read says so before the
  // screen is pushed; the pagination waits for setMetrics, which is the first
  // moment a column height exists.
  chapter_.begin(fs, where);
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
  buildIndex();
  at_ = 0;
  seekTo(0);
  syncVm();
}

void ReaderScreen::buildIndex() {
  starts_.clear();
  pb_.reset();
  if (body_ == nullptr || !chapter_.ok()) return;
  if (!chapter_.rewind()) return;

  PageBuilder pb(*body_, metrics_);
  if (!pb.viable()) return;

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
  const Page last = pb.finish();
  if (!last.lines.empty() && static_cast<int>(starts_.size()) < kMaxPages)
    starts_.push_back(pending);
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
  Block b;
  for (int guard = 0; guard < kMaxPages * 4; ++guard) {
    if (pb_->ready()) break;
    if (!chapter_.next(b)) break;
    pb_->add(b, fed_++);
    b = Block{};
  }
  if (pb_->ready()) {
    page_ = pb_->take();
  } else {
    page_ = pb_->finish();
    pb_.reset();
  }
  ++at_;
  page_.lastPage = (at_ + 1 >= static_cast<int>(starts_.size()));
  return true;
}

void ReaderScreen::syncVm() {
  vm_.bookTitle = bookTitle_;
  vm_.chapter = chapter_label_;
  const int total = static_cast<int>(starts_.size());
  vm_.pageTotal = total;
  vm_.page = total == 0 ? 0 : at_ + 1;
  // Rounded once, and off the page just READ rather than the one about to be: the
  // board's 53 of 890 is 5.955%, shown as 6%, so the number is the position reached
  // and not the position started from.
  vm_.progressPercent = total == 0 ? 0 : (vm_.page * 100 + total / 2) / total;
}

Action ReaderScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next: {
      // A HELD button turns pages one at a time here, unlike a list. `steps` is
      // what accelerates a Library scroll; on a panel that costs ~520 ms a repaint
      // and a page that has to be decoded, a repeat that skipped four pages would
      // be four pages the reader never saw.
      if (at_ + 1 >= static_cast<int>(starts_.size())) return Action::none();
      // THE FAST PATH: continue the live stream rather than decoding the chapter
      // again. Falls back to a seek if the stream is not positioned -- after the
      // last page, or after a backward turn that spent the builder.
      if (!advance() && !seekTo(at_ + 1)) return Action::none();
      syncVm();
      return Action::redraw();
    }
    case Gesture::Prev: {
      // Where the page index earns itself: the stream only goes forward, so an
      // earlier page means rewinding and decoding to its recorded cursor. Without
      // the index there would be no cursor to decode TO.
      if (at_ <= 0) return Action::none();
      if (!seekTo(at_ - 1)) return Action::none();
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
