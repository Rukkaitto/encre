#pragma once
#include "reader/toc.h"
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/name_extracts.h"
#include "reader/names.h"
#include "reader/screen_settings.h"
#include "reader/screen_wallabag_account.h"
#include "reader/screen_wallabag_dialogs.h"
#include "reader/screen_wifi_error.h"
#include "reader/screen_wifi_network_actions.h"
#include "reader/screen_wifi_settings.h"
#include "reader/wifi_radio.h"
#include "reader/wifi_store.h"
#include "reader/screen_typography.h"
#include "reader/screen_book_details.h"
#include "reader/screen_article_actions.h"
#include "reader/screen_article_end.h"
#include "reader/screen_articles.h"
#include "reader/screen_book_end.h"
#include "reader/screen_book_error.h"
#include "reader/screen_delete_confirm.h"
#include "reader/screen_library.h"
#include "reader/book.h"
#include "reader/screen_peek.h"
#include "reader/screen_reader.h"
#include "reader/viewmodel.h"

namespace reader {

// The demo content Phase 2B navigates. Real content arrives in Phase 2C from the
// SD card and the settings store; until then this is the single definition both
// the simulator and the shell build from, so a screenshot from the desktop is
// evidence about the device rather than about a second, similar-looking
// catalogue.
HomeViewModel demoHomeVm();

// Home's menu rows, in order, and the screen each one opens.
// design/LibraryScrolled.dc.html -- 24 books, so the rail has proportions.
std::vector<LibraryItem> demoLibraryScrolledItems();

// design/HomeEmpty.dc.html -- Home with nothing to continue.
HomeViewModel demoHomeEmptyVm();

// design/HomeUnopened.dc.html. Books on the card, none of them open -- the third
// Home state and the one the device is actually in until a reading position
// exists. Same mechanism as the empty variant (nothingToContinue), different copy,
// and a real count on the LIBRARY row rather than `EMPTY`: that count is the whole
// fact separating the two screens, because it is what tells the user the books are
// there and it is worth going to look.
HomeViewModel demoHomeUnopenedVm();

// design/HomeMissing.dc.html. The pointer names a book the card no longer has, so
// the reading column stays -- the pointer still knows the name, the author and the
// percentage -- and a bordered strip above the stats says why they describe a book
// that will not open. demoHomeVm's OWN content with three fields changed, for
// demoHomeUnopenedVm's reason: the board is the same screen in a different
// condition, and a second copy of Middlemarch here would be a second place for it
// to drift.
HomeViewModel demoHomeMissingVm();

std::vector<ScreenId> demoHomeTargets();

// design/Sleep.dc.html's own values -- see the definition.
SleepViewModel demoSleepVm();
// design/Contents.dc.html's own list, for the simulator and the goldens.
// design/Names.dc.html's ten rows and design/Mentions.dc.html's sightings, shared by
// the simulator AND the goldens so the two cannot drift.
//
// TWO TALL ROWS IN TEN, which is the board's mix and the measured one: rows with no
// fuller form are 84% on a real novel and 97% on another. The board's first draft
// drew five in eight, from an estimate made before anything grouped.
std::vector<NameGroup> demoNames();
std::vector<StoredExtract> demoMentions();
// The fullest form Mentions' band shows for `demoMentions`.
const char* demoMentionsSubject();

std::vector<TocEntry> demoContents();
int demoContentsSpine();
// design/SleepIdle.dc.html: asleep with nothing open, so the badge without the card.
SleepViewModel demoSleepIdleVm();

// design/BookEnd.dc.html's own book -- the same Middlemarch design/Main.dc.html gives
// `CH. 01 OF 24`, because two boards drawing one demo book must agree.
BookEndScreen::Facts demoBookEndFacts();

// design/BookError.dc.html's own book -- the `dubliners.epub` its paragraph names,
// which is also the row design/Library.dc.html draws focused, because the board
// stacks this dialog over that list.
BookErrorScreen::Facts demoBookErrorFacts();
// design/BookErrorUnreadable.dc.html: the same file, the other refusal.
BookErrorScreen::Facts demoBookErrorUnreadableFacts();
// design/BookErrorMemory.dc.html: the same file, the third -- fine, and it did not fit.
BookErrorScreen::Facts demoBookErrorMemoryFacts();

// design/Peek.dc.html's own peeked text -- Middlemarch's opening, which is the board's
// story: the reader is at CH. 07, 34%, has met a name they cannot place, and has peeked
// back to CH. 01, 4%, to read the sentence that introduced her.
//
// DECLARED HERE WHERE demoReaderXhtml IS NOT, and the asymmetry is worth a line: that
// one has no declaration at all, because nothing outside screens.cpp has ever wanted
// it. This one is declared so a test can assert the demo peek shows the BOARD'S text
// rather than only that it shows some.
//
// A `std::string` by value, where demoReaderXhtml returns a view of a literal. Safe
// because ChapterReader::beginBuffer COPIES ("the bytes are copied, so the caller need
// not keep them"), which is what makes the temporary at the call site legal -- the
// notdef-box lifetime bug this project shipped once was exactly a view outliving its
// temporary, so the reason is written down rather than assumed.
std::string demoPeekXhtml();

// design/Library.dc.html's own seven rows, with the authors and percentages the
// board draws. The device fills the same fields from the card -- filenames, blank
// authors, NEW -- so this is what keeps `make compare` and the goldens testing
// the RENDERING while the data they show is still Phase 3's.
std::vector<LibraryItem> demoLibraryItems();

// design/WifiPickerScrolled.dc.html's fiction: EIGHTEEN networks, which is what
// a scan in a block of flats returns and what makes the rail's proportions
// mean something. Its own function rather than a flag on setWifiDemo, because
// the two boards are two specimens and a screen that could not tell them apart
// would not be comparing either.
std::vector<ScanResult> demoWifiScanLong();
// design/Articles.dc.html's own five rows. Exported for the reason
// demoWifiScanLong is: the simulator needs the board's content to re-prime a
// VARIANT of the same screen, and a second copy in sim/ would be a board
// specimen the comparison sheet could drift from.
std::vector<ArticleItem> demoArticles();

class DemoScreenFactory : public ScreenFactory, public LibraryWatcher {
 public:
  DemoScreenFactory() = default;
  // Over a card: the Library lists `root` through `fs`. Without one it lists the
  // board's sample content, which is what the simulator and the goldens want.
  DemoScreenFactory(FileSystem& fs, std::string root);
  // THE WATCHER LETS GO ON THE WAY OUT. The Library holds a pointer back here so
  // it can null library_ when it dies, which makes the factory dying first the
  // mirror of the dangle that pointer used to have -- and it is not hypothetical:
  // the shell declares gApp before gFactory, so at process exit the factory would
  // go first and the App's Library would notify freed memory. A device never
  // exits, which is precisely the reasoning that let the first hole stand.
  ~DemoScreenFactory() override { dropWatch(); }

