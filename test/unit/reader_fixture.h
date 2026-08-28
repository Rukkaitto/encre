#pragma once
// The Reader's test fixtures, shared.
//
// These lived in an anonymous namespace inside test_theme_reader_golden.cpp until a
// second test file wanted them. Extracted rather than copied, on this project's own
// rule that the second copy is the extraction point -- a `Reading` that drifted
// between two files would have two tests believing they set up the same screen.
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/screen_reader.h"
#include "reader/theme_quiet.h"

namespace readerfix {

// The body face at the board's `font-size: 32px`, with its bytes beside it: a
// ScalableFont borrows the buffer it was initialised from and never copies it.
//
// THE ppem IS A PARAMETER, defaulted to the reading default so every caller is
// untouched -- AND NO CALLER PASSES ONE TODAY, which is worth stating rather than
// leaving to be discovered. It was added for a Typography golden at ppem 38, and
// that golden is at the default now because the BOARD moved to 15 PT; the parameter
// is kept because the reason for it survives the number. A ScalableFont is pinned
// to one pixel size by init(), so any screen previewing or rendering at a
// non-default size needs a face built at it, and a fixture that can only make one
// size forces a local copy of itself -- which test_theme_typography.cpp's `BodyAt`
// already is.
struct Body {
  std::vector<uint8_t> bytes = golden::slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
  reader::ScalableFont face;
  explicit Body(int ppem = reader::kBodyPpem) {
    REQUIRE(face.init(bytes.data(), bytes.size(), ppem));
    REQUIRE(face.ready());
  }
};

// The ITALIC face, at the same ppem. A second file, because Literata.ttf carries no
// `ital` and no `slnt` axis -- see document.h.
struct Italic {
  std::vector<uint8_t> bytes =
      golden::slurp(std::string(ASSETS_DIR) + "/built/literata_italic.ttf");
  reader::ScalableFont face;
  explicit Italic(int ppem = reader::kBodyPpem) {
    REQUIRE(face.init(bytes.data(), bytes.size(), ppem));
    REQUIRE(face.ready());
  }
};

// A chapter big enough to have real pagination, as XHTML so it goes through the
// same tokenizer and block builder a card would feed.
inline std::string longChapter(int paragraphs) {
  std::string d = "<html><body><h1>Chapter One</h1>";
  for (int i = 0; i < paragraphs; ++i) {
    d += "<p>Paragraph " + std::to_string(i) +
         " of a chapter long enough that its pages have to be found rather than "
         "assumed, carrying an accent (caf&#233;) and an em dash &#8212; so the "
         "decoder is exercised alongside the layout.</p>";
  }
  d += "</body></html>";
  return d;
}

// A chapter big enough that its page count is DEFERRED rather than taken before the
// first paint. Sized against the constant rather than a magic paragraph count, so it
// stays a deferred chapter if the threshold ever moves.
inline std::string deferredChapter() {
  int paragraphs = 64;
  std::string d = longChapter(paragraphs);
  while (d.size() <= reader::ReaderScreen::kEagerCountBytes + 4096) {
    paragraphs *= 2;
    d = longChapter(paragraphs);
    if (paragraphs > 8192) break;  // guard; never reached with any sane threshold
  }
  return d;
}

struct Reading {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  Body body;
  reader::PageMetrics m;
  std::unique_ptr<reader::ReaderScreen> scr;

  // `settled` completes the page index, which is what the device does within five
  // seconds of a chapter opening -- so it is the state almost every test wants. Pass
  // false to observe the moment a chapter opens, before the count is known.
  //
  // `startAt` restores a saved position, exactly as the shell does: set before
  // setMetrics, because setMetrics is the landing.
  explicit Reading(const std::string& xhtml, bool settled = true, int w = 480, int h = 800,
                   reader::Cursor startAt = reader::Cursor{}) {
    theme.readerMetrics(w, h, ramp.fonts, body.face, reader::Settings{}, m);
    scr = std::make_unique<reader::ReaderScreen>(xhtml, "Middlemarch", "CH. 01", &body.face);
    if (startAt != reader::Cursor{}) scr->restoreAt(startAt);
    scr->setMetrics(m);
    if (settled) scr->completeIndex();
  }
};

inline std::string pageText(const reader::Page& p) {
  std::string s;
  for (const reader::LaidLine& ln : p.lines) s += ln.text + "|";
  return s;
}

}  // namespace readerfix
