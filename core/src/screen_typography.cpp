#include "reader/screen_typography.h"

#include <array>
#include <iterator>
#include <string>

#include "reader/theme.h"

namespace reader {
namespace {

// THE BOARD'S ROWS, IN THE BOARD'S ORDER. Five rows, no section headers -- the
// board draws one flat block.
struct Item {
  const char* label;
  TypographyScreen::Field field;
};
constexpr std::array<Item, 5> kItems{{
    {"Font", TypographyScreen::Field::Font},
    {"Size", TypographyScreen::Field::Size},
    {"Margins", TypographyScreen::Field::Margins},
    {"Line spacing", TypographyScreen::Field::LineSpacing},
    {"Alignment", TypographyScreen::Field::Alignment},
}};

// --- THE FOUR LABELS, AND THEY ARE LOCAL TO THIS FILE -------------------------
//
// An earlier draft put these in settings.h, because Settings was going to read the
// same five values out in its own TYPOGRAPHY section. It does not any more -- those
// five rows became one disclosing door -- so there is exactly ONE caller. The rule
// is "the second copy is the extraction point", not "extract in advance of one".

// pt = ppem * 72 / 150, TRUNCATED, exactly as sleepLabel truncates minutes: the
// row describes a size the reader is looking at, and rounding up would name a size
// the panel is not showing. Every offered ppem gives the same label either way
// (25->12, 32->15, 38->18, 42->20, 46->22), which is why the bottom step is 25 and
// not 27 -- see kBodyPpemSteps' own comment.
std::string typographySizeLabel(int ppem) {
  return std::to_string(ppem * 72 / 150) + " PT";
}

// A PARALLEL ARRAY TO kMarginSteps, with a static_assert on the lengths, so a step
// added without a label fails to compile rather than drawing an empty value.
constexpr const char* kMarginLabels[] = {"TIGHT", "COMFORTABLE", "WIDE"};
static_assert(std::size(kMarginLabels) == std::size(kMarginSteps),
              "every margin step needs a label");

std::string typographyMarginLabel(int margins) {
  for (size_t i = 0; i < std::size(kMarginSteps); ++i)
    if (kMarginSteps[i] == margins) return kMarginLabels[i];
  // Unreachable: validate() snaps the field onto the table before the screen sees
  // it. The middle step's label rather than an empty string, because a row with no
  // value reads as a screen that failed to load something.
  return kMarginLabels[std::size(kMarginLabels) / 2];
}

// `1.7`, and `2.0` rather than `2`: a bare integer reads as a count beside `1.85`
// instead of as a ratio.
std::string typographyLeadLabel(int em1000) {
  return std::to_string(em1000 / 1000) + "." + std::to_string((em1000 / 100) % 10);
}

const char* typographyAlignLabel(bool justify) { return justify ? "JUSTIFIED" : "RAGGED"; }

// Where `value` sits in an ascending table, or 0 when it is not on it.
//
// Settings::validate() snaps every field onto its table before the screen ever
// sees it, so the fallback is unreachable in practice. It is 0 rather than an
// assert because a screen that cannot be constructed is a device that cannot show
// its settings, and index 0 is a value the cycle can leave.
template <size_t N>
int indexIn(const int (&table)[N], int value) {
  for (size_t i = 0; i < N; ++i)
    if (table[i] == value) return static_cast<int>(i);
  return 0;
}

// The next index in a wrapping cycle. Not Focus's: this is an index into a value
// table, not a focus, and borrowing Focus here would mean a second Focus object
// per screen whose range changes with the focused row.
int nextIndex(int at, int count) { return count <= 1 ? at : (at + 1) % count; }

}  // namespace

TypographyScreen::TypographyScreen(const Settings& initial, SettingsSink* sink,
                                  const GlyphSource* body)
    : FocusScreen(static_cast<int>(kItems.size()), static_cast<int>(kItems.size())),
      settings_(initial),
      sink_(sink),
      body_(body) {
  // SNAPPED ON THE WAY IN, so the cycle indexes a table the value is on. The
  // shell's settings have already been through validate(), but the simulator and
  // the tests construct this directly -- and a screen that trusted its caller here
  // would show a value it could not cycle off.
  settings_.validate();
  // The first focusable row, not row 0: row 0 is `Font`, which has one value.
  setFocus(firstFocusable());
  syncVm();
}

int TypographyScreen::valueCount(Field f) {
  switch (f) {
    // ONE FACE IS VENDORED. This is the number that makes the Font row
    // unreachable, and it is the one line to change when a second face lands --
    // focusability is derived from it, so nothing else has to be touched.
    case Field::Font: return 1;
    case Field::Size: return static_cast<int>(std::size(kBodyPpemSteps));
    case Field::Margins: return static_cast<int>(std::size(kMarginSteps));
    case Field::LineSpacing: return static_cast<int>(std::size(kLineSpacingSteps));
    case Field::Alignment: return 2;
  }
  return 1;
}

bool TypographyScreen::focusable(int index) const {
  if (index < 0 || index >= static_cast<int>(kItems.size())) return false;
  return valueCount(kItems[static_cast<size_t>(index)].field) > 1;
}

int TypographyScreen::firstFocusable() const {
  for (size_t i = 0; i < kItems.size(); ++i)
    if (focusable(static_cast<int>(i))) return static_cast<int>(i);
  // Unreachable with the table above, and not an assert: a table edited down to
  // nothing focusable should render a readable screen rather than abort a boot.
  return 0;
}

Action TypographyScreen::cycleFocused() {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(kItems.size())) return Action::none();
  const Field field = kItems[static_cast<size_t>(f)].field;