  std::unique_ptr<Screen> create(ScreenId id) override;

  // The Library this factory built last, or null before it has built one -- and
  // null again the moment that Library is destroyed.
  //
  // An overlay acts on the focused row of the screen UNDER it, and it reads that
  // through this pointer rather than being handed a copy of the selection: the
  // actions overlay and the delete confirmation behind it have to agree about
  // which book they mean even across a rescan, and two copies of an index
  // cannot. The App owns the screen; this only observes it.
  //
  // The pointer is overwritten on every Library this factory builds, so it names
  // the one on the stack: an overlay is only ever created BY a live Library
  // (through Action::push from its own onEvent), which is by construction the
  // most recent one. It is not a general-purpose handle.
  //
  // WHAT BOUNDS ITS LIFETIME: the Library itself. Every Library this factory
  // builds is registered with it (LibraryWatcher), so the screen's own destructor
  // nulls this -- whether it was popped off a live App, taken down with an App the
  // shell replaced, or simply dropped. There is nothing for a caller to remember
  // and no ordering to get right.
  //
  // IT USED TO BE A RULE IN THIS COMMENT, and the rule had a hole. It said the
  // pointer was valid "exactly as long as the App that built it" and named the
  // three places the shell replaces its App, each of which called forgetLibrary().
  // A POP was none of those: Home > Library > Back destroys the Library and keeps
  // the App, so the pointer dangled from then on. Nothing could reach it -- the
  // overlays are only pushed by a live Library and the shell's open path only asks
  // for one with a Library on top -- so nothing failed, which is what let it stand.
  // The mechanism is the screen's destructor now, and this paragraph is history
  // rather than instructions.
  LibraryScreen* library() const { return library_; }

  // LibraryWatcher: the Library this factory named is being destroyed.
  //
  // GUARDED ON WHICH ONE, and not defensively: a second Library can be built
  // before the first is destroyed (two on a stack, or two held side by side), and
  // clearing unconditionally would null a pointer to a live screen. That failure
  // would be reachable, where the dangle this replaces was not.
  void libraryGone(const LibraryScreen* which) override {
    if (library_ == which) library_ = nullptr;
  }

