#include "reader/app.h"

namespace reader {

namespace {

// PUT ONE ENTRY'S POSITION BACK: the place first, and the focus ONLY if the place
// was honoured.
//
// The order is the whole of #14. A focus is an index into a list, and the Library
// can be listing a subfolder of /books -- so applying row 3 to a screen that is
// showing a different directory from the one the record named is a plausible
// looking wrong row, which is worse than no row at all. Every way a place can
// fail to come back -- the folder was deleted while the device slept, the card in
// the slot is a different card, a screen that reports a place and never learned
// to accept one -- goes down this one branch and lands the user at the top of
// whatever list the screen DID build. That is reading_position.h's grading rule
// over a different quantity: degrade, never mislead.
//
// An empty place means the screen has only ever one list, so the focus means what
// it always meant. That is every screen but the Library.
void restoreFocusIn(Screen& screen, const StackEntry& entry) {
  if (!entry.place.empty() && !screen.setPlace(entry.place)) return;
  screen.setFocus(entry.focus);
}

}  // namespace

// WHICH OF THE THREE EVERY SCREEN IS (#49) -- see Restore in the header for what
// the answers mean and why the question exists at all.
//
// A TABLE INDEXED BY ORDINAL, static_assert'ed against Count, and NOT a switch.
// An exhaustive switch with no `default:` leans on -Wswitch, which this project
// does not build with -Werror -- CLAUDE.md records a screen appended while three
// such switches answered it wrongly and the only diagnostic was three warnings
// scrolling past. An array whose length is asserted cannot be short: appending a
// ScreenId FAILS THE BUILD here until the new screen answers this question, which
// is the whole point of the mechanism and is #42's sentinel doing its job again.
//
// IN ENUM ORDER, and the order is load-bearing because the index IS the ordinal.
constexpr Restore kRestorability[] = {
    // Home -- the root. A wake owes it nothing: restore() sets the root's focus
    // rather than rebuilding it, which is a fact about the root and not about Home.
    Restore::Ready,
    // Library -- the factory holds the filesystem and the root path it lists.
    Restore::Ready,
    // ItemActions -- reads the focused row of the Library beneath it, and restore()
    // puts that Library back, focused, BEFORE this is pushed. That ordering is why
    // an overlay is restorable at all.
    Restore::Ready,
    // DeleteConfirm -- the same, through the Library's focused row.
    Restore::Ready,
    // BookDetails -- the same again.
    Restore::Ready,
    // Settings -- the factory holds the settings copy and the sink.
    Restore::Ready,
    // Sleep -- NEVER. It is painted directly and never pushed, on its own argument:
    // the record names the top of the stack, so a pushed SleepScreen would make the
    // next wake restore INTO it -- press power, get "asleep, hold power to wake"
    // back. The factory BUILDS one happily, so before this the rule was "nothing
    // pushes it" and nothing enforced that.
    Restore::Never,
    // Reader -- the book. The shell primes it from last.json; the factory refuses a
    // Reader with neither a book nor an explicit demo, which is what once woke this
    // device into Middlemarch.
    Restore::NeedsPriming,
    // ReaderMenu -- its header, which is the open book's title and progress.
    Restore::NeedsPriming,
    // Contents -- the book's table of contents, read off the card.
    Restore::NeedsPriming,
    // SdMissing -- takes no arguments. A missing card is a missing card.
    Restore::Ready,
    // Typography -- the same settings copy and sink Settings gets.
    Restore::Ready,
    // Peek -- NEVER, and this is the one entry that was already a DECISION rather
    // than a defect. Confirmed on device (2026-08-29): a peek is a transient "am I
    // sure?", and waking onto your own page is the calmer default. It was expressed
    // only as a factory refusal, which is the same observation as the three screens
    // refused because nobody had primed them -- so it is stated here, and snapshot()
    // now keeps it out of the record instead of letting the wake discover it.
    // Restoring one would also need a peeked cursor nothing persists.
    Restore::Never,
    // BookEnd -- its Facts, primed by openBookAt beside the Reader's book.
    Restore::NeedsPriming,
    // BookError -- NEVER, and the reasoning was already written down in the shell
    // with nowhere to live: openBookAt raises this dialog only when `push` is true,
    // which is a reader's press, "because waking into a modal about a book nobody
    // just asked for replaces a calm landing with an interruption". That is a
    // decision about restorability, so it belongs where that question is now asked.
    Restore::Never,
    // BatteryEmpty -- NEVER, on Sleep's argument and stated in its own enum comment:
    // painted directly and never pushed, because a wake into it is a battery-empty
    // prompt over a pack that has just been charged. Buildable, so the same gap.
    Restore::Never,
    // WifiSettings -- READY, and this entry was written NeedsPriming until the
    // catalogue test said otherwise, which is the declaration being checked rather
    // than believed. loadWifi() hands the factory the saved list and the sink at
    // boot and again after every change to it, so the hub is standing before a
    // restore ever asks -- device state the factory holds, exactly as the settings
    // copy is, and not content a press produces. The hub is also the ONE
    // connect-flow screen a wake may put back, and it is honest there: its band
    // reads `ON DEMAND`, which after a chip reset is exactly true.
    Restore::Ready,
    // WifiPicker -- NEVER. The five screens past the hub each describe something IN
    // FLIGHT, and deep sleep is a chip reset that ends all of it along with the
    // radio. A restored picker would show an empty scan list with nothing scanning:
    // a boarded state, and a false one, because it says nothing was found where
    // nothing looked.
    Restore::Never,
    // WifiPassword -- NEVER. A half-typed passphrase is not persisted and must not
    // be: the record is NVS and in the clear, and a keyboard that came back holding
    // the last attempt's passphrase is the defect setWifiTarget was rewritten for.
    Restore::Never,
    // WifiConnect -- NEVER. There is no join in flight after a chip reset, so the
    // dialog would say CONNECTING... about nothing and never resolve.
    Restore::Never,
    // WifiError -- NEVER. The attempt it describes is gone with the radio, and
    // endWifiSession() has already cleared the SSID it names.
    Restore::Never,
    // WifiNetworkActions -- NEVER, on Peek's argument rather than the radio's: its
    // Facts belong to the press that opened it, and it is re-primed on every loop
    // iteration the hub is on top -- which a restore, happening before any iteration
    // runs, is not.
    Restore::Never,
    // Articles -- READY, and for the Library's reason rather than the hub's: the
    // factory holds the FileSystem the list is built from, so a wake rebuilds it
    // off the card exactly as it rebuilds the Library. What the list shows is a
    // directory, which survives a chip reset because it is on the card.
    Restore::Ready,
    // ArticleActions -- NEVER, on WifiNetworkActions' argument: its Facts belong to
    // the press that opened it. Nothing re-primes them on a restore, and an overlay
    // naming the wrong article would archive the wrong article.
    Restore::Never,
    // ArticleEnd -- NeedsPriming, on BookEnd's: its Facts come from the article
    // openBookAt opened, beside the Reader's own book.
    Restore::NeedsPriming,
    // WallabagAccount -- READY, and this row is WifiSettings' twice over. The
    // factory holds the FileSystem, so the counts and the watermark come off the
    // card; and what the screen says after a chip reset is true, because every
    // value on it describes the card rather than anything in flight.
    Restore::Ready,
    // WallabagConnecting -- NEVER. A sync is in flight, and deep sleep is a chip
    // reset that ends it along with the radio. A restored dialog would say
    // SYNCING... about nothing and never resolve, which is WifiConnect's own
    // sentence one flow over.
    Restore::Never,
    // WallabagError -- NEVER. The attempt it describes is gone with the radio, and
    // waking into a modal about a sync nobody remembers asking for is Peek's
    // argument and BookError's.
    Restore::Never,
    // ArticlesRemoveConfirm -- NEVER, on DeleteConfirm's OPPOSITE answer, and the
    // difference is worth stating. That confirmation is Ready because it reads
    // the Library's focused row and restore() puts that Library back first. This
    // one is built from the ACCOUNT screen's press and carries no row to be
    // rebuilt from -- and waking into "remove every article?" is a destructive
    // question nobody asked, which is BookError's argument at its sharpest.
    Restore::Never,
    // Names -- READY, AND THIS ROW WAS WRITTEN NeedsPriming FIRST, from reading the
    // design rather than the screen. test_focus_restore.cpp caught it in the same
    // pass that fixed the Wi-Fi hub the same way: the line between the two answers
    // is "did a PRESS produce it", and today nothing did. The screen has no rows --
    // the card's name store is #156 -- so a boot-configured factory builds it
    // complete, and declaring otherwise would have claimed a debt the shell does not
    // owe and could not pay.
    //
    // IT BECOMES NeedsPriming WHEN THE STORE LANDS, in that change and not before,
    // because that is when a press starts producing something a wake cannot. The
    // test above is what will say so.
    Restore::Ready,
};
static_assert(sizeof(kRestorability) / sizeof(kRestorability[0]) ==
                  static_cast<size_t>(ScreenId::Count),
              "a ScreenId was added or removed; say whether a wake may put it back, "
              "and what it owes first -- see Restore in app.h");

Restore restorability(ScreenId id) {
  // The sentinel is not a screen and nothing may give it a row. Answering Never
  // rather than indexing past the table is the same refusal sessionWireName and
  // screenName make, and for the same reason: a sentinel that became restorable
  // would be a worse version of the bug the sentinel closes.
  if (id >= ScreenId::Count) return Restore::Never;
  return kRestorability[static_cast<size_t>(id)];
}

bool screenUsesRadio(ScreenId id) {
  // NO `default:`, deliberately -- see the header. The cost of getting this
  // wrong is not a mis-labelled log line, it is the radio running behind a
  // screen that does not say so, and -Wswitch is what makes a seventh screen
  // answer the question rather than inherit an answer.
  switch (id) {
    case ScreenId::WifiPicker:    // SCANNING
    case ScreenId::WifiConnect:   // CONNECTING...
    case ScreenId::WallabagConnecting:  // CONNECTING... then SYNCING...
      return true;
    // The other four Wi-Fi screens have nothing in flight. The hub in
    // particular says `ON DEMAND`, which is a claim the radio is off.
    case ScreenId::WifiSettings:
    case ScreenId::WifiPassword:
    case ScreenId::WifiError:
    case ScreenId::WifiNetworkActions:
    // ARTICLES OVER WALLABAG, and exactly ONE of the six answers true. The sync
    // runs behind WallabagConnecting and nowhere else: the list, the overlay,
    // the end screen and the account screen are all read off the CARD, and the
    // error dialog describes a radio that is already down. So this function's
    // true-set goes from two to three, and that count is asserted in
    // test_article_outcomes.cpp rather than left as a comment.
    // Names reads the CARD and never the radio, which is the Articles list's own
    // answer one flow over.
    case ScreenId::Names:
    case ScreenId::Articles:
    case ScreenId::ArticlesRemoveConfirm:
    case ScreenId::ArticleActions:
    case ScreenId::ArticleEnd:
    case ScreenId::WallabagAccount:
    case ScreenId::WallabagError:
    case ScreenId::Home:
    case ScreenId::Library:
    case ScreenId::ItemActions:
    case ScreenId::DeleteConfirm:
    case ScreenId::BookDetails:
    case ScreenId::Settings:
    case ScreenId::Typography:
    case ScreenId::Reader:
    case ScreenId::ReaderMenu:
    case ScreenId::Contents:
    case ScreenId::Peek:
    case ScreenId::BookError:
    case ScreenId::BookEnd:
    case ScreenId::BatteryEmpty:
    case ScreenId::Sleep:
    case ScreenId::SdMissing:
    case ScreenId::Count:
      return false;
  }
  return false;
}

const char* screenName(ScreenId id) {
  switch (id) {
    case ScreenId::Home: return "HOME";
    case ScreenId::Library: return "LIBRARY";
    case ScreenId::ItemActions: return "ITEM-ACTIONS";
    case ScreenId::DeleteConfirm: return "DELETE-CONFIRM";
    case ScreenId::BookDetails: return "BOOK-DETAILS";
    // Without these the paint and alive lines said `screen=?` for both new screens --
    // a log label, so this is not the session record's name table (that one is a
    // storage format and lives in session_record.cpp).
    case ScreenId::ReaderMenu: return "READER-MENU";
    case ScreenId::Contents: return "CONTENTS";
    case ScreenId::Settings: return "SETTINGS";
    // Sleep was missing from this switch and fell through to "?", so every log
    // line naming it named nothing. Not caught by -Wswitch because the function
    // has a return after the switch -- which it needs, for an id cast from a
    // stored byte.
    case ScreenId::Sleep: return "SLEEP";
    case ScreenId::Reader: return "READER";
    case ScreenId::SdMissing: return "SD-MISSING";
    // A LOG LABEL, and the session record's "typography" is a storage format.
    // Two separate facts that happen to agree; this one is free to be reworded.
    case ScreenId::Typography: return "TYPOGRAPHY";
    case ScreenId::Peek: return "PEEK";
    // Kebab, as ITEM-ACTIONS and BOOK-DETAILS are. A log label, free to be
    // reworded; the session record's spelling is a storage format and is not this.
    case ScreenId::BookEnd: return "BOOK-END";
    case ScreenId::BookError: return "BOOK-ERROR";
    case ScreenId::BatteryEmpty: return "BATTERY-EMPTY";
    // The V1.1 connect flow. Kebab like their neighbours -- these are log
    // labels and are free to be reworded; session_record.cpp's hyphenated
    // spellings are a storage format and are not these, however alike they
    // happen to look.
    case ScreenId::WifiSettings: return "WIFI-SETTINGS";
    case ScreenId::WifiPicker: return "WIFI-PICKER";
    case ScreenId::WifiPassword: return "WIFI-PASSWORD";
    case ScreenId::WifiConnect: return "WIFI-CONNECT";
    case ScreenId::WifiError: return "WIFI-ERROR";
    case ScreenId::WifiNetworkActions: return "WIFI-NETWORK-ACTIONS";
    case ScreenId::Articles: return "ARTICLES";
    case ScreenId::ArticleActions: return "ARTICLE-ACTIONS";
    case ScreenId::ArticleEnd: return "ARTICLE-END";
    // A log label, free to be reworded; session_record.cpp's "names" is a storage
    // format and is not this, however alike the two happen to look.
    case ScreenId::Names: return "NAMES";
    case ScreenId::WallabagAccount: return "WALLABAG-ACCOUNT";
    case ScreenId::WallabagConnecting: return "WALLABAG-CONNECTING";
    case ScreenId::WallabagError: return "WALLABAG-ERROR";
    case ScreenId::ArticlesRemoveConfirm: return "ARTICLES-REMOVE-CONFIRM";
    // NOT A SCREEN -- see ScreenId::Count's own comment. Refused explicitly so this
    // switch stays exhaustive, the same reason session_record.cpp's does.
    case ScreenId::Count: return "?";
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

size_t App::clampIndex(int index) const {
  // ONE COPY OF THE BOUNDS RULE, shared by at() and atMut() -- two clamps would be two
  // chances to disagree about an out-of-range index, and the whole point of clamping
  // rather than asserting is that a caller bug yields a readable screen.
  if (index < 0) index = 0;
  if (index >= static_cast<int>(stack_.size())) index = static_cast<int>(stack_.size()) - 1;
  return static_cast<size_t>(index);
}

const Screen& App::at(int index) const { return *stack_[clampIndex(index)]; }

// See the header for why a mutable one exists. No cast: `stack_` is not const here.
Screen& App::atMut(int index) { return *stack_[clampIndex(index)]; }

Screen& App::top() { return *stack_.back(); }
const Screen& App::top() const { return *stack_.back(); }

std::vector<StackEntry> App::snapshot() const {
  std::vector<StackEntry> out;
  out.reserve(stack_.size());
  // The place is COPIED, because the snapshot's whole job is to outlive these
  // screens: Screen::place() hands back a view of the screen's own member.
  for (const auto& screen : stack_) {
    // STOPS AT THE FIRST SCREEN A WAKE WILL NOT PUT BACK (#49), so the record only
    // ever names screens that come back -- see the header for why it stops rather
    // than filtering. A peek over a reader stores the reader and ends there, and
    // the wake then reports a COMPLETE restore instead of reporting that it stopped
    // short, which is what a wake owed a screen nobody primed also reports.
    if (restorability(screen->id()) == Restore::Never) break;
    out.push_back({screen->id(), screen->focus(), std::string(screen->place())});
  }
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
  restoreFocusIn(*stack_.front(), stack.front());
  r.restored = 1;

  for (size_t i = 1; i < stack.size(); ++i) {
    // REFUSED BEFORE THE FACTORY IS ASKED (#49). snapshot() will not write one of
    // these, so what reaches here is a record an older firmware wrote -- and the
    // screens that most need refusing are the ones the factory would BUILD: Sleep
    // and BatteryEmpty are buildable, and were kept out of a record only by nothing
    // ever pushing them.
    if (restorability(stack[i].screen) == Restore::Never) {
      r.stopped = true;
      r.stoppedAt = stack[i].screen;
      break;
    }
    if (!pushScreen(stack[i].screen)) {
      // WHICH SCREEN, so the caller can say WHY. A factory refusing a screen this
      // build declares restorable means its construction inputs were never primed,
      // which is a firmware defect; ask restorability() about `stoppedAt` and the
      // two outcomes stop reading alike.
      r.stopped = true;
      r.stoppedAt = stack[i].screen;
      break;
    }
    // Before the next push, because an overlay reads the focused row of the
    // screen under it at construction time.
    restoreFocusIn(top(), stack[i]);
    ++r.restored;
  }

  // A restored stack has never been painted, whatever the root's focus did or did
  // not change. pushScreen sets these for every entry above the root; a record
  // that only moved the root's focus would otherwise come back unpainted.
  dirty_ = true;
  transition_ = true;
  return r;
}

bool App::popScreen() {
  // The root is the app: popping it would leave nothing to render and
  // nothing to receive the next event.
  if (stack_.size() <= 1) return false;
  stack_.pop_back();
  dirty_ = true;
  transition_ = true;
  return true;
}

bool App::replaceScreen(ScreenId id) {
  // PUSHED BEFORE THE OLD ONE IS REMOVED, so a factory that refuses leaves the
  // stack exactly as it was. Popping first would lose the screen that asked and
  // put the reader back on the list with nothing to show for the press -- the
  // same "wrong in a way the reader cannot see through" the factory's refusals
  // exist to avoid.
  const size_t before = stack_.size();
  if (!pushScreen(id)) return false;
  // The root is the app: with only a root there is nothing beneath the new
  // screen to remove, and erasing it would leave nothing to render and nothing
  // to receive the next event. That degrades to a plain Push, which is the right
  // answer for a caller that is somehow the root.
  if (before >= 2) stack_.erase(stack_.end() - 2);
  // pushScreen already set dirty_ and transition_. A replace IS a screen change,
  // so it takes the transition's full refresh and is never a partial repaint --
  // which it must not be, since the frame beneath it is about to be wrong.
  return true;
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

void App::markDirty() {
  dirty_ = true;
  // See app.h: this is the one route to dirty_ that is not about the top
  // screen, so the partial-repaint record cannot be trusted to still describe
  // what the next paint needs to cover. Resetting it to a fresh App's state is
  // what makes the next paint go through App::render regardless of what is on
  // top, rather than relying on every future caller to know not to call this
  // with an overlay up.
  painted_ = PaintRecord{};
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
      popScreen();
      break;
    case Action::Kind::PopTo:
      // Down to `target`, or to the root if it is not on the stack -- never past
      // it. Only one dirty/transition pair for however many screens go, because
      // the user sees one screen change however deep the flow was.
      while (stack_.size() > 1 && top().id() != a.target) stack_.pop_back();
      dirty_ = true;
      transition_ = true;
      break;
    case Action::Kind::Replace: {
      replaceScreen(a.target);
      break;
    }
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
    case Action::Kind::Finish:
      // Latched for the reason Retry and Open are: the card is the shell's. Nothing
      // is marked dirty -- what the write changes on glass is the shell's to decide,
      // and it is usually a screen change rather than a repaint of this one.
      finish_ = true;
      break;
    case Action::Kind::Delete:
      // Latched for the reason Retry, Open and Finish are: the card is the shell's.
      // Nothing is marked dirty and NOTHING IS POPPED -- the shell pops with
      // popTo(facts().returnTo) after the file is gone, because where a completed
      // delete lands is a fact about how the confirmation was reached.
      delete_ = true;
      break;
    case Action::Kind::Wifi:
      // Delete's contract exactly, and for the sharper version of its reason:
      // the radio is the shell's, and NOTHING IS POPPED because the shell has
      // to ask the screen which outcome it was. The five connect-flow screens
      // each popped themselves and then offered a getter, and a popped screen
      // is a DESTROYED screen -- see Action::wifi().
      wifi_ = true;
      break;
    case Action::Kind::Article:
      // Wifi's contract exactly, and for the same two reasons: the card and the
      // radio are the shell's, and NOTHING IS POPPED because the shell has to
      // ask the screen which of six outcomes it was. See Action::article().
      article_ = true;
      break;
  }
}

}  // namespace reader
