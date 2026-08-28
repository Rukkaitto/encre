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

TEST_CASE("CHANGE cycles the focused value and commits what it shows") {
  RecordingSink sink;
  reader::TypographyScreen scr(reader::Settings{}, &sink, nullptr);
  REQUIRE(scr.focus() == 1);  // Size

  scr.onEvent(kConfirm);
  CHECK(scr.settings().bodyPpem == 38);  // 32 -> 38
  CHECK(scr.vm().rows[1].value == "18 PT");
  CHECK(scr.focus() == 1);  // the focus did NOT move
  // AND THE COMMIT CARRIES WHAT THE ROW SHOWS. A screen that cycled its own copy
  // and committed a stale one would be invisible until the next boot.
  CHECK(sink.commits == 1);
  CHECK(sink.last.bodyPpem == 38);
}

TEST_CASE("every cycle returns to where it started, on every multi-value row") {
  // Checked on all four, because the cycle is four switch arms. And the whole
  // table is walked rather than one step taken: a cycle that skipped a value or
  // stuck at the end would pass a single-step test.
  struct Case {
    int row;
    int count;
  };
  const Case cases[] = {{1, 5}, {2, 3}, {3, 5}, {4, 2}};
  for (const Case& c : cases) {
    CAPTURE(c.row);
    reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
    while (scr.focus() != c.row) scr.onEvent(kDown);
    const std::string first = scr.vm().rows[static_cast<size_t>(c.row)].value;

    // Every intermediate value is DISTINCT from the first, so a cycle that
    // returned early -- or never moved -- fails here rather than at the end.
    for (int i = 1; i < c.count; ++i) {
      scr.onEvent(kConfirm);
      CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value != first);
    }
    scr.onEvent(kConfirm);
    CHECK(scr.vm().rows[static_cast<size_t>(c.row)].value == first);  // WRAPPED
  }
}

TEST_CASE("the size cycle visits every offered step, in the table's order") {
  // The one row where the ORDER is visible to the reader, and where a wrong order
  // would read as a broken control rather than as a different design.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  REQUIRE(scr.focus() == 1);
  const char* want[] = {"18 PT", "20 PT", "22 PT", "12 PT", "15 PT"};  // wraps at 46 -> 25
  for (const char* w : want) {
    scr.onEvent(kConfirm);
    CHECK(scr.vm().rows[1].value == std::string(w));
  }
}

TEST_CASE("CHANGE on the Font row cannot happen, and costs nothing if it does") {
  // The row is not focusable, so a press cannot reach it. Asserted through
  // setFocus anyway, because a restored focus is the one way a number could arrive
  // from outside -- and a waveform spent on an identical frame is the cost.
  RecordingSink sink;
  reader::TypographyScreen scr(reader::Settings{}, &sink, nullptr);
  CHECK_FALSE(scr.setFocus(0));  // refused: not focusable
  CHECK(scr.focus() == 1);       // still on Size
  CHECK(sink.commits == 0);
}

TEST_CASE("a refused write still shows the new value") {
  // SettingsSink's contract: the change HAS taken effect in RAM, and reverting the
  // display would make a read-only card look like a screen that ignores its
  // buttons.
  RecordingSink sink;
  sink.answer = false;
  reader::TypographyScreen scr(reader::Settings{}, &sink, nullptr);
  scr.onEvent(kConfirm);
  CHECK(sink.commits == 1);
  CHECK(scr.settings().bodyPpem == 38);
  CHECK(scr.vm().rows[1].value == "18 PT");
}

TEST_CASE("a held mover carries one row, not its distance") {
  // The gesture layer drops a Long on a mover and only emits Repeat where the
  // screen asked for one -- and this screen asks for none. `steps` is ignored here
  // rather than trusted, because a repeat that did arrive would walk the focus
  // forty rows through a five-row list.
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  reader::GestureEvent g;
  g.what = reader::Gesture::Next;
  g.steps = 40;
  g.held = true;
  scr.onGesture(g);
  CHECK(scr.focus() == 2);  // ONE row, not forty
}

TEST_CASE("every value the screen can reach survives validate") {
  // The screen may only offer values validate accepts, or a step would be undone
  // by the save that follows it. Walked through the SCREEN rather than over the
  // tables, so it covers the cycle and the tables together.
  for (int row = 1; row <= 4; ++row) {
    CAPTURE(row);
    reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
    while (scr.focus() != row) scr.onEvent(kDown);
    for (int i = 0; i < 6; ++i) {
      scr.onEvent(kConfirm);
      reader::Settings s = scr.settings();
      CHECK(s.validate());  // already valid: nothing to snap
      CHECK(s == scr.settings());
    }
  }
}

TEST_CASE("the screen reports the right id and fidelity") {
  reader::TypographyScreen scr(reader::Settings{}, nullptr, nullptr);
  CHECK(scr.id() == reader::ScreenId::Typography);
  // Chrome, not the reader: one waveform, not three.
  CHECK(scr.fidelity() == reader::Fidelity::Mono);
}