  // How many rows a Library this factory builds should show, from
  // Theme::libraryVisibleRows. Held here because the factory is what constructs
  // the screen and the panel size is not something core/ can ask for; 0 means
  // "not told", and the Library then shows nothing rather than guessing.
  void setLibraryVisibleRows(int n) { libraryVisibleRows_ = n; }

  // Which demo books a card-less Library shows. Empty means demoLibraryItems() --
  // the board's own seven. Set it to render a state the default list cannot
  // produce, which today is the SCROLLED library: a rail's proportions come from
  // the list's length, so a seven-item list cannot show one.
  //
  // Ignored when the factory has a real filesystem; a card's contents are the
  // card's.
  void setLibraryItems(std::vector<LibraryItem> items) { libraryItems_ = std::move(items); }

  // What a Settings screen this factory builds starts from, and where its changes
  // go. Held here for the same reason the Library's row count is: the factory is
  // what constructs the screen, and neither the current settings nor a place to
  // write them is something `core/` can go and find.
  //
  // A null sink is the simulator and the golden tests -- see SettingsSink. The
  // metrics are Theme::settingsMetrics's three numbers; zero means "not told", and
  // the screen then shows nothing rather than guessing, exactly as the Library
  // does.
  void setSettings(const Settings& s) { settings_ = s; }
  void setSettingsSink(SettingsSink* sink) { settingsSink_ = sink; }
  void setSettingsMetrics(int listH, int rowH, int headerH) {
    settingsListH_ = listH;
    settingsRowH_ = rowH;
    settingsHeaderH_ = headerH;
  }

  // What a Reader this factory builds shows, and what it draws body text with.
  // Held here for the reason the Library's row count and Settings' sink are: the
  // factory constructs the screen, and neither the chapter nor a rasterised face
  // is something `core/` can go and find.
  //
  // With no body face set the factory REFUSES to build a Reader, rather than
  // building one that renders nothing: a screen with no text is
  // indistinguishable from a book that failed to open.
  void setReaderBody(const GlyphSource* body) { readerBody_ = body; }
  // The italic face for emphasis, or null for "draw it roman". Set alongside the
  // body, because it belongs to `readerMetrics_` too and the WRAP reads it.
  void setReaderItalic(const GlyphSource* italic) { readerItalic_ = italic; }

  // The book's italic class names, read from its stylesheets when it was opened.
  // Beside the face rather than with the book, because both answer the same
  // question -- how is emphasis rendered -- and a Reader needs both or neither.
  void setReaderItalicClasses(std::vector<std::string> classes) {
    readerItalicClasses_ = std::move(classes);
  }
  void setReaderMetrics(const PageMetrics& m) { readerMetrics_ = m; }
  // The column the Reader is laid out in. Read by a caller that has to record WHICH
  // geometry a saved line was measured at -- see ReadingPosition. One source of
  // truth: the screen was built from this same value.
  const PageMetrics& readerMetrics() const { return readerMetrics_; }
  // WHERE the chapter is, not the chapter itself -- a path and three numbers, which
  // is what openBook hands back and all a ChapterReader needs. An empty bookPath
  // means the demo content: design/Reader.dc.html's own two paragraphs, streamed
  // from memory, which is what the simulator and the goldens render, on the same
  // reasoning as demoSleepVm().
  // THE DEMO CHAPTER, EXPLICITLY. design/Reader.dc.html's own two paragraphs,
  // streamed from memory -- what the simulator and the goldens render, on the same
  // reasoning as demoSleepVm().
  //
  // It has to be ASKED FOR, and that is the whole point of it being its own setter.
  // It used to be the fallback for "no book set", which meant a session restore --
  // where nothing has called setReaderBook, because the shell only calls it from a
  // button press -- silently built a Reader full of Middlemarch. The device woke
  // from sleep showing fiction from a book the user was not reading. A factory that
  // substitutes content is worse than one that refuses.
  void setReaderDemo() { readerDemo_ = true; }
  // WHICH STYLED SPECIMEN, and asked for rather than inferred -- the rule setReaderDemo
  // established after the factory substituted the demo for a real book and hid a
  // failure to load its contents.
  enum class ReaderStyleDemo { None, ChapterOpen, List };
  void setReaderStyleDemo(ReaderStyleDemo which) { readerStyleDemo_ = which; }

