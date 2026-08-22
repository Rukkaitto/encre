#pragma once
#include <memory>
#include <string>
#include <vector>

#include "reader/app.h"
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
// --- FORWARD IS FREE; BACKWARD RE-DECODES ------------------------------------
//
// A DEFLATE stream cannot be seeked, and checkpointing one costs 32 KB a
// checkpoint. So the reading position keeps its stream and its PageBuilder alive
// and TURNING FORWARD CONTINUES THEM -- the common case, and it costs one page of
// layout. Going back, or jumping, rewinds and decodes forward to the target: ~100
// to 300 ms on device against a ~520 ms panel refresh.
class ReaderScreen : public Screen {
 public:
  // `fs` and `body` must outlive the screen. `where` is what openBook returned.
  //
  // The screen is not renderable until setMetrics has been called, exactly as
  // Library is not until setVisibleRows: a page count depends on a column height
  // and onGesture has no framebuffer to ask. Before it, the page is empty and the
  // counter reads 0 -- a readable screen rather than an abort.
  ReaderScreen(FileSystem& fs, const ChapterLocation& where, std::string bookTitle,
               std::string chapter, const GlyphSource* body);

  // A chapter already in memory, through the same layers minus the inflate. What
  // the simulator and the goldens render, having no card -- and the reason
  // ChapterReader has a buffer entry point at all.
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
  int pageCount() const { return static_cast<int>(starts_.size()); }
  int pageIndex() const { return at_; }

  // Why the chapter stopped being readable, or empty. A card pulled mid-book, or a
  // stream that turned out to be corrupt partway through.
  const char* error() const { return chapter_.error(); }

 private:
  void buildIndex();
  // Renders page `p` by rewinding and decoding forward to it. The general path.
  bool seekTo(int p);
  // Renders the page after the current one by continuing the live stream. The
  // common path, and the reason the builder is kept alive between turns.
  bool advance();
  void syncVm();

  ChapterReader chapter_;
  const GlyphSource* body_;
  PageMetrics metrics_{};

  // One cursor per page, in order.
  std::vector<Cursor> starts_;
  int at_ = 0;

  // The live position: a builder mid-chapter and the index of the next block to
  // feed it. Null when the stream is not positioned for a forward turn.
  std::unique_ptr<PageBuilder> pb_;
  int fed_ = 0;

  Page page_{};
  ReaderViewModel vm_{};
  std::string bookTitle_, chapter_label_;
};

}  // namespace reader
