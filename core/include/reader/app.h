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
  enum class Kind : uint8_t { None, Redraw, Push, Pop, PopTo, Sleep, Retry };
  Kind kind = Kind::None;
  ScreenId target = ScreenId::Home;  // meaningful for Push and PopTo

  static Action none() { return {}; }
  static Action redraw() { return {Kind::Redraw, ScreenId::Home}; }
  static Action push(ScreenId t) { return {Kind::Push, t}; }
  static Action pop() { return {Kind::Pop, ScreenId::Home}; }
  // Pops until `target` is on top -- "dismiss the flow I am in", which is a
  // different thing from "go back one".
  //
  // The delete confirmation is what needs it: confirming a delete puts the user
  // back on the Library with the book gone, and the actions panel it was opened
  // from must go too. Two Pops cannot express that, because a screen returns one
  // Action -- and a Pop that the confirm screen followed with a second Pop of its
  // own would be the confirm screen reaching into the stack.
  //
  // Stops at the root if `target` is not on the stack, rather than emptying it:
  // an id that is not there is a caller bug, and unwinding to nothing would take
  // the device down on the next paint.
  static Action popTo(ScreenId t) { return {Kind::PopTo, t}; }
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

  // Buttons this screen wants to auto-repeat while held, accelerating. Zero for
  // everything but a list long enough to need it: on a four-row overlay a held
  // button that ran away would be a defect, not a convenience.
  //
  // Deliberately NOT derived from the hint bar, which is where longPressable()
  // comes from. A hold ring promises a DIFFERENT action; auto-repeat is more of
  // the same one, so it has nothing to announce and no slot to announce it in.
  virtual ButtonMask autoRepeat() const { return 0; }

  // WHERE THE SELECTION IS, as an index into whatever the screen considers its
  // whole list -- not into the slice on glass. The session record stores this
  // number and a wake hands it back, so the two have to mean the same thing on a
  // list that scrolls: a Library scrolled to row 40 restores to row 40, and the
  // window it lands in is ScrollWindow's business, not the record's.
  //
  // The pair was deliberately deferred in 2C-1 and is justified now: Library is
  // the first screen that can produce a value, and 2C-1's session record was
  // writing a hardcoded 0 into a field nothing could fill.
  //
  // DEFAULTS THAT MEAN "I HAVE NO FOCUS TO REPORT OR RESTORE": 0, and false.
  // A screen with one thing on it (the SD-missing prompt) is not obliged to
  // pretend otherwise, and setFocus returning false says the restore did not
  // land -- the same contract ScrollWindow::setFocus uses, so a caller can tell
  // "restored" from "ignored" without asking which screen it is holding.
  //
  // A negative focus is legitimate and means "nothing selected" (Home's Continue
  // block, an empty Library). It is the CALLER's job to decide what to do with
  // that before putting it in a record whose field is unsigned; see
  // saveWhereWeAre in shell/src/main.cpp.
  virtual int focus() const { return 0; }
  virtual bool setFocus(int index) {
    (void)index;
    return false;
  }

  // WHICH PIXELS THIS SCREEN'S PAINT COVERS, as a token rather than a rectangle.
  //
  // The promise: two paints of this screen whose tokens are EQUAL write exactly
  // the same set of pixels opaquely, so the later one completely replaces the
  // earlier one. That is the whole precondition App::renderTopOnly needs -- it
  // repaints this screen over the frame this screen's own last paint left behind,
  // so anything the previous paint inked and this one does not reach survives as
  // a stale pixel.
  //
  // ZERO MEANS "NO PROMISE", and it is the default, so a screen is ineligible for
  // a partial repaint until it says otherwise. A token is a token: the numbers
  // mean nothing except equal-or-not, and they are compared only against another
  // token from the same screen.
  //
  // WHAT A SCREEN HAS TO KNOW TO ANSWER: whatever in its own view-model changes
  // the box its theme draws. That is layout knowledge, which normally belongs in
  // the theme -- so the two screens that answer name the theme's rule they
  // mirror, and test_partial_repaint.cpp renders EVERY pair of their reachable
  // states both ways and asserts the bytes agree whenever the tokens do. Getting
  // this wrong in the safe direction (a token that changes more often than the
  // box) costs a repaint; getting it wrong the other way ships stale pixels, so
  // the test enumerates rather than samples.
  virtual uint32_t paintFootprint() const { return 0; }

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

  // REPAINTS ONLY THE TOP SCREEN, over the frame `fb` already holds.
  //
  // A focus move inside an overlay changes nothing below it: the parent received
  // no event, and the veil over it is already drawn. Re-rendering the stack for
  // that costs the parent's whole text pass plus a veil over every pixel of the
  // frame -- measured at 4.6 ms on the desktop for the actions overlay against
  // 0.9 ms for the top screen alone once the veil was made byte-wise, and this
  // project's desktop-to-device ratio is about 65x.
  //
  // Returns FALSE and paints NOTHING when the precondition does not hold, so the
  // caller falls back to render(). It is deliberately not an assert: every
  // refusal is a correct full repaint, and the cost of being wrong the other way
  // is stale pixels from a previous frame -- which reads as a rendering bug
  // rather than as a caching one, and is the hardest kind of defect to trace back
  // to here.
  //
  // THE CALLER STILL MUST NOT CLEAR THE FRAME FIRST. Clearing and then partially
  // repainting is exactly the stale-pixel bug with white in place of the stale
  // pixels: an overlay panel floating on paper, which is the same wrong frame
  // App::render exists to prevent.
  bool renderTopOnly(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const;

  // Whether renderTopOnly would paint. Exposed so the shell can log the decision
  // and so each condition is testable on its own; renderTopOnly calls it rather
  // than trusting a caller to have called it.
  //
  // EVERY CONDITION, and the failure each one is there for:
  //
  //   1. dirty() -- nothing to paint at all otherwise.
  //   2. !transition() -- A PUSH OR A POP IS NEVER PARTIAL. The stack changed, so
  //      everything below the top may be different. This is also what makes the
  //      first frame after boot full: a fresh App is dirty AND in transition.
  //   3. the frame is the one this App last painted, in the same plane, with the
  //      same screen on top, at the same depth. THE FRAME'S CONTENTS ARE THE
  //      PRECONDITION, and this is the part a caller cannot be trusted to check,
  //      because the caller is the thing that would have clobbered it. A fresh
  //      App has painted nothing, so this refuses the first frame too --
  //      independently of (2), because "there is nothing in the frame yet" and
  //      "the stack just changed" are different reasons.
  //   4. the top screen is an OVERLAY. A non-overlay fills the frame, so there is
  //      nothing underneath to preserve and no saving to make -- and it would be
  //      actively wrong, because a full paint clears the frame first and a partial
  //      one must not, so every pixel the screen does not draw would be stale.
  //   5. it is not on the GRAYSCALE path. That path renders the screen three
  //      times plus a rebase, and the frame between passes holds a DIFFERENT
  //      plane, so "the frame holds the previous paint of this plane" is false for
  //      every pass but the first. Condition (3)'s plane check already refuses it;
  //      this says so on purpose rather than by accident, because a future two-
  //      frame grayscale path would silently satisfy the plane check.
  //   6. the top screen's paintFootprint() is non-zero and unchanged since that
  //      paint -- the screen's own promise that this paint covers that one.
  bool canRenderTopOnly(const Framebuffer& fb, Plane plane) const;

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
  // ships fullOnTransition TRUE, so a transition forces a FULL refresh and a
  // focus move inside one screen does not. (This comment said the opposite until
  // a review caught it -- it predates the 2B decision that flipped the default,
  // and it contradicted the reasoning recorded in CLAUDE.md under Runtime.)
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
  ButtonMask autoRepeat() const { return top().autoRepeat(); }

 private:
  // V1's deepest path is Home > Library > item actions > delete confirm.
  static constexpr size_t kMaxDepth = 8;

  // WHAT THE LAST FULL PAINT PUT WHERE, so canRenderTopOnly can check its own
  // precondition instead of trusting the caller with it.
  //
  // `frame` being null is "nothing has been painted yet", which is the state a
  // fresh App is in -- including the one the shell builds when a card is pulled
  // at runtime, or after a successful RETRY. Those replace the App, so the record
  // goes with it and the next paint is full, which is what a new stack needs
  // anyway.
  //
  // The pointer is compared, never dereferenced, so a stale one is only ever
  // wrong in the safe direction: a different framebuffer at the same address
  // would have to be the same size and rotation to be handed to the same App, and
  // the shell allocates exactly one for the life of the process.
  struct PaintRecord {
    const Framebuffer* frame = nullptr;
    const Screen* top = nullptr;
    Plane plane = Plane::Bw;
    int depth = 0;
    uint32_t footprint = 0;
  };

  std::vector<std::unique_ptr<Screen>> stack_;
  ScreenFactory& factory_;
  bool dirty_ = true;  // the first frame always needs painting
  bool transition_ = true;
  bool sleep_ = false;
  bool retry_ = false;
  // Mutable because render() is const: painting does not change the app, but it
  // does change what is on glass, and this is what remembers that. The
  // alternative -- a non-const render() -- would make every const App& in the
  // tests and the simulator unable to paint, for no gain.
  mutable PaintRecord painted_;
};

}  // namespace reader
