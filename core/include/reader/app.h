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

enum class ScreenId : uint8_t { Home, Library, Settings, InputMonitor };

// A screen's name, for logs. Same reasoning as buttonName: a numeric ScreenId in
// a serial log is one more thing to decode while diagnosing a device.
const char* screenName(ScreenId id);

// What a screen asks the app to do after handling an event.
struct Action {
  enum class Kind : uint8_t { None, Redraw, Push, Pop, Sleep };
  Kind kind = Kind::None;
  ScreenId target = ScreenId::Home;  // meaningful for Push only

  static Action none() { return {}; }
  static Action redraw() { return {Kind::Redraw, ScreenId::Home}; }
  static Action push(ScreenId t) { return {Kind::Push, t}; }
  static Action pop() { return {Kind::Pop, ScreenId::Home}; }
  static Action sleep() { return {Kind::Sleep, ScreenId::Home}; }
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
  virtual Fidelity fidelity() const { return Fidelity::Gray; }
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

  void dispatch(const InputEvent& ev);

  // Something on screen changed and needs painting.
  bool dirty() const { return dirty_; }
  // ...and the change was a screen change, so the refresh must be FULL.
  bool transition() const { return transition_; }
  void clearDirty();

  bool sleepRequested() const { return sleep_; }
  void clearSleepRequest() { sleep_ = false; }

  ButtonMask longPressable() const { return top().longPressable(); }

 private:
  // V1's deepest path is Home > Library > item actions > delete confirm.
  static constexpr size_t kMaxDepth = 8;

  std::vector<std::unique_ptr<Screen>> stack_;
  ScreenFactory& factory_;
  bool dirty_ = true;  // the first frame always needs painting
  bool transition_ = true;
  bool sleep_ = false;
};

}  // namespace reader
