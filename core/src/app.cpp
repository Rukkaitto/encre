#include "reader/app.h"

namespace reader {

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
    case Action::Kind::Push: {
      auto next = factory_.create(a.target);
      // A factory that cannot build the screen is a bug in the caller, not a
      // reason to push a null onto the stack and crash on the next render.
      if (!next) break;
      stack_.push_back(std::move(next));
      dirty_ = true;
      transition_ = true;
      break;
    }
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
  }
}

}  // namespace reader
