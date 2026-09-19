// The join dialog's fourth copy shape, and the two things a copy change owes.
//
// #162: a ninth network joined, was refused a slot and was silently not saved.
// The fix is a sentence on the glass, and a sentence on the glass is a WRAP
// question -- `test_book_error_copy.cpp` is the same file one screen over, and
// its own header records what the omission cost there: the shipped copy broke
// after `appears to be` because the next word needed 337px against a 336px
// column, ONE PIXEL, and the panel grew 41px taller than the board's while
// design-vs-firmware read 11.12% against 3.58% for the sibling screen.
//
// THE SLACK IS THE WRONG METRIC. A line with 15px of slack is safe when the
// next word is 130px wide and on a knife edge when it is 14px, so what is
// asserted is by how much the NEXT WORD overflowed.
//
// ONLY THE NEW SHAPE IS GATED, AND THAT IS A STATEMENT ABOUT THE OTHERS RATHER
// THAN AN OMISSION. Measured in this tree, the shipped shapes clear by 19px
// (BadPassword), 6px (NotFound) and 40px (Incomplete) -- so NotFound already
// sits INSIDE the floor this file would impose, by about half. That is a real
// finding and it is not this card's to fix: changing it is a copy change on a
// compared board, with its own re-bless and its own humanizer pass. The figure
// is recorded here rather than asserted, so the next person to touch that
// sentence finds the number instead of rediscovering it.
#include <algorithm>
#include <string>
#include <string_view>

#include "doctest.h"
#include "ramp.h"
#include "reader/components.h"
#include "reader/screen_wifi_error.h"
#include "reader/wifi_store.h"

