#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "reader/gesture.h"
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
  // What the panel holds while the device sleeps -- see screen_sleep.h. A screen
  // rather than a special case in the shell, so the simulator and the goldens can
  // render it like everything else.
  Sleep,
  // The reading page -- see screen_reader.h. Last of the V1 screens to arrive and
  // the only one whose content is the book's rather than the app's.
  Reader,
  // The overlay the Reader's Activate opens, and the chapter list it reaches. APPENDED
  // rather than inserted beside Reader: the session record stores a screen by NAME
  // (session_record.h) so an insertion could not silently become another screen, but
  // appending also leaves every existing ordinal where it was.
  ReaderMenu,
  Contents,
  SdMissing,
  // design/Typography.dc.html -- the reader's type panel. APPENDED for the reason
  // ReaderMenu was: the record stores a name, so an insertion could not silently
  // become another screen, but appending also leaves every existing ordinal where
  // it was.
  Typography,
  // design/Peek.dc.html -- a page of the book over the veiled page you are on. APPENDED
  // for the reason ReaderMenu and Typography were: the session record stores a screen by
  // NAME (session_record.h), so an insertion could not silently become another screen,
  // but appending also leaves every existing ordinal where it was.
  Peek,
  // design/BookEnd.dc.html -- the screen a book's last page turns into. APPENDED
  // for the reason ReaderMenu, Typography and Peek were: the session record stores
  // a screen by NAME (session_record.h) so an insertion could not silently become
  // another screen, but appending also leaves every existing ordinal where it was.
  //
  // AND THE GUARD IN test_focus_restore.cpp DID NOT NOTICE THIS APPEND, which is
  // worth writing down where the next member will be added. That static_assert
  // compares the catalogue's length against `ScreenId::Peek + 1` -- a NAMED member,
  // not the last one -- so an append leaves both sides at 13 and it passes over a
  // screen the catalogue does not cover. That is #42, and it is still open: it fires
  // only once the array grows, which is the wrong way round for a guard whose job is
  // to force the array to grow.
  BookEnd,
  // design/BookError.dc.html -- the dialog a book that will not open raises. The
  // session record stores a screen by NAME, so appending cannot silently become
  // another screen, and appending also leaves every existing ordinal where it was.
  BookError,
  // design/BatteryEmpty.dc.html -- what the panel holds after a critical shutdown.
  // APPENDED after BookError rather than before it: BookError had already landed on
  // main when this screen merged, and re-ordering a member that has shipped moves
  // ordinals for nothing. The record stores a NAME, so neither order can silently
  // become another screen.
  //
  // PAINTED DIRECTLY AND NEVER PUSHED, on SleepScreen's argument: the record names
  // the top of the stack, so pushing it would make the next wake restore INTO it --
  // press power, get "battery empty" back on a pack that has just been charged.
  //
  // AND #42 IS FIXED, so this append was caught rather than waved through. Every
  // bound that used to name a member by hand now names the Count sentinel below --
  // session_record.cpp's table and decode loop, test_focus_restore.cpp's catalogue
  // and assert, and test_session_record.cpp's two every-id walks. Appending this
  // member failed all of them at once, which is the whole point: the guards that
  // stayed quiet for Typography and then BookEnd cannot stay quiet for the next one.
  BatteryEmpty,
  // THE V1.1 CONNECT FLOW, six screens appended together. Appending SIX at once is
  // the case #42's sentinel was really written for -- the last time two screens
  // arrived at once it was a merge, and every guard that named a member instead of
  // Count would have let the loser of that merge serialise as `home`.
  //
  // design/WifiSettings.dc.html -- the hub: saved networks, and the door to a scan.
  // Reached from Settings' CONNECTIONS row, which is the only place the radio may
  // come up: the Reader is not on the stack there and the Library is not resident,
  // so there is ~133 KB free against Wi-Fi's ~23 KB of static allocation. With a
  // book open on a large card the measured floor is 13,696 bytes, so this is not a
  // preference about battery -- the flow is entered from Settings because nowhere
  // else has the heap.
  WifiSettings,
  // design/WifiPicker.dc.html -- the scan list. The second scrolling list in the
  // firmware after the Library, and the second user of the rail.
  WifiPicker,
  // design/WifiPassword.dc.html -- the on-device keyboard, and the first text entry
  // anywhere in this firmware. 46 cells in five rows, the last of them ragged. It
  // derives from TextEntryScreen now rather than straight from GridFocusScreen (#126,
  // the second copy being the extraction point); what stays here is 802.11's bound,
  // its floor, four strings and two Actions.
  WifiPassword,
  // design/WifiConnect.dc.html -- the connecting dialog. An overlay, and it REPLACES
  // the join stack rather than sitting on it (Action::replace), which is what makes
  // one veiled parent truthful for both entry paths: an open network arrives here
  // straight from the picker and has no WifiPassword to veil.
  WifiConnect,
  // design/WifiError.dc.html and its two siblings -- one screen, THREE COPY SHAPES.
  // A join fails three distinguishable ways and one sentence would be a lie, which
  // is BookError's argument; the two new shapes also DROP the EDIT PASSWORD slab,
  // because the password is not what went wrong. Absent, not inert.
  WifiError,
  // design/WifiNetworkActions.dc.html -- what holding Confirm on a saved network
  // opens. ItemActions reads the LIBRARY's focused row, so it could not be reused.
  WifiNetworkActions,
  // NOT A SCREEN. A bound, so a guard can name "one past the last member" without
  // naming a member -- which is #42, and which had gone quiet twice by the time it
  // was fixed: session_record.cpp spelled three bounds `<= ScreenId::Peek` and then
  // `<= ScreenId::BookEnd`, and each append satisfied them unchanged while leaving
  // the table short, so the new screen serialised as `home`.
  //
  // Nothing may give this a row, a name or a case. `sessionWireName` and
  // `screenName` both refuse it, and the static_assert on kNames is what proves the
  // table did not quietly grow one for it -- a sentinel that became serialisable
  // would be a worse version of the bug this fixes.
  Count
};

