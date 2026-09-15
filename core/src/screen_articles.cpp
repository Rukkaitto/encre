#include "reader/screen_articles.h"

#include <string>

#include "reader/article_store.h"
#include "reader/reading_position.h"
#include "reader/reading_store.h"
#include "reader/theme.h"
#include "reader/wallabag_credentials.h"

namespace reader {
namespace {

// U+00B7, as its own two-byte literal. A C++ hex escape is UNBOUNDED, so
// "\xB7 MIN" parses `\xB7M` as one escape -- clang rejects it and the ESP32's
// GCC accepts it and emits a byte that is not a middle dot. Every other site in
// this firmware that draws one spells it this way for this reason.
constexpr const char* kMiddot = "\xC2\xB7";

std::string metaFor(const ArticleItem& a) {
  std::string s = a.domain;
  s += " ";
  s += kMiddot;
  s += " " + std::to_string(a.readingMinutes) + " MIN";
  // `READ` is a third field on the board rather than a different row treatment,
  // and the bullet carries the same fact as a shape. Two spellings of one fact,
  // which the board asks for: the mark is legible at a glance and the word is
  // unambiguous.
  // `READ` MEANS FINISHED, not opened. The bullet answers "have I started this"
  // and this answers "did I get to the end" -- two questions, and they were one
  // flag, which made an article opened for ten seconds claim both.
  if (a.finished) {
    s += " ";
    s += kMiddot;
    s += " READ";
  }
  return s;
}

const char* const kReadHint = "READ";
const char* const kSyncHint = "SYNC";
const std::array<std::string, 4> kHints{"BACK", kReadHint, "UP", "DOWN"};
// BACK and three empty slots. An empty slot is 36px and not zero
// (kHintEmptySlotW), which the board authors as a spacer div -- measuring one as
// nothing draws the live slot in the wrong place.
const std::array<std::string, 4> kSetupHints{"BACK", "", "", ""};

// design/ArticlesSetup.dc.html's own words, in ONE place: two constructors set
// them, and a second copy is a board change that reaches one variant.
constexpr const char* kSetupTitle = "READ IT LATER";
constexpr const char* kSetupProse =
    "Put the SD card in your computer and fill in your Wallabag details in its "
    "/.reader/wallabag.json file.";
constexpr const char* kSetupNote = "THE FILE IS ALREADY ON THE CARD.";

}  // namespace

ArticlesScreen::ArticlesScreen(std::vector<ArticleItem> items, std::string stamp)
    : FocusScreen(static_cast<int>(items.size()), static_cast<int>(items.size()),
                  Focus::WithNone),
      items_(std::move(items)) {
  vm_.title = "ARTICLES";
  vm_.syncLabel = "Sync now";
  vm_.syncStamp = std::move(stamp);
  vm_.hints = kHints;
  // A HOLD ON CONFIRM AND NOTHING ELSE, and the two movers repeat. `setAutoRepeat`
  // enforces the exclusion rather than documenting it, so this pair cannot
  // overlap by accident.
  vm_.holds = {false, true, false, false};
  declareHints(vm_.holds);
  declareRepeat(static_cast<ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down)));
  // THE FOCUS STARTS ON THE FIRST ARTICLE, NOT ON THE SYNC ROW, AND THE BOARD IS
  // WHAT SAYS SO -- design/Articles.dc.html draws row 0 inverted and the sync row
  // plain. It is also the better landing: a reader who opens this screen has come
  // to READ, and a sync is the thing they do occasionally.
  //
  // WITH NO ARTICLES THERE IS NOWHERE ELSE TO BE, so -1 stands and the sync row
  // is focused -- which is the only state where pressing Confirm immediately is
  // what the reader wants.
  if (!items_.empty()) setFocus(0);
  syncVm();
}

ArticlesScreen::ArticlesScreen()
    : FocusScreen(0, 0, Focus::Noneless) {
  vm_.title = "ARTICLES";
  vm_.notSetUp = true;
  // The board's own words. They live on the model rather than in the theme for
  // the reason every other string here does: the board owns the copy, and a
  // sentence in a renderer is a copy change that needs a code change.
  vm_.setupTitle = kSetupTitle;
  vm_.setupProse = kSetupProse;
  vm_.setupNote = kSetupNote;
  vm_.hints = kSetupHints;
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  syncVm();
}

ArticlesScreen::ArticlesScreen(FileSystem& fs) : FocusScreen(0, 0, Focus::WithNone), fs_(&fs) {
  vm_.title = "ARTICLES";
  vm_.syncLabel = "Sync now";
  vm_.hints = kHints;
  vm_.holds = {false, true, false, false};
  declareHints(vm_.holds);
  declareRepeat(static_cast<ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down)));
  load();
  if (!items_.empty()) setFocus(0);
  syncVm();
}

