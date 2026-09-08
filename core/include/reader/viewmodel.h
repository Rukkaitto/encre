#pragma once
#include <array>
#include <string>
#include <vector>

#include "reader/layout.h"  // kBodyLeadEm
#include "reader/settings.h"  // SleepShows

namespace reader {

struct MenuEntry {
  std::string label;
  std::string value;
};

// Semantic content + interaction state only. No geometry, no style.
struct HomeViewModel {
  std::string title;
  std::string author;
  // THE CHAPTER'S NAME, and it is ReaderViewModel::chapter's own string rather than
  // a second derivation of the same fact -- the Reader's header band, Contents' `NOW`
  // row and this line all name the reader's chapter, and two screens naming it
  // differently would be two spellings of one thing.
  //
  // IT WAS `CH. 14 OF 36`, A SPINE POSITION OF A SPINE COUNT, AND THAT WAS FALSE. A
  // spine counts the cover, the title page, the copyright, the contents, the part
  // dividers, the notes and the colophon alongside the chapters, so the pair invited
  // an arithmetic the numbers do not support -- and a reader did it, then found
  // Contents disagreeing. 183 of 206 corpus books with a usable NCX (88.8%) have a
  // spine count that is not the count of chapters their TOC offers, so there is no
  // total to substitute; see design/Main.dc.html for the figures and the report.
  //
  // EMPTY IS A LEGAL STATE and the line is then blank: a pointer written before
  // last.json carried this key cannot say, and an absent claim beats a false one. It
  // is NOT empty for a book with no contents -- the Reader falls back to `CH. 08`
  // there, a position with no total, which is the only handle such a book offers.
  //
  // The theme ELIDES it: the words come off the card and a chapter name runs long,
  // where this line has one line of a 304px column.
  std::string chapterLabel;
  int percent = 0;
  // NO PAGE COUNTER HERE, and design/Main.dc.html states why: a page count for the
  // BOOK means paginating every chapter -- ~49 s of decode on this device for a real
  // novel. The Reader's own footer is a different question: that counter is within
  // ONE chapter, which is affordable, and it lives in ReaderViewModel.
  // -1 = THE GAUGE DID NOT ANSWER, and the band then draws its mark alone.
  //
  // Not 0, and the distinction is the whole point: BatteryMonitor answers a
  // FAILED read with 0 -- readPercentage() returns 0, and percentageFromMillivolts
  // maps a failed 0 mV to 0% rather than 100%, deliberately -- so a 0 taken at
  // face value puts a flat battery on the panel of a device that is fine. This is
  // the same call homeVmForCard() already makes for the LIBRARY row's count, where
  // -1 means "could not look" and draws nothing: "no books" and "could not look"
  // are different claims, and so are "flat" and "did not answer".
  //
  // The default is -1 rather than a number for the same reason: a view model
  // nobody has told about the battery must not claim one. The demo view-models
  // set the board's 87 explicitly, exactly as they set the board's LIBRARY 12.
  int batteryPercent = -1;
  // Drawn as a bolt knocked out of the battery's fill. X3 only in practice: the
  // X4 profile declares no gauge and no charge-status pin, so BatteryMonitor
  // answers isCharging() false there unconditionally.
  bool batteryCharging = false;
  // Cover art is not decoded yet (Phase 3 owns EPUB images), so the theme draws
  // a dithered placeholder carrying the title. This flag says whether a real
  // cover exists, so the placeholder can be replaced without a view-model change.
  bool hasCover = false;
  // NOTHING TO CONTINUE: the whole reading column -- cover, title, progress,
  // CONTINUE -- is replaced by a centred block of copy, and the focus moves off
  // the CONTINUE block onto the first menu row.
  //
  // THERE ARE TWO REASONS FOR IT, which is why this is not called `libraryEmpty`
  // any more. It was, and the name named only the first:
  //
  //   * `/books` holds no readable book        -- design/HomeEmpty.dc.html
  //   * books are there but none has been open -- design/HomeUnopened.dc.html
  //
  // One mechanism, two copies. The states differ in what they SAY (and in the
  // LIBRARY row's value, `EMPTY` against a count), never in what they draw, so a
  // second flag or a second render branch would be two ways to spell one layout.
  //
  // A flag rather than inferring it from an empty `title`, because those are
  // different facts: a book whose metadata gave no title is still a book to
  // continue, and it would be a mistake to draw "NO BOOKS YET" over one.
  bool nothingToContinue = false;
  // What that block says. Copy lives in the view model for the same reason
  // SdMissing's does -- it is the board's words, and a theme that held them would
  // be a theme deciding what the device tells the user.
  std::string emptyTitle;
  std::string emptyBody;
  std::vector<MenuEntry> menu;
  int focusedMenuIndex = -1;                 // -1 = Continue block focused
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
  // Which of those four buttons also has a long-press action. The theme draws a
  // hollow ring on the slot (design 662557d) and the screen builds its
  // long-press mask from the same array, so the affordance and the behaviour
  // cannot drift apart -- a ring always means a hold is bound, and a bound hold
  // always shows a ring.
  std::array<bool, 4> holds{};
};

// The no-card prompt (spec 6): design/SdMissing.dc.html. Content only -- the
// board's own copy, which the screen supplies and the theme lays out.
//
// There is no `retrying` or `failed` flag here, and that is deliberate: a retry
// takes a mount attempt and a repaint, and the screen cannot know the outcome
// because it is not the thing that mounts (see Action::Kind::Retry). Either the
// card is there, in which case the shell replaces this screen, or it is not, in
// which case the honest UI is the same prompt again. A "checking..." state that
// no code could ever clear would be a lie drawn on glass.
struct SdMissingViewModel {
  std::string title;    // "NO SD CARD"
  std::string message;  // the paragraph under it, wrapped by the theme
  std::string action;   // the button's label
  std::array<std::string, 4> hints{};  // Back, Confirm, Up, Down
  std::array<bool, 4> holds{};
};

// design/BatteryEmpty.dc.html. SdMissing's shape without the action slab, plus
// Sleep's badge -- and the hints are all empty, deliberately: the shell paints this
// and calls deep sleep, so there is nobody left to press anything and a bar is a
// contract about four buttons that do nothing.
struct BatteryEmptyViewModel {
  std::string title;    // "BATTERY EMPTY"
  std::string message;  // the paragraph under it, wrapped by the theme
  std::string note;     // the badge's label: "CHARGE · HOLD POWER TO WAKE"
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// The end of a book (design/BookEnd.dc.html). Semantic content only: every string
// here is composed by the screen, because a byline and a chapter count are CONTENT
// and the theme has no business knowing that a book has an author.
struct BookEndViewModel {
  // NO BAND VALUE. This carried the book's shouted name until a long title squeezed
  // the band's own LABEL until it elided -- and the slot was redundant besides, since
  // `byline` states the same book forty pixels below. The board's slot is reserved
  // with an nbsp and the theme passes "" (drawHeaderBand's phantom gap cancels, so an
  // empty value lands the band's right edge on the margin exactly).
  std::string title;   // "THE END"
  // "Middlemarch · George Eliot"; the name alone if no author. IT WRAPS, so it must
  // outlive the render that reads it -- Prose holds views into it.
  std::string byline;
  std::string meta;         // "24 CHAPTERS"; EMPTY when the count is unknown
  std::string finishLabel;  // the filled slab
  // "BACK TO LIBRARY" or "BACK TO HOME". TWO SPELLINGS OF ONE BUTTON, and that is
  // deliberate: popTo(Library) stops at the root when no Library is on the stack, so
  // the button always works and only its NAME could be wrong. A slab that says
  // LIBRARY and lands on Home is the `About this book` shape -- right in the common
  // case, quietly wrong otherwise, and nobody can learn the rule.
  std::string leaveLabel;
  std::string note;  // the footnote above the hint bar
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// One Library row as the theme draws it (design/Library.dc.html). Nothing here
// addresses a file: the leaf name the card knows the thing by stays on the
// screen's side of the wall, because the theme has no business with it and a
// view-model that carried it would be the seam through which layout learned
// about storage.
//
// `meta` is the second line, and it is composed by the screen rather than by the
// theme because it is CONTENT: an author, or a folder's "FOLDER - 6 BOOKS"
// summary. It is empty on the device today -- an author needs the EPUB's OPF,
// which is Phase 3 -- and the row's height does not depend on it, so a blank
// line leaves the list on the same grid.
struct LibraryRow {
  std::string title;
  std::string meta;
  std::string value;  // "6%", "DONE", "NEW"; empty on a folder, which discloses
  bool isFolder = false;
};

// The Library (spec 4.1), from design/Library.dc.html.
//
// `rows` is EXACTLY what is on glass, never the whole directory: the scroll
// window is the screen's business, and handing the theme a hundred books plus a
// first-visible index would put the one rule that matters -- that the focus is
// inside the window -- in two places. `focusedRow` therefore indexes `rows`, and
// a screen with a focus scrolled out of view is not expressible.
struct LibraryViewModel {
  std::string title;  // the band's label: "LIBRARY", or a subfolder's own name
  // The band's value, as a number: the theme formats it, because "12 BOOKS"
  // against "1 BOOK" is a presentation decision and a pre-formatted string in
  // here would be a screen making one.
  int bookCount = 0;
  std::vector<LibraryRow> rows;
  int focusedRow = -1;
  // Where the visible rows sit in the whole list, for the scroll rail
  // (design/LibraryScrolled.dc.html). NOT derivable from `rows`, which holds
  // only what is on screen -- so the screen has to say, and these are the two
  // numbers the rail's proportions come from: thumb height is rows/total and
  // thumb top is firstRow/total.
  //
  // totalRows == rows.size() means the list does not overflow, and the rail is
  // not drawn: a full-height thumb says nothing.
  int firstRow = 0;
  int totalRows = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// The item actions overlay (design/LibraryActions.dc.html): a panel over the
// veiled Library, captioned with the book it acts on.
struct ItemActionEntry {
  std::string label;
  // Whether the row leads somewhere, which is the board's rule for the trailing
  // chevron: Open and Book details have one, Mark as finished and Delete... do
  // not. A flag rather than the theme keying on the row's index, which would
  // silently mark the wrong row the first time the list is reordered.
  bool discloses = false;
};

struct ItemActionsViewModel {
  std::string title;   // the book's name; the theme shouts it, as a caps label
  // The caption's right-hand value: "31%", "DONE" or "NEW". It is `item->progress`
  // verbatim, so it inherits whatever applyProgress derives -- which is how DONE
  // reached this overlay with no code of its own, and is the point of that
  // derivation being spelled once.
  std::string status;
  std::vector<ItemActionEntry> actions;
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// The delete confirmation (design/DeleteConfirm.dc.html). Two action slabs, and
// the focused one is the FILLED one -- which is why there is no `destructive`
// flag here: the boards fill whichever slab the focus is on and outline the
// rest, and the focus starts on CANCEL, which is where a destructive prompt's
// focus belongs.
struct DeleteConfirmViewModel {
  std::string title;    // the caption, with the book's name in it; the theme wraps it
  std::string message;  // the paragraph under it
  std::string cancelLabel;
  std::string confirmLabel;
  // 0 = cancel, 1 = delete, in the board's own top-to-bottom order. An index
  // rather than a bool because the focus moves through a list, and because
  // BookError's board pairs the same two slabs with a third.
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// The corrupt-book dialog (design/BookError.dc.html, and
// design/BookErrorUnreadable.dc.html for the refusal that is not damage).
//
// Semantic content only. Which of the two sentences is in `message` is decided by
// the screen from a bounded BookErrorReason -- never by the theme, which is layout,
// and never from openBook's `why` string, which is developer English
// ("the spine names no chapters"), unstyled and unbounded, and which no board has a
// slot for. The reason still goes to the serial log, where it is actionable.
struct BookErrorViewModel {
  std::string title;    // the caption: the board's fixed `CAN'T OPEN FILE`
  std::string message;  // the paragraph, with the file's name in it
  std::string okLabel;
  std::string deleteLabel;
  // WHETHER THE SECOND SLAB IS DRAWN AT ALL. False on the OutOfMemory shape alone
  // (design/BookErrorMemory.dc.html): that file is fine and the device was
  // momentarily short, so offering to delete a good book is a nudge in the wrong
  // direction. HomeEmpty's cut action slab is the precedent -- `a primary action
  // that cannot work is worse than none` -- and the slab is ABSENT rather than
  // inert, because a slab that draws and does nothing is the `works only sometimes`
  // trap.
  //
  // AN EXPLICIT FLAG, NOT `deleteLabel.empty()`. `ListRow::discloses` is the
  // recorded precedent for exactly this: deriving it from an empty value drew a
  // chevron on a row that acted in place, and a slab is a bigger claim than a
  // chevron. The label IS cleared with it -- there is nothing left to draw -- but
  // the flag is the authority and the emptiness is the consequence.
  bool offersDelete = true;
  // 0 = OK, 1 = delete, in the board's own top-to-bottom order. Spelled exactly as
  // DeleteConfirmViewModel::focusedAction because it is the same fact, and one rule
  // should have one spelling.
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// Book details (design/BookDetails.dc.html) -- a full screen, NOT an overlay.
//
// Its board has no veil and no panel: it has its own header band and its own hint
// bar. The `.dim-veil` rule in its stylesheet is declared and never used, which
// is template residue rather than an intention (`grep -c dim-veil` gives 2 for
// each real overlay and 1 for this one), and an earlier draft of the 2C-2 plan
// read it as an overlay on that evidence.
//
// Most of these fields need EPUB metadata or per-book state and are BLANK on the
// device until Phase 3. They are carried anyway: the board draws them, the golden
// pins them, and the field is where Phase 3 plugs in. Dropping them until
// something populated them would mean re-deriving this layout then.
struct BookDetailsViewModel {
  std::string title;     // "Dubliners"              -- from the filename today
  std::string author;    // "James Joyce"            -- Phase 3
  // NO SUBTITLE. The board drew one and no book carries the data: across four real
  // EPUBs, not one has a `title-type=subtitle` refinement or any subtitle marker. A
  // field that can never be filled reads as a device that failed to load something.
  std::string format;    // the band's value: "EPUB" / "TXT", from the extension
  // The board's six label/value rows, in its order: Progress, Current story,
  // Bookmarks, File size, Added, Location. A vector rather than six fields
  // because the theme draws them as a list and Phase 3 adds to it.
  std::vector<MenuEntry> fields;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// A provisional titled-list surface: Phase 2B's Library and Settings
// placeholders and its Input Monitor. It exists so the interaction runtime can
// be navigated and verified before the real screens are built, and Phase 2C
// deletes it. Deliberately plain, and it carries `note` so nobody reads it as a
// design.
// design/Settings.dc.html. One flat list of ITEMS, because that is what scrolls:
// a section header and a setting row move together and the rail counts both.
// design/Sleep.dc.html: what is on the glass while the device is asleep. No hints
// and no focus -- the shell paints this and then sleeps, so there is nobody to
// press anything. The only way out is the power button, which the badge says.
struct SleepViewModel {
  // NOTHING TO CONTINUE: no reading card, only the badge. design/SleepIdle.dc.html.
  //
  // The same name HomeViewModel uses for the same fact, deliberately -- one rule
  // should have one spelling, and this project has twice had to write a rule down in
  // several comments to keep it single. A flag rather than inferring it from an empty
  // title, for HomeViewModel's reason: a book whose metadata gave no title is still a
  // book being read.
  bool nothingToContinue = false;
  std::string label;      // "NOW READING"
  std::string title;      // the book, shouted by the theme
  std::string author;
  int progressPercent = 0;
  std::string progress;   // "6% - CH. 01", the line under the bar
  std::string note;       // "ASLEEP - HOLD POWER TO WAKE"