  // Build the IDLE sleep screen -- asleep with no book open, design/SleepIdle.dc.html
  // -- rather than the board's reading one. Asked for, like setReaderDemo, because a
  // screen nothing navigates to has no state to infer it from: the simulator names
  // which of the two it wants, and the shell builds its own view model either way.
  void setSleepIdle() { sleepIdle_ = true; }

  // design/SleepWaking.dc.html -- the same badge, waking rather than asleep. It is a
  // NOTE and nothing else: SleepViewModel::note already carries that line's words, so
  // the waking state needed no field, no flag on the theme and no second render path.
  // The two boards differ by one run for the same reason.
  void setSleepWaking() { sleepWaking_ = true; }

  // THE BOARD'S OWN CONTENTS AND MENU HEADER, ASKED FOR. Same rule as setReaderDemo,
  // and it is here because the alternative had just shipped its consequence: the
  // factory fell back to a demo table of contents whenever nothing had set one, so a
  // failure to read the real one showed as MIDDLEMARCH'S CHAPTERS over Le Fleau. A
  // silent substitution turned a diagnosable failure into a puzzle.
  //
  // "A factory that substitutes content is worse than one that refuses" was already
  // written down for exactly this, one screen earlier.
  void setContentsDemo() { contentsDemo_ = true; }

  // --- The V1.1 connect flow --------------------------------------------
  //
  // EVERY ONE OF THESE IS ASKED FOR, and an unprimed screen is REFUSED rather
  // than substituted. That rule is setReaderDemo's and setContentsDemo's, and
  // it exists because this project has shipped the substitution twice -- once
  // waking a device into Middlemarch, once showing a book's contents from a
  // different book -- and each time it hid the real cause.
  //
  // The flow's own priming is the shell's: it hands over the saved list, the
  // scan, the SSID being joined. The `*Demo` flags are the simulator's and the
  // goldens', which have no radio and no NVS.
  void setWifiNetworks(SavedNetworks nets) {
    wifiNets_ = std::move(nets);
    wifiPrimed_ = true;
  }
  void setWifiSink(WifiSink* sink) { wifiSink_ = sink; }
  void setWifiScan(std::vector<ScanResult> rows) {
    wifiScan_ = std::move(rows);
    wifiScanPrimed_ = true;
  }
  void setWifiPickerVisibleRows(int n) { wifiPickerRows_ = n; }
  // The network a join is about, AND what the keyboard's field starts with:
  // the keyboard's band, the dialog's sentence and the error's. One setter,
  // because they are one fact about one join attempt.
  //
  // IT WAS TWO SETTERS AND THE TEXT WAS NEVER CLEARED, which is how a
  // passphrase typed for one network reached another network's keyboard in
  // clear. `setWifiEntered` had no counterpart: fail on HOME, press EDIT
  // PASSWORD, cancel, pick CAFE-BIBLIO off the scan, and its keyboard came up
  // holding HOME's passphrase -- `clearDeleteFacts`' defect verbatim, where
  // the delete confirmation named the previous book.
  //
  // AND THE HEADER ALREADY SAID SO. The old comment here read "a fresh join
  // primes the SSID and NOT the text -- and a keyboard that came up holding
  // the last attempt's passphrase would be worse than one that came up
  // empty", which is the rule stated beside the defect it forbids: this
  // project's most expensive recurring shape.
  //
  // A `clearWifiEntered()` beside the other clears would have been an ORDERING
  // maintained in prose -- clear, then set, and only for the EDIT PASSWORD
  // path -- which CLAUDE.md calls a function not yet written. One call with a
  // defaulted second argument cannot be half-taken: priming a target IS
  // deciding what is in the field, and the common answer is nothing.
  void setWifiTarget(std::string ssid, std::string entered = {}) {
    wifiTarget_ = std::move(ssid);
    wifiEntered_ = std::move(entered);
    wifiTargetPrimed_ = true;
  }
  void setWifiFailure(JoinFailure why) { wifiFailure_ = why; }
  // --- Articles over wallabag (V1.1) -----------------------------------
  //
  // EVERY ONE OF THESE CARRIES A PRIMED FLAG RATHER THAN "the data is not
  // empty", which is contentsPrimed_'s rule: a configured card with no articles
  // yet primes an EMPTY list and must still build, because a reader who has just
  // filled the credentials in must not be told to go and fill them in.
  void setArticles(std::vector<ArticleItem> rows, std::string stamp) {
    articles_ = std::move(rows);
    articlesStamp_ = std::move(stamp);
    articlesNotSetUp_ = false;
    articlesPrimed_ = true;
  }
  // The not-set-up variant: no /.reader/wallabag.json, or one with a value
  // missing. Its own setter rather than a flag on the one above, so "no
  // credentials" and "no articles" cannot be spelled the same way by accident.
  void setArticlesNotSetUp() {
    articles_.clear();
    articlesStamp_.clear();
    articlesNotSetUp_ = true;
    articlesPrimed_ = true;
  }
  void setArticlesVisibleRows(int n) { articlesRows_ = n; }
  // THE CARD, and it makes `Articles` and `WallabagAccount` buildable from boot
  // -- which is what their `Restore::Ready` declaration rests on. The factory
  // holds a FileSystem* for them exactly as it holds one for the Library, and
  // for the same reason: what the screen shows is a directory, and a directory
  // survives a chip reset.
  //
  // IT WINS OVER THE FIXTURE SETTERS, because a device has a card and the
  // goldens do not. setArticlesDemo() is the simulator's door and clears this,
  // so a demo cannot be quietly overlaid on a real card.
  void setArticleStore(FileSystem* fs) {
    articleFs_ = fs;
    if (fs != nullptr) articlesPrimed_ = true;
  }
  void setArticlesStatusLine(std::string line) { articlesStatus_ = std::move(line); }
  void setArticleActionsFacts(ArticleActionsScreen::Facts f) {
    articleActionFacts_ = std::move(f);
    articleActionFactsSet_ = true;
  }
  // CLEARED ON THE WAY OUT, which BookDetails' own facts pair is the precedent
  // for and the reason: without it, holding Confirm on one article and then
  // reaching this overlay another way would act on the article before last.
  void clearArticleActionsFacts() { articleActionFactsSet_ = false; }
  // WHICH END SCREEN THE READER IT BUILDS WILL PUSH. `BookEnd` unless the shell
  // says otherwise, and the shell says otherwise for exactly one thing: a file
  // under `/.reader/articles/`. Held here rather than passed at construction
  // because the factory is what constructs the screen -- `libraryVisibleRows`'
  // own reason -- and the shell sets it on EVERY open rather than only when it
  // changes, so a book opened after an article cannot inherit its board.
  void setReaderEndScreen(ScreenId id) { readerEndScreen_ = id; }

