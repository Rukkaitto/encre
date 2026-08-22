#include "reader/screen_sleep.h"

#include <utility>

#include "reader/theme.h"

namespace reader {

SleepScreen::SleepScreen(SleepViewModel vm) : vm_(std::move(vm)) {}

Action SleepScreen::onEvent(const InputEvent&) {
  // Everything, including Back. See the header: the device is asleep, and a screen
  // that answered a button would be claiming otherwise.
  return Action::none();
}

void SleepScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                         Plane plane) const {
  theme.renderSleep(fb, fonts, vm_, plane);
}

}  // namespace reader