bool ArticlesScreen::load() {
  if (fs_ == nullptr) return false;
  const int visible = window().visibleRows();

  // WHICH VARIANT IS READ OFF THE CARD, and it is the CREDENTIALS that decide --
  // never the row count. `Absent` and `Unconfigured` are both "nobody has set
  // this up"; anything else is the list, empty or not.
  WallabagCredentials creds;
  std::string why;
  const CredentialsResult r = loadWallabagCredentials(*fs_, creds, why);
  const bool configured = (r == CredentialsResult::Ok);

  const std::vector<ArticleItem> before = items_;
  const std::string beforeStamp = vm_.syncStamp;
  const bool beforeNotSetUp = vm_.notSetUp;

  items_.clear();
  vm_.notSetUp = !configured;
  if (!configured) {
    vm_.syncStamp.clear();
    // The not-set-up variant's copy, which the fixture constructor also sets:
    // one place, so the two cannot drift.
    vm_.setupTitle = kSetupTitle;
    vm_.setupProse = kSetupProse;
    vm_.setupNote = kSetupNote;
    // Noneless: there is nothing to focus, and -1 would be a selection on an
    // invisible row -- HomeEmpty's rule.
    window() = ScrollWindow(0, 0, Focus::Noneless);
    vm_.hints = kSetupHints;
    vm_.holds = {false, false, false, false};
    declareHints(vm_.holds);
  } else {
    const ArticleStore store(*fs_);
    std::vector<ProgressEntry> progress;
    loadProgressIndex(*fs_, progress);
    for (const ArticleMeta& m : store.list()) {
      if (m.archived) continue;
      const ProgressEntry* p = progressFor(progress, store.epubPath(m.id));
      items_.push_back({m.id, m.title, m.domain, m.readingTime,
                        ArticleStore::openedFromProgress(p),
                        p != nullptr && p->finished, m.starred});
    }
    // THE STAMP IS WHAT THIS DEVICE OWES THE SERVER, not what the last sync did.
    // `WALLABAG . NO NEW` was redundant -- the account screen's `Last sync` row
    // draws the same watermark through the same `outcomeLabel`, and a reader
    // standing here has just been told the outcome by the screen that reported
    // it. What nothing else says is that a star or an archive is waiting to go
    // out, which is the one fact that makes pressing `Sync now` worth doing when
    // there is nothing new to fetch. design/Articles.dc.html has the argument.
    //
    // THE ACCOUNT SCREEN'S OWN WORDS, so the two cannot name one fact
    // differently -- that board's existing rule, applied to a new string.
    //
    // AND ZERO IS NOT A COUNT: an empty queue draws an empty stamp rather than
    // `0 TO PUSH`, which is Home's ARTICLES row one screen over and its reason.
    const int pending = store.pendingCount();
    vm_.syncStamp = pending > 0 ? std::to_string(pending) + " TO PUSH" : std::string();
    // THE WINDOW IS REBUILT HERE, which is where the count is known. It was not,
    // and the constructor over a FileSystem then had a zero-high window: the
    // slice was empty, the screen rendered no rows at all, and refreshProgress
    // walked off the end of it. `visibleRows` is carried, because setVisibleRows
    // may already have been called -- the shell sets it from the theme before
    // anything else, and a rescan must not throw it away.
    window() = ScrollWindow(static_cast<int>(items_.size()), visible, Focus::WithNone);
    vm_.hints = kHints;
    vm_.holds = {false, true, false, false};
    declareHints(vm_.holds);
  }
  return items_ != before || vm_.syncStamp != beforeStamp || vm_.notSetUp != beforeNotSetUp;
}

bool ArticlesScreen::rescan() {
  if (fs_ == nullptr) return false;
  const int was = focus();
  const bool moved = load();
  // THE FOCUS IS CARRIED RATHER THAN RESET, and clamped by the window: a sync
  // that removed rows must not move a selection the reader did not touch any
  // further than it has to. -1 is the sync row and stays there.
  if (!vm_.notSetUp && !items_.empty() && was >= 0) window().setFocus(was, nullptr);
  syncVm();
  return moved;
}

bool ArticlesScreen::refreshProgress() {
  // NO LISTING OF THE ARTICLES DIRECTORY, which is the point: only /.reader/state
  // changed, and the Library's own refreshProgress exists for the same reason --
  // a rescan on the critical path of a Back costs a directory walk per row.
  if (fs_ == nullptr || vm_.notSetUp) return false;
  std::vector<ProgressEntry> progress;
  loadProgressIndex(*fs_, progress);
  const ArticleStore store(*fs_);
  bool moved = false;
  for (ArticleItem& a : items_) {
    const ProgressEntry* p = progressFor(progress, store.epubPath(a.id));
    const bool opened = ArticleStore::openedFromProgress(p);
    const bool finished = p != nullptr && p->finished;
    if (opened != a.opened || finished != a.finished) {
      a.opened = opened;
      a.finished = finished;
      moved = true;
    }
  }
  if (moved) syncVm();
  return moved;
}

void ArticlesScreen::setVisibleRows(int n) {
  window().setVisibleRows(n);
  syncVm();
}

