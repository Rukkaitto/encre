#include "reader/screen_settings.h"

#include <array>

#include "reader/theme.h"
#include "reader/version.h"

namespace reader {
namespace {

// THE BOARD'S ROWS, IN THE BOARD'S ORDER, and the order is the only thing that
// makes this table checkable against design/Settings.dc.html by eye. Seven items:
// two section headers and five rows, which FITS the panel -- so Settings draws no
// rail today.
//
// IT WAS ELEVEN. A TYPOGRAPHY section carried Font, Size, Margins, Line spacing and
// Alignment, drawn and unreachable because the settings behind them did not exist.
// They do now, and they are edited on their own screen -- so five rows that merely
// DISPLAYED them became one row that OPENS it. A placeholder is right only until
// the setting exists; after that it is a screen showing a number nobody can trust.
// READING rather than TYPOGRAPHY so the section is a sibling of DEVICE, has room
// for the reading settings still to come, and does not repeat the row's own word
// directly above it.
//
// It also had a CONNECTIONS section with a Wi-Fi row, and losing that is what
// brought the list back inside the panel in the first place: V1 is card-transfer
// only, Wi-Fi having been cut as too big. Nothing here has to change when the list
// overflows again -- renderSettings reads `totalRows > rows` and draws the rail and
// takes its gutter only then.
//
// The placeholder string is the BOARD'S value for the one row whose setting does not
// exist yet, kept verbatim so the screen matches the board. It is not a default and
// nothing reads it back.
constexpr std::array<SettingsScreen::Item, 7> kItems{{
    {"READING", SettingsScreen::Field::None, true, false, ""},
    {"Typography", SettingsScreen::Field::Typography, false, true, ""},
    {"DEVICE", SettingsScreen::Field::None, true, false, ""},
    {"Sleep after", SettingsScreen::Field::SleepAfter, false, true, ""},
    {"Full refresh", SettingsScreen::Field::FullRefresh, false, true, ""},
    {"Refresh on screen change", SettingsScreen::Field::OnTransition, false, true, ""},
    {"Sleep screen", SettingsScreen::Field::None, false, false, "BOOK COVER"},
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
    : FocusScreen(static_cast<int>(kItems.size()), 0), settings_(initial), sink_(sink) {
  // The first focusable row, not row 0: row 0 is the READING header. That lands on
  // `Typography`, which is the first row with anything behind it -- the focus sat on
  // `Sleep after` only while every row above it was inert. syncVm
  // runs unconditionally after, because a table edited down to nothing focusable
  // leaves the setFocus refused and the screen must still render readably.
  setFocus(firstFocusable());
  syncVm();
}

bool SettingsScreen::focusable(int index) const {
  if (index < 0 || index >= static_cast<int>(kItems.size())) return false;
  const Item& it = kItems[static_cast<size_t>(index)];
  // NOT `field != None`, which used to serve here and cannot any more: `Typography`
  // has no field and must be focusable, because it discloses a screen instead of
  // editing a value. See Item::reachable.
  return !it.isHeader && it.reachable;
}

int SettingsScreen::firstFocusable() const {
  for (size_t i = 0; i < kItems.size(); ++i)
    if (focusable(static_cast<int>(i))) return static_cast<int>(i);
  // Unreachable with the table above, and not an assert: a table edited down to
  // nothing focusable should render a readable screen rather than abort a boot.
  return 0;
}

void SettingsScreen::setMetrics(int listH, int rowH, int headerH) {
  // Counted from the TOP of the list, and that is the conservative end on purpose.
  // The top window carries the most headers -- both sections begin within the first
  // three items -- so any window further down fits at least as many
  // items. A count that varied with scroll position would make the rail's
  // proportion move as the user scrolled, which reads as the list changing length.
  int used = 0, n = 0;
  for (const Item& it : kItems) {
    const int h = it.isHeader ? headerH : rowH;
    if (h <= 0 || used + h > listH) break;
    used += h;
    ++n;
  }
  window().setVisibleRows(n);
  syncVm();
}

Action SettingsScreen::cycleFocused() {
  const int f = focus();
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
    case Field::Typography:
      // Handled by onGesture BEFORE we get here -- this row discloses rather than
      // edits, so there is nothing to cycle and nothing to commit. Listed rather
      // than swept into a `default:`: -Wswitch naming a field nobody handled is the
      // point of this switch, and a `default:` would throw that away the day a
      // fifth field arrives.
      return Action::none();
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
    // ONE ROW HERE OPENS A SCREEN AND THE REST EDIT IN PLACE, so Activate answers
    // the push before it can reach cycleFocused -- which has nothing to cycle for
    // that row and says so.
    case Gesture::Activate: {
      const int f = focus();
      if (f >= 0 && f < static_cast<int>(kItems.size()) &&
          kItems[static_cast<size_t>(f)].field == Field::Typography)
        return Action::push(ScreenId::Typography);
      return cycleFocused();
    }
    case Gesture::Back: return Action::pop();
    default: return Action::none();
  }
}

void SettingsScreen::syncVm() {
  vm_.title = "SETTINGS";
  vm_.version = std::string("V ") + kVersion;

  const ScrollWindow::Slice s = window().slice();
  vm_.firstRow = s.first;
  vm_.totalRows = window().count();
  // Slice's own rule: the focus as an index into what is drawn, or -1, so this
  // cannot name a row that is not on glass.
  vm_.focusedRow = s.focused;

  vm_.rows.clear();
  vm_.rows.reserve(static_cast<size_t>(s.count));
  for (int i = 0; i < s.count; ++i) {
    const int at = s.first + i;
    const Item& it = kItems[static_cast<size_t>(at)];
    SettingsRow row;
    row.label = it.label;
    row.isHeader = it.isHeader;
    row.focusable = focusable(at);
    if (!it.isHeader) {
      row.discloses = it.field == Field::Typography;
      switch (it.field) {
        // A DISCLOSING ROW HAS NO VALUE. Home's menu rows state the rule -- a row
        // states a quantity or discloses a screen, never both -- and summarising
        // four typography settings into the right slot would break it and would not
        // fit. The chevron is the whole content of that slot.
        case Field::Typography: break;
        case Field::SleepAfter: row.value = sleepLabel(settings_.sleepAfterMs); break;
        case Field::FullRefresh: row.value = refreshLabel(settings_.fullRefreshEvery); break;
        case Field::OnTransition: row.value = settings_.fullOnTransition ? "ON" : "OFF"; break;
        case Field::None: row.value = it.placeholder; break;
      }
    }
    vm_.rows.push_back(std::move(row));
  }

  // THE CONFIRM LABEL FOLLOWS THE FOCUSED ROW, and this is the FIRST hint bar in
  // this firmware whose text varies within one screen.
  //
  // This comment used to state the premise outright -- "CHANGE, not OPEN: nothing
  // here pushes a screen, every focusable row edits a value in place" -- and the
  // READING row makes that false. The alternative is worse than a moving label: a
  // Confirm labelled CHANGE that opens a screen is the misleading-button defect this
  // project keeps recording, and it is the one thing a hint bar exists to prevent.
  // So one slot moves as the focus moves, and the bar stays true of the button it
  // names. The other three never move, because Back, Up and Down mean the same
  // thing on every row.
  const int f = focus();
  const bool opens = f >= 0 && f < static_cast<int>(kItems.size()) &&
                     kItems[static_cast<size_t>(f)].field == Field::Typography;
  vm_.hints = {"BACK", opens ? "OPEN" : "CHANGE", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

void SettingsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderSettings(fb, fonts, vm_, plane);
}

}  // namespace reader
