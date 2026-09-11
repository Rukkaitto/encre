#include "reader/screen_wifi_settings.h"

#include "reader/theme.h"

namespace reader {
namespace {

// The board's own copy. Held here rather than in the theme because it is
// content: a theme that owned the sentence would be a second place to change
// it when the board does.
constexpr const char* kProse =
    "Wi-Fi stays off. It connects only while receiving books \xE2\x80\x94 then turns off.";
constexpr const char* kEmptyTitle = "No networks saved";
constexpr const char* kEmptyProse =
    "Join one below and Encre remembers it. Whichever you save first becomes automatic.";
constexpr const char* kJoinRow = "Join another network\xE2\x80\xA6";

}  // namespace

WifiSettingsScreen::WifiSettingsScreen(SavedNetworks nets, WifiSink* sink)
    // The row count is not known until rebuild() runs, so the base is given
    // zero and told the real count below -- a window with visibleRows == count
    // never scrolls and behaves as a bare Focus, which is what this list wants:
    // nine rows at the very most, against twelve that fit.
    : FocusScreen(0, 0), nets_(std::move(nets)), sink_(sink) {
  rebuild();
}

void WifiSettingsScreen::rebuild() {
  vm_.title = "WI-FI";
  vm_.state = "ON DEMAND";
  vm_.prose = kProse;
  vm_.nothingSaved = nets_.all().empty();
  vm_.emptyTitle = kEmptyTitle;
  vm_.emptyProse = kEmptyProse;
  vm_.rows.clear();
  rowSsid_.clear();

  if (!vm_.nothingSaved) {
    ListRow header;
    header.label = "SAVED NETWORKS";
    header.isHeader = true;
    vm_.rows.push_back(header);
    rowSsid_.emplace_back();
    for (const SavedNetwork& n : nets_.all()) {
      ListRow row;
      row.label = n.ssid;
      // AUTO or SAVED, which is a VALUE and not a chevron: this row acts in
      // place rather than disclosing a screen, and ListRow::discloses is an
      // explicit flag precisely so that is not inferred from an empty value.
      row.value = n.automatic ? "AUTO" : "SAVED";
      row.focusable = true;
      vm_.rows.push_back(row);
      rowSsid_.push_back(n.ssid);
    }
  }

  ListRow setup;
  setup.label = "SETUP";
  setup.isHeader = true;
  vm_.rows.push_back(setup);
  rowSsid_.emplace_back();

  ListRow join;
  join.label = kJoinRow;
  join.discloses = true;
  join.focusable = true;
  vm_.rows.push_back(join);
  rowSsid_.emplace_back();

  window().setCount(static_cast<int>(vm_.rows.size()));
  window().setVisibleRows(static_cast<int>(vm_.rows.size()));
  // The first focusable row: the gate refuses the header above it, and set()
  // with a gate refuses rather than clamping onto it.
  if (focus() < 0 || !focusable(focus())) {
    for (int i = 0; i < static_cast<int>(vm_.rows.size()); ++i) {
      if (focusable(i)) {
        window().setFocus(i, nullptr);
        break;
      }
    }
  }
  syncVm();
}

bool WifiSettingsScreen::focusable(int index) const {
  if (index < 0 || index >= static_cast<int>(vm_.rows.size())) return false;
  return vm_.rows[static_cast<size_t>(index)].focusable;
}

std::string WifiSettingsScreen::focusedSsid() const {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(rowSsid_.size())) return {};
  return rowSsid_[static_cast<size_t>(f)];
}

void WifiSettingsScreen::syncVm() {
  vm_.focusedRow = focus();
  // THE CONFIRM LABEL FOLLOWS THE FOCUSED ROW, which Settings is the precedent
  // for: `CHANGE` on a network, because SELECT toggles AUTO in place, and
  // `OPEN` on the row that pushes the picker. A Confirm labelled CHANGE that
  // opened a screen would be the misleading-button defect.
  //
  // AND THE HOLD RING FOLLOWS IT TOO. The ring binds FORGET, and there is
  // nothing to forget from the SETUP row -- a bar promising a hold with
  // nothing behind it is the dead-affordance the four-slot rule exists to
  // prevent, and it is why the empty variant's bar has no ring at all.
  const bool onNetwork = !focusedSsid().empty();
  // AND THE TWO MOVERS GO QUIET WHEN THERE IS NOWHERE TO MOVE, which the
  // empty variant's board already draws: design/WifiSettingsEmpty.dc.html
  // authors both slots as the 36px spacer eight other boards use, and this
  // drew UP and DOWN over them. One focusable row means a press that reports
  // no change and spends nothing, under a bar promising it will -- the dead
  // affordance the four-slot rule exists to prevent, and the picker's empty
  // variant got this right one screen over.
  //
  // Counted rather than read off `nothingSaved`: what decides it is how many
  // rows a focus can LAND on, and the saved list is only one of the things
  // that changes that.
  int landable = 0;
  for (int i = 0; i < static_cast<int>(vm_.rows.size()); ++i)
    if (focusable(i)) ++landable;
  const bool movable = landable > 1;
  vm_.hints = {"BACK", onNetwork ? "CHANGE" : "OPEN", movable ? "UP" : "",
               movable ? "DOWN" : ""};
  vm_.holds = {false, onNetwork, false, false};
  declareHints(vm_.holds);
}

Action WifiSettingsScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Back:
      return Action::pop();
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Activate: {
      const std::string ssid = focusedSsid();
      if (ssid.empty()) {
        // The SETUP row. An empty list reaches here too, which is the point of
        // the empty variant keeping a live action.
        return Action::push(ScreenId::WifiPicker);
      }
      // SELECT toggles the preference in place. The list is rebuilt rather
      // than the one row patched, because the flag is exclusive and the row
      // that LOST it has to be redrawn as well.
      if (!nets_.toggleAutomatic(ssid)) return Action::none();
      if (sink_ != nullptr) sink_->commit(nets_);
      const int keep = focus();
      rebuild();
      window().setFocus(keep, nullptr);
      syncVm();
      return Action::redraw();
    }
    case Gesture::Secondary:
      // The hold. Only a network row has one, which syncVm has already told
      // the recognizer -- so this cannot fire on the SETUP row.
      if (focusedSsid().empty()) return Action::none();
      return Action::push(ScreenId::WifiNetworkActions);
    default:
      return Action::none();
  }
}

void WifiSettingsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                Plane plane) const {
  theme.renderWifiSettings(fb, fonts, vm_, plane);
}

}  // namespace reader