  switch (field) {
    case Field::Font:
      // Unreachable: a one-value row is not focusable. Answers none() rather than
      // asserting, because a restored focus is the one way a number could arrive
      // here from outside -- and FocusScreen refuses an unlandable restore for
      // exactly that reason.
      return Action::none();
    case Field::Size:
      settings_.bodyPpem =
          kBodyPpemSteps[nextIndex(indexIn(kBodyPpemSteps, settings_.bodyPpem), valueCount(field))];
      break;
    case Field::Margins:
      settings_.margins =
          kMarginSteps[nextIndex(indexIn(kMarginSteps, settings_.margins), valueCount(field))];
      break;
    case Field::LineSpacing:
      settings_.lineSpacing = kLineSpacingSteps[nextIndex(
          indexIn(kLineSpacingSteps, settings_.lineSpacing), valueCount(field))];
      break;
    case Field::Alignment:
      settings_.justify = !settings_.justify;
      break;
  }

  // The value is shown whether or not the write succeeded -- see SettingsSink.
  if (sink_ != nullptr) sink_->commit(settings_);
  syncVm();
  return Action::redraw();
}

Action TypographyScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    // ONE ROW AT A TIME whatever the distance: this screen declares no repeat, so
    // a gesture never carries more than one step, and stepping past the
    // unfocusable row is what moveFocus is for.
    case Gesture::Prev: return moveFocus(-1);
    case Gesture::Next: return moveFocus(+1);
    case Gesture::Activate: return cycleFocused();
    // A PLAIN POP, and it was popTo(ScreenId::Reader) until Settings became a
    // second door. From Settings there is no Reader on the stack and popTo stops at
    // the root (app.h), so it would have dumped the user on Home and lost Settings.
    //
    // The panel cannot re-paginate the Reader itself in either case: core/ has no
    // faces to re-rasterise. The shell does it, keyed on a Reader being ANYWHERE on
    // the stack rather than on top -- which the reader-menu route needs anyway,
    // because popping here lands on the MENU and the menu is an overlay, so
    // App::render paints the stale page beneath it on the very next frame.
    case Gesture::Back: return Action::pop();
    default: return Action::none();
  }
}

void TypographyScreen::syncVm() {
  vm_.title = "TYPOGRAPHY";
  vm_.specimen = kSpecimen;
  // THE PREVIEW'S LEAD IS THE SETTING, so the box shows Line spacing as well as
  // Size. The theme cannot derive it: a face is pinned to a ppem by init() and
  // carries no leading.
  vm_.leadEm1000 = settings_.lineSpacing;
  vm_.focusedRow = focus();

  vm_.rows.clear();
  vm_.rows.reserve(kItems.size());
  for (const Item& it : kItems) {
    ListRow row;
    row.label = it.label;
    switch (it.field) {
      // ONE FACE, and the row states its name rather than reading a field. A
      // `font` setting whose only value is its default would be a second spelling
      // of this constant.
      case Field::Font: row.value = "LITERATA"; break;
      case Field::Size: row.value = typographySizeLabel(settings_.bodyPpem); break;
      case Field::Margins: row.value = typographyMarginLabel(settings_.margins); break;
      case Field::LineSpacing: row.value = typographyLeadLabel(settings_.lineSpacing); break;
      case Field::Alignment: row.value = typographyAlignLabel(settings_.justify); break;
    }
    row.isHeader = false;
    // NOTHING DISCLOSES. Every row edits in place, so no row draws a chevron.
    row.discloses = false;
    row.focusable = valueCount(it.field) > 1;
    vm_.rows.push_back(std::move(row));
  }

  // The board's own labels, and they do not change: there is one mode.
  vm_.hints = {"BACK", "CHANGE", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

void TypographyScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                              Plane plane) const {
  // body_ may be null, and the THEME handles that -- it is what knows the specimen
  // cannot be drawn without a face, and resolving it here would mean inventing a
  // null GlyphSource for a case one branch covers.
  theme.renderTypography(fb, fonts, body_, vm_, plane);
}

}  // namespace reader