// A screen's name, for logs. Same reasoning as buttonName: a numeric ScreenId in
// a serial log is one more thing to decode while diagnosing a device.
const char* screenName(ScreenId id);

// WHETHER THIS SCREEN IS ONE THE RADIO MAY BE ON BEHIND, which is TWO of the
// six Wi-Fi screens and not all of them: the picker while it scans, and the
// connecting dialog while it joins. Those are the two that put SCANNING and
// CONNECTING... on the glass, so the radio being on is exactly what they say.
//
// THE OTHER FOUR ARE FALSE DELIBERATELY. The hub's band reads `ON DEMAND`,
// which is a claim that the radio is OFF -- so leaving it up there is a false
// claim, not merely untidy, and that is the case that found this rule: Back
// off the picker MID-SCAN pops to the hub, and a predicate covering all six
// would have let the radio sit there indefinitely. The keyboard and the error
// panel have nothing in flight either; a join is started from the keyboard by
// beginJoin, which brings the radio back up.
//
// IT IS HERE RATHER THAN IN shell/ FOR #42's REASON. This is a fact about the
// screen catalogue, and its body is an exhaustive switch with NO `default:`,
// so a seventh Wi-Fi screen fails the build with -Wswitch rather than being
// quietly answered `false`. `shell/` has no harness and this has a test.
bool screenUsesRadio(ScreenId id);

// WHETHER A WAKE MAY PUT THIS SCREEN BACK, AND WHAT IT OWES FIRST (#49).
//
// A wake replays a stack of screen NAMES and the factory rebuilds each one from
// state the shell must have primed. Nothing connected "the record names screen X"
// to "X's construction inputs are primed", so each screen that needed inputs
// rediscovered the same failure -- the restore pushes, the factory refuses
// (correctly), App::restore stops there and keeps what stands, and the reader
// reports "it went back to the book". Reader, ReaderMenu and Contents each landed
// that way; Peek lands that way ON PURPOSE and was confirmed on device.
//
// THE GAP WAS NEVER THE REFUSAL, WHICH IS RIGHT AND STAYS. Substituting content is
// worse and this project has shipped that twice. The gap is that from outside, "this
// screen deliberately does not come back" and "somebody forgot to prime it" are the
// same observation. This is the question that tells them apart, and every ScreenId
// has to answer it: the table behind this is static_assert'ed against Count, so a
// screen appended to the enum FAILS THE BUILD until it says which of the three it is.
//
// It is here rather than in shell/ for screenUsesRadio's reason, one line up: this is
// a fact about the screen catalogue, `shell/` has no harness, and five bugs have
// hidden there.
enum class Restore : uint8_t {
  // A WAKE OWES IT NOTHING. The factory can rebuild it from what BOOT has already
  // given it -- a filesystem and a root, panel geometry, a settings copy and a
  // sink, the saved Wi-Fi list -- or from the parent screen the restore has just
  // put underneath it. This is the default answer and the cheap one.
  //
  // THE LINE BETWEEN THIS AND NeedsPriming IS "did a PRESS produce it", and
  // test_focus_restore.cpp draws it once by building every screen from a factory
  // configured the way setup() leaves it and no further. That check moved an entry
  // the first time it ran: the Wi-Fi hub was written NeedsPriming from reading its
  // factory case, and loadWifi() primes it at boot.
  Ready,
  // REBUILDABLE, BUT ONLY AFTER THE SHELL HAS HANDED THE FACTORY SOMETHING A PRESS
  // WOULD HAVE. Today that is one thing wearing four names -- the open book, which
  // the page, the menu over it, its chapter list and its end screen are all built
  // from -- and a wake makes no press, so the shell has to do it from last.json
  // before the replay starts.
  //
  // The priming itself can never move here: core/ does not know what a filesystem, a
  // book or last.json is. WHICH screens owe one is a fact about the catalogue, so the
  // shell reads that list from here rather than keeping its own. A list the shell
  // kept is what the hand-written `namesReader` scan was.
  NeedsPriming,
  // NEVER COMES BACK, AND THAT IS A DECISION RATHER THAN AN OMISSION. App::snapshot()
  // stops the record at one of these and App::restore refuses to push one, so the
  // screens that must not be woken into cannot be -- which is stronger than today,
  // where Sleep and BatteryEmpty are kept out of a record only by nothing ever
  // pushing them, and the factory would happily build either.
  Never,
};

