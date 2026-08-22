#include "reader/screen_settings.h"

#include <array>

#include "reader/theme.h"
#include "reader/version.h"

namespace reader {
namespace {

// THE BOARD'S ROWS, IN THE BOARD'S ORDER, and the order is the only thing that
// makes this table checkable against design/Settings.dc.html by eye. Eleven items:
// two section headers and nine rows, which FITS the panel -- so Settings draws no
// rail today.
//
// It did have a CONNECTIONS section with a Wi-Fi row, and losing them is what
// brought the list back inside the panel: V1 is card-transfer only, Wi-Fi having
// been cut as too big. Phase 3's typography settings will push it over again, and
// nothing here has to change when they do -- renderSettings reads
// `totalRows > rows` and draws the rail and takes its gutter only then.
//
// The placeholder strings are the BOARD'S values for rows whose settings do not
// exist yet, kept verbatim so the screen matches the board before Phase 3's reader
// and Phase 4's Wi-Fi arrive. They are not defaults and nothing reads them back.
constexpr std::array<SettingsScreen::Item, 11> kItems{{
    {"TYPOGRAPHY", SettingsScreen::Field::None, true, ""},
    {"Font", SettingsScreen::Field::None, false, "LITERATA"},
    {"Size", SettingsScreen::Field::None, false, "18 PT"},
    {"Margins", SettingsScreen::Field::None, false, "COMFORTABLE"},
    {"Line spacing", SettingsScreen::Field::None, false, "1.7"},
    {"Alignment", SettingsScreen::Field::None, false, "JUSTIFIED"},
    {"DEVICE", SettingsScreen::Field::None, true, ""},
    {"Sleep after", SettingsScreen::Field::SleepAfter, false, ""},
    {"Full refresh", SettingsScreen::Field::FullRefresh, false, ""},
    {"Refresh on screen change", SettingsScreen::Field::OnTransition, false, ""},
    {"Sleep screen", SettingsScreen::Field::None, false, "BOOK COVER"},
}};

// The values CHANGE cycles through, and they wrap: this is one button, so there is
// no way back except round. Five sleep steps and three cadences keeps a full cycle
// short enough to be usable on a panel that costs ~520 ms a repaint -- a 30-step
// list would be a minute of pressing to undo an accidental press.
constexpr std::array<uint32_t, 5> kSleepMinutes{{1, 5, 10, 15, 30}};
constexpr std::array<int, 3> kRefreshEvery{{0, 5, 15}};

std::string sleepLabel(uint32_t ms) {
  // Whole minutes. validate() clamps sleepAfterMs into a range whose bounds are
  // both whole minutes, and a file hand-edited to 90 s reads as "1 MIN" rather
  // than lying with a rounded 2 -- truncation is the honest direction here,
  // because the row is describing a timer the user cannot see tick.
  return std::to_string(ms / 60000u) + " MIN";
}

std::string refreshLabel(int every) {
  if (every <= 0) return "NEVER";
  return "EVERY " + std::to_string(every) + " PAGES";
}

}  // namespace

SettingsScreen::SettingsScreen(const Settings& initial, SettingsSink* sink)
    : settings_(initial), sink_(sink) {
  window_ = ScrollWindow(static_cast<int>(kItems.size()), 0);
  // The first focusable row, not row 0: row 0 is the TYPOGRAPHY header.
  window_.setFocus(firstFocusable());
  syncVm();
}

int SettingsScreen::firstFocusable() const {
  for (size_t i = 0; i < kItems.size(); ++i)
    if (!kItems[i].isHeader && kItems[i].field != Field::None) return static_cast<int>(i);
  // Unreachable with the table above, and not an assert: a table edited down to
  // nothing focusable should render a readable screen rather than abort a boot.
  return 0;
}

void SettingsScreen::setMetrics(int listH, int rowH, int headerH) {
  // Counted from the TOP of the list, and that is the conservative end on purpose.
  // The top window carries the most headers -- all three sections begin within the
  // first thirteen items -- so any window further down fits at least as many
  // items. A count that varied with scroll position would make the rail's
  // proportion move as the user scrolled, which reads as the list changing length.
  int used = 0, n = 0;
  for (const Item& it : kItems) {
    const int h = it.isHeader ? headerH : rowH;
    if (h <= 0 || used + h > listH) break;
    used += h;
    ++n;
  }
  window_.setVisibleRows(n);
  syncVm();
}

bool SettingsScreen::setFocus(int index) {
  // REFUSED rather than clamped when the index is not focusable. A restored focus
  // that landed on a section header could not be moved off it in one press -- the
  // move would step to the next focusable row and look like it skipped one -- and
  // "the restore did not land" is a thing the shell already knows how to report.
  if (index < 0 || index >= static_cast<int>(kItems.size())) return false;
  const Item& it = kItems[static_cast<size_t>(index)];
  if (it.isHeader || it.field == Field::None) return false;

  // "SOMETHING CHANGED", which is the contract every other screen answers and
  // which Screen::setFocus now states without contradicting itself.
  //
  // This returned "the restore LANDED" instead -- window_.focus() == index -- on
  // the reasoning that restoring onto the row a screen is already on is a
  // perfectly successful restore. That reasoning is correct and it is not what
  // this bool is for; app.h has the argument, including why the base class's own
  // default is wrong under the other reading. The question it was answering is
  // still answered, by the caller that actually wants it: App::restore compares
  // focus() to what the record asked for, which says "the record named row 12 and
  // the screen is on row 4" where a bool could only say "no".
  const bool moved = window_.setFocus(index);
  syncVm();
  return moved;
}

Action SettingsScreen::moveFocus(int dir) {
  // Step ONE AT A TIME past anything unfocusable, rather than moving by `dir` and
  // then correcting: correcting could land back where it started, which reads as
  // a dead button. `dir` is only ever +1 or -1 -- this screen does not declare
  // auto-repeat, because thirteen items do not need it.
  //
  // AND IT WRAPS, like every other list. This is the one screen that cannot get
  // wrapping from Focus by using it, because the scan past section headers is its
  // own: it stepped to the end of kItems and returned none(), so Settings quietly
  // stopped at the last row while Home, the Library and both overlays rolled over.
  // test_focus_restore.cpp's wrap case is what catches that for the next screen
  // that hand-rolls its stepping.
  const int n = static_cast<int>(kItems.size());
  const int from = window_.focus();
  int i = from;
  for (;;) {
    i += dir;
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    // All the way round without finding anywhere to land: a table edited down to
    // nothing focusable renders a readable screen rather than spinning here.
    if (i == from) return Action::none();
    const Item& it = kItems[static_cast<size_t>(i)];
    if (!it.isHeader && it.field != Field::None) break;
  }
  if (!window_.setFocus(i)) return Action::none();
  syncVm();
  return Action::redraw();
}

Action SettingsScreen::cycleFocused() {
  const int f = window_.focus();
  if (f < 0 || f >= static_cast<int>(kItems.size())) return Action::none();
  const Field field = kItems[static_cast<size_t>(f)].field;
  if (field == Field::None) return Action::none();  // cannot be focused, so cannot happen

  switch (field) {
    case Field::SleepAfter: {
      size_t at = 0;
      for (size_t i = 0; i < kSleepMinutes.size(); ++i)
        if (settings_.sleepAfterMs == kSleepMinutes[i] * 60000u) at = i;
      // A value from a hand-edited file that is not in the list lands on index 0's
      // successor rather than being preserved: the cycle is the only way to change
      // this field, so a value outside it would otherwise be unreachable to leave.
      settings_.sleepAfterMs = kSleepMinutes[(at + 1) % kSleepMinutes.size()] * 60000u;
      break;
    }
    case Field::FullRefresh: {
      size_t at = 0;
      for (size_t i = 0; i < kRefreshEvery.size(); ++i)
        if (settings_.fullRefreshEvery == kRefreshEvery[i]) at = i;
      settings_.fullRefreshEvery = kRefreshEvery[(at + 1) % kRefreshEvery.size()];
      break;
    }
    case Field::OnTransition:
      settings_.fullOnTransition = !settings_.fullOnTransition;
      break;
    case Field::None:
      return Action::none();
  }

  // The value is shown whether or not the write succeeded -- see SettingsSink.
  // The change has already taken effect in this object; a screen that reverted it
  // would be showing the user a lie about what the device is doing.
  if (sink_ != nullptr) sink_->commit(settings_);
  syncVm();
  return Action::redraw();
}

Action SettingsScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    // One at a time whatever the distance: this screen declares no repeat, so a
    // gesture never carries more than one step, and stepping past unfocusable rows
    // is what moveFocus is for.
    case Gesture::Next: return moveFocus(+1);
    case Gesture::Prev: return moveFocus(-1);
    case Gesture::Activate: return cycleFocused();
    case Gesture::Back: return Action::pop();
    default: return Action::none();
  }
}