namespace {

// design/WifiErrorListFull.dc.html: the 380px panel less its two borders and
// two 20px paddings -- panelContentW(kConfirmPanelW) - 2 * kPanelPadX, which is
// what renderWifiError wraps against. The same column BookError uses.
constexpr int kCopyColW = 336;
// The lead renderWifiError wraps with (the board's `line-height: 1.45`).
constexpr int kCopyLeadEm = 1450;

// The engines differ by ~3% of the column, which is ~10px here, so anything
// under that is a coin toss between Chrome and the panel. 12 is that with a
// little over -- test_book_error_copy.cpp's number, deliberately the same.
constexpr int kMinOverflow = 12;

int tightestNextWordOverflow(const reader::GlyphSource& body, const std::string& text) {
  // WordBreak::Anywhere, because that is what renderWifiError passes: an SSID
  // is 32 arbitrary octets and is frequently one unbreakable token.
  const reader::Prose p = reader::wrapProse(body, text, kCopyColW, kCopyLeadEm, {},
                                            reader::WordBreak::Anywhere);
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

std::string messageFor(reader::JoinFailure why) {
  // The BOARD's SSID. A shorter specimen would measure a sentence nobody drew.
  return reader::WifiErrorScreen("PENDRAGON", why).vm().message;
}

}  // namespace

TEST_CASE("the list-full sentence does not break within a pixel of the column") {
  ramp::Ramp ramp;
  const reader::GlyphSource& body = ramp.fonts[reader::Role::Body400];

  // Written against this test rather than measured after the fact, which is
  // the point of the rule being mechanical for a screen. Four wordings that
  // state the cap outright were tried and three of them landed at 2px -- the
  // shipped one clears by 17.
  const int over = tightestNextWordOverflow(body, messageFor(reader::JoinFailure::ListFull));
  CAPTURE(over);
  CHECK(over >= kMinOverflow);
}

TEST_CASE("the list-full copy says the three things it exists to say") {
  // THE BOARD IS THE AUTHORITY and this is not a second transcript of it: each
  // check is one CLAIM the sentence has to carry, so a rewording that keeps
  // the job passes and one that drops a job fails. `make compare` would report
  // `ok` either way, because it measures the board against the panel and is
  // blind to what the board SAYS.
  const std::string m = messageFor(reader::JoinFailure::ListFull);

  // 1. THE JOIN WORKED. The reader just watched it finish, and a sentence that
  //    led with the refusal would send them back to re-type a passphrase that
  //    was accepted. `joined` is the first word after the name for that
  //    reason, so its POSITION is asserted and not merely its presence.
  const size_t joined = m.find("joined");
  REQUIRE(joined != std::string::npos);
  CHECK(joined < m.find("full"));

  // 2. THE CAP IS NAMED. "The list is full" without a number leaves the reader
  //    unable to tell a limit from a fault.
  CHECK(m.find("full at eight") != std::string::npos);

  // 3. THERE IS A REMEDY. A refusal with no remedy reads as a fault -- which
  //    is the difference between this dialog and the silence #162 reported.
  CHECK(m.find("Forget one") != std::string::npos);

  // And the closer all four shapes share, true on every path.
  CHECK(m.find("Wi-Fi is off again.") != std::string::npos);

  // NOT A FAILURE, AND THE CAPTION HAS TO SAY SO. `COULDN'T JOIN` on a join
  // that succeeded is the false-claim shape the other three shapes exist to
  // prevent, arriving from the other side.
  const std::string caption = reader::WifiErrorScreen("PENDRAGON", reader::JoinFailure::ListFull)
                                  .vm()
                                  .caption;
  CHECK(caption.find("JOIN") == std::string::npos);
  CHECK(caption.find("SAVE") != std::string::npos);
  // The three radio failures keep theirs.
  CHECK(reader::WifiErrorScreen("PENDRAGON", reader::JoinFailure::NotFound)
            .vm()
            .caption.find("JOIN") != std::string::npos);
}

TEST_CASE("the sentence's `eight` is kMaxSavedNetworks, spelled") {
  // THE COPY CANNOT READ THE CONSTANT -- it is a WORD, because the sentence was
  // measured against the wrap floor at this width and a number it does not know
  // at authoring time cannot be measured at all. So the coupling is asserted
  // instead: raising the cap is a COPY CHANGE, and this is what says so before
  // the device starts telling readers the list is full at eight while holding
  // sixteen.
  CHECK(reader::kMaxSavedNetworks == 8);
  CHECK(messageFor(reader::JoinFailure::ListFull).find("eight") != std::string::npos);
}

TEST_CASE("the one-slab shape empties the movers rather than promising a choice") {
  // With one slab `SELECT` promises a choice and Up and Down have no second row
  // to reach -- SdMissing's rule, and WallabagError's at its own one-slab
  // shapes. The empty slots are 36px wide, not zero (kHintEmptySlotW); this
  // asserts the LABELS, and the golden asserts the geometry.
  // NAMED, not `Screen(...).vm()`: binding a reference to a member of a
  // returned temporary does not extend its lifetime, and the dangling read
  // came back as an EMPTY slab list -- which is exactly what the bug this
  // file is about looks like.
  const reader::WifiErrorScreen listFull("PENDRAGON", reader::JoinFailure::ListFull);
  const reader::WifiErrorViewModel& full = listFull.vm();
  REQUIRE(full.actions.size() == 1);
  CHECK(full.hints[1] == "OK");
  CHECK(full.hints[2].empty());
  CHECK(full.hints[3].empty());
  // THE HOLD RING IS EMPTY ON ALL FOUR. A screen may not promise a hold it has
  // not bound.
  for (bool h : full.holds) CHECK_FALSE(h);

  // And a multi-slab shape still names the movers, so the branch is read off
  // the slab list rather than off `why_`.
  const reader::WifiErrorScreen notFound("PENDRAGON", reader::JoinFailure::NotFound);
  const reader::WifiErrorViewModel& two = notFound.vm();
  REQUIRE(two.actions.size() == 2);
  CHECK(two.hints[1] == "SELECT");
  CHECK(two.hints[2] == "UP");
  CHECK(two.hints[3] == "DOWN");
}

TEST_CASE("EDIT PASSWORD and TRY AGAIN are ABSENT on the list-full shape, not inert") {
  // A slab that draws and does nothing is the works-only-sometimes trap this
  // project has shipped twice; a slab that is not there teaches nothing because
  // there is nothing to press. BookErrorMemory's lesson, verbatim.
  //
  //   EDIT PASSWORD -- the password is not what went wrong. It WORKED.
  //   TRY AGAIN     -- the cap does not change between two presses of a slab,
  //                    so it would join, be refused identically and land back
  //                    here. WallabagErrorNoNetwork drops it on that argument.
  const reader::WifiErrorScreen screen("PENDRAGON", reader::JoinFailure::ListFull);
  const reader::WifiErrorViewModel& vm = screen.vm();
  CHECK_FALSE(vm.offersEdit);
  for (const std::string& a : vm.actions) {
    CHECK(a != "EDIT PASSWORD");
    CHECK(a != "TRY AGAIN");
  }
}