  void setArticleEndFacts(ArticleEndScreen::Facts f) {
    articleEndFacts_ = std::move(f);
    articleEndFactsSet_ = true;
  }
  void setWallabagAccountFacts(WallabagAccountScreen::Facts f) {
    wallabagAccountFacts_ = std::move(f);
    wallabagAccountSet_ = true;
  }
  void setWallabagHost(std::string host) {
    wallabagHost_ = std::move(host);
    wallabagHostSet_ = true;
  }
  void setWallabagFailure(WallabagErrorScreen::Shape shape) {
    wallabagFailure_ = shape;
    wallabagFailureSet_ = true;
  }
  // The simulator's and the goldens' door, asked for rather than fallen back to
  // -- setReaderDemo()'s rule, and for its reason: a factory that SUBSTITUTES
  // content is worse than one that refuses, which is how this device once woke
  // into a book nobody was reading.
  void setArticlesDemo();

  void setWifiNetworkFacts(WifiNetworkActionsScreen::Facts f) {
    wifiActionFacts_ = std::move(f);
    wifiActionFactsSet_ = true;
  }
  void clearWifiNetworkFacts() { wifiActionFactsSet_ = false; }
  // The demo content the simulator and the goldens use, asked for exactly as
  // setContentsDemo is.
  void setWifiDemo();


  // THE BOARD'S OWN PEEK, ASKED FOR. Same rule as setReaderDemo and setContentsDemo:
  // the factory refuses a Peek nothing primed rather than substituting, because this
  // project has shipped that substitution twice and each time it hid the real cause.
  void setPeekDemo() { peekDemo_ = true; }

