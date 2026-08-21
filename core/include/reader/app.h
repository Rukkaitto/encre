#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "reader/input.h"
#include "reader/refresh.h"
#include "reader/text.h"  // Plane

namespace reader {

class Framebuffer;
class FontSet;
class Theme;

enum class ScreenId : uint8_t {
  Home,
  Library,
  ItemActions,    // the overlay a long press on a Library row opens
  DeleteConfirm,  // the overlay that overlay's Delete... opens
  BookDetails,    // a full screen, NOT an overlay -- see its board
  Settings,
  InputMonitor,
  SdMissing
};

// A screen's name, for logs. Same reasoning as buttonName: a numeric ScreenId in
// a serial log is one more thing to decode while diagnosing a device.
const char* screenName(ScreenId id);

// What a screen asks the app to do after handling an event.
//
// `Retry` is the odd one out and is deliberately shaped like `Sleep`: both name
// something only the shell can do. Storage is not core/'s -- the SD-missing
// screen cannot mount a card, and spec 6 requires its button actually re-attempt
// the mount rather than repaint the same message -- so the screen asks, App
// latches the request, and the shell answers it. See App::retryRequested().
struct Action {
  enum class Kind : uint8_t { None, Redraw, Push, Pop, Sleep, Retry };
  Kind kind = Kind::None;
  ScreenId target = ScreenId::Home;  // meaningful for Push only

  static Action none() { return {}; }
  static Action redraw() { return {Kind::Redraw, ScreenId::Home}; }
  static Action push(ScreenId t) { return {Kind::Push, t}; }
  static Action pop() { return {Kind::Pop, ScreenId::Home}; }
  static Action sleep() { return {Kind::Sleep, ScreenId::Home}; }
  static Action retry() { return {Kind::Retry, ScreenId::Home}; }
};

// The four hint slots are the four front buttons in hardware order (spec 4.0).
// Getting this order wrong would bind a ring drawn over one button to a hold on
// another, which is why it is one shared helper and not four call sites.
inline constexpr std::array<Button, 4> kHintSlotButtons = {Button::Back, Button::Confirm,
                                                           Button::Up, Button::Down};

// The long-press mask a screen's hint slots imply.
constexpr ButtonMask hintHoldMask(const std::array<bool, 4>& holds) {
  ButtonMask m = 0;
  for (int i = 0; i < 4; ++i)
    if (holds[i]) m |= buttonBit(kHintSlotButtons[i]);
  return m;
}

// A screen produces a view-model and lets the theme render it (spec 3.3) --
// screens never draw pixels themselves. `render` exists on the screen only to
// pick which typed theme method its own view-model belongs to.
class Screen {
 public:
  virtual ~Screen() = default;
  virtual ScreenId id() const = 0;
  // Mono by default, which is what chrome ships on and what the reference
  // firmware does on this panel. Both other paths cost an explicit override: the
  // stipple because it is a deliberate aesthetic choice rather than the house
  // style, and grayscale because it is ~5x more expensive.
  virtual Fidelity fidelity() const { return Fidelity::Mono; }
  // A panel over a still-visible parent rather than a whole screen: the item
  // actions, delete confirm and book details boards are all one centred panel
  // over a veiled Library. An overlay draws its own veil before its panel, and
  // App::render is what puts the parent underneath it.
  //
  // Default false, so being see-through costs an override. It changes ONLY what
  // gets painted: input, fidelity and the long-press mask still come from the
  // top of the stack alone (see App::dispatch and App::longPressable), because an
  // overlay whose parent also received events would move a focus the user cannot
  // see.
  virtual bool isOverlay() const { return false; }
  virtual ButtonMask longPressable() const = 0;
  virtual Action onEvent(const InputEvent& ev) = 0;
  virtual void render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                      Plane plane) const = 0;
};

// Builds a screen on demand. An interface rather than a std::function so the
// firmware pulls in no <functional> and does no allocation per push, and so
// Phase 2C can give the factory the SD card and settings it will need.
class ScreenFactory {
 public:
  virtual ~ScreenFactory() = default;
  // Returning nullptr means "no such screen": the app refuses the push and
  // leaves the stack alone rather than pushing a hole into it.
  virtual std::unique_ptr<Screen> create(ScreenId id) = 0;
};

class App {
 public:
  App(std::unique_ptr<Screen> root, ScreenFactory& factory);

  Screen& top();
  const Screen& top() const;
  int depth() const { return static_cast<int>(stack_.size()); }

  // Paints the stack: the topmost non-overlay screen, then every overlay above
  // it in order, each drawing its own veil before its panel.
  //
  // Callers go through this rather than through top().render(), and the
  // difference is only visible when an overlay is up -- which is exactly why it
  // has to be the one entry point. Two paint paths would mean the shell and the
  // simulator could disagree about what an overlay looks like, and the goldens
  // would keep passing while the device drew a panel floating on white.
  //
  // Screens below the topmost non-overlay are NOT painted: they are entirely
  // hidden, and a screen's worth of text rendering is not free on this chip.
  // Clearing the framebuffer stays the caller's job, as it was.
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const;

  void dispatch(const InputEvent& ev);

  // Push a screen with no input event behind it. The one caller is the shell's
  // wake restore: the NVS session record names a screen, and there is no press
  // that implies it -- the alternative would be constructing the App with that
  // screen as its ROOT, which leaves Back dead on a screen the user reached by
  // going forward.
  //
  // False, with the stack untouched, when the factory cannot build `id` or the
  // stack is full. A record written by a newer firmware can name a screen this
  // build has no factory case for, and pushing the nullptr it returns would
  // crash on the next render rather than falling back to Home.
  //
  // Dirty and transition are set on success, exactly as a Push action's are: the
  // restored screen still has to be painted.
  bool pushScreen(ScreenId id);

  // Something on screen changed and needs painting.
  bool dirty() const { return dirty_; }
  // ...and the change was a screen change rather than a change within one. What
  // the refresh does with that is RefreshPolicy's business, not the app's: chrome
  // constructs its policy with fullOnTransition false, so a transition is an
  // ordinary FAST refresh.
  bool transition() const { return transition_; }
  void clearDirty();

  bool sleepRequested() const { return sleep_; }
  void clearSleepRequest() { sleep_ = false; }

  // A screen asked for storage to be re-attempted (today: the SD-missing
  // screen's RETRY). Latched exactly like a sleep request, and for the same
  // reason -- core/ has no filesystem and no card -- so the shell's loop is what
  // acts on it.
  //
  // WHAT THE SHELL MUST DO (Task 8 of the 2C-1 plan owns this):
  //   1. clearRetryRequest(), so a failed attempt does not re-fire forever;
  //   2. re-run the SdFileSystem mount, keeping SD traffic off the display bus
  //      -- the card shares the panel's SPI with no locking anywhere in
  //      SDCardManager, so this must not race a refresh;
  //   3. on success, load the settings, replace the root screen with Home and
  //      repaint; on failure, repaint this screen -- the message is still true.
  // Nothing here repaints on its own: a retry that failed changes nothing on
  // glass, and a screen change is the shell's to make.
  bool retryRequested() const { return retry_; }
  void clearRetryRequest() { retry_ = false; }

  ButtonMask longPressable() const { return top().longPressable(); }

 private:
  // V1's deepest path is Home > Library > item actions > delete confirm.
  static constexpr size_t kMaxDepth = 8;

  std::vector<std::unique_ptr<Screen>> stack_;
  ScreenFactory& factory_;
  bool dirty_ = true;  // the first frame always needs painting
  bool transition_ = true;
  bool sleep_ = false;
  bool retry_ = false;
};

}  // namespace reader
