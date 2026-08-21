#include "reader/app.h"

namespace reader {

const char* screenName(ScreenId id) {
  switch (id) {
    case ScreenId::Home: return "HOME";
    case ScreenId::Library: return "LIBRARY";
    case ScreenId::Settings: return "SETTINGS";
    case ScreenId::InputMonitor: return "INPUT-MONITOR";
    case ScreenId::SdMissing: return "SD-MISSING";
  }
  return "?";
}

App::App(std::unique_ptr<Screen> root, ScreenFactory& factory) : factory_(factory) {
  // Reserve up front. The firmware is built -fno-exceptions, so a vector that
  // cannot grow calls abort() and takes the whole device down with no
  // diagnostic -- this project has already lost a boot to exactly that. V1's
  // deepest path is Home > Library > actions overlay > delete confirm, so four
  // is the real ceiling and eight is slack; reserving means a push allocates
  // only the screen itself.
  stack_.reserve(kMaxDepth);
  stack_.push_back(std::move(root));
}

Screen& App::top() { return *stack_.back(); }
const Screen& App::top() const { return *stack_.back(); }

bool App::pushScreen(ScreenId id) {
  // The reserve() in the constructor is what keeps a push from allocating the
  // vector again, and -fno-exceptions makes a failed reallocation an abort()
  // with no diagnostic -- so the ceiling it reserved for is enforced here rather
  // than trusted. V1's deepest path is four; refusing the ninth push loses a
  // screen, and growing past it can lose the device.
  if (stack_.size() >= kMaxDepth) return false;
  auto next = factory_.create(id);
  // A factory that cannot build the screen is a bug in the caller, not a reason
  // to push a null onto the stack and crash on the next render.
  if (!next) return false;
  stack_.push_back(std::move(next));
  dirty_ = true;
  transition_ = true;
  return true;
}

void App::clearDirty() {
  dirty_ = false;
  transition_ = false;
}

void App::dispatch(const InputEvent& ev) {
  const Action a = top().onEvent(ev);
  switch (a.kind) {
    case Action::Kind::None:
      break;
    case Action::Kind::Redraw:
      dirty_ = true;
      break;
    case Action::Kind::Push:
      // Through the same function the shell's wake restore uses, so a push from
      // a button and a push from a session record cannot diverge on the refused
      // cases.
      pushScreen(a.target);
      break;
    case Action::Kind::Pop:
      // The root is the app: popping it would leave nothing to render and
      // nothing to receive the next event.
      if (stack_.size() <= 1) break;
      stack_.pop_back();
      dirty_ = true;
      transition_ = true;
      break;
    case Action::Kind::Sleep:
      sleep_ = true;
      break;
    case Action::Kind::Retry:
      // Latched, not acted on: the mount is the shell's, and so is the decision
      // to swap this screen for Home when it succeeds. Nothing is marked dirty
      // -- a retry that fails changes nothing on the panel, and repainting an
      // identical screen would spend a full refresh saying so.
      retry_ = true;
      break;
  }
}

}  // namespace reader
