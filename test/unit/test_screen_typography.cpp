// The Typography panel: five rows over four settings, one mode, and a value
// cycle that wraps.
#include <string>

#include "doctest.h"
#include "reader/screen_typography.h"
#include "reader/settings.h"

namespace {

using reader::Button;
using reader::PressKind;

// A sink that records what it was handed, so a test can assert the screen commits
// -- and can assert it commits the value it is SHOWING, which is the one
// disagreement that would be invisible on glass.
struct RecordingSink : reader::SettingsSink {
  reader::Settings last{};
  int commits = 0;
  bool answer = true;
  bool commit(const reader::Settings& s) override {
    last = s;
    ++commits;
    return answer;
  }
};

const reader::InputEvent kDown{Button::Down, PressKind::Short};
const reader::InputEvent kUp{Button::Up, PressKind::Short};
const reader::InputEvent kConfirm{Button::Confirm, PressKind::Short};
const reader::InputEvent kBack{Button::Back, PressKind::Short};

}  // namespace

TEST_CASE("the panel draws the board's five rows, in the board's order") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const reader::TypographyViewModel& vm = scr.vm();
  REQUIRE(vm.rows.size() == 5);
  CHECK(vm.rows[0].label == "Font");
  CHECK(vm.rows[1].label == "Size");
  CHECK(vm.rows[2].label == "Margins");
  CHECK(vm.rows[3].label == "Line spacing");
  CHECK(vm.rows[4].label == "Alignment");
  for (const reader::ListRow& r : vm.rows) {
    CHECK_FALSE(r.isHeader);
    // NOTHING DISCLOSES: every row edits in place.
    CHECK_FALSE(r.discloses);
    // The tracking column has no producer anywhere in this firmware and must not
    // acquire one here by accident.
    CHECK(r.trackingEm1000 == 0);
  }
  CHECK_FALSE(vm.specimen.empty());
  CHECK(vm.title == "TYPOGRAPHY");
}

TEST_CASE("the values shown are the settings', in the board's forms") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const reader::TypographyViewModel& vm = scr.vm();
  CHECK(vm.rows[0].value == "LITERATA");
  // 32 * 72 / 150 = 15.36, truncated -- and 15 PT is what Settings.dc.html now
  // states for the same default.
  CHECK(vm.rows[1].value == "15 PT");
  CHECK(vm.rows[2].value == "COMFORTABLE");
  CHECK(vm.rows[3].value == "1.7");
  CHECK(vm.rows[4].value == "JUSTIFIED");
}

TEST_CASE("the preview's lead is the Line spacing setting") {
  // THE THEME HAS NO OTHER WAY TO KNOW IT. A face is pinned to a ppem by init()
  // and carries no leading, so without this field the preview would be drawn at a
  // constant 1.7 and would contradict the row directly beneath it on four of that
  // row's five steps.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.vm().leadEm1000 == 1700);
  while (scr.focus() != 3) scr.onEvent(kDown);
  scr.onEvent(kConfirm);
  CHECK(scr.vm().leadEm1000 == scr.settings().lineSpacing);
  CHECK(scr.vm().leadEm1000 != 1700);
}

TEST_CASE("the focus starts on Size and skips Font") {
  // FONT HAS ONE VALUE, so CHANGE on it would produce an identical frame -- the
  // silent no-op this project has been bitten by twice. The focus skips it, which
  // is Settings' rule for a row with nothing behind it, and it is drawn exactly
  // as any unfocused row.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.focus() == 1);
  CHECK_FALSE(scr.vm().rows[0].focusable);
  for (size_t i = 1; i < 5; ++i) CHECK(scr.vm().rows[i].focusable);
}

TEST_CASE("the focus wraps and never lands on Font") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  // Down from 1 to 4, then the wrap must skip row 0 and land on 1.
  CHECK(scr.focus() == 1);
  for (int i = 0; i < 3; ++i) scr.onEvent(kDown);
  CHECK(scr.focus() == 4);
  scr.onEvent(kDown);
  CHECK(scr.focus() == 1);  // WRAPPED, over Font
  scr.onEvent(kUp);
  CHECK(scr.focus() == 4);  // and the other way, also over Font
  // Exhaustively: forty presses either way never reach row 0.
  for (int i = 0; i < 40; ++i) {
    scr.onEvent(kDown);
    CHECK(scr.focus() != 0);
  }
  for (int i = 0; i < 40; ++i) {
    scr.onEvent(kUp);
    CHECK(scr.focus() != 0);
  }
}

TEST_CASE("Back is a plain pop, not a popTo(Reader)") {
  // IT WAS popTo(ScreenId::Reader), and Settings becoming a second entry point is
  // what broke it: from Settings there is no Reader on the stack, and popTo stops
  // at the root (app.h), so BACK would have dumped the user on Home and lost the
  // Settings screen underneath.
  //
  // The reader-menu route loses nothing by this. Popping the panel lands on the
  // MENU, which is an overlay -- App::render walks down to the Reader and paints
  // it under the veil -- so the shell has to re-lay the page out on the way past
  // anyway, keyed on a Reader being ANYWHERE on the stack rather than on top.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const reader::Action a = scr.onEvent(kBack);
  CHECK(a.kind == reader::Action::Kind::Pop);
}

TEST_CASE("the hint bar is the board's, and never changes") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  const std::array<std::string, 4> want{"BACK", "CHANGE", "UP", "DOWN"};
  CHECK(scr.vm().hints == want);
  scr.onEvent(kConfirm);
  CHECK(scr.vm().hints == want);
  scr.onEvent(kDown);
  CHECK(scr.vm().hints == want);
  for (const bool h : scr.vm().holds) CHECK_FALSE(h);
}

TEST_CASE("the screen reports the right id and fidelity") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.id() == reader::ScreenId::Typography);
  // Chrome, not the reader: one waveform, not three.
  CHECK(scr.fidelity() == reader::Fidelity::Mono);
}
