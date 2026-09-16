#include "reader/screen_settings.h"

#include <array>

#include "reader/theme.h"
#include "reader/version.h"

namespace reader {
namespace {

// THE BOARD'S ROWS, IN THE BOARD'S ORDER, and the order is the only thing that
// makes this table checkable against design/Settings.dc.html by eye. Nine items:
// three section headers and six rows, which FITS the panel -- so Settings draws no
// rail today.
//
// IT WAS ELEVEN, THEN SEVEN. A TYPOGRAPHY section carried Font, Size, Margins, Line
// spacing and Alignment, drawn and unreachable because the settings behind them did
// not exist. They do now, and they are edited on their own screen -- so five rows
// that merely DISPLAYED them became one row that OPENS it. A placeholder is right
// only until the setting exists; after that it is a screen showing a number nobody
// can trust. READING rather than TYPOGRAPHY so the section is a sibling of DEVICE,
// has room for the reading settings still to come, and does not repeat the row's own
// word directly above it.
//
// SLEEP SCREEN PUT TWO BACK, and took the last placeholder out with them. `Sleep
// screen` / `BOOK COVER` sat at the bottom of DEVICE, drawn and inert, the row
// CLAUDE.md ties to issue #11 by number -- so nothing here now states a value that
// does not come from `settings_`. The row is `Shows` rather than `Sleep screen` for
// the same reason the section above is READING rather than TYPOGRAPHY: a section
// must not repeat the word of the row directly under it.
//
// AND CONNECTIONS IS BACK, WHICH IS WHAT MAKES THE FLOW REACHABLE AT ALL. This
// paragraph read "it also had a CONNECTIONS section with a Wi-Fi row, and losing
// that is what brought the list back inside the panel" -- true of V1, where Wi-Fi
// was cut as too big, and false the moment V1.1's connect flow landed. The six
// Wi-Fi screens shipped with a board saying CONNECTIONS IS BACK and this table
// still at nine, so nothing on the device could reach WifiSettings: every screen
// built, every golden passed, and the feature had no door. `make compare` could
// not see it either -- it renders the BOARD beside the firmware, and the board
// was right; what it measured was Settings drifting AWAY from its board, 1.91%
// to 2.53%, in the one direction CLAUDE.md says silently invalidates the check.
//
// ELEVEN ITEMS, AND IT STILL DOES NOT SCROLL -- which is the thing to check
// rather than assume, because losing this section is what stopped it scrolling.
// Twelve fit, so there is no rail and no 14px gutter, and rows still run to the
// panel edge. Nothing here has to change when it overflows again: renderSettings
// reads `totalRows > rows` and takes the gutter only then.
constexpr std::array<SettingsScreen::Item, 12> kItems{{
    {"READING", SettingsScreen::Field::None, true, false},
    {"Typography", SettingsScreen::Field::Typography, false, true},
    {"SLEEP SCREEN", SettingsScreen::Field::None, true, false},
    {"Shows", SettingsScreen::Field::SleepShows, false, true},
    // `reachable` is a CEILING and not the answer here: focusable() also asks
    // whether `Shows` is showing a cover. See focusable().
    {"Cover fit", SettingsScreen::Field::CoverFit, false, true},
    {"DEVICE", SettingsScreen::Field::None, true, false},
    {"Sleep after", SettingsScreen::Field::SleepAfter, false, true},
    {"Full refresh", SettingsScreen::Field::FullRefresh, false, true},
    {"Refresh on screen change", SettingsScreen::Field::OnTransition, false, true},
    {"CONNECTIONS", SettingsScreen::Field::None, true, false},
    // A CHEVRON AND NO VALUE, which is `Typography`'s rule two sections up: a row
    // states a quantity or discloses a screen, never both. The tempting
    // `Wi-Fi . ON DEMAND` is exactly the shape that forbids, and the state it
    // would state is the one WifiSettings' own header band already carries.
    {"Wi-Fi", SettingsScreen::Field::Wifi, false, true},
    // THE DOOR TO THE ACCOUNT SCREEN, and without it that screen is reachable
    // from nothing -- which is what design/Settings.dc.html's note says this row
    // is for. Home's ARTICLES row opens the LIST, and this opens SETUP AND
    // STATUS: two doors to two different rooms.
    //
    // A CHEVRON AND NO VALUE, like the row above it and for its reason. The
    // tempting `wallabag . NOT SET UP` is the shape a row states a quantity or
    // discloses a screen, never both forbids -- and the state it would state is
    // the one WallabagAccount's own header band already carries.
    {"Wallabag", SettingsScreen::Field::Wallabag, false, true},
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

// THE BOARD'S OWN STRINGS. Deliberately not `FILL` / `FIT` for the fit: one
// letter apart is bad at 25px on this glass, which design/Settings.dc.html says
// at the row.
const char* showsLabel(SleepShows s) {
  switch (s) {
    case SleepShows::Cover: return "COVER";
    case SleepShows::CoverAndDetails: return "COVER + DETAILS";
    case SleepShows::Details: return "DETAILS";
  }
  return "COVER + DETAILS";  // unreachable; validate() refuses anything else
}

const char* fitLabel(CoverFit f) {
  switch (f) {
    case CoverFit::Fill: return "FILL";
    case CoverFit::Whole: return "WHOLE";
  }
  return "FILL";
}

// Whether the sleep screen draws a cover at all, which is what decides whether
// `Cover fit` can be acted on. ONE SPELLING, asked by focusable() -- a second
// would be the drifting-condition defect this project has shipped twice, both
// times as a dead button.
bool showsACover(SleepShows s) { return s != SleepShows::Details; }

}  // namespace

bool SettingsScreen::disclosedScreen(Field f, ScreenId& out) {
  switch (f) {
    case Field::Typography: out = ScreenId::Typography; return true;
    case Field::Wifi: out = ScreenId::WifiSettings; return true;
    case Field::Wallabag: out = ScreenId::WallabagAccount; return true;
    // Named rather than swept into a `default:`, so -Wswitch is still the
    // reminder that a new field has to answer this question -- which is the
    // whole reason the mapping is a switch and not a table lookup.
    case Field::None:
    case Field::SleepShows:
    case Field::CoverFit:
    case Field::SleepAfter:
    case Field::FullRefresh:
    case Field::OnTransition:
      return false;
  }
  return false;
}

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
  if (it.isHeader || !it.reachable) return false;
  // AND THE TABLE IS NOT THE WHOLE ANSWER. A fit is meaningless with no cover on
  // the screen, so `Cover fit` is unreachable while `Shows` reads DETAILS --
  // DERIVED from settings_ rather than tabulated, which is Typography's precedent
  // (`Font` is unreachable while one body face is vendored and becomes reachable
  // the moment a second lands, with no line to remember). The gate is asked per
  // landing, so cycling `Shows` changes this answer with nothing to invalidate.
  if (it.field == Field::CoverFit) return showsACover(settings_.sleepShows);
  return true;
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
  // The top window carries the most headers -- three of the FOUR sections begin
  // within the first six items and the fourth is last, so no window further down
  // can hold more than the top one and every one of them therefore fits at least
  // as many items. A count that varied with scroll position would make the rail's
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
    case Field::SleepShows: {
      // Wraps on kSleepShowsCount, which settings.h derives from the last
      // enumerator -- so a fourth mode joins the cycle without a number here to
      // remember. CYCLING THIS ROW CAN MAKE THE ROW BELOW INERT, and that is safe
      // by construction: the focus is on THIS row, and focusable() is re-asked on
      // the next move, so the step simply passes over `Cover fit`.
      const int at = static_cast<int>(settings_.sleepShows);
      settings_.sleepShows = static_cast<SleepShows>((at + 1) % kSleepShowsCount);
      break;
    }
    case Field::CoverFit: {
      const int at = static_cast<int>(settings_.coverFit);
      settings_.coverFit = static_cast<CoverFit>((at + 1) % kCoverFitCount);
      break;
    }
    case Field::Typography:
    case Field::Wifi:
    case Field::Wallabag:
      // Handled by onGesture BEFORE we get here -- these rows disclose rather than
      // edit, so there is nothing to cycle and nothing to commit. Listed rather
      // than swept into a `default:`: -Wswitch naming a field nobody handled is the
      // point of this switch, and a `default:` would throw that away the day a
      // sixth field arrives.
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
    // TWO ROWS HERE OPEN A SCREEN AND THE REST EDIT IN PLACE, so Activate answers
    // the push before it can reach cycleFocused -- which has nothing to cycle for
    // those rows and says so. WHICH screen comes from disclosedScreen, the same
    // call the chevron and the Confirm hint make, so a row cannot draw one and do
    // the other.
    case Gesture::Activate: {
      const int f = focus();
      ScreenId opens = ScreenId::Settings;
      if (f >= 0 && f < static_cast<int>(kItems.size()) &&
          disclosedScreen(kItems[static_cast<size_t>(f)].field, opens))
        return Action::push(opens);
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
      ScreenId to = ScreenId::Settings;
      row.discloses = disclosedScreen(it.field, to);
      switch (it.field) {
        // A DISCLOSING ROW HAS NO VALUE. Home's menu rows state the rule -- a row
        // states a quantity or discloses a screen, never both -- and summarising
        // four typography settings into the right slot would break it and would not
        // fit. The chevron is the whole content of that slot.
        case Field::Typography:
        case Field::Wifi:
        case Field::Wallabag: break;
        case Field::SleepShows: row.value = showsLabel(settings_.sleepShows); break;
        case Field::CoverFit: row.value = fitLabel(settings_.coverFit); break;
        case Field::SleepAfter: row.value = sleepLabel(settings_.sleepAfterMs); break;
        case Field::FullRefresh: row.value = refreshLabel(settings_.fullRefreshEvery); break;
        case Field::OnTransition: row.value = settings_.fullOnTransition ? "ON" : "OFF"; break;
        // A ROW WITH NO FIELD AND NO CHEVRON DRAWS NOTHING, and there is no
        // longer any such row: `Item::placeholder` was removed with `Sleep
        // screen`. Named rather than swept into a `default:` so -Wswitch still
        // fails the build the day a field is added and not handled here.
        case Field::None: break;
      }
    }
    vm_.rows.push_back(std::move(row));
  }

  // THE CONFIRM LABEL FOLLOWS THE FOCUSED ROW, and this is the FIRST hint bar in
  // this firmware whose text varies within one screen.
  //
  // This comment used to state the premise outright -- "CHANGE, not OPEN: nothing
  // here pushes a screen, every focusable row edits a value in place" -- and the
  // READING row makes that false. It is still ONE row against five: `Typography`
  // opens, and both SLEEP SCREEN rows and all three DEVICE rows cycle in place. The alternative is worse than a moving label: a
  // Confirm labelled CHANGE that opens a screen is the misleading-button defect this
  // project keeps recording, and it is the one thing a hint bar exists to prevent.
  // So one slot moves as the focus moves, and the bar stays true of the button it
  // names. The other three never move, because Back, Up and Down mean the same
  // thing on every row.
  const int f = focus();
  ScreenId to = ScreenId::Settings;
  const bool opens = f >= 0 && f < static_cast<int>(kItems.size()) &&
                     disclosedScreen(kItems[static_cast<size_t>(f)].field, to);
  vm_.hints = {"BACK", opens ? "OPEN" : "CHANGE", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

void SettingsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderSettings(fb, fonts, vm_, plane);
}

}  // namespace reader