// Which of the three `id` is. Total, and the table behind it cannot be short.
Restore restorability(ScreenId id);

// What a screen asks the app to do after handling an event.
//
// FIVE OF THE KINDS ARE LATCHES, not instructions: `Sleep`, `Retry`, `Open`,
// `Finish` and `Delete` each name something only the shell can do, so the screen
// asks, App records the request, and the shell answers it on its next pass. (This
// line called `Retry` "the odd one out" when it was the only one, and then said
// FOUR; the count is what keeps going stale, so read the enum.)
//
// Storage is not core/'s -- the SD-missing
// screen cannot mount a card, and spec 6 requires its button actually re-attempt
// the mount rather than repaint the same message -- so the screen asks, App
// latches the request, and the shell answers it. See App::retryRequested().
struct Action {
  // APPENDED, never inserted -- a Kind is compared, never stored, but appending
  // costs nothing and keeps every existing value where it was.
  enum class Kind : uint8_t {
    None, Redraw, Push, Pop, PopTo, Replace, Sleep, Retry, Open, Finish, Delete, Wifi
  };
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
  // "Put `target` where I am" -- one screen leaves and one arrives, in one Action.
  //
  // ONE MODAL AT A TIME, AND A PUSH CANNOT EXPRESS IT. App::render draws EVERY
  // overlay above the topmost non-overlay, so pushing one overlay from another
  // leaves the asking screen's panel standing under the new one's veil, visible
  // wherever the two panels differ in size. That is invisible between ItemActions
  // and DeleteConfirm -- the confirmation is 380 wide against 340 and taller on
  // both geometries, so it covers it completely, which is why the boards do not
  // draw the actions panel behind it. It is NOT invisible under BookError, whose
  // paragraph makes its panel TALLER than the confirmation's, so the error dialog
  // stood out above and below the confirmation meant to replace it. Reported off
  // the device.
  //
  // Two Actions cannot express it either, for Action::popTo's reason: a screen
  // returns ONE Action, and a screen that followed a Pop with a Push of its own
  // would be reaching into the stack.
  static Action replace(ScreenId t) { return {Kind::Replace, t}; }
  static Action sleep() { return {Kind::Sleep, ScreenId::Home}; }
  static Action retry() { return {Kind::Retry, ScreenId::Home}; }
  // "Open the book I have selected." Shaped like Retry and for the same reason:
  // opening a book is READING A FILE OFF THE CARD, and storage is not core/'s. The
  // screen cannot push a Reader itself because a Reader needs a Document, and a
  // Document needs a zip, an inflate and an XHTML parse over a FileHandle the
  // screen has no way to get.
  //
  // It carries no path, deliberately. Adding one would put a std::string in every
  // Action -- returned by value from every gesture on every screen -- to serve one
  // Action kind. The shell already holds the LibraryScreen, so it can ask which
  // book is selected; see App::openRequested().
  static Action open() { return {Kind::Open, ScreenId::Reader}; }
  // "Mark the book I mean as finished." Shaped like Retry and Open and for the
  // identical reason: this is a WRITE TO THE CARD, and storage is not core/'s.
  //
  // It carries no path, deliberately, exactly as open() carries none -- adding one
  // would put a std::string in every Action returned by every gesture on every
  // screen to serve one kind. TWO screens ask and they mean different books:
  // BookEnd means the open book, the item-actions overlay means the Library's
  // focused row. The shell resolves it the way handleOpen already resolves open().
  static Action finish() { return {Kind::Finish, ScreenId::Home}; }
  // "Remove the book this confirmation names." A latch like Retry, Open and Finish,
  // and for their reason: the card is the shell's. It carries no path for the reason
  // Open carries none -- a std::string in every Action, returned by value from every
  // gesture on every screen, to serve one. The shell reads the path off the screen
  // that is still on top when the dispatch runs.
  static Action del() { return {Kind::Delete, ScreenId::Home}; }
  // "A Wi-Fi screen has an outcome for you." A latch like Delete, and it takes
  // Delete's contract exactly: NOTHING IS POPPED, so the shell reads the
  // outcome off the screen that is still on top and then pops it itself.
  //
  // THE FIVE CONNECT-FLOW SCREENS EACH SHIPPED WITH A GETTER THE SHELL COULD
  // NOT CALL. `joinChosen()`, `cancelled()`, `chosen()` and `forgetChosen()`
  // all latched a result and then returned `Action::pop()` -- and dispatch's
  // Pop is `stack_.pop_back()`, which DESTROYS the screen. Every one of those
  // headers said the shell reads it after the pop; after the pop there is no
  // screen left to ask. shell/src/main.cpp already records this lesson for the
  // peek, and `deleteRequested()` already states the fix in its own words:
  // read it "WHILE IT IS STILL ON TOP, because the dispatch that follows pops
  // it".
  //
  // IT CARRIES NO OUTCOME, for the reason Open and Finish carry no path: a
  // payload here is a payload in every Action returned by every gesture on
  // every screen, to serve one kind. The outcomes are five different shapes --
  // an SSID and a lock bit, a passphrase, a three-way choice -- and no one
  // field could hold them. The screen is still standing, so it can be asked.
  //
  // A SCREEN LATCHES WHEN THE SHELL HAS WORK TO DO AND POPS ITSELF WHEN IT HAS
  // NOT. A plain Back off the picker is navigation and nothing else, so it
  // stays `Action::pop()` -- routing it through here would be machinery bought
  // for no work. Back off the CONNECTING dialog is not navigation: a join is in
  // flight and the radio has to be told.
  static Action wifi() { return {Kind::Wifi, ScreenId::Home}; }
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