  // WHICH OF THE THREE SLEEP BOARDS THIS IS -- design/Sleep.dc.html,
  // SleepCoverDetails.dc.html, SleepCover.dc.html.
  //
  // THE DEFAULT IS Details AND Settings' DEFAULT IS CoverAndDetails, and the two
  // disagreeing is deliberate rather than an oversight. This struct's default is
  // "what a view model built without being told does", and that has to be the
  // shipped screen to the pixel: every existing sleep golden constructs one of
  // these and sets no `shows`, so any other default here would move them and the
  // property this whole feature rests on -- with no cached cover every mode
  // paints byte-identically to today -- would stop being checkable.
  //
  // It is only ever a REQUEST. The screen draws a cover if it also has a
  // CoverSource that answers; see screen_sleep.h.
  SleepShows shows = SleepShows::Details;

  // WAKING RATHER THAN ASLEEP -- design/SleepWaking.dc.html -- AND IT EXISTS TO
  // KEEP THE BADGE WHERE COVER MODE DROPS IT.
  //
  // theme_quiet.cpp's `coverOnly` takes the card and the badge away together,
  // and the badge half is allowed to go ONLY because a full-bleed book cover is
  // not a screen this device can otherwise be in: the picture says "asleep" by
  // itself, so no words are needed to say it.
  //
  // A SCREEN SAYING "WAKING" IS MAKING A DIFFERENT CLAIM AND CANNOT DELEGATE IT
  // TO THE PICTURE. The cover is identical in both states -- it is the note text
  // that differs -- so with the badge suppressed a COVER-mode wake would paint
  // something indistinguishable from the sleep it is waking from, which is worse
  // than the stale screen it replaced. So this suppresses the SUPPRESSION, for
  // the badge only: the card stays hidden by `coverOnly` alone, because a waking
  // COVER screen is the cover and the words, not the cover and the reading card.
  //
  // A fact about WHICH SCREEN THIS IS, not about what it holds, which is why it
  // is a flag here rather than something derived from `note` -- the note is free
  // copy and a theme must not read words to decide a layout.
  //
  // FALSE IS THE SHIPPED SCREEN. Nothing that renders a sleep view model without
  // setting this can move, which is what keeps every existing sleep golden --
  // including sleep_waking, which has no cover source and so never reaches
  // `coverOnly` at all -- byte-identical.
  bool waking = false;
};

// design/Reader.dc.html's CHROME -- the header band and the footer. The page's
// text is NOT here: a laid-out page is geometry (reader/layout.h's LaidLine
// carries an x and a baseline), and this file's rule is semantic content plus
// interaction state, no geometry and no styling. So renderReader takes the Page
// as its own argument beside this, and the split says which half is which.
struct ReaderViewModel {
  std::string bookTitle;  // "Middlemarch" -- the board shouts it, the theme does that
  std::string chapter;    // "CH. 01", already composed: the theme does not do arithmetic
  int progressPercent = 0;
  // The footer's "53 / 890". CHAPTER-RELATIVE, not book-wide -- a book-wide page
  // number needs an index of every chapter, which is a pass over the whole EPUB.
  // Carried as two plain numbers so that pass can fill them in later without this
  // struct or the theme changing.
  int page = 0;

