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
  // THE POINTER NAMES A BOOK THE CARD NO LONGER HAS -- deleted from a computer
  // between sessions, or a different card in the slot. design/HomeMissing.dc.html.
  //
  // IT IS NOT `nothingToContinue`, AND THE DIFFERENCE IS THE WHOLE STATE. The
  // pointer still KNOWS the book: its name, its author, how far in the reader was.
  // What is missing is the file. So the reading column is drawn, the spine still
  // carries the title, and a bordered strip above the stats says why the numbers
  // below it describe a book that will not open. Falling back to the centred block
  // -- which is what this shipped as, and what CLAUDE.md called "honest if less
  // informative" -- throws away the one piece of information the reader needs to
  // understand what happened, and reads as the device having forgotten rather than
  // as the card having changed.
  bool bookMissing = false;
  // The strip's sentence, and it names the book. Composed by `missingBookNote` so
  // that the shell's and the demo's spelling of it cannot diverge, and so that it
  // cannot name a different book from the spine two inches to its left.
  //
  // A FLAG AND A STRING RATHER THAN `!missingNote.empty()`, on ListRow::discloses'
  // rule: deriving a state from an empty value makes a book whose metadata gave no
  // title indistinguishable from a book that is present. The theme ELIDES this --
  // it wraps, and then clamps against the stats it may not push into.
  std::string missingNote;
  std::vector<MenuEntry> menu;
  int focusedMenuIndex = -1;                 // -1 = Continue block focused
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
  // Which of those four buttons also has a long-press action. The theme draws a
  // hollow ring on the slot (design 662557d) and the screen builds its
  // long-press mask from the same array, so the affordance and the behaviour
  // cannot drift apart -- a ring always means a hold is bound, and a bound hold
  // always shows a ring.
  std::array<bool, 4> holds{};

  // DOES THIS SCREEN DRAW A CONTINUE BLOCK? One question asked once, by the three
  // things that have to agree about it: the focus ring (-1 is the block's own
  // position and must be unreachable where there is no block), the Back gesture
  // (Home's board binds Back to READ, which is CONTINUE's action from a button
  // instead of a selection) and the theme.
  //
  // Two of those spelled it `!nothingToContinue` independently, which was one
  // condition in two places -- and the third state to stop offering CONTINUE would
  // have had to be remembered in both. This project has shipped a dead button twice
  // from exactly that shape, which is the shape the missing-book state closes.
  bool offersContinue() const { return !nothingToContinue && !bookMissing; }
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

  // THE CHAPTER'S NAME, and the run it sits in used to be `6% - CH. 01` -- the
  // percentage and a SPINE POSITION, composed by the shell into one string.
  //
  // IT IS THE NAME NOW, AND THE PERCENTAGE IS NO LONGER A FIELD. The theme
  // composes that from `progressPercent`, which the bar directly above it already
  // reads, so the number under the bar and the length of the bar cannot disagree
  // -- they were two spellings of one fact and the shell was free to set them
  // independently. HomeViewModel::percent is the same field doing the same job.
  //
  // ELIDED BY THE THEME ON ONE LINE, never wrapped, and the reason is this run
  // rather than its width: a chapter CHANGES while a book is being read and an
  // author does not. This card's height is a sum and the title takes whatever is
  // left, so a chapter free to grow would make the BOOK's name reflow -- or newly
  // acquire an ellipsis -- because the reader turned a page. Fixed at one line,
  // the card's layout is a function of the book alone. design/Sleep.dc.html
  // carries the corpus measurement behind that (8,617 real chapter labels).
  //
  // ReaderViewModel::chapter's own string, not a second derivation of it: the
  // Reader's band, Contents' NOW row, Home's meta line and this all name the
  // reader's chapter in the same words.
  //
  // EMPTY DRAWS NOTHING AND COSTS NO LINE -- unlike Home's, which reserves its
  // line because runs sit below it. This is the card's LAST run, so an absent
  // chapter simply shortens the card, and an absent claim beats a false one: a
  // pointer written before last.json carried a chapter cannot say which one this
  // is, and must not fall back to the position it used to show.
  std::string chapter;

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

  // NEGATIVE MEANS NOT KNOWN, and `kProgressUnknown` is the value the producer writes.
  //
  // This is `pageTotal`'s unknown, not a second one: the number IS the counter below
  // as a fraction (53 of 890 is 5.955%, drawn as 6%), so it is divided by that total
  // and is unknown in exactly the moments the total is. One condition, two slots.
  //
  // IT WAS `0` FOR TWO PHASES, and 0 is a REACHABLE SETTLED VALUE here -- page 1 of a
  // 300-page chapter rounds to it and is right -- so the unknown was drawn identically
  // to the top of the chapter while the counter beside it honestly said `53 / —`. A
  // reader turning pages steadily never lets the count's quiet window fire, so they
  // could be thirty pages into a chapter and still be told 0%. Reported off a device
  // after a week of use. `percentFor`'s -1 for "not started" and BatteryTracker's
  // `kUnknownPercent` are the same sentinel for the same reason: a false claim is worse
  // than an absent one.
  //
  // The theme draws `—%` for it and omits the progress bar, which cannot hold a dash
  // -- see design/Reader.dc.html's footer, which states both.
  static constexpr int kProgressUnknown = -1;
  int progressPercent = kProgressUnknown;
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
// design/NamesEmpty.dc.html, and design/Names.dc.html for the same id.
//
// ONE VIEW MODEL FOR BOTH BOARDS, which is what makes the empty state a VARIANT
// rather than a second screen -- HomeEmpty's rule, and the reason is the same: two
// render branches would be two ways to spell one layout.
//
// IT CARRIES NO ROWS YET, AND THAT IS NOT AN OVERSIGHT. The card's name store is
// #156 and the display-time grouping is #157, so `NamesScreen` has nothing to list
// and this screen renders the empty variant always. The row fields arrive with the
// shared stacked-row primitive (#167) that would draw them; adding them here first
// would be a field nothing sets and a render branch nothing draws, which is the
// shape this repo's own rule refuses -- the Typography panel's formatters were
// extracted for a second caller that never came and had to be put back.
//
// THE BAND HAS NO RIGHT SLOT ON EITHER BOARD. It said `TO CH. 07` -- the coverage
// boundary -- until backfill made that a temporary state rather than a standing
// one. The slot's LINE BOX is still reserved, which is Typography's and BookEnd's
// convention and why `drawHeaderBand` is passed an empty value rather than the band
// being drawn some other way.
struct NamesViewModel {
  std::string title;       // "NAMES"
  std::string emptyTitle;  // "NO NAMES YET"
  std::string emptyBody;   // the paragraph under it
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

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
  std::string version;  // the band's right slot: "V " + reader::kVersion
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

// ---------------------------------------------------------------------------
// THE V1.1 CONNECT FLOW. Six screens, and three of them reuse ListRow because a
// label with a right-aligned value IS ListRow's shape -- a fourth copy of it
// would be the duplication this project extracted it to end.
// ---------------------------------------------------------------------------

// design/WifiSettings.dc.html, and design/WifiSettingsEmpty.dc.html.
//
// ONE MODEL FOR BOTH, which is HomeEmpty's and NamesEmpty's mechanism: a
// variant rather than a second screen, because two render branches would be two
// ways to spell one layout and they would drift. `nothingSaved` is the whole
// difference, and it is named for the STATE rather than for the layout so the
// theme decides what that means.
struct WifiSettingsViewModel {
  std::string title;  // "WI-FI"
  // "ON DEMAND", never "CONNECTED" -- spec 4.1b:249 forbids the second, because
  // the radio is off whenever this screen is on glass.
  std::string state;
  // The empty variant: no saved networks yet, which is the state every user
  // meets first. The SETUP row is still drawn and still focusable, which is
  // what makes this NOT HomeEmpty's shape -- there is something to press.
  bool nothingSaved = false;
  std::string emptyTitle;
  std::string emptyProse;
  std::vector<ListRow> rows;
  int focusedRow = -1;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// One scan result as the picker draws it.
struct WifiScanRow {
  std::string ssid;
  // 1..3, the board's three-bar glyph. NOT an rssi: the theme draws one of
  // three marks, and a view-model carrying dBm would make the theme do the
  // banding -- which is a decision, and decisions do not belong there.
  int bars = 1;
  bool locked = false;
  // The last row. A row rather than a slab because the board draws it as one,
  // and it is what the Confirm hint names when it is focused.
  bool isRescan = false;
};

// design/WifiPicker.dc.html, plus its scrolled and empty variants.
struct WifiPickerViewModel {
  std::string title;  // "JOIN NETWORK"
  std::string found;  // "18 FOUND", or "NONE FOUND"
  // The scan has not come back. drawStatusBar REPLACES the hint bar with one
  // centred tracked line -- LibraryOpening's mechanism, reused rather than
  // re-boarded.
  bool scanning = false;
  std::string statusLabel;  // "SCANNING"
  // The scan came back with nothing. Its own board, because the list is
  // replaced by copy.
  bool nothingFound = false;
  std::string emptyTitle;
  // TWO PARAGRAPHS, not one: the second carries the 2.4 GHz caveat, which is a
  // separate thought and could not be made to clear the wrap boundary inside
  // one block. See design/WifiPickerEmpty.dc.html, which also records that
  // whether the caveat belongs here at all is still open.
  std::string emptyProse;
  std::string emptyCaveat;
  std::vector<WifiScanRow> rows;  // the VISIBLE window, not the whole list
  int focusedRow = -1;            // an index into `rows`
  // The rail's two numbers, over the WHOLE list. Not derivable from `rows`.
  int firstRow = 0;
  int totalRows = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WifiPassword.dc.html -- the text-entry keyboard. NOT WI-FI'S, although
// Wi-Fi's passphrase board is the only thing drawing it today: the screen behind
// it was extracted before its second caller arrived (#126), so this carries a
// FIELD NAME rather than a network and has no Wi-Fi in it anywhere.
struct TextEntryViewModel {
  std::string title;      // the band's left slot -- "PASSWORD"
  std::string fieldName;  // the band's right slot -- what the text is FOR
  // WHAT IS IN THE FIELD, IN CLEAR -- which is the CALLER's decision and not
  // this struct's. `visibility` below is what states it on the glass, and the
  // argument for showing a passphrase lives at the Wi-Fi call site: there is no
  // masked mode today and whether to have one wants a board.
  std::string entered;
  // WHERE THE CARET SITS, as a byte offset into `entered`. The field draws the
  // text either side of it rather than a block on the end -- the caret is a
  // position now, not a terminator.
  size_t caret = 0;
  std::string counter;     // "10 CHARS"
  std::string visibility;  // "SHOWN WHILE TYPING"
  // The cells, row-major, and the widths that cut them into rows. The theme
  // draws what it is given rather than knowing the layout, so a layer change is
  // a screen change.
  std::vector<std::string> cells;
  std::vector<int> rowWidths;
  int focusedCell = 0;
  std::string note;  // "UP AND DOWN MOVE BETWEEN ROWS; ..."
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WifiConnect.dc.html -- the connecting dialog.
//
// NO PROGRESS FIELD, deliberately. The board's eight-cell ticker is gone: it
// read as a fraction of a known total, a join takes an unknown one to ten
// seconds, and nothing on this device animates. The LABEL is the indicator.
struct WifiConnectViewModel {
  std::string caption;  // "CONNECTING..." stepping to "READY"
  std::string right;    // "WI-FI"
  std::string message;  // Joining "HOME" to test the password.
  std::string note;     // WI-FI TURNS OFF AGAIN AFTERWARDS.
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WifiError.dc.html and its two siblings -- ONE SCREEN, THREE COPY
// SHAPES, which is BookError's argument: a join fails three distinguishable
// ways and one sentence would be a lie.
struct WifiErrorViewModel {
  // "COULDN'T JOIN" on the three radio failures and "COULDN'T SAVE IT" on
  // ListFull, whose join SUCCEEDED -- see screen_wifi_error.cpp.
  std::string caption;
  std::string message;
  // WHETHER THE EDIT-PASSWORD SLAB IS DRAWN AT ALL. Absent, not inert: on the
  // two shapes where the password is not what went wrong, a slab offering to
  // change it points the wrong way, and a slab that draws and does nothing is
  // the works-only-sometimes trap this project has shipped twice.
  //
  // An explicit flag rather than an empty label, for ListRow::discloses'
  // reason: a slab is a bigger claim than a chevron.
  bool offersEdit = false;
  std::vector<std::string> actions;  // the slab labels, first is focused-filled
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WifiNetworkActions.dc.html -- what holding Confirm on a saved network
// opens. One row today, deliberately; it is where Connect now and Make
// automatic go when there is a reason for them.
struct WifiNetworkActionsViewModel {
  std::string caption;       // the SSID, truncated on one line
  std::string captionValue;  // "AUTO" or "SAVED"
  std::vector<ListRow> rows;
  int focusedRow = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// ---------------------------------------------------------------------------
// ARTICLES OVER WALLABAG (V1.1). design/Articles.dc.html and its five siblings.

// One row of the Articles list (design/Articles.dc.html). LibraryRow's shape and
// deliberately not LibraryRow itself: these live in different directories, carry
// different second lines and have different values, and sharing the struct would
// tempt a theme into sharing the renderer, which is where the two lists would
// start constraining each other.
struct ArticleRow {
  std::string title;
  // "LONGREADS - 22 MIN", or with " - READ" on the end. Composed by the SCREEN,
  // as LibraryRow::meta is and for its reason: it is content.
  std::string meta;
  // TWO FACTS AND NOT ONE, because they answer different questions: `opened` is
  // "have I started this" and drives the BULLET, and `finished` is "did I get to
  // the end" and drives the `. READ` on the meta line above.
  //
  // THEY WERE ONE FLAG AND IT WAS WRONG IN BOTH DIRECTIONS AT ONCE. An article
  // opened for ten seconds lost its bullet AND claimed to have been read; an
  // article read to its last page could claim neither, because nothing set
  // `finished` for one at all. design/Articles.dc.html draws all three states.
  //
  // FLAGS RATHER THAN THE THEME KEYING ON `meta` ENDING IN READ, which would make
  // a string the source of truth for a mark.
  bool opened = false;
  bool finished = false;
};

// design/Articles.dc.html, with design/ArticlesSetup.dc.html and
// design/SyncDone.dc.html as VARIANTS of it -- same ScreenId, same model, same
// renderer. HomeEmpty's rule: what differs between the three is what this struct
// says, never which function draws it.
struct ArticlesViewModel {
  std::string title;  // the band's label: "ARTICLES"
  // The band's value, PRE-FORMATTED here where LibraryViewModel's is an int, and
  // the difference is that this slot holds two KINDS of answer: "3 UNREAD" is a
  // count and "NOT SET UP" is a state. An int plus a flag would be the theme
  // deciding which of two sentences to build, which is a screen's decision.
  std::string bandValue;
  // THE NOT-SET-UP VARIANT, which replaces the list and the sync row with a
  // centred block. Not a second screen and not a second renderer: one flag, as
  // HomeViewModel::nothingToContinue is, and spelled to match it.
  bool notSetUp = false;
  std::string setupTitle;   // "READ IT LATER"
  std::string setupProse;   // the paragraph naming the file
  std::string setupNote;    // "THE FILE IS ALREADY ON THE CARD."
  // The sync row's right-hand stamp: "WALLABAG - NO NEW", "- NEVER", "- 3 NEW",
  // "- FAILED". AN OUTCOME AND NEVER AN AGE, because this device has no clock
  // (#132) -- design/Articles.dc.html carries the argument and the four values.
  std::string syncLabel;  // "Sync now"
  std::string syncStamp;
  // design/SyncDone.dc.html's status block, above the sync row. EMPTY on every
  // other path to this screen, which is what makes it a variant rather than a
  // state: it is the ONE place a push count is stated, and a list reached any
  // other way must not claim one.
  std::string statusLine;
  // WHETHER THE SYNC ROW HOLDS THE FOCUS. -1 in `focusedRow` says "no ARTICLE is
  // focused" and cannot say where the focus went instead, so a renderer reading
  // only that draws a screen with nothing selected -- which is what shipped, and
  // was reported as "the sync now row doesn't look focused, even when it is".
  // Worst on an empty list, where the sync row is the only row and always has
  // it. design/Articles.dc.html states the rule the specimen cannot show.
  bool syncFocused = false;
  std::vector<ArticleRow> rows;  // the VISIBLE slice, never the whole directory
  int focusedRow = -1;           // an index into `rows`
  // The rail's two numbers, over the whole list. LibraryViewModel's pair exactly.
  int firstRow = 0;
  int totalRows = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/ArticleActions.dc.html -- the overlay a HOLD on an article row opens.
// ItemActionsViewModel's shape with one slot fewer: there is no status value in
// its caption, because an article has no percentage the list already knows.
struct ArticleActionsViewModel {
  std::string caption;  // the article's title, shouted and elided by the theme
  std::vector<ItemActionEntry> actions;
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/ArticleEnd.dc.html -- what an article's last page turns into.
// BookEndViewModel's shape, and the two differences are both real:
//
//   - the band HAS a value here, where BookEnd's is deliberately empty. `2 LEFT`
//     is a count of files on the card, not the thing the screen is about, so it
//     cannot squeeze the label the way a shouted book title did there;
//   - FOUR slabs rather than two, and the third is ABSENT rather than inert when
//     there is no next unread article -- WifiError's rule that the slab list IS
//     the shape, which is why `actions` is a vector and not four named strings.
struct ArticleEndViewModel {
  std::string title;      // "ARTICLE FINISHED"
  std::string leftValue;  // "2 LEFT", the band's right slot; empty when none remain
  // The article's name. IT WRAPS, so it must outlive the render that reads it --
  // Prose holds views into it, which is the use-after-free Home shipped once.
  std::string articleTitle;
  std::string meta;  // "LONGREADS - 22 MIN"
  std::vector<std::string> actions;  // slab labels; the focused one is filled
  int focusedAction = 0;
  std::string note;  // "SYNCS ON THE NEXT CONNECTION."
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WallabagAccount.dc.html -- setup and status. SettingsViewModel's shape,
// and it reuses ListRow for its rows because a label and a right-aligned value is
// exactly ListRow's shape -- TypographyViewModel's reason for the same choice.
struct WallabagAccountViewModel {
  std::string title;      // "WALLABAG"
  std::string bandValue;  // "SIGNED IN" or "NOT SET UP"
  std::vector<ListRow> rows;
  int focusedRow = -1;
  int firstRow = 0;
  int totalRows = 0;
  std::string note;  // the paragraph under the last row
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WallabagConnecting.dc.html AND design/WallabagFetching.dc.html -- ONE
// screen, TWO STAGES, and one model for both. WifiConnectViewModel's shape.
//
// The caption and the message are the whole difference between the stages, which
// is why neither is a flag: a `fetching` bool would put the two captions in the
// THEME, where a copy change means a code change and the board is no longer the
// source of truth for the words.
struct WallabagConnectingViewModel {
  std::string caption;  // "CONNECTING..." then "SYNCING..."
  std::string message;  // "Connecting to wallabag.lan." then "Fetching 3 of 12."
  std::string note;     // the footnote; it differs per stage too
  // WHICH STAGE, AND THEREFORE WHICH MARK. The first stage IS the radio -- it is
  // joining a network and the message names the host -- and the second is past
  // it, with bytes landing on the card one article at a time. Drawn as `kWifi`
  // and `kDownload`, which is what design/WallabagConnecting.dc.html and
  // design/WallabagFetching.dc.html respectively carry.
  //
  // A FLAG AND NOT A DERIVATION FROM THE CAPTION: the caption is copy and may be
  // reworded, where this is a state. `setFetching` is the one thing that sets
  // it, beside the three strings it already rewrites.
  bool fetching = false;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// design/WallabagError.dc.html and its two siblings -- ONE SCREEN, THREE COPY
// SHAPES, which is BookError's argument and the join flow's precedent: a sync
// fails three distinguishable ways and one sentence would be a lie.
//
// THERE IS NO `offersRetry` FLAG, where WifiErrorViewModel has `offersEdit`. The
// slab LIST is the shape here: two of the three drop `TRY AGAIN` and the vector
// is already shorter, so a flag would be a second spelling of its length -- and
// the two could disagree. WifiError carries its flag because its slab is not the
// last one in the list and its absence cannot be read off the count.
struct WallabagErrorViewModel {
  std::string caption;  // "COULDN'T SIGN IN" or "COULDN'T CONNECT"
  std::string message;
  std::vector<std::string> actions;  // slab labels; the focused one is filled
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

}  // namespace reader
