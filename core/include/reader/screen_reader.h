#pragma once
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/document.h"
#include "reader/layout.h"
#include "reader/viewmodel.h"

namespace reader {
class GlyphSource;

// design/Reader.dc.html: the reading page.
//
// IT OWNS THE CHAPTER. A LaidLine is a view into the Document's own text, so the
// Document has to outlive every page laid out from it, and the screen is the
// natural owner -- the alternative is a chapter held somewhere above with a
// lifetime rule nothing enforces.
//
// THE CHAPTER IS PAGINATED ONCE, into a list of page-start cursors, and that one
// decision answers three things layout.h deliberately leaves open:
//
//   * How many pages there are, which the footer's counter needs.
//   * Which page this is, ditto.
//   * How to go BACK a page. layoutPage only walks forward -- it takes a start
//     and reports where the next page begins -- so the previous page's start is
//     not recoverable from the current one without re-walking from the top.
//
// It costs one wrap per page (~12 for a chapter) and every wrap is advances only,
// so the whole index is measured, not rasterised. It is NOT a book-wide index:
// the board's footer says "53 / 890" and this says "3 / 12", because a book-wide
// number is a pass over every chapter in the EPUB and that pass does not exist.
// ReaderViewModel carries the two numbers plainly so that pass can fill them in
// later without this screen or the theme changing.
class ReaderScreen : public Screen {
 public:
  // `body` must outlive the screen -- it is the ScalableFont the shell and the
  // simulator each own. `doc` is MOVED IN, for the ownership reason above.
  //
  // The screen is not renderable until setMetrics has been called, exactly as
  // Library is not until setVisibleRows: a page count depends on a column height
  // and onGesture has no framebuffer to ask. Before it, the page is empty and the
  // counter reads 0 -- which is a readable screen rather than an abort.
  ReaderScreen(Document doc, std::string bookTitle, std::string chapter,
               const GlyphSource* body);

  ScreenId id() const override { return ScreenId::Reader; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // Body text is the one thing on this device drawn from a runtime-rasterised
  // face, and the whole reason ScalableFont packs its cache in fontc.py's 2bpp
  // format is so its edges can carry grey. Mono would throw that away and
  // hard-threshold every stem of a serif face at 32px.
  Fidelity fidelity() const override { return Fidelity::Grayscale; }

  // The column, from Theme::readerMetrics. Re-paginates.
  void setMetrics(const PageMetrics& m);

  const ReaderViewModel& vm() const { return vm_; }
  const Page& page() const { return page_; }
  int pageCount() const { return static_cast<int>(starts_.size()); }
  int pageIndex() const { return at_; }

 private:
  void paginate();
  void layoutCurrent();
  void syncVm();

  Document doc_;
  const GlyphSource* body_;
  PageMetrics metrics_{};
  // One cursor per page, in order. Empty until setMetrics.
  std::vector<Cursor> starts_;
  int at_ = 0;
  Page page_{};
  ReaderViewModel vm_{};
  std::string bookTitle_, chapter_;
};

}  // namespace reader