 private:
  ButtonMask holds_ = 0;
  ButtonMask repeats_ = 0;
  bool splitMovers_ = false;

 public:
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
  // WHAT THE FOUR FRONT BUTTONS DO, and no longer a virtual each screen answers.
  //
  // Nine screens overrode this with the identical `hintHoldMask(vm_.holds)`, which
  // is the same fact the hint bar already states -- and two spellings of one fact
  // is how a ring ends up promising a hold nothing bound. A screen now DECLARES it
  // once, where it builds its view model, and these read the declaration.
  ButtonMask longPressable() const { return holds_; }
  ButtonMask autoRepeat() const { return repeats_; }

  // Buttons this screen wants to auto-repeat while held, accelerating. Zero for
  // everything but a list long enough to need it: on a four-row overlay a held
  // button that ran away would be a defect, not a convenience.
  //
  // Deliberately NOT derived from the hint bar, which is where longPressable()
  // comes from. A hold ring promises a DIFFERENT action; auto-repeat is more of
  // the same one, so it has nothing to announce and no slot to announce it in.

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
  // OVERRIDE THEM IN PAIRS -- and the way to do that is to derive from
  // FocusScreen (focus_screen.h), where the pair is final and cannot be
  // half-taken. A screen that reports a focus and does not accept one back is
  // not a screen with a limitation, it is a screen that loses the user's place
  // on every wake without saying so -- three of them shipped that way, each with
  // a header comment explaining why its own case was the exception. There is no
  // exception; test_focus_restore.cpp walks the whole catalogue, and every
  // focused screen now inherits both halves from one mechanism.
  //
  // THE BOOL MEANS "SOMETHING MOVED", NOT "THE RESTORE LANDED", and this comment
  // used to claim both in one sentence -- "setFocus returning false says the
  // restore did not land -- the same contract ScrollWindow::setFocus uses". Those
  // are different questions and ScrollWindow answers the first, so the two halves
  // contradicted each other, and the two screens written since each followed a
  // different half. SettingsScreen returned "landed" on the reasoning that
  // restoring onto the row a screen is already on is a successful restore, which
  // is TRUE and is not what this bool is for.
  //
  // "Moved" wins for three reasons. It is what ScrollWindow, Focus and every
  // other screen already answer. It is what moveFocus needs, to return None
  // instead of paying a 520 ms refresh that repaints an identical screen. And the
  // base-class default below is false for a screen with no focus -- which is the
  // right answer to "did anything move" and the wrong answer to "did it land",
  // since asking a focusless screen for 0 lands perfectly well.
  //
  // "DID IT LAND" IS STILL ANSWERABLE, and by the caller that wants it: compare
  // focus() to what you asked for. App::restore's logging does exactly that,
  // because a bool cannot say "the record named row 12 and the screen is on row
  // 4" and that is the sentence a log reader needs.
  //
  // DEFAULTS THAT MEAN "I HAVE NO FOCUS TO REPORT OR RESTORE": 0, and false. A
  // screen with one thing on it (the SD-missing prompt) is not obliged to pretend
  // otherwise.
  //
  // A negative focus is legitimate and means "nothing selected" (Home's Continue
  // block, an empty Library), and a screen that reports one must accept it back:
  // on Home, -1 (Continue) and 0 (the first menu row) are two different places
  // the user can be, so a round trip that cannot tell them apart is a wake onto
  // the wrong one. The session record's field is signed for that reason; see
  // saveWhereWeAre in shell/src/main.cpp.
  virtual int focus() const { return 0; }
  virtual bool setFocus(int index) {
    (void)index;
    return false;
  }