  // The peeked chapter of the book the reader has open: which spine entry, and nothing
  // else. It took a book-wide PERCENTAGE too, computed by the shell, and the peek held
  // that figure for its whole life -- so the band's number stayed on the chapter the
  // panel was opened at while its label followed the reader across a boundary. The
  // panel derives it from the chapter it is showing now; see PeekScreen::percentHere.
  //
  // `peekPrimed_` IS ITS OWN FLAG rather than "spine >= 0": spine 0 is a real target --
  // it is the book's cover, which an NCX section header can legitimately name -- so a
  // sentinel would refuse a valid peek. Only "nothing was primed at all" is refused.
  //
  // THERE IS NO clearPeek(). One was written and had no caller anywhere, tests
  // included: this runs on every press that opens a panel, so nothing can go stale,
  // and an unused setter is a second way to reach a state only one path should own.
  void setPeek(int spine) {
    peekSpine_ = spine;
    peekPrimed_ = true;
  }

  // The panel's column, from Theme::peekMetrics. Separate from setReaderMetrics because
  // they are DIFFERENT COLUMNS -- that is the whole design -- and one setter for both
  // would be an invitation to hand the peek the reading measure, which is the bug the
  // "peek's page is not the reader's page" test exists to catch.
  void setPeekMetrics(const PageMetrics& m) { peekMetrics_ = m; }

  // THE AUTHOR FOR BOOK DETAILS, read by the shell from the one book that screen shows.
  //
  // It cannot come from the Library's scan: the author lives in the OPF, so learning it
  // per row means opening every book on the card -- ~100 ms each, ~20 s for a 203-book
  // library, on a screen that has to paint. Book details shows ONE book, so it is one
  // archive open on the press that opens it, and there is heap for it because no Reader
  // is on the stack.
  //
  // Empty leaves the row blank, which is what it has always drawn.
  void setDetailsAuthor(std::string author) { detailsAuthor_ = std::move(author); }

  // EVERYTHING BOOK DETAILS DRAWS, for the caller that has no Library row to point at.
  // The reader menu's `About this book` is that caller: it opens from a Reader, which may
  // have been reached through Home's CONTINUE with no Library on the stack. Clearing it
  // puts the screen back on the Library's row, which is what the simulator uses.
  void setDetailsFacts(BookDetailsScreen::Facts f) {
    detailsFacts_ = std::move(f);
    detailsFactsSet_ = true;
  }
  void clearDetailsFacts() { detailsFactsSet_ = false; }

  // EVERYTHING BookError DRAWS, for the same reason setDetailsFacts exists: the dialog
  // is raised from the Library AND from Home's CONTINUE, and only one of those has a
  // Library row to ask. Its own flag rather than an inference from the Facts, for
  // bookEndPrimed_'s reason -- a Damaged reason and an empty display name are both
  // representable, so emptiness cannot stand for "nothing primed it".
  void setBookErrorFacts(BookErrorScreen::Facts f) {
    bookErrorFacts_ = std::move(f);
    bookErrorFactsSet_ = true;
  }
  void clearBookErrorFacts() { bookErrorFactsSet_ = false; }

  // WHICH FILE THE CONFIRMATION IS ABOUT, and where a completed delete lands, for the
  // caller that has no Library row to point at. BookError's `DELETE FILE...` is that
  // caller: the dialog is raised from the Library AND from Home's CONTINUE, and only
  // one of those has a Library. Clearing it puts the screen back on the Library's
  // focused row, which is what the simulator and the goldens use.
  //
  // Its own flag rather than an inference from the Facts, for setBookErrorFacts'
  // reason: an empty display name is representable, so emptiness cannot stand for
  // "nothing primed it".
  void setDeleteFacts(DeleteConfirmScreen::Facts f) {
    deleteFacts_ = std::move(f);
    deleteFactsSet_ = true;
  }
  void clearDeleteFacts() { deleteFactsSet_ = false; }

  // EVERYTHING BookEnd DRAWS, handed over rather than reached for. The reader menu's
  // `About this book` is the precedent: a screen built from another screen refuses to
  // open when that screen is not on the stack, which made it a button that worked only
  // sometimes.
  //
  // `bookEndPrimed_` IS ITS OWN FLAG rather than an inference from the Facts, for
  // contentsPrimed_'s reason: an EPUB that names no author and a spine count of zero
  // are both legitimate primed states -- the byline drops its middot and the meta line
  // is not drawn -- so emptiness cannot stand for "nothing was primed at all".
  void setBookEndFacts(BookEndScreen::Facts f) {
    bookEndFacts_ = std::move(f);
    bookEndPrimed_ = true;
  }