  // ZERO MEANS NOT KNOWN YET, and the theme draws an em dash for it -- see
  // design/Reader.dc.html's footer. Knowing the total means paginating the whole
  // chapter, one decode of it, and paying that before the first page appears made
  // crossing into a chapter cost twice what a page turn costs.
  int pageTotal = 0;

  // --- The way back, and EMPTY MEANS THERE IS NONE -------------------------------
  //
  // design/ReaderAnchored.dc.html: a third footer field, between the percent and the
  // counter, present only while the reader has somewhere to return to. Empty is the
  // common case and draws nothing, which is the whole affordance -- the field IS the
  // promise, and `AltPrev` does nothing without it. Wiring both to one string rather
  // than to two conditions that have to agree is deliberate.
  //
  // A STRING RATHER THAN A NUMBER, because the honest label is not always a page.
  // The board says `P. 300` and that is right for the common case, where the anchor
  // is in the chapter being read: its page is a lookup in `starts_`, free. Across
  // chapters it is NOT free -- naming the page would mean paginating the anchor's
  // chapter, ~7.2 ms/KB on the device and the cost this reader is built to avoid --
  // so a cross-chapter anchor says its CHAPTER instead. Same move as the footer's em
  // dash for an unknown total and the bare `CH. 03` before the contents existed: say
  // the true thing rather than the impressive one.
  //
  // It costs no vertical space, which matters more here than anywhere else on the
  // device: a footer that changed height would reflow the text column and
  // re-paginate the chapter mid-read.
  std::string anchorLabel;