  // WHAT THE FOCUS IS AN INDEX INTO, when the screen's list is not the only list
  // it could be showing. Empty means "there is only ever one", which is the
  // default and true of every screen but the Library.
  //
  // THE DEFECT IT CLOSES (#14): the Library can be listing a SUBFOLDER of
  // /books, and the record carried no way to say which -- so sleeping in
  // /books/Classics on row 3 woke on /books row 3. That is worse than losing the
  // position, because row 3 of the wrong folder looks exactly like a restore that
  // worked. A focus is only meaningful relative to the list it indexes, so the
  // list has to be part of the record.
  //
  // AN OPAQUE STRING, AND core/ NEVER LEARNS WHAT IS IN IT. The Library's is a
  // directory path; nothing here knows that, and the record's format knows only
  // how to carry bytes across a chip reset (see session_record.h, which escapes
  // the two characters the wire uses). A `path` field on StackEntry would put a
  // filesystem into App and name one screen in a format that names none -- the
  // ladder-of-screen-names shape App::restore exists to have deleted.
  //
  // THE BOOL MEANS "YOU ARE THERE NOW", AND THAT IS NOT setFocus's BOOL. That
  // difference is deliberate and it has one reader: App::restore, which applies
  // the focus ONLY when the place was honoured. A place is not a coordinate you
  // can be part of the way to -- either the screen is showing that list or it is
  // showing a different one -- and asking for the place you are already on is a
  // restore that landed, where an unchanged FOCUS genuinely means no repaint is
  // owed. setFocus's "something moved" cannot answer this question, so it is not
  // spelled that way here.
  //
  // FALSE IS THE DEFAULT, WHICH IS WHAT MAKES THE HALF-TAKEN PAIR DEGRADE RATHER
  // THAN MISLEAD. Screen::focus/setFocus shipped one-way on three screens, each
  // behind a comment arguing its own case was the exception, and the fix was
  // FocusScreen making the pair final. There is no equivalent here -- one screen
  // has a place, and a shared base for one caller is a header edge bought for
  // nothing -- so the enforcement is at the one place that applies a focus: a
  // screen that reports a place and forgets to accept one back has its restored
  // focus DROPPED and lands at the top of its own list, which is the honest
  // outcome rather than the plausible-looking wrong row. reading_position.h's
  // fitOf grading is the same rule: degrade, never mislead.
  virtual std::string_view place() const { return {}; }
  virtual bool setPlace(std::string_view place) {
    (void)place;
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

  // A RAW PRESS ARRIVES HERE AND A GESTURE LEAVES. Not virtual: the translation is
  // one mapping and it was previously done nine times, six of them as a guard a
  // screen had to remember (`if (ev.kind != Short) return none()`) whose omission
  // silently made a hold do a press's job.
  Action onEvent(const InputEvent& ev) {
    const GestureEvent g = gestureFor(ev, holds_, repeats_, splitMovers_);
    if (g.what == Gesture::None) return Action::none();
    return onGesture(g);
  }

  // What a screen implements instead. It sees intent and distance, never a
  // PressKind -- see gesture.h.
  virtual Action onGesture(const GestureEvent& g) = 0;

 protected:
  // Declared where the screen builds its hint bar, so the ring and the binding
  // cannot drift: a ring always means a hold is bound, and a bound hold always
  // shows a ring.
  void declareHints(const std::array<bool, 4>& holds) { holds_ = hintHoldMask(holds); }
  // Which buttons scroll while held. Only a list long enough to need it -- on a
  // four-row overlay a held button that ran away would be a defect.
  void declareRepeat(ButtonMask mask) { repeats_ = mask; }
  // Keep the front row and the side buttons apart -- see Gesture::AltPrev. Only
  // the Reader asks, because only the Reader has a use for a fifth binding and no
  // free button to put it on.
  void declareSplitMovers() { splitMovers_ = true; }

 public:
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

// ONE SCREEN'S PLACE ON THE STACK: which screen, where its focus was, and WHAT
// THAT FOCUS IS AN INDEX INTO. A snapshot is a vector of these, ROOT FIRST, and
// it is everything a wake needs to put the user back exactly where they were.
//
// Deliberately not a Screen* or an index into anything: it survives a chip reset,
// which is what deep sleep is, so it can only hold values.
struct StackEntry {
  ScreenId screen = ScreenId::Home;
  // Screen::focus()'s number, and negative is a position (Home's CONTINUE block,
  // an empty Library), not an error. See Screen::focus.
  int focus = 0;
  // Screen::place()'s string, OWNED -- the snapshot outlives the screens it
  // describes by design, since the point of it is to survive their destruction.
  // Empty for every screen but the Library, and short enough to fit a small
  // string on both host libraries when it is not (`/books/Classics` is 15 bytes),
  // so a snapshot per press costs no allocation for the folders a card carries.
  std::string place;

  friend bool operator==(const StackEntry& a, const StackEntry& b) {
    return a.screen == b.screen && a.focus == b.focus && a.place == b.place;
  }
  friend bool operator!=(const StackEntry& a, const StackEntry& b) { return !(a == b); }
};

class App {
 public:
  // V1's deepest path is Home > Library > item actions > delete confirm. Public
  // because the record format has to refuse a stack this cannot hold -- a longer
  // one could only ever be half-restored, and "the record was usable" has to stay
  // a single yes-or-no.
  static constexpr size_t kMaxDepth = 8;

  App(std::unique_ptr<Screen> root, ScreenFactory& factory);

  Screen& top();
  const Screen& top() const;

  // THE SCREEN AT `depth` FROM THE ROOT, 0 being the root itself. What a caller needs
  // to ask an overlay's PARENT something -- the reader menu is a panel over the Reader,
  // and the chapter it marks `NOW` is the Reader's, not the menu's.
  //
  // Bounds-checked to the top rather than asserting: an out-of-range index is a caller
  // bug, and returning the top is a readable screen where a crash is a dead device.
  const Screen& at(int index) const;

  // The stack, MUTABLY, by index. `at()` is const because a renderer must not move a
  // screen it is drawing; this exists for the shell, which legitimately has to reach
  // a screen BELOW the top -- the Typography panel's apply path re-paginates the
  // Reader under the menu it was dismissed from, and gives its page ring back on the
  // way in.
  //
  // A const_cast at the call site would do the same thing and say nothing about why
  // it is allowed, which is the difference worth one method. Same bounds rule as
  // `at()`, so the two cannot disagree about an out-of-range index.
  Screen& atMut(int index);

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

  // WHERE THE USER IS, root first, for the wake record to store.
  //
  // Every screen answers through Screen::focus(), so nothing here knows what any
  // of them are -- which is the point. The mechanic used to be a ladder in the
  // shell with Home and the SD-missing screen written into it by name, and every
  // screen that was not in the ladder silently lost the user's place.
  //
  // IT STOPS AT THE FIRST SCREEN A WAKE WILL NOT PUT BACK (#49), so every screen
  // the record names is one that comes back and the record is TRUE rather than
  // aspirational. Sleeping under a peek therefore stores `...;reader:0` and the
  // wake reports a COMPLETE restore, where it used to store the peek as well and
  // then report stopping short -- which reads in a log exactly like the three
  // defects that were stopping short for want of priming.
  //
  // TRUNCATED, NOT FILTERED. Dropping a Restore::Never entry from the MIDDLE
  // would hand the wake a stack that never existed; stopping is the honest rule
  // and costs nothing today, since every Never screen is a modal on top or is
  // painted without being pushed at all.
  std::vector<StackEntry> snapshot() const;

  // WHAT A RESTORE DID, so the caller can log it without asking which screens
  // were involved. `restored` counts the entries now standing, the root included,
  // so restored == requested is a complete restore and anything less names how
  // far it got.
  struct RestoreReport {
    int requested = 0;
    int restored = 0;
    // The record's root is this App's root. False means nothing was restored at
    // all: an empty record, a stack that is already deep, or a root that
    // disagrees -- the card went away while the device slept, so the boot path
    // rooted this App at the no-card screen and a record naming Home must not be
    // layered over it. One rule, no screen named.
    bool rootMatched = false;
    // WHICH SCREEN THE RESTORE STOPPED AT, when it stopped short, and `stopped`
    // is what says whether `stoppedAt` means anything -- Home is a real id, so
    // it cannot double as "nothing stopped", which is the sentinel mistake this
    // file records for CoverResult and for peekPrimed_.
    //
    // IT EXISTS SO THE CALLER CAN ATTRIBUTE THE STOP (#49). Ask restorability()
    // about it: `Never` is the mechanism working and nothing was owed, and
    // anything else is the factory refusing a screen a wake was supposed to get
    // -- which is a firmware defect and must not read like the other one. Before
    // this, a restore that stopped printed one line whichever it was, and three
    // screens shipped the defect while a fourth shipped the decision.
    bool stopped = false;
    ScreenId stoppedAt = ScreenId::Home;
  };

  // PUT A SNAPSHOT BACK. Only meaningful on a fresh App, which is the only thing
  // that ever calls it -- the boot path, right after building the root.
  //
  // IN ORDER, AND EACH ENTRY'S FOCUS BEFORE THE NEXT PUSH. That ordering is not
  // tidiness: an overlay reads the focused row of the screen underneath it AT
  // CONSTRUCTION, so the parent has to be both present and already focused before
  // the overlay is built. It is also what makes an overlay restorable at all --
  // the old one-screen record could never satisfy the factory, which correctly
  // refuses to build an overlay with no live Library under it.
  //
  // A push the factory refuses STOPS the restore and keeps what already stands: a
  // record from a newer firmware naming a screen this build cannot make should
  // not cost the user the Library they really were in. Either way the report
  // names the screen it stopped at, so the caller can say WHICH of the two it
  // was -- see RestoreReport::stoppedAt.
  //
  // AND A Restore::Never ENTRY IS REFUSED BEFORE THE FACTORY IS ASKED (#49).
  // snapshot() will not write one, so this is for a record an older firmware
  // wrote -- and for the screens the factory would cheerfully BUILD. Sleep and
  // BatteryEmpty are both buildable, and both are kept out of a record today only
  // by nothing ever pushing them, which is a property of the shell rather than a
  // rule: restoring either would wake the device onto `ASLEEP, HOLD POWER TO
  // WAKE` or onto a battery-empty prompt over a pack that has just been charged.
  RestoreReport restore(const std::vector<StackEntry>& stack);

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

  // "PUT `id` WHERE THE TOP SCREEN IS" -- Action::Kind::Replace's own body,
  // extracted because the SHELL is its second caller and the second copy is
  // the extraction point. It drives the connect flow, where every step
  // replaces the one that asked for it: the keyboard must not be left
  // standing under the CONNECTING dialog, and the dialog must not be left
  // under the error panel. App::render draws EVERY overlay above the topmost
  // non-overlay, so a push there leaves the asking panel visible wherever the
  // two differ in size -- which is how that defect was reported off a device.
  //
  // PUSHES BEFORE IT REMOVES, so a factory that refuses leaves the stack
  // exactly as it was, and degrades to a plain push at the root. Same
  // guarantees as the Action, because it is the same code.
  bool replaceScreen(ScreenId id);

  // POP, WITHOUT A PRESS. Action::Kind::Pop's own body, extracted for
  // replaceScreen's reason: the shell is its second caller.
  //
  // THE SHELL COULD NOT DO THIS BY SYNTHESISING A BACK, and that is not a
  // convenience argument -- it is a correctness one. `dispatchBack()` sends a
  // Back PRESS to the top screen, so what happens next is whatever that
  // screen's onGesture does with it. Every connect-flow screen answers Back
  // with Action::wifi(), so the shell's own cancel handling re-latched the
  // request it was in the middle of serving and the screen never left: an
  // infinite latch loop that reached the glass as a Back hint that did
  // nothing. dispatchBack works for DeleteConfirm only because THAT screen's
  // Back returns a pop.
  //
  // Refuses the root, exactly as the Action does: popping it would leave
  // nothing to render and nothing to receive the next event.
  bool popScreen();

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

  // SOMETHING OUTSIDE THE INPUT PATH CHANGED WHAT THE TOP SCREEN DRAWS.
  //
  // Every other route to dirty_ is a dispatch or construction, because until now
  // every reason to repaint was a press. The battery is the first fact that moves
  // on its own: the shell polls the gauge and a plug-in has to reach the glass
  // without a button being touched. It is the third member of the dirty/clearDirty
  // pair, not a pull latch like sleepRequested/retryRequested/openRequested below
  // -- there is nothing to notice and act on, it sets the same dirty_ the shell
  // already polls every loop.
  //
  // It does NOT set transition_. A transition means a screen changed, and that is
  // what kFullOnTransition spends the 693 ms GC waveform on; this is the same
  // screen with one mark different and takes the 389 ms DU.
  //
  // IT ALSO INVALIDATES THE PARTIAL-REPAINT RECORD, and that is not incidental.
  // canRenderTopOnly's clauses were all written assuming dirty_ only ever turns
  // true for a reason the TOP screen knows about -- a dispatch changes the top
  // screen's own state, a push or pop also sets transition_. This is the first
  // route that dirties the app for a reason unrelated to whatever is on top, and
  // with an overlay up every other clause still passes: the frame is unchanged,
  // the top is unchanged, the footprint is unchanged. Without this, renderTopOnly
  // would repaint only the overlay's own pixels and leave whatever markDirty was
  // actually called for stale on glass, with dirty_ cleared and nothing left to
  // correct it -- ItemActions and DeleteConfirm both carry non-zero footprints,
  // so this was reachable, not theoretical. Resetting painted_ forces the next
  // paint through App::render, full stack, whatever is on top -- an API a future
  // caller cannot misuse, rather than a comment saying not to. For Home this
  // changes nothing: a non-overlay was never eligible for the partial path.
  void markDirty();

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

  // The user pressed select on a book. The shell's job, in this order:
  //
  //   1. clearOpenRequest(), so a book that fails to open does not re-fire;
  //   2. ask the LibraryScreen which item is focused and openChapter() it --
  //      keeping SD traffic off the display bus, exactly as the retry does;
  //   3. on success, setReaderChapter() on the factory and pushScreen(Reader);
  //      on failure, log the reason and leave the Library standing.
  //
  // Nothing here repaints on its own, for the reason Retry gives: a book that
  // failed to open has not changed what is on glass, and a screen change is the
  // shell's to make.
  bool openRequested() const { return open_; }
  void clearOpenRequest() { open_ = false; }

  // The user asked for a book to be marked finished. The shell's job, in order:
  //
  //   1. clearFinishRequest(), so a failed write does not re-fire forever;
  //   2. work out WHICH book -- the open one if a Reader is on the stack under
  //      BookEnd, otherwise the Library's focused row;
  //   3. load that book's position (or build a minimal record for a book never
  //      opened), set `finished`, savePosition, keeping SD traffic off the display
  //      bus exactly as the retry and the open do;
  //   4. clear last.json ONLY IF it names this book -- clearing it unconditionally
  //      would take an unrelated book off Home's CONTINUE block, which is another
  //      book's state destroyed by this book's button;
  //   5. set gHomeStale AND gLibraryStale, separately, because each is consumed
  //      when its own screen is reachable and one shared flag lets Library, Back,
  //      Home clear it before Home uses it.
  //
  // A FAILED SAVE IS LOGGED AND NOT FATAL. writeAll calls noteCardGone() on a write
  // that fails after opening, which pollCardPresence turns into an App rooted at
  // SdMissingScreen -- so treating this as fatal would throw a reader out of a book
  // they can still read, over a flag. reading_store.h states the same hazard for
  // savePosition and it applies unchanged.
  bool finishRequested() const { return finish_; }
  void clearFinishRequest() { finish_ = false; }

  // The user confirmed a delete. The shell's job, in this order:
  //
  //   1. clearDeleteRequest(), so a failed removal does not re-fire forever;
  //   2. read the path off the DeleteConfirm screen -- WHILE IT IS STILL ON TOP,
  //      because the dispatch that follows pops it and after that there is no screen
  //      left to ask. Contents' chosenSpine() has exactly this shape;
  //   3. fs.remove(path), keeping SD traffic off the display bus;
  //   4. if a Library exists, rescan() it; and set gHomeStale AND gLibraryStale,
  //      separately, because each is consumed when its own screen is reachable.
  //
  // THE RESULT IS NOT BRANCHED ON. FileSystem::remove reports the END STATE, so a
  // false means the file is still there -- and the list the reader lands on already
  // says which it was. An error panel would be a screen with no board saying
  // something the Library already shows.
  bool deleteRequested() const { return delete_; }
  void clearDeleteRequest() { delete_ = false; }

  // A connect-flow screen has latched an outcome. The shell's job, in order:
  //
  //   1. clearWifiRequest(), so a failed attempt does not re-fire forever;
  //   2. ask the screen that is STILL ON TOP which outcome it was -- the
  //      picker's chosenSsid()/chosenLocked()/rescanChosen(), the keyboard's
  //      joinChosen()/entered()/cancelled(), the dialog's cancelled(), the
  //      error's chosen(), the actions overlay's forgetChosen(). This is the
  //      whole reason nothing is popped here: after a pop there is no screen
  //      left to ask, which is the defect this latch closes;
  //   3. drive the radio or the store, keeping neither in core/ -- a radio is
  //      the shell's exactly as the card is;
  //   4. pop, or replace, or leave the screen standing, whichever the outcome
  //      calls for. Where the flow lands is a fact about the outcome and not
  //      about the screen, which is why the screen does not decide it -- the
  //      same argument Delete makes for popTo(facts().returnTo).
  //
  // Nothing here repaints on its own, for Retry's reason: what a join changes
  // on glass is a screen change rather than a repaint of this one.
  bool wifiRequested() const { return wifi_; }
  void clearWifiRequest() { wifi_ = false; }

  ButtonMask longPressable() const { return top().longPressable(); }
  ButtonMask autoRepeat() const { return top().autoRepeat(); }

 private:
  // The bounds rule at() and atMut() share. See its body.
  size_t clampIndex(int index) const;

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
  bool open_ = false;
  bool finish_ = false;
  bool delete_ = false;
  bool wifi_ = false;
  // Mutable because render() is const: painting does not change the app, but it
  // does change what is on glass, and this is what remembers that. The
  // alternative -- a non-const render() -- would make every const App& in the
  // tests and the simulator unable to paint, for no gain.
  mutable PaintRecord painted_;
};

}  // namespace reader
