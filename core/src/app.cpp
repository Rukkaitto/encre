#include "reader/app.h"

namespace reader {

const char* screenName(ScreenId id) {
  switch (id) {
    case ScreenId::Home: return "HOME";
    case ScreenId::Library: return "LIBRARY";
    case ScreenId::ItemActions: return "ITEM-ACTIONS";
    case ScreenId::DeleteConfirm: return "DELETE-CONFIRM";
    case ScreenId::BookDetails: return "BOOK-DETAILS";
    case ScreenId::Settings: return "SETTINGS";
    // Sleep was missing from this switch and fell through to "?", so every log
    // line naming it named nothing. Not caught by -Wswitch because the function
    // has a return after the switch -- which it needs, for an id cast from a
    // stored byte.
    case ScreenId::Sleep: return "SLEEP";
    case ScreenId::Reader: return "READER";
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

std::vector<StackEntry> App::snapshot() const {
  std::vector<StackEntry> out;
  out.reserve(stack_.size());
  for (const auto& screen : stack_) out.push_back({screen->id(), screen->focus()});
  return out;
}

App::RestoreReport App::restore(const std::vector<StackEntry>& stack) {
  RestoreReport r;
  r.requested = static_cast<int>(stack.size());
  // Nothing to put back, or this App is not the fresh one a boot builds, or the
  // record describes a different world from the one that booted. All three are
  // "leave the stack exactly as it is", and none of them names a screen.
  if (stack.empty() || stack_.size() != 1 || stack.front().screen != stack_.front()->id())
    return r;
  r.rootMatched = true;

  // The root is never rebuilt -- the factory refuses Home on purpose, since
  // popping back to it must return the same object with its own state -- so it
  // gets its focus set rather than being pushed. That is the ONLY difference
  // between the root and everything above it, and it is a difference about the
  // root, not about Home.
  stack_.front()->setFocus(stack.front().focus);
  r.restored = 1;

  for (size_t i = 1; i < stack.size(); ++i) {
    if (!pushScreen(stack[i].screen)) break;
    // Before the next push, because an overlay reads the focused row of the
    // screen under it at construction time.
    top().setFocus(stack[i].focus);
    ++r.restored;
  }

  // A restored stack has never been painted, whatever the root's focus did or did
  // not change. pushScreen sets these for every entry above the root; a record
  // that only moved the root's focus would otherwise come back unpainted.
  dirty_ = true;
  transition_ = true;
  return r;
}

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

void App::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  // Walk down from the top to the first screen that is not an overlay -- the
  // parent the overlays are floating over -- then paint upward from there.
  //
  // The loop stops at 0 as well as at a non-overlay, so an overlay at the ROOT
  // renders only itself. That should not happen: an overlay veils a parent and
  // the root has none. But "should not happen" is how a walk ends up reading
  // stack_[-1], and the stack is never empty, so index 0 is always a valid
  // thing to start from.
  size_t base = stack_.size() - 1;
  while (base > 0 && stack_[base]->isOverlay()) --base;
  for (size_t i = base; i < stack_.size(); ++i) stack_[i]->render(fb, fonts, theme, plane);

  // Record what the frame now holds. This is the only place the record is
  // written, and it is written AFTER the paint so a render that somehow did not
  // complete cannot leave a claim behind it.
  painted_ = {&fb, stack_.back().get(), plane, depth(), stack_.back()->paintFootprint()};
}

bool App::canRenderTopOnly(const Framebuffer& fb, Plane plane) const {
  // Each clause is one of the ways a partial repaint goes wrong; app.h names the
  // failure beside each. The order is cheapest-first, and the two that matter
  // most -- the transition and the frame identity -- are the two at the top.
  if (!dirty_ || transition_) return false;
  if (painted_.frame != &fb || painted_.plane != plane) return false;
  if (painted_.top != stack_.back().get() || painted_.depth != depth()) return false;
  const Screen& t = *stack_.back();
  if (!t.isOverlay()) return false;
  if (t.fidelity() == Fidelity::Grayscale) return false;
  const uint32_t footprint = t.paintFootprint();
  return footprint != 0 && footprint == painted_.footprint;
}

bool App::renderTopOnly(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  if (!canRenderTopOnly(fb, plane)) return false;
  // The top screen alone, over what is already there. NOT App::render's walk:
  // the parent and the veil over it are exactly what this is skipping, and they
  // are still in the frame.
  stack_.back()->render(fb, fonts, theme, plane);
  // The record does not change: same frame, same screen, same plane, same depth,
  // and the footprint is equal by the check above. Nothing to rewrite, so two
  // partial repaints in a row are both allowed.
  return true;
}

void App::dispatch(const InputEvent& ev) {
  // The TOP screen only, overlay or not. An overlay that let its parent see the
  // event would move a focus that is behind a veil -- and the symptom would show
  // up on the parent after the overlay was dismissed, which reads as a rendering
  // bug rather than a dispatch one.
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
    case Action::Kind::PopTo:
      // Down to `target`, or to the root if it is not on the stack -- never past
      // it. Only one dirty/transition pair for however many screens go, because
      // the user sees one screen change however deep the flow was.
      while (stack_.size() > 1 && top().id() != a.target) stack_.pop_back();
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
    case Action::Kind::Open:
      // Latched for the same reason Retry is: the card is the shell's. See
      // Action::open().
      open_ = true;
      break;
  }
}

}  // namespace reader
