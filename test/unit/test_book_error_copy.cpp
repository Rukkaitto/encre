// The corrupt-book dialog's copy must not sit on the wrap boundary. #76.
//
// A SPECIMEN BOARD MUST NOT PUT A LINE ON THE WRAP BOUNDARY is one of this
// project's own rules, and until now it was only a rule -- `ReaderList`'s "Space is
// measured in rows." was moved by hand for 24px of clearance, and nothing stopped
// the next board doing it again. This screen did it again.
//
// WHAT WENT WRONG, measured rather than described: the shipped sentence broke after
// `appears to be` because the next word `damaged` needed 337px against a 336px
// column. **One pixel.** Chrome fits it, because the firmware's whole-pixel advances
// measure ~3% wider than Chrome's subpixel ones -- so the firmware wrapped to six
// lines where the board wrapped to five, the centred panel was 41px taller, every
// rule landed ~20px out, and design-vs-firmware read 11.12%/11.70% against 3.58%
// for the sibling screen that differs from it only by a sentence.
//
// THE SLACK IS THE WRONG METRIC AND CHECKING IT WOULD NOT HAVE CAUGHT THIS. A line
// with 15px of slack is perfectly safe when the next word is 130px wide, and on a
// knife edge when the next word is 14px. What decides a break is by how much the
// NEXT WORD overflowed, so that is what this asserts.
#include <algorithm>
#include <string>
#include <string_view>

#include "doctest.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/screen_book_error.h"

namespace {

// design/BookError.dc.html: panelContentW(380) less two 20px paddings.
constexpr int kCopyColW = 336;
// The lead renderBookError wraps with (the board's `line-height: 1.45`).
constexpr int kCopyLeadEm = 1450;

// How close the tightest break came to going the other way, in pixels.
//
// The engines differ by ~3% of the column, which is ~10px here, so anything under
// that is a coin toss between Chrome and the panel. 12 is that with a little over.
constexpr int kMinOverflow = 12;

int tightestNextWordOverflow(const reader::GlyphSource& body, const std::string& text) {
  const reader::Prose p = reader::wrapProse(body, text, kCopyColW, kCopyLeadEm);
  const int spaceW = body.measure(" ", p.tracking);
  int worst = 99999;
  for (int i = 0; i + 1 < p.lineCount(); ++i) {
    const std::string_view next = p.lines[i + 1];
    const size_t sp = next.find(' ');
    const std::string_view word = sp == std::string_view::npos ? next : next.substr(0, sp);
    const int over = body.measure(p.lines[i], p.tracking) + spaceW +
                     body.measure(word, p.tracking) - kCopyColW;
    worst = std::min(worst, over);
  }
  return worst;
}

std::string messageFor(reader::BookErrorReason why) {
  return reader::BookErrorScreen({"/books/dubliners.epub", "dubliners.epub", why,
                                  reader::ScreenId::Library})
      .vm()
      .message;
}

}  // namespace

TEST_CASE("neither copy shape breaks within a pixel of the column") {
  ramp::Ramp ramp;
  const reader::GlyphSource& body = ramp.fonts[reader::Role::Body400];

  SUBCASE("damaged") {
    // Was 2 before #76 -- `damaged` needed 337 against 336.
    const int over = tightestNextWordOverflow(body, messageFor(reader::BookErrorReason::Damaged));
    CAPTURE(over);
    CHECK(over >= kMinOverflow);
  }
  SUBCASE("unreadable") {
    // Was 3, so this shape agreed with Chrome by luck rather than by clearance.
    // It measured 3.58% and would have flipped on any change to the face or the ramp.
    const int over =
        tightestNextWordOverflow(body, messageFor(reader::BookErrorReason::Unreadable));
    CAPTURE(over);
    CHECK(over >= kMinOverflow);
  }
}

TEST_CASE("the copy the screen composes is the copy the board draws") {
  // THE BOARD IS THE AUTHORITY, so the check is against its text and not against a
  // second transcript of it. A drift here is a screen that renders something no
  // board has approved -- and `make compare` would report `ok` either way, because
  // it prints a word and not a percentage (#41).
  CHECK(messageFor(reader::BookErrorReason::Damaged).find("appears damaged") !=
        std::string::npos);
  CHECK(messageFor(reader::BookErrorReason::Unreadable).find("read from the card") !=
        std::string::npos);
  // The hedge is load-bearing: the firmware cannot know the file is damaged, only
  // that nothing it understands is in it. Dropping "appears" would state as fact
  // something it inferred -- the same reason there are two copy shapes at all.
  CHECK(messageFor(reader::BookErrorReason::Damaged).find("appears") != std::string::npos);
}