  // --- The low-battery banner, and -1 MEANS THERE IS NONE -------------------------
  //
  // design/LowBattery.dc.html: an inverted 78px band over the bottom of the page.
  // One field with a sentinel rather than a bool and an int, for anchorLabel's
  // reason -- the field IS the condition, so it cannot be spelled twice and the two
  // spellings cannot drift.
  //
  // IT IS DRAWN OVER THE PAGE AND NEVER DISPLACES IT. The band inside the column
  // would take a default page from 12 lines to 10 and re-paginate the whole
  // chapter, at the moment the device has least energy to spend and with the
  // reader's page moving under them -- which is the identical reasoning anchorLabel
  // carries for the footer's third field, turned ninety degrees.
  int batteryLowPercent = -1;
};

// design/Peek.dc.html -- book text over the veiled page, for looking somewhere else
// without going there.
//
// TWO RUNS AND NO PAGE NUMBER, and the absence is the design. The panel is inset, so
// its column is narrower, so its text re-wraps -- and re-wrapped text paginates
// differently, which means "page 53" inside the peek is not page 53 of the book. It
// says chapter and percent instead, which are true at any column width. Committing is
// still exact: openAtCursor lands on the page CONTAINING a cursor and counts
// boundaries to name it, so the cursor is what travels and the number is computed on
// arrival.
struct PeekViewModel {
  // `PEEK`. Names the STATE, because `CH. 01 · 4%` alone would read as the Reader's
  // own header and this panel has to be unmistakably not that.
  std::string title = "PEEK";
  // `CH. 01 · 4%`, already composed -- the theme does no arithmetic. The chapter is
  // the book's own name for it where its contents supply one and the `CH. NN`
  // position where they do not, exactly as the Reader's header falls back.
  std::string where;
  // NO LEAD HERE, AND THE ABSENCE IS THE DESIGN CHANGE. This model carried a
  // `leadEm1000` so renderPeek could recompute the panel's box from the same line
  // height peekMetrics did -- necessary while the HEIGHT was a result of the line
  // count, because a render that assumed the default lead drew the border a line away
  // from its own text. The box is fixed now (theme.h's kPeekPanelH), so neither
  // function has a lead to disagree about and the field had no other reader. What
  // varies with the reader's typography is how many lines FIT, which is a question for
  // Theme::peekVisibleLines and never for a view model.
  //
  // CLOSE / GO HERE / — / —, in the boards' hardware order (Back, Confirm, Up, Down).
  // The last two are EMPTY, not absent: an empty slot is 36px wide (kHintEmptySlotW),
  // and measuring it as zero is not "drawing nothing", it is drawing the other two in
  // the wrong places.
  //
  // UP AND DOWN ARE DEAD ON PURPOSE. `Up` already means "return to where I was" on the
  // screen underneath (design/ReaderAnchored.dc.html), and one button with two meanings
  // across a single press is worse than an unbound one -- so the side buttons page in
  // the peek exactly as they do while reading. The four-label bar also did not fit:
  // measured at 480 wide it left ~4px of slack against faces that measure ~3% wider
  // than Chrome.
  std::array<std::string, 4> hints{{"CLOSE", "GO HERE", "", ""}};
  std::array<bool, 4> holds{{false, false, false, false}};
};

// A ROW IN A LIST THAT INTERLEAVES SECTION HEADERS WITH ITEMS, and where some items
// do not respond.
//
// Settings defined this shape; the reader menu and the table of contents both wanted
// it, which makes them the second and third copies -- so it is extracted rather than
// retyped, on this project's own rule. `SettingsRow` remains as an alias, because
// Settings' code reads better naming its own rows and nothing about the shape is
// Settings-specific.
//
// The three screens use it differently and that is the point of it being one type:
// Settings has headers and inert rows, the reader menu has inert rows and no headers,
// and Contents has headers (an NCX's depth-1 entries) with every row live.
struct ListRow {
  std::string label;
  std::string value;      // empty on a section header, or where the row discloses
  bool isHeader = false;  // tracked caps, its own rule, never focusable
  // WHETHER THIS ROW LEADS SOMEWHERE, drawn as a chevron. It cannot be derived from an
  // empty `value`: the reader menu's `Close book` has neither a value NOR a chevron,
  // because it acts in place rather than disclosing a screen -- and deriving it drew a
  // chevron promising a screen that does not exist. ItemActionEntry carries the same
  // flag explicitly, for the same reason.
  bool discloses = false;
  // The board's own tracking where it gives a row one. `Close book` is `0.06em` and its
  // five siblings are untracked, which is 1.5px a gap at Value500 -- about 15px across
  // that label, so it is visible rather than pedantic.
  int trackingEm1000 = 0;
  // Whether this row responds to a press. An unfocusable row is drawn EXACTLY as an
  // unfocused focusable one -- the flag is about input, not about appearance, and the
  // theme must not be tempted to dim it.
  bool focusable = false;
};
using SettingsRow = ListRow;

// design/ReaderMenu.dc.html: the overlay the Reader's Activate opens.
//
// An OVERLAY, so the page stays visible under a veil -- the reader has not left the
// book, they have asked it a question. ONE of its four rows is not built, and it is
// DRAWN and skipped by the focus, which is Settings' rule: a row that cannot be reached
// cannot mislead, where a row that focuses and then does nothing is the silent no-op
// this project has been bitten by twice. (This line said "four of its six" through two
// separate cuts -- a count in prose beside a table is a second copy of the table, and
// it drifts. It is asserted in test_screen_contents.cpp, which is where a number that
// has to stay true belongs.)
struct ReaderMenuViewModel {
  std::string bookTitle;  // the panel's header, shouted by the theme
  std::string progress;   // its right slot: "6%"
  std::vector<ListRow> rows;
  int focusedRow = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/Contents.dc.html: the book's chapters, and a jump to one.
//
// A full screen rather than an overlay -- it is a list you read and scroll, not a
// question about the page behind it. Its rows come from `toc.h`, whose entries carry a
// DEPTH: an NCX's depth-1 entries become section headers and the rest become rows,
// which is what draws the board's `BOOK I - MISS BROOKE` grouping. A flat NCX (two of
// the four books measured) yields no headers at all and the screen is simply a list.
struct ContentsViewModel {
  std::string title;      // "CONTENTS"
  std::string bookTitle;  // the band's right slot
  // The VISIBLE slice, as every windowed list here reports it -- never the whole book.
  std::vector<ListRow> rows;
  int focusedRow = -1;  // within `rows`, or -1 when the focus is off-window
  bool scrollable = false;
  int scrollFirst = 0, scrollCount = 0, scrollTotal = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

struct SettingsViewModel {
  std::string title;    // "SETTINGS"
  std::string version;  // the band's right slot: "V 0.1.0"
  std::vector<SettingsRow> rows;  // the VISIBLE window, not the whole list
  int focusedRow = -1;            // an index into `rows`, not into the whole list
  // The rail's two numbers, over the WHOLE list including headers. Not derivable
  // from `rows`, which holds only what is on screen.
  int firstRow = 0;
  int totalRows = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/Typography.dc.html.
//
// ONE STATE, ONE BOARD, ONE MODEL. An earlier design had a browse mode and an
// edit mode with a second board; the hint bar could not tell them apart (`DONE`
// and `OK` are synonyms) so the mode went, and with it a per-row editing flag and
// a chevron-availability flag that used to live here.
//
// It reuses ListRow for the rows, because a label and a right-aligned value is
// exactly ListRow's shape and this would be its fourth copy.
//
// THERE IS NO BOOK TITLE. The band's right slot is empty: these settings are
// device-wide, and naming one book would contradict the footnote directly below
// it. The slot is still RESERVED on the board -- a band's height must not vary by
// screen -- but nothing here supplies its content.
struct TypographyViewModel {
  std::string title;  // "TYPOGRAPHY"
  // The preview's copy. A FIXED specimen, not the book's text: see
  // screen_typography.h, which owns the string and the reason.
  std::string specimen;
  // THE PREVIEW'S SIDE PADDING, as Settings::margins is -- and it is here for the
  // same reason the two fields below it are: the theme is handed this model and a
  // face and nothing else, so without this field the box could not answer the
  // `Margins` row. Reported off the device as "changing the margins doesn't update
  // the live preview", which is the Alignment argument arriving a second time.
  //
  // THE BOX IS THE PAGE AND ITS PADDING IS THE MARGIN. The spec said the box
  // "cannot preview the margins" because it is chrome geometry -- 396px of measure
  // where the reading column is 444 -- and that was the wrong framing: the base
  // measure being narrower than the column does not stop the padding tracking the
  // setting. design/Typography.dc.html carries the decision.
  //
  // 18 is Settings::margins' own default and design/Reader.dc.html's column
  // padding, so a view model built by hand previews the board.
  int margins = 18;
  // THE PREVIEW'S LINE BOX, em x 1000, as PageMetrics::leadEm1000 is -- and it is
  // here because the preview has to SHOW the Line spacing setting, which the theme
  // has no other way to learn. renderTypography is handed the body face and this
  // model and nothing else, and a face carries its ppem but not its leading, so a
  // theme that resolved the lead itself would have to pin 1.7 and the preview
  // would then contradict the row directly under it on four of the five steps.
  //
  // The SIZE needs no such field: a ScalableFont is pinned to a pixel size by
  // init(), so the face this screen is handed IS the chosen ppem -- the same
  // reason Theme::readerMetrics reads three typography fields and not four.
  int leadEm1000 = 1700;
  // WHETHER THE PREVIEW IS STRETCHED TO ITS BOX, from Settings::justify -- and it
  // is here for the same reason the lead is: the box is labelled `LIVE PREVIEW`, and
  // without this field pressing CHANGE on the `Alignment` row spends a ~520 ms
  // repaint moving four characters of a row value while the box itself does not
  // move. A preview visibly ignoring one of its four rows reads as a screen that
  // does not work.
  //
  // `true` is design/Typography.dc.html's own `text-align: justify` and
  // Settings::justify's default, so a view model built by hand previews the board.
  bool justify = true;
  std::vector<ListRow> rows;
  int focusedRow = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

}  // namespace reader