  // design/BookEnd.dc.html's own content, ASKED FOR. Same rule as setReaderDemo,
  // setContentsDemo and setPeekDemo: the factory refuses a BookEnd nothing primed
  // rather than substituting, because this project has shipped that substitution twice
  // and each time it hid the real cause -- once as a device waking into a book the user
  // was not reading, once as one book showing another's chapters.
  void setBookEndDemo() {
    bookEndFacts_ = demoBookEndFacts();
    bookEndPrimed_ = true;
  }

  // THE BOOK'S TABLE OF CONTENTS, for the Contents screen. Set by the shell when the
  // menu's Contents row is chosen -- reading it is card work (`toc.h` re-opens the
  // archive) and `core/` does no storage, so the factory is handed the answer rather
  // than the question. Empty means the book has none, which Contents renders as an
  // empty list rather than refusing: a book with no NCX still reads.
  // How many Contents rows fit, from Theme::contentsVisibleRows. Held here for the
  // reason libraryVisibleRows is: the factory constructs the screen and a panel height
  // is not something `core/` can ask for. 0 means "not told", and the list renders
  // empty rather than guessing.
  // --- Names and Mentions (3E) ---------------------------------------------
  //
  // GROUPS, NOT THE CARD. Grouping is a batch pass over the whole index and it
  // happens when the screen opens; who reads the store and runs it is the shell's
  // business. The factory takes the answer, which keeps both screens testable
  // without a filesystem.
  void setNames(std::vector<NameGroup> groups) {
    namesGroups_ = std::move(groups);
    namesPrimed_ = true;
  }
  void setNamesVisibleRows(int n) { namesRows_ = n; }
  void clearNames() {
    namesGroups_.clear();
    namesPrimed_ = false;
  }
  // `subject` is the band's right slot and is never empty -- with the label naming
  // the screen it is the only thing saying whose mentions these are.
  void setMentions(std::string subject, std::vector<StoredExtract> extracts,
                   std::vector<std::string> chapterNames) {
    mentionsSubject_ = std::move(subject);
    mentionsExtracts_ = std::move(extracts);
    mentionsChapterNames_ = std::move(chapterNames);
    mentionsPrimed_ = true;
  }
  void setMentionsVisibleRows(int n) { mentionsRows_ = n; }
  void clearMentions() {
    mentionsExtracts_.clear();
    mentionsSubject_.clear();
    mentionsPrimed_ = false;
  }

  void setContentsVisibleRows(int n) { contentsRows_ = n; }

  void setContents(std::vector<TocEntry> toc, int spine) {
    contentsToc_ = std::move(toc);
    contentsSpine_ = spine;
    // PRIMED IS ITS OWN FLAG, not "the list is non-empty". A real book with no NCX
    // primes an EMPTY list and must still build -- it reads fine and simply cannot name
    // its chapters. Inferring from emptiness collapses that into "nothing was primed",
    // which is a shell bug and is refused.
    contentsPrimed_ = true;
  }

  // What the reader menu's header says. Two strings rather than a reach down the stack
  // into the Reader: an overlay that read its parent would be a second place that
  // knows how a Reader is shaped.
  void setReaderMenuHeader(std::string bookTitle, std::string progress) {
    menuTitle_ = std::move(bookTitle);
    menuProgress_ = std::move(progress);
  }

  // THE BOOK'S WHOLE GEOMETRY, from one openBook: its path, its metadata and twelve
  // bytes an entry. The reader reaches another chapter by picking a row out of it,
  // where it used to re-parse the archive per chapter.
  // `startAt` is a RESTORED POSITION -- the cursor of the page the reader was on --
  // and Cursor{} means page one, which is both "no saved position" and "the top of
  // the chapter". The screen spends it on the first chapter it lands on.
  void setReaderBook(OpenedBook book, int startChapter, Cursor startAt = Cursor{}) {
    readerBook_ = std::move(book);
    readerStartChapter_ = startChapter;
    readerStartAt_ = startAt;
  }
  // The restored way back, or nothing. Separate from setReaderBook because it comes
  // from a DIFFERENT grade of the same record -- restoreFrom keeps the anchor only at
  // an Exact fit -- and folding it into the book call would invite a caller to pass
  // one the fit had already refused.
  void setReaderAnchor(const AnchorPos& a) {
    readerAnchor_ = a;
    readerHasAnchor_ = true;
  }
  void clearReaderAnchor() { readerHasAnchor_ = false; }

  // The open book's geometry, for a caller that needs to say something about the
  // book as a whole -- progressPercent sums its chapters' sizes. A reference rather
  // than a copy: this is 12 bytes a spine entry and the shell would otherwise keep a
  // third copy of it beside this one and openBook's.
  const OpenedBook& readerBook() const { return readerBook_; }