void SettingsScreen::syncVm() {
  vm_.title = "SETTINGS";
  vm_.version = std::string("V ") + kVersion;

  vm_.firstRow = window_.firstVisible();
  vm_.totalRows = window_.count();

  vm_.rows.clear();
  const int first = window_.firstVisible();
  const int count = window_.visibleCount();
  vm_.rows.reserve(static_cast<size_t>(count));
  vm_.focusedRow = -1;
  for (int i = 0; i < count; ++i) {
    const int at = first + i;
    const Item& it = kItems[static_cast<size_t>(at)];
    SettingsRow row;
    row.label = it.label;
    row.isHeader = it.isHeader;
    row.focusable = !it.isHeader && it.field != Field::None;
    if (!it.isHeader) {
      switch (it.field) {
        case Field::SleepAfter: row.value = sleepLabel(settings_.sleepAfterMs); break;
        case Field::FullRefresh: row.value = refreshLabel(settings_.fullRefreshEvery); break;
        case Field::OnTransition: row.value = settings_.fullOnTransition ? "ON" : "OFF"; break;
        case Field::None: row.value = it.placeholder; break;
      }
    }
    if (at == window_.focus()) vm_.focusedRow = i;
    vm_.rows.push_back(std::move(row));
  }

  // CHANGE, not OPEN: nothing here pushes a screen, every focusable row edits a
  // value in place. The board says CHANGE and this is the one screen where the
  // confirm button's label is not about navigation.
  vm_.hints = {"BACK", "CHANGE", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

void SettingsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderSettings(fb, fonts, vm_, plane);
}

}  // namespace reader
