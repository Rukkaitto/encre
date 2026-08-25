// Does inline emphasis reach the glass, and does it survive the wrap?
//
// Two questions that look like one on a device and are not. A word that should be
// italic and is not has three separate explanations:
//
//   1. the PARSE found no emphasis -- document.cpp reads <em>, <i> and <cite>, and a
//      book marking its italics with a class and a stylesheet carries none of them;
//   2. the parse found it and the WRAP lost it, re-basing spans onto a line that
//      falls the wrong side of a boundary (clipTo, emphasis.h);
//   3. no italic face is installed, where drawTextStyled falls back to the roman --
//      silently and correctly, which is what makes it hard to see.
//
// (1) is a property of the book and cannot be tested here. These pin (2) and (3),
// and the device's `[page] ... emph=N ital=N` line reports all three so a report
// from glass can be told apart without guessing.
#include <string>
#include "doctest.h"
#include "ramp.h"
#include "reader_fixture.h"
#include "reader/layout.h"
#include "reader/screen_reader.h"
#include "reader/theme_quiet.h"

namespace {
std::string oneParaEmphAt(int wordIndex, int words) {
  std::string d = "<html><body><p>";
  for (int w = 0; w < words; ++w)
    d += (w == wordIndex) ? "<em>faim</em> " : "mot ";
  return d + "</p></body></html>";
}
// Total emphasised bytes across every line of every page of the chapter.
size_t emphBytesOverChapter(reader::ReaderScreen& s) {
  size_t n = 0;
  for (int i = 0; i < 200; ++i) {
    for (const reader::LaidLine& ln : s.page().lines)
      for (const reader::Span& sp : ln.emphasis) n += sp.len;
    const int was = s.pageIndex();
    s.onGesture({reader::Gesture::Next});
    if (s.pageIndex() == was) break;
  }
  return n;
}
}  // namespace

TEST_CASE("emphasis survives the wrap wherever the word falls") {
  ramp::Ramp ramp;
  readerfix::Body body;
  readerfix::Italic italic;
  reader::QuietTheme theme;

  for (auto geo : {std::pair<int,int>{480,800}, std::pair<int,int>{528,792}}) {
    reader::PageMetrics m;
    theme.readerMetrics(geo.first, geo.second, ramp.fonts, body.face, m);
    int lost = 0;
    std::string lostAt;
    for (int w = 0; w < 60; ++w) {
      const std::string doc = oneParaEmphAt(w, 60);
      auto scr = std::make_unique<reader::ReaderScreen>(doc, "T", "CH. 01", &body.face);
      scr->setItalic(&italic.face);
      scr->setMetrics(m);
      scr->completeIndex();
      const size_t got = emphBytesOverChapter(*scr);
      if (got != 4) {  // "faim"
        ++lost;
        lostAt += " w=" + std::to_string(w) + "(" + std::to_string(got) + ")";
      }
    }
    MESSAGE("geometry " << geo.first << "x" << geo.second << " lost=" << lost << lostAt);
    CHECK(lost == 0);
  }
}

TEST_CASE("the italic face reaches the glass, and its absence is silent") {
  // The frames must DIFFER. If they do not, the italic face never reached the page
  // and every emphasised word on the device is drawn roman -- which is exactly what
  // an unset face looks like, and is why this compares pixels rather than spans.
  const std::string doc = oneParaEmphAt(20, 60);
  ramp::Ramp ramp;
  readerfix::Body body;
  readerfix::Italic italic;
  reader::QuietTheme theme;
  reader::PageMetrics m;
  theme.readerMetrics(480, 800, ramp.fonts, body.face, m);

  auto renderWith = [&](const reader::GlyphSource* ital) {
    auto scr = std::make_unique<reader::ReaderScreen>(doc, "T", "CH. 01", &body.face);
    scr->setItalic(ital);
    scr->setMetrics(m);
    scr->completeIndex();
    REQUIRE(scr->pageEmphasisRuns() > 0);
    CHECK(scr->hasItalic() == (ital != nullptr));
    reader::Framebuffer fb(480, 800);
    fb.clear(true);
    scr->render(fb, ramp.fonts, theme, reader::Plane::Bw);
    return fb;
  };

  const reader::Framebuffer roman = renderWith(nullptr);
  const reader::Framebuffer slanted = renderWith(&italic.face);
  bool identical = true;
  for (int i = 0; i < roman.sizeBytes(); ++i)
    if (roman.data()[i] != slanted.data()[i]) { identical = false; break; }
  CHECK_FALSE(identical);
}