 private:
  // Break the link in the direction the SCREEN holds it: called when a newer
  // Library replaces the one library_ names, and from the destructor, so AT MOST
  // ONE live Library ever points back here -- which is what makes the destructor's
  // guarantee total rather than covering only the tracked one.
  //
  // Dereferencing library_ is safe for exactly the reason the pointer exists: it
  // is nulled when the screen dies. The two halves are each other's guarantee.
  void dropWatch() {
    if (library_ != nullptr) library_->stopWatching(*this);
  }

  FileSystem* fs_ = nullptr;
  std::string root_;
  LibraryScreen* library_ = nullptr;
  int libraryVisibleRows_ = 0;
  std::vector<LibraryItem> libraryItems_;
  Settings settings_{};
  SettingsSink* settingsSink_ = nullptr;
  int settingsListH_ = 0;
  int settingsRowH_ = 0;
  int settingsHeaderH_ = 0;
  const GlyphSource* readerBody_ = nullptr;
  const GlyphSource* readerItalic_ = nullptr;
  std::vector<std::string> readerItalicClasses_;
  ReaderStyleDemo readerStyleDemo_ = ReaderStyleDemo::None;
  AnchorPos readerAnchor_{};
  bool readerHasAnchor_ = false;
  PageMetrics readerMetrics_{};
  OpenedBook readerBook_{};
  bool readerDemo_ = false;
  bool peekDemo_ = false;
  bool peekPrimed_ = false;
  int peekSpine_ = 0;
  PageMetrics peekMetrics_{};
  bool sleepIdle_ = false;
  bool sleepWaking_ = false;
  bool contentsDemo_ = false;
  std::string detailsAuthor_;
  BookDetailsScreen::Facts detailsFacts_{};
  bool detailsFactsSet_ = false;
  BookEndScreen::Facts bookEndFacts_{};
  bool bookEndPrimed_ = false;
  std::vector<ArticleItem> articles_;
  std::string articlesStamp_;
  std::string articlesStatus_;
  bool articlesNotSetUp_ = false;
  bool articlesPrimed_ = false;
  FileSystem* articleFs_ = nullptr;
  int articlesRows_ = 0;
  ScreenId readerEndScreen_ = ScreenId::BookEnd;
  ArticleActionsScreen::Facts articleActionFacts_;
  bool articleActionFactsSet_ = false;
  ArticleEndScreen::Facts articleEndFacts_;
  bool articleEndFactsSet_ = false;
  WallabagAccountScreen::Facts wallabagAccountFacts_;
  bool wallabagAccountSet_ = false;
  std::string wallabagHost_;
  bool wallabagHostSet_ = false;
  WallabagErrorScreen::Shape wallabagFailure_ = WallabagErrorScreen::Shape::SignIn;
  bool wallabagFailureSet_ = false;
  BookErrorScreen::Facts bookErrorFacts_{};
  bool bookErrorFactsSet_ = false;
  DeleteConfirmScreen::Facts deleteFacts_{};
  bool deleteFactsSet_ = false;
  std::vector<TocEntry> contentsToc_;
  int contentsSpine_ = 0;
  bool contentsPrimed_ = false;
  SavedNetworks wifiNets_;
  WifiSink* wifiSink_ = nullptr;
  bool wifiPrimed_ = false;
  std::vector<ScanResult> wifiScan_;
  bool wifiScanPrimed_ = false;
  int wifiPickerRows_ = 0;
  std::string wifiTarget_;
  bool wifiTargetPrimed_ = false;
  std::string wifiEntered_;
  JoinFailure wifiFailure_ = JoinFailure::BadPassword;
  WifiNetworkActionsScreen::Facts wifiActionFacts_;
  bool wifiActionFactsSet_ = false;
  int contentsRows_ = 0;
  std::vector<NameGroup> namesGroups_;
  bool namesPrimed_ = false;
  int namesRows_ = 0;
  std::string mentionsSubject_;
  std::vector<StoredExtract> mentionsExtracts_;
  std::vector<std::string> mentionsChapterNames_;
  bool mentionsPrimed_ = false;
  int mentionsRows_ = 0;
  std::string menuTitle_, menuProgress_;
  int readerStartChapter_ = 0;
  Cursor readerStartAt_{};
  std::string readerBookTitle_;
  std::string readerChapter_;
};

}  // namespace reader