const ArticleItem* ArticlesScreen::focusedItem() const {
  const int f = focus();
  if (f < 0 || f >= static_cast<int>(items_.size())) return nullptr;
  return &items_[static_cast<size_t>(f)];
}

int ArticlesScreen::focusedId() const {
  const ArticleItem* a = focusedItem();
  return a == nullptr ? 0 : a->id;
}

std::string ArticlesScreen::focusedTitle() const {
  const ArticleItem* a = focusedItem();
  return a == nullptr ? std::string() : a->title;
}

void ArticlesScreen::setStamp(std::string stamp) {
  vm_.syncStamp = std::move(stamp);
}

void ArticlesScreen::setStatusLine(std::string line) {
  vm_.statusLine = std::move(line);
}

void ArticlesScreen::syncVm() {
  if (vm_.notSetUp) {
    vm_.bandValue = "NOT SET UP";
    vm_.rows.clear();
    // EXPLICIT, not left at whatever the configured branch set: this variant
    // draws no sync row, so a true here would invert a row that is not there.
    vm_.syncFocused = false;
    vm_.focusedRow = -1;
    vm_.firstRow = 0;
    vm_.totalRows = 0;
    return;
  }

  // UNREAD IS NEVER-OPENED, the same question the bullet answers, so the band and
  // the marks below it cannot disagree.
  int unread = 0;
  for (const ArticleItem& a : items_)
    if (!a.opened) ++unread;
  vm_.bandValue = std::to_string(unread) + " UNREAD";

  const ScrollWindow::Slice s = window().slice();
  vm_.rows.clear();
  vm_.rows.reserve(static_cast<size_t>(s.count));
  for (int i = 0; i < s.count; ++i) {
    const ArticleItem& a = items_[static_cast<size_t>(s.first + i)];
    vm_.rows.push_back({a.title, metaFor(a), a.opened, a.finished});
  }
  vm_.focusedRow = s.focused;
  vm_.firstRow = s.first;
  vm_.totalRows = static_cast<int>(items_.size());

  // -1 IS THE SYNC ROW, which is Home's CONTINUE block one screen over and the
  // whole reason the ring is `Focus::WithNone`.
  vm_.syncFocused = focus() < 0;

  // AND THE CONFIRM HINT FOLLOWS IT. The second hint bar in this firmware whose
  // text varies within a screen -- Settings' is the first -- and for its reason:
  // a bar reading READ while the focus sits on the sync row names an action the
  // press will not take.
  vm_.hints = kHints;
  vm_.hints[1] = vm_.syncFocused ? kSyncHint : kReadHint;
  // AND SO DOES THE HOLD RING. The bar's ring and the long-press binding read
  // ONE field -- `holds`, through `hintHoldMask()` -- precisely so a screen
  // cannot promise a hold it has not bound. It promised one here anyway, because
  // the array was set once at construction and never followed the focus: the
  // sync row drew a ring and a hold on it did nothing, which is the dead-button
  // shape this project has shipped three times. There is no actions overlay for
  // "sync now" to open.
  vm_.holds = {false, !vm_.syncFocused, false, false};
  declareHints(vm_.holds);
  // WITH NO ARTICLES THE MOVERS GO QUIET, because one focusable row means UP and
  // DOWN would promise a press that changes nothing -- WifiSettingsEmpty's rule,
  // and an empty slot is 36px rather than zero, so the live slots keep their
  // places.
  if (items_.empty()) {
    vm_.hints[2].clear();
    vm_.hints[3].clear();
  }
}

Action ArticlesScreen::onGesture(const GestureEvent& g) {
  chosen_ = Chosen::None;
  if (vm_.notSetUp) {
    // Back is navigation and pops itself; everything else is refused, because
    // there is nothing here to act on. A screen that latched from this state
    // would be a press with no work behind it, which is the dead-button shape
    // this project has shipped twice.
    return g.what == Gesture::Back ? Action::pop() : Action::none();
  }

  if (g.what == Gesture::Secondary) {
    // ON AN ARTICLE ROW ONLY. From the sync row there is nothing to act on, and
    // an overlay captioned with whichever article happened to be first would be
    // acting on a row the reader did not choose.
    if (focusedItem() == nullptr) return Action::none();
    return Action::push(ScreenId::ArticleActions);
  }

  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate:
      if (focusedItem() == nullptr) {
        // The sync row. A LATCH rather than a push: bringing the radio up and
        // driving the engine is the shell's, and the dialog it pushes is the
        // shell's to choose -- a device with no saved network gets an error
        // shape rather than a connecting one, and this screen cannot know which.
        chosen_ = Chosen::Sync;
        return Action::article();
      }
      // Action::open() carries no id, for its own stated reason: the shell reads
      // it off this screen, which is still on top.
      return Action::open();
    case Gesture::Back:
      return Action::pop();
    default:
      return Action::none();
  }
}

void ArticlesScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                            Plane plane) const {
  theme.renderArticles(fb, fonts, vm_, plane);
}

}  // namespace reader
