# BookError (the corrupt-book dialog) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put a dialog on the panel when a book refuses to open, so the press that
opens a damaged book stops being a press that visibly does nothing.

**Architecture:** A new overlay screen `BookError` over whatever asked to open the
book -- the Library or Home. It carries a bounded `BookErrorReason`, never
`openBook`'s `why` string, and renders one of two copy shapes so it never claims
damage for a card that was merely pulled. Its `DELETE FILE...` slab needs
`DeleteConfirmScreen` to stop holding a `LibraryScreen&`, so that screen moves to
`Facts` (the `BookDetailsScreen::Facts` precedent) and its removal becomes a shell
latch beside `Open`/`Retry`/`Finish`. The append also fixes #42 with a `ScreenId::Count`
sentinel.

**Tech Stack:** C++20 (`core/`, no Arduino/ESP/host-OS), doctest, CMake, the
`.dc.html` design boards, `tools/iconc.py`, `tools/compare-design.py`, PlatformIO
for the ESP32-C3 shell.

**Spec:** `docs/superpowers/specs/2026-09-06-book-error-design.md`

**Read first:** `CLAUDE.md`, sections *Overlays and lists*, *Storage* (the session
record), *Invariants worth not relearning*, and *Goldens*.

---

## File Structure

| File | Responsibility | New? |
|---|---|---|
| `design/BookErrorUnreadable.dc.html` | the second copy shape's board | create |
| `core/include/reader/app.h` | `ScreenId::BookError`, `ScreenId::Count`, `Action::Kind::Delete` | modify |
| `core/src/app.cpp` | latch `Delete` in `dispatch` | modify |
| `core/src/session_record.cpp` | wire name for BookError; four bounds repointed at `Count` | modify |
| `core/include/reader/viewmodel.h` | `BookErrorViewModel` | modify |
| `core/include/reader/screen_book_error.h` | the screen and its `Facts`/`Reason` | create |
| `core/src/screen_book_error.cpp` | its copy, focus and gestures | create |
| `core/include/reader/theme.h` | `renderBookError` virtual | modify |
| `core/src/theme_quiet.cpp` | `QuietTheme::renderBookError` | modify |
| `core/include/reader/icons.h`, `core/src/icons_data.h` | `kWarning` | modify (generated) |
| `tools/iconc.py` | the `warning` ICONS entry | modify |
| `core/include/reader/screen_delete_confirm.h`, `core/src/screen_delete_confirm.cpp` | `Facts` instead of `LibraryScreen&`; latch instead of `deleteFocused()` | modify |
| `core/include/reader/screens.h`, `core/src/screens.cpp` | factory: prime and build both screens | modify |
| `sim/main.cpp` | `book_error`, `book_error_unreadable` | modify |
| `tools/compare-design.py` | `book_error_unreadable` row | modify |
| `test/unit/test_screen_book_error.cpp` | the screen's behaviour | create |
| `test/unit/test_theme_book_error_golden.cpp` | 4 goldens | create |
| `test/unit/test_session_record.cpp` | every ScreenId has a distinct wire name | modify |
| `test/unit/test_focus_restore.cpp` | catalogue row + assert repointed | modify |
| `test/unit/test_screen_delete_confirm.cpp` | the refactor | modify |
| `shell/src/main.cpp` | raise the dialog; answer the Delete latch | modify |

**CMake uses `file(GLOB ...)`** -- re-run `cmake -S . -B build` after creating any
source file, or it is silently ignored.

---

### Task 1: Pin that every ScreenId has its own wire name

This test is green today. It exists so Task 2 can show it going red, which is the
whole demonstration that #42 is real.

**Files:**
- Test: `test/unit/test_session_record.cpp`

- [ ] **Step 1: Write the test**

Append to `test/unit/test_session_record.cpp`:

```cpp
// EVERY SCREEN, NOT EVERY SCREEN SOMEBODY REMEMBERED. session_record.cpp's table and
// switch have twice been left short by an append -- Typography, then BookEnd -- and
// each time the new screen fell through to `return kNames[0]` and serialised as
// `home`, so a reader idle-sleeping on it woke on Home with no failing test and no log
// line. A static_assert on the table's LENGTH cannot see that, because the bound it
// compares against is a hand-named member that the append does not move.
//
// This walks the enum by ORDINAL up to the Count sentinel, so it cannot be left short.
TEST_CASE("every ScreenId has its own wire name") {
  std::set<std::string> seen;
  for (int i = 0; i < static_cast<int>(reader::ScreenId::Count); ++i) {
    const reader::ScreenId id = static_cast<reader::ScreenId>(i);
    const char* n = reader::sessionWireName(id);
    REQUIRE(n != nullptr);
    CAPTURE(i);
    CAPTURE(n);
    // Distinct: a screen that fell through to kNames[0] collides with Home, and a
    // collision is exactly what the fall-through produces.
    CHECK(seen.insert(n).second);
  }
  CHECK(seen.size() == static_cast<size_t>(reader::ScreenId::Count));
}
```

Add `#include <set>` and `#include <string>` to that file's includes if absent.

- [ ] **Step 2: Run it — it must not compile yet**

Run: `cmake -S . -B build && cmake --build build -j8 2>&1 | tail -20`
Expected: FAIL — `'Count' is not a member of 'reader::ScreenId'`. That is correct;
the sentinel arrives in Task 2. Do not add it here.

- [ ] **Step 3: Commit the test alone**

```bash
git add test/unit/test_session_record.cpp
git commit -m "test(session-record): pin that every ScreenId has its own wire name

Walks the enum by ORDINAL rather than trusting a table's length against a
hand-named bound, which is the guard shape #42 is about: it has been left short
twice and said nothing both times.

Does not compile until the Count sentinel lands; that is the next commit.

Refs #42"
```

---

### Task 2: Append `ScreenId::BookError` and fix #42 with a `Count` sentinel

**Files:**
- Modify: `core/include/reader/app.h` (the `ScreenId` enum)
- Modify: `core/src/session_record.cpp:34-35`, `:45`, `:75-122`
- Modify: `test/unit/test_focus_restore.cpp:37-62`

- [ ] **Step 1: Append BookError ONLY, and watch the guard say nothing**

In `core/include/reader/app.h`, replace `  BookEnd\n};` with:

```cpp
  BookEnd,
  // design/BookError.dc.html -- the dialog a book that will not open raises.
  BookError
};
```

- [ ] **Step 2: Run the suite to see the trap fire**

Run: `cmake --build build -j8 2>&1 | tail -20`
Expected: still FAIL on `ScreenId::Count` (Task 1's test). Confirm by eye that
**neither `static_assert` fired** -- `session_record.cpp:34` and
`test_focus_restore.cpp:59` both compare against `ScreenId::BookEnd + 1`, which the
append did not move. That silence is #42, reproduced.

- [ ] **Step 3: Add the sentinel**

In `core/include/reader/app.h`, replace the `BookError` block from Step 1 with:

```cpp
  BookEnd,
  // design/BookError.dc.html -- the dialog a book that will not open raises.
  BookError,
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
```

- [ ] **Step 4: Repoint session_record.cpp at the sentinel**

In `core/src/session_record.cpp`, add BookError's row to `kNames` (after `"book-end"`,
matching the enum's order):

```cpp
    "book-error",
```

Replace the `static_assert` at `:34-35` with:

```cpp
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(ScreenId::Count),
              "a ScreenId was added or removed; give it a row in kNames and a case in"
              " sessionWireName. This names the Count SENTINEL, never a member -- a"
              " named member does not move when a screen is appended, which is how"
              " Typography and then BookEnd each shipped serialising as `home`.");
```

Replace the loop bound at `:45`:

```cpp
  for (int i = 0; i < static_cast<int>(ScreenId::Count); ++i) {
```

Add the case beside `BookEnd`'s in `sessionWireName`:

```cpp
    case ScreenId::BookError: return kNames[14];
    // NOT A SCREEN, so it has no name and must never reach the fall-through below,
    // which is what silently made a missing case read as `home`.
    case ScreenId::Count: break;
```

- [ ] **Step 5: Repoint the focus-restore catalogue**

In `test/unit/test_focus_restore.cpp`, add `ScreenId::BookError,` to `kAllScreens`,
and replace the comment and `static_assert` at `:55-62` with:

```cpp
// NAMES THE SENTINEL, so an append cannot satisfy it unchanged. It used to name the
// last member by hand -- `ScreenId::Peek + 1`, then `ScreenId::BookEnd + 1` -- and
// both times an append left both sides equal and the guard that exists to force a
// new screen into kAllScreens said nothing. That was #42.
static_assert(sizeof(kAllScreens) / sizeof(kAllScreens[0]) ==
                  static_cast<size_t>(ScreenId::Count),
              "a ScreenId was added or removed; give it a row in kAllScreens");
```

- [ ] **Step 6: Check `screenName` refuses the sentinel**

Run: `grep -n "screenName" core/src/app.cpp | head -3`, then read its body. If it is a
switch, add `case ScreenId::Count: return "?";` beside the others. If it indexes a
table, give the table the same `ScreenId::Count` `static_assert` as `kNames`.

- [ ] **Step 7: Build and run**

Run: `make test 2>&1 | tail -20`
Expected: PASS, including `every ScreenId has its own wire name`.
The factory has no `BookError` case yet, so `create()` returns nullptr for it and
`test_focus_restore.cpp` treats it as an unbuildable screen — that is fine and is what
Task 10 fills in.

- [ ] **Step 8: Commit**

```bash
git add core/include/reader/app.h core/src/session_record.cpp core/src/app.cpp \
        test/unit/test_focus_restore.cpp
git commit -m "fix(screens): a Count sentinel, so an appended screen cannot serialise as \`home\`

Appending ScreenId::BookError reproduced #42 exactly: session_record.cpp's three
bounds and test_focus_restore.cpp's static_assert each name a MEMBER by hand, so
the append left every one of them satisfied and the new screen fell through
sessionWireName to \`return kNames[0]\`. It had happened for Typography and again
for BookEnd, and both files carried a comment saying the next append needed the
line moved by hand.

Count is not a screen and nothing may give it a row, a name or a case -- a
sentinel that became serialisable would be a worse version of the bug.

Closes #42
Refs #5"
```

---

### Task 3: The second board

Per the design-first rule: the copy goes on a board before any code renders it.

**Files:**
- Create: `design/BookErrorUnreadable.dc.html`

- [ ] **Step 1: Copy the board and change only its sentence**

```bash
cp design/BookError.dc.html design/BookErrorUnreadable.dc.html
```

- [ ] **Step 2: Replace the prose**

In `design/BookErrorUnreadable.dc.html`, replace exactly this string:

```
&ldquo;dubliners.epub&rdquo; appears to be damaged and can&rsquo;t be opened. The file was left untouched on the card.
```

with:

```
&ldquo;dubliners.epub&rdquo; could not be read from the SD card. The file was left untouched.
```

Change **nothing else** — same panel, same caption, same slabs, same hint bar. The
two boards are one screen with different copy, as `SleepIdle.dc.html` is to
`Sleep.dc.html`.

- [ ] **Step 3: Verify exactly one line differs**

Run: `diff design/BookError.dc.html design/BookErrorUnreadable.dc.html`
Expected: one `<` / `>` pair, the prose line only.

- [ ] **Step 4: Commit**

```bash
git add design/BookErrorUnreadable.dc.html
git commit -m "design(book-error): a second board for the refusal that is not damage

openBook's four reasons are not one event: three are parse failures and the
fourth is openRead returning null -- a file that is gone, or a card that is. And
SdFileSystem::openRead does not call noteCardGone(); only a short handle read
does, so a card pulled between the listing and the press is noticed 2-25s later
by pollCardPresence. For that whole window the shipped board would tell the
reader a healthy book \`appears to be damaged\`.

A false claim is worse than an absent one -- the same call this project already
makes for an unread battery gauge and for a book with no reading position.

One screen with different copy, as SleepIdle is to Sleep. Nothing but the
sentence differs.

Refs #5"
```

---

### Task 4: The `kWarning` icon

**Files:**
- Modify: `tools/iconc.py` (the `ICONS` dict)
- Modify: `core/include/reader/icons.h`
- Regenerated: `core/src/icons_data.h`

- [ ] **Step 1: Add the ICONS entry**

In `tools/iconc.py`, inside `ICONS`, after the `"check"` entry:

```python
    # The corrupt-book dialog's mark. Matched on the TRIANGLE's own path rather
    # than on the exclamation stroke or the dot: BookError.dc.html and
    # BookErrorUnreadable.dc.html both carry this svg, so `source` names which
    # board owns it -- the same second line of defence kBook/kBookLarge and
    # kBattery/kBatteryCharging already need.
    #
    # THREE DIAGONALS, which is the shape Mono thresholding treats worst:
    # CLAUDE.md records kChevron coming out a notch lighter because its stroke is
    # mostly coverage-1 pixels. At 32x28 with a 1.6 stroke this should hold, but
    # that is an argument and only the panel can settle it.
    "warning": {
        "symbol": "kWarning",
        "note": "a warning triangle: the corrupt-book dialog's mark",
        "source": "design/BookError.dc.html",
        "match": "M9 1 17 15H1z",
    },
```

- [ ] **Step 2: Declare it**

In `core/include/reader/icons.h`, after `extern const Icon kCheck;`:

```cpp
extern const Icon kWarning;  // a warning triangle: the corrupt-book dialog's mark
```

- [ ] **Step 3: Generate**

Run: `make icons`
Expected: exits 0. `iconc.py` refuses to emit anything if the match string does not
locate exactly one `<svg>`, so a silent wrong pick is not possible.

- [ ] **Step 4: Verify the geometry came from the board, not from a guess**

Run: `grep -n "kWarning" -A 3 core/src/icons_data.h | head -8`
Expected: a `w` of 32 and an `h` of 28 — the board's own `width`/`height` attributes.
If they differ, the match found the wrong element; stop and fix the match string.

- [ ] **Step 5: Build and run the suite**

Run: `make test 2>&1 | tail -5`
Expected: PASS, unchanged count. No golden draws this icon yet.

- [ ] **Step 6: Commit**

```bash
git add tools/iconc.py core/include/reader/icons.h core/src/icons_data.h
git commit -m "feat(icons): kWarning, generated from the corrupt-book board

Generated rather than transcribed, as every mark here is: iconc.py reads the
board's own svg at generation time, so a design change propagates on the next
\`make icons\` and cannot be missed. Matched on the triangle's path with \`source\`
naming the owning board, because both BookError boards carry this svg.

Refs #5"
```

---

### Task 5: The view-model and the screen

**Files:**
- Modify: `core/include/reader/viewmodel.h`
- Create: `core/include/reader/screen_book_error.h`
- Create: `core/src/screen_book_error.cpp`
- Create: `test/unit/test_screen_book_error.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/unit/test_screen_book_error.cpp`:

```cpp
// design/BookError.dc.html and design/BookErrorUnreadable.dc.html -- one screen,
// two copy shapes.
#include "doctest.h"
#include "reader/screen_book_error.h"

using reader::Action;
using reader::BookErrorReason;
using reader::BookErrorScreen;
using reader::Gesture;
using reader::GestureEvent;
using reader::ScreenId;

namespace {
BookErrorScreen::Facts damaged() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Damaged,
          ScreenId::Library};
}
BookErrorScreen::Facts unreadable() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Unreadable,
          ScreenId::Home};
}
}  // namespace

TEST_CASE("the dialog is an overlay over whatever asked to open the book") {
  BookErrorScreen s(damaged());
  CHECK(s.id() == ScreenId::BookError);
  CHECK(s.isOverlay());
  // Chrome, one waveform. The parent may be the Reader-less Library or Home; neither
  // is grayscale, and this screen has no continuous tone of its own.
  CHECK(s.fidelity() == reader::Fidelity::Mono);
}

TEST_CASE("the damaged shape names the file and says it was left alone") {
  BookErrorScreen s(damaged());
  CHECK(s.vm().title == "CAN\xE2\x80\x99T OPEN FILE");
  // NAMES THE FILE. A dialog that does not name the thing is one people learn to
  // dismiss without reading -- DeleteConfirmScreen's own stated reason.
  CHECK(s.vm().message.find("dubliners.epub") != std::string::npos);
  CHECK(s.vm().message.find("damaged") != std::string::npos);
  CHECK(s.vm().okLabel == "OK");
  CHECK(s.vm().deleteLabel == "DELETE FILE\xE2\x80\xA6");
}

TEST_CASE("the unreadable shape does not claim the book is damaged") {
  BookErrorScreen s(unreadable());
  CHECK(s.vm().message.find("dubliners.epub") != std::string::npos);
  // THE WHOLE POINT OF THE SECOND SHAPE. openRead returning null is a file that is
  // gone or a card that is, and pollCardPresence takes 2-25s to notice -- so this
  // must not say `damaged` for a book that is perfectly fine.
  CHECK(s.vm().message.find("damaged") == std::string::npos);
  CHECK(s.vm().message.find("could not be read") != std::string::npos);
}

TEST_CASE("the board's four hint slots, and no hold") {
  BookErrorScreen s(damaged());
  CHECK(s.vm().hints[0] == "CLOSE");
  CHECK(s.vm().hints[1] == "SELECT");
  CHECK(s.vm().hints[2] == "UP");
  CHECK(s.vm().hints[3] == "DOWN");
  for (const bool h : s.vm().holds) CHECK_FALSE(h);
}

TEST_CASE("focus starts on OK, so a press before reading dismisses") {
  BookErrorScreen s(damaged());
  CHECK(s.focus() == 0);
  CHECK(s.vm().focusedAction == 0);
}

TEST_CASE("Back is close, and so is OK") {
  BookErrorScreen s(damaged());
  CHECK(s.onGesture({Gesture::Back}).kind == Action::Kind::Pop);
  CHECK(s.onGesture({Gesture::Activate}).kind == Action::Kind::Pop);
}

TEST_CASE("DELETE FILE... opens the confirmation") {
  BookErrorScreen s(damaged());
  REQUIRE(s.onGesture({Gesture::Next}).kind != Action::Kind::None);
  REQUIRE(s.focus() == 1);
  const Action a = s.onGesture({Gesture::Activate});
  CHECK(a.kind == Action::Kind::Push);
  CHECK(a.target == ScreenId::DeleteConfirm);
}

TEST_CASE("the delete slab is reachable on BOTH shapes") {
  // Making it inert on the unreadable shape was considered and rejected: the two
  // shapes differ only by a sentence of prose, so a reader meeting an inert slab
  // has nothing to learn the rule from. That is the `works only sometimes` trap.
  BookErrorScreen s(unreadable());
  REQUIRE(s.onGesture({Gesture::Next}).kind != Action::Kind::None);
  CHECK(s.focus() == 1);
  CHECK(s.onGesture({Gesture::Activate}).target == ScreenId::DeleteConfirm);
}

TEST_CASE("the focus wraps, as every list here does") {
  BookErrorScreen s(damaged());
  s.onGesture({Gesture::Prev});
  CHECK(s.focus() == 1);
}

TEST_CASE("the facts carry where to return to after a delete") {
  CHECK(BookErrorScreen(damaged()).facts().returnTo == ScreenId::Library);
  // The whole reason DeleteConfirm takes Facts: Home's CONTINUE has no Library.
  CHECK(BookErrorScreen(unreadable()).facts().returnTo == ScreenId::Home);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S . -B build && cmake --build build -j8 2>&1 | tail -10`
Expected: FAIL — `reader/screen_book_error.h: No such file or directory`.

- [ ] **Step 3: Add the view-model**

In `core/include/reader/viewmodel.h`, after `DeleteConfirmViewModel`:

```cpp
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
  // 0 = OK, 1 = delete, in the board's own top-to-bottom order. Spelled exactly as
  // DeleteConfirmViewModel::focusedAction because it is the same fact, and one rule
  // should have one spelling.
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};
```

- [ ] **Step 4: Write the screen header**

Create `core/include/reader/screen_book_error.h`:

```cpp
#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// WHY THE BOOK WOULD NOT OPEN, in the only vocabulary a screen may have.
//
// openBook has four reasons and they are not one event. Three are parse failures --
// a zip that is not one, an OPF that will not parse, a spine with nothing in it --
// and the fourth is fs.openRead() returning null, which is a file that is gone or a
// card that is. SdFileSystem::openRead does NOT call noteCardGone(); only a handle
// read that comes up short does, so a card pulled between the Library's listing and
// the press is noticed by pollCardPresence between 2s (the fast probe) and 25s (the
// FAT-scan backstop) -- and for that whole window a single sentence would tell the
// reader a perfectly healthy book "appears to be damaged".
//
// A false claim is worse than an absent one. That is the call this project already
// makes for an unread battery gauge (-1, not 0%), for a book with no reading
// position (no demo substitute), and for the charging bolt that spends a refresh on
// the unplug edge rather than staying wrong on glass.
enum class BookErrorReason : uint8_t { Damaged, Unreadable };

// The corrupt-book dialog (design/BookError.dc.html).
//
// An overlay, so App::render paints whatever is under it -- the Library, which is
// what the board draws, or HOME, which no board draws and which the CONTINUE path
// reaches. The screen does not care which, and must not: the reachability of a
// screen is a fact about the shell's stack, and encoding it here is how a screen
// ends up silently one-way.
class BookErrorScreen : public FocusScreen {
 public:
  // FACTS, NOT A REFERENCE -- BookDetailsScreen::Facts' precedent, and for its
  // reason: this screen is reached from the Library AND from Home's CONTINUE, and
  // only one of those has a Library to ask.
  struct Facts {
    std::string path;         // absolute on the filesystem
    std::string displayName;  // the leaf name, for the prose
    BookErrorReason reason = BookErrorReason::Damaged;
    // Where a completed delete should land. A FIELD rather than a derivation,
    // because the screen must not have to know how it was reached -- the same
    // reason Facts replaced the reference.
    ScreenId returnTo = ScreenId::Library;
  };

  explicit BookErrorScreen(Facts facts);

  ScreenId id() const override { return ScreenId::BookError; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  const BookErrorViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }

  // CONSTANT, so every focus move here is a partial repaint -- DeleteConfirmScreen's
  // reasoning verbatim. Nothing this screen draws changes shape with the focus: the
  // panel is sized from the caption's wrap and the paragraph's, both fixed once the
  // screen exists, plus two kActionH slabs that are both always drawn. Focus only
  // decides which is filled and which is outlined, in the same box.
  //
  // The two copy shapes wrap to different heights, and that does not matter: a push
  // is never a partial repaint (App::transition() is the signal), so two instances
  // can never be compared against one frame record. The token only has to hold
  // across focus moves within one screen's life -- which is also why DeleteConfirm
  // is constant while its caption carries a book title of any length.
  uint32_t paintFootprint() const override { return 1; }

 private:
  enum Row { kOk = 0, kDelete, kRowCount };

  void syncVm() override;

  Facts facts_;
  BookErrorViewModel vm_;
};

}  // namespace reader
```

- [ ] **Step 5: Write the screen body**

Create `core/src/screen_book_error.cpp`:

```cpp
#include "reader/screen_book_error.h"

#include "reader/theme.h"

namespace reader {

BookErrorScreen::BookErrorScreen(Facts facts)
    : FocusScreen(kRowCount, kRowCount), facts_(std::move(facts)) {
  // The board's caption, fixed. It does NOT carry the book's name, unlike
  // DeleteConfirm's -- the name is in the prose here, and following the board is the
  // rule. U+2019 as the board spells it (&rsquo;).
  vm_.title = "CAN\xE2\x80\x99T OPEN FILE";

  // BOTH SENTENCES NAME THE FILE, in the board's own U+201C/U+201D quotes
  // (&ldquo;/&rdquo;). A dialog that does not name the thing is one people learn to
  // dismiss without reading.
  const std::string quoted = "\xE2\x80\x9C" + facts_.displayName + "\xE2\x80\x9D";
  vm_.message =
      facts_.reason == BookErrorReason::Damaged
          // design/BookError.dc.html, verbatim.
          ? quoted +
                " appears to be damaged and can\xE2\x80\x99t be opened. The file was left"
                " untouched on the card."
          // design/BookErrorUnreadable.dc.html. Makes no promise about retrying,
          // because this board has no RETRY slab -- unlike SdMissing, which does.
          : quoted + " could not be read from the SD card. The file was left untouched.";

  vm_.okLabel = "OK";
  vm_.deleteLabel = "DELETE FILE\xE2\x80\xA6";  // U+2026, the board's &hellip;
  vm_.hints = {"CLOSE", "SELECT", "UP", "DOWN"};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
  // The base's focus starts on the first row, which is kOk -- the board's filled
  // slab, and where a prompt with a destructive second option belongs: a press made
  // before the user has read anything dismisses. DeleteConfirmScreen's rule.
  syncVm();
}

void BookErrorScreen::syncVm() { vm_.focusedAction = focus(); }

Action BookErrorScreen::onGesture(const GestureEvent& g) {
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+1);
    case Gesture::Prev:
      return moveFocus(-1);
    case Gesture::Back:
      // Back IS close, which is what the board's first hint slot says -- and it is
      // the same thing OK does, so a reader who presses either gets the same result.
      return Action::pop();
    case Gesture::Activate:
      if (vm_.focusedAction == kOk) return Action::pop();
      // The confirmation owns the deleting. This screen owns saying what went wrong.
      // It is reachable on BOTH copy shapes: making it inert on `Unreadable` was
      // considered and rejected, because the shapes differ only by a sentence and a
      // reader meeting an inert slab would have nothing to learn the rule from --
      // the `works only sometimes` trap. If the card really is gone the removal
      // simply fails, which needs no branch here: FileSystem::remove reports the END
      // STATE and the list the reader lands on already says which it was.
      return Action::push(ScreenId::DeleteConfirm);
    default:
      return Action::none();
  }
}

void BookErrorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                             Plane plane) const {
  theme.renderBookError(fb, fonts, vm_, plane);
}

}  // namespace reader
```

- [ ] **Step 6: Run — it fails on the missing theme method**

Run: `cmake -S . -B build && cmake --build build -j8 2>&1 | tail -10`
Expected: FAIL — `'class reader::Theme' has no member named 'renderBookError'`.
Task 6 adds it. Do not stub it here; a stub is a screen that draws nothing.

- [ ] **Step 7: Commit after Task 6 builds**

This task's files are committed together with Task 6's, because neither compiles
alone. Proceed to Task 6.

---

### Task 6: `renderBookError`

The board is `renderDeleteConfirm`'s panel with an icon inserted above the prose.
Verified: both boards are 380 wide at `left: 50px`, `border: 2px`, caption
`padding: 21px 20px`, body `padding: 18px 20px`, buttons `padding: 0 20px 20px 20px`
with `gap: 12px`, prose `line-height: 1.45`. So `kConfirmPanelW`, `kConfirmProsePadY`,
`kConfirmProseLeadEm`, `kConfirmButtonGap` and `kConfirmButtonPadBottom` are this
board's numbers too, not a borrowing.

**Files:**
- Modify: `core/include/reader/theme.h`
- Modify: `core/include/reader/theme_quiet.h` (the override declaration)
- Modify: `core/src/theme_quiet.cpp`

- [ ] **Step 1: Declare the virtual**

In `core/include/reader/theme.h`, after `renderDeleteConfirm`'s declaration:

```cpp
  // The corrupt-book dialog, an overlay like the confirmation and drawing over a
  // parent App::render has painted -- so this must NOT clear the framebuffer.
  //
  // Its own typed method rather than a shared "panel with a paragraph and two
  // slabs" surface: it draws a MARK the confirmation does not, and a shared
  // abstraction would have to be told which board it was drawing. That is the same
  // reasoning renderSdMissing and renderBookEnd each carry.
  virtual void renderBookError(Framebuffer& fb, const FontSet& fonts,
                               const BookErrorViewModel& vm, Plane plane = Plane::Bw) = 0;
```

- [ ] **Step 2: Declare the override**

In `core/include/reader/theme_quiet.h`, add beside `renderDeleteConfirm`:

```cpp
  void renderBookError(Framebuffer& fb, const FontSet& fonts, const BookErrorViewModel& vm,
                       Plane plane = Plane::Bw) override;
```

- [ ] **Step 3: Add the constant and the implementation**

In `core/src/theme_quiet.cpp`, beside the other confirm constants (around `:648-656`):

```cpp
// design/BookError.dc.html's body is `display: flex; flex-direction: column;
// gap: 12px` holding the mark and then the paragraph -- so this is the gap BELOW
// the icon, and it is the board's own number rather than the button gap reused.
constexpr int kBookErrorIconGap = 12;
```

Then, immediately after `renderDeleteConfirm`'s closing brace:

```cpp
void QuietTheme::renderBookError(Framebuffer& fb, const FontSet& fonts,
                                 const BookErrorViewModel& vm, Plane plane) {
  // No fb.clear(): the parent -- the Library, or HOME on the CONTINUE path -- is
  // already painted. Getting this wrong is a panel floating on white, and nothing on
  // the desktop can catch it, because the simulator and every golden go through
  // App::render.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const int contentW = panelContentW(kConfirmPanelW);
  const int colW = contentW - 2 * kPanelPadX;
  const Font& body = fonts[Role::Body400];
  const Icon& mark = icons::kWarning;

  // This caption is the board's fixed `CAN'T OPEN FILE` and cannot overflow, unlike
  // the confirmation's, which is a sentence with a filename in it. It goes through
  // the same wrap anyway so the two panels cannot disagree about a caption's height.
  std::string captionTail;
  Prose label = wrapPanelCaption(fonts, vm.title, contentW, WordBreak::Anywhere);

  // Wrapped once, before anything is placed: its height is what it wraps to, and the
  // panel's own bottom edge hangs off that. Two wraps would be two chances to
  // disagree, and the disagreement reads as a paragraph drifted off centre.
  const Prose prose = wrapProse(body, vm.message, colW, kConfirmProseLeadEm);
  const int proseH = f26ToPx(prose.heightF26());

  // The board's `max-height: 100%; overflow: hidden`, in the one form a firmware can
  // honour it -- renderDeleteConfirm's rule, with the mark added to the fixed part.
  const int panelFixedH = 2 * kPanelBorder + 2 * kPanelCaptionPadY + kPanelCaptionRuleH +
                          (2 * kConfirmProsePadY + mark.h + kBookErrorIconGap + proseH) +
                          (2 * kActionH + kConfirmButtonGap + kConfirmButtonPadBottom);
  const int captionRoom = fb.height() - panelFixedH;
  int maxCaptionLines = 1;
  while (maxCaptionLines < label.lineCount() &&
         f26ToPx((maxCaptionLines + 1) * label.leadF26) <= captionRoom)
    ++maxCaptionLines;
  clampProse(fonts[Role::Label500], label, maxCaptionLines, panelCaptionColumnW(contentW),
             captionTail);

  const int panelH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) +
                     (kConfirmProsePadY + mark.h + kBookErrorIconGap + proseH +
                      kConfirmProsePadY) +
                     (2 * kActionH + kConfirmButtonGap + kConfirmButtonPadBottom);

  const int x = panelLeft(fb.width(), kConfirmPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kConfirmPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, "", plane);

  cy += kConfirmProsePadY;
  // LEFT-ALIGNED at the column's own left edge, not centred: the board's body is a
  // flex COLUMN with default `align-items: stretch`, so the mark sits at the start of
  // the line box rather than in the middle of the panel.
  drawIcon(fb, mark, cx + kPanelPadX, cy, Ink::Black, plane);
  cy += mark.h + kBookErrorIconGap;

  // Left-aligned: the board's paragraph declares no `text-align`, so it is a plain
  // block -- unlike a full-screen prompt's, which is centred.
  cy += f26ToPx(drawProse(fb, body, prose, cx + kPanelPadX, colW, pxToF26(cy), Ink::Black,
                          plane, ProseAlign::Left));
  cy += kConfirmProsePadY;

  // The focused slab is filled and the other outlined, which is the boards' rule
  // wherever they pair the two. Focus starts on OK.
  drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.okLabel, vm.focusedAction == 0,
                   plane);
  cy += kActionH + kConfirmButtonGap;
  drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.deleteLabel,
                   vm.focusedAction == 1, plane);

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  drawOverlayHintBar(fb, fonts, hints, plane);
}
```

Add `#include "reader/icons.h"` to `theme_quiet.cpp` if it is not already there
(check with `grep -n 'icons.h' core/src/theme_quiet.cpp`).

`drawOverlayHintBar` is **file-local to `theme_quiet.cpp`** (defined at `:680`), not a
`components.h` primitive -- so this function must be placed after it, which putting it
beside `renderDeleteConfirm` (`:746`) already does.

- [ ] **Step 4: Build and run**

Run: `cmake -S . -B build && make test 2>&1 | tail -20`
Expected: PASS, including every case in `test_screen_book_error.cpp`.

- [ ] **Step 5: Commit Tasks 5 and 6 together**

```bash
git add core/include/reader/viewmodel.h core/include/reader/screen_book_error.h \
        core/src/screen_book_error.cpp core/include/reader/theme.h \
        core/include/reader/theme_quiet.h core/src/theme_quiet.cpp \
        test/unit/test_screen_book_error.cpp
git commit -m "feat(book-error): the dialog a book that will not open raises

openBookAt refuses with a log line and NOTHING on the panel, so Confirm on a
damaged book is a press that produces no visible change -- worse than a dead
button, because the press was correct and the file is the problem. book.h
already anticipated this screen in as many words: a refusal is \"NEVER an abort
... the caller has a screen it can put the reason on\".

Two copy shapes from a bounded BookErrorReason, never openBook's \`why\` string,
so the dialog cannot tell a reader a healthy book is damaged during the 2-25s
before pollCardPresence notices a pulled card.

The panel is renderDeleteConfirm's with a mark inserted: both boards state 380
wide, 21px 20px caption, 18px 20px body, 12px gaps and a 1.45 lead, so the
confirm constants are this board's numbers too rather than a borrowing.

Refs #5"
```

---

### Task 7: Simulator subcommands and the comparison row

**Files:**
- Modify: `sim/main.cpp`
- Modify: `tools/compare-design.py:157`
- Modify: `core/include/reader/screens.h`, `core/src/screens.cpp` (the demo facts)

- [ ] **Step 1: Add the demo facts to the factory**

In `core/include/reader/screens.h`, beside the other demo declarations:

```cpp
// design/BookError.dc.html's own book -- the `dubliners.epub` its paragraph names,
// which is also the row design/Library.dc.html draws focused, because the board
// stacks this dialog over that list.
BookErrorScreen::Facts demoBookErrorFacts();
// design/BookErrorUnreadable.dc.html: the same file, the other refusal.
BookErrorScreen::Facts demoBookErrorUnreadableFacts();
```

Add `#include "reader/screen_book_error.h"` to that header's include block.

In `core/src/screens.cpp`:

```cpp
BookErrorScreen::Facts demoBookErrorFacts() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Damaged,
          ScreenId::Library};
}

BookErrorScreen::Facts demoBookErrorUnreadableFacts() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Unreadable,
          ScreenId::Library};
}
```

- [ ] **Step 2: Add the subcommands**

In `sim/main.cpp`, beside `isDeleteConfirm`:

```cpp
  // design/BookError.dc.html and design/BookErrorUnreadable.dc.html: one screen with
  // two copy shapes. Their own subcommands rather than a flag, for the reason every
  // other state board has one -- a flag could not be named by the comparison sheet
  // or by a golden.
  const bool isBookError = std::strcmp(argv[1], "book_error") == 0;
  const bool isBookErrorUnreadable = std::strcmp(argv[1], "book_error_unreadable") == 0;
```

Follow `delete_confirm`'s own branch structure to push `Library` and then
`BookError` onto the app after priming the factory with the matching demo facts (see
Step 3), and add `'book_error', 'book_error_unreadable', ` to the usage string at
`:732-737`.

- [ ] **Step 3: Prime and build in the factory**

In `core/include/reader/screens.h`, beside `setDetailsFacts`:

```cpp
  void setBookErrorFacts(BookErrorScreen::Facts f) {
    bookErrorFacts_ = std::move(f);
    bookErrorFactsSet_ = true;
  }
  void clearBookErrorFacts() { bookErrorFactsSet_ = false; }
```

with members `BookErrorScreen::Facts bookErrorFacts_{}; bool bookErrorFactsSet_ = false;`.

In `core/src/screens.cpp`'s `create()`:

```cpp
    case ScreenId::BookError:
      // REFUSED WHEN NOTHING PRIMED IT, never substituted. A dialog naming a book the
      // reader did not try to open is how this device once woke into Middlemarch.
      // A refused push leaves the parent standing, which is wrong in a way the reader
      // can see through, and the shell logs why.
      if (!bookErrorFactsSet_) return nullptr;
      return std::make_unique<BookErrorScreen>(bookErrorFacts_);
```

- [ ] **Step 4: Add the comparison row**

In `tools/compare-design.py`, after the `book_error` row at `:157`:

```python
    ("book_error_unreadable", "BookErrorUnreadable.dc.html", "Book error (unreadable)"),
```

- [ ] **Step 5: Render both and look at them**

```bash
cmake -S . -B build && cmake --build build -j8
./build/reader_sim book_error /tmp/be.png --canvas 528x792
./build/reader_sim book_error_unreadable /tmp/beu.png --canvas 528x792
```

Open both with the Read tool. Walk the board item by item: the veil over the
Library, the panel's border, the caption's rule, the triangle, the paragraph's
wrap, both slabs with OK filled, the four hint slots. Say what you see, including
anything that looks wrong.

- [ ] **Step 6: Commit**

```bash
git add sim/main.cpp tools/compare-design.py core/include/reader/screens.h core/src/screens.cpp
git commit -m "feat(sim): render both corrupt-book shapes, and refuse an unprimed one

Two subcommands rather than a flag, for the reason every other state board has
one: a flag can be named by neither the comparison sheet nor a golden.

The factory REFUSES a BookError nothing primed rather than substituting a demo
book, which is the rule setReaderDemo and setContentsDemo already follow -- a
dialog naming a book the reader did not try to open is how this device once woke
into Middlemarch.

Refs #5"
```

---

### Task 8: Goldens, four of them

**Files:**
- Create: `test/unit/test_theme_book_error_golden.cpp`
- Create: `test/golden/book_error_480x800.png`, `book_error_528x792.png`,
  `book_error_unreadable_480x800.png`, `book_error_unreadable_528x792.png`

- [ ] **Step 1: Write the golden test**

Create `test/unit/test_theme_book_error_golden.cpp`, modelled on
`test_theme_book_end_golden.cpp`:

```cpp
// design/BookError.dc.html and design/BookErrorUnreadable.dc.html, pinned per pixel
// at both panel geometries.
//
// BOTH GEOMETRIES, because a layout that fits one can clip the other and the X3 is
// the dev device. This panel is 380 wide on a 480 canvas -- 50px of margin either
// side on the X4 against 74 on the X3 -- and its paragraph wraps against a column
// that is the same width on both, so the two differ in where the panel is CENTRED
// and in how much veil surrounds it.
//
// BOTH SHAPES, because the only difference between them is a sentence, and a
// sentence is exactly what a structural test cannot see: the two wrap to different
// heights, which moves the panel, both slabs and the whole centring. Only a pixel
// catches that.
#include <memory>
#include <string>

#include "doctest.h"
#include "golden.h"
#include "ramp.h"
#include "reader/framebuffer.h"
#include "reader/screen_book_error.h"
#include "reader/screens.h"
#include "reader/theme_quiet.h"

TEST_CASE("QuietTheme renders both corrupt-book shapes to golden at both geometries") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;

  // The fidelity is asserted, not assumed: this test names a plane, and if the
  // screen's declared path ever moves, the golden must stop matching rather than
  // quietly keep pinning a path nothing paints. BookError takes the default
  // Fidelity::Mono, so the golden is the single 1-bit frame the panel is handed.
  REQUIRE(reader::BookErrorScreen(reader::demoBookErrorFacts()).fidelity() ==
          reader::Fidelity::Mono);

  auto renderOne = [&](reader::BookErrorScreen::Facts facts, int w, int h,
                       const std::string& name) {
    reader::DemoScreenFactory factory;
    factory.setBookErrorFacts(std::move(facts));
    // App takes an OWNED root screen, not an id -- App(std::unique_ptr<Screen>,
    // ScreenFactory&). The Library is the parent the board draws under the veil.
    std::unique_ptr<reader::Screen> root = factory.create(reader::ScreenId::Library);
    REQUIRE(root != nullptr);
    reader::App app(std::move(root), factory);
    REQUIRE(app.pushScreen(reader::ScreenId::BookError));

    reader::Framebuffer fb(w, h);
    // App::render, NEVER top().render -- an overlay rendered on its own is a panel
    // floating on white, and nothing on the desktop catches it because the simulator
    // and every golden go through App::render. It has happened once.
    app.render(fb, ramp.fonts(), theme, reader::Plane::Bw);
    golden::checkGolden(fb, name);
  };

  renderOne(reader::demoBookErrorFacts(), 480, 800, "book_error_480x800");
  renderOne(reader::demoBookErrorFacts(), 528, 792, "book_error_528x792");
  renderOne(reader::demoBookErrorUnreadableFacts(), 480, 800,
            "book_error_unreadable_480x800");
  renderOne(reader::demoBookErrorUnreadableFacts(), 528, 792,
            "book_error_unreadable_528x792");
}
```

- [ ] **Step 2: Run to produce candidates**

Run: `cmake -S . -B build && make test 2>&1 | grep -A 3 book_error`
Expected: FAIL, four times, each naming a `build/<name>_candidate.png`.

- [ ] **Step 3: Inspect every candidate with the Read tool**

Open all four. For each, walk the board: veil density (clustered white on a 3px grid
-- a 4px veil reads as a smudge), the panel's 2px border, the caption's rule, the
triangle's three diagonals (the one thing `Mono` treats worst -- say whether it still
reads as a warning mark), the paragraph's break positions, OK filled and DELETE FILE...
outlined, four hint slots with the empty-slot rule.

**Describe honestly what you see, including anything that looks wrong even if you go
on to bless it.** An icon has passed review here twice while reading as the letters
"OC".

- [ ] **Step 4: Bless only after inspecting**

```bash
for n in book_error_480x800 book_error_528x792 \
         book_error_unreadable_480x800 book_error_unreadable_528x792; do
  cp "build/${n}_candidate.png" "test/golden/${n}.png"
done
make test 2>&1 | tail -5
```
Expected: PASS.

- [ ] **Step 5: Prove the goldens bite**

Mutate and confirm a real failure — the check that a golden is worth having.
**Commit first**, then edit; never `git checkout` to undo a mutation in a file whose
change is uncommitted, which reverts the work as well as the mutation.

```bash
git add -A && git commit -m "wip: goldens before mutation check"
cp core/src/theme_quiet.cpp /tmp/tq.bak
# drop the icon gap, so the paragraph rides up 12px
sed -i '' 's/constexpr int kBookErrorIconGap = 12;/constexpr int kBookErrorIconGap = 0;/' core/src/theme_quiet.cpp
make test 2>&1 | grep -c "book_error"
```
Expected: non-zero — the goldens fail. Then restore and confirm green:

```bash
cp /tmp/tq.bak core/src/theme_quiet.cpp && touch core/src/theme_quiet.cpp
make test 2>&1 | tail -3
```
Expected: PASS. (`touch` matters: `cp` and the previous compile inside the same
second leave make thinking the object is current, so the fixed source tests as
though it were still mutated.)

- [ ] **Step 6: Commit**

```bash
git add test/unit/test_theme_book_error_golden.cpp test/golden/book_error*.png
git commit -m "test(book-error): goldens at both geometries for both copy shapes

Four rather than two, because the shapes differ only by a sentence -- and a
sentence is what a structural test cannot see: the two wrap to different heights,
which moves the panel, both slabs and the whole centring.

Proved by mutation: dropping kBookErrorIconGap fails them.

Refs #5"
```

---

### Task 9: `DeleteConfirmScreen` takes facts, and the delete becomes a latch

The largest and riskiest task: a shipped, tested screen gains a second parent.

**Files:**
- Modify: `core/include/reader/app.h` (`Action::Kind::Delete`, the latch)
- Modify: `core/src/app.cpp` (dispatch)
- Modify: `core/include/reader/screen_delete_confirm.h`, `core/src/screen_delete_confirm.cpp`
- Modify: `test/unit/test_screen_delete_confirm.cpp`

- [ ] **Step 1: Write the failing tests**

Rewrite `test/unit/test_screen_delete_confirm.cpp`'s construction to use facts, and
add:

```cpp
TEST_CASE("the confirmation names the book from its facts, not from a Library") {
  reader::DeleteConfirmScreen s(
      {"/books/dubliners.epub", "Dubliners", reader::ScreenId::Library});
  CHECK(s.vm().title == "DELETE \xE2\x80\x9CDUBLINERS\xE2\x80\x9D?");
}

TEST_CASE("confirming latches the delete rather than doing it") {
  // The removal is the SHELL's: forgetCardFacts, the Library's rescan, gHomeStale
  // and gLibraryStale all live there, and core/ has no filesystem. Shaped like
  // Open/Retry/Finish for that reason.
  reader::DeleteConfirmScreen s(
      {"/books/dubliners.epub", "Dubliners", reader::ScreenId::Library});
  REQUIRE(s.onGesture({reader::Gesture::Next}).kind != reader::Action::Kind::None);
  REQUIRE(s.focus() == 1);
  const reader::Action a = s.onGesture({reader::Gesture::Activate});
  CHECK(a.kind == reader::Action::Kind::Delete);
}

TEST_CASE("the app latches a delete request") {
  reader::DemoScreenFactory factory;
  std::unique_ptr<reader::Screen> root = factory.create(reader::ScreenId::Library);
  REQUIRE(root != nullptr);
  reader::App app(std::move(root), factory);
  CHECK_FALSE(app.deleteRequested());
  app.dispatch(reader::Action{reader::Action::Kind::Delete, reader::ScreenId::Home});
  CHECK(app.deleteRequested());
  app.clearDeleteRequest();
  CHECK_FALSE(app.deleteRequested());
}

TEST_CASE("where a completed delete returns to comes from the facts") {
  // The whole reason for Facts: BookError over Home's CONTINUE has no Library.
  CHECK(reader::DeleteConfirmScreen({"/b/x.epub", "X", reader::ScreenId::Home})
            .facts().returnTo == reader::ScreenId::Home);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build -j8 2>&1 | tail -10`
Expected: FAIL — no matching constructor, and `Kind::Delete` undeclared.

- [ ] **Step 3: Add the Action kind and the latch**

In `core/include/reader/app.h`, extend the enum:

```cpp
  enum class Kind : uint8_t { None, Redraw, Push, Pop, PopTo, Sleep, Retry, Open, Finish, Delete };
```

and add beside `Action::finish()`:

```cpp
  // "Remove the book this confirmation names." A latch like Retry, Open and Finish,
  // and for their reason: the card is the shell's. It carries no path for the reason
  // Open carries none -- a std::string in every Action, returned by value from every
  // gesture on every screen, to serve one. The shell reads the path off the screen
  // that is still on top when the dispatch runs.
  static Action del() { return {Kind::Delete, ScreenId::Home}; }
```

and beside `clearFinishRequest()`:

```cpp
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
```

with `bool delete_ = false;` beside the other latch members.

In `core/src/app.cpp`'s `dispatch`, beside `Kind::Finish`:

```cpp
    case Action::Kind::Delete:
      // Latched for the reason Retry, Open and Finish are: the card is the shell's.
      delete_ = true;
      break;
```

- [ ] **Step 4: Move DeleteConfirmScreen to facts**

In `core/include/reader/screen_delete_confirm.h`, replace the `LibraryScreen`
forward declaration, the constructor and the `library_` member with:

```cpp
class DeleteConfirmScreen : public FocusScreen {
 public:
  // FACTS, NOT A REFERENCE. This screen used to hold a LibraryScreen& and act
  // through deleteFocused(), which made it unreachable from Home's CONTINUE -- and
  // CONTINUE is the likeliest real corruption path, because it is a book the reader
  // was part-way through. BookDetailsScreen::Facts solved the identical problem for
  // the identical reason.
  //
  // Spec 4.0: delete "has a confirmation step and never erases reading progress".
  // The shell removes exactly one file and touches /.reader/state/ not at all.
  struct Facts {
    std::string path;         // absolute on the filesystem
    std::string displayName;  // for the caption
    // Where to land once the file is gone. From the actions panel that is the
    // Library; from BookError it is the Library OR Home, which is the case a
    // LibraryScreen& could not express.
    ScreenId returnTo = ScreenId::Library;
  };

  explicit DeleteConfirmScreen(Facts facts);
  const Facts& facts() const { return facts_; }
  // KEEP EXACTLY AS THEY ARE, all six: id(), isOverlay(), onGesture(), render(),
  // vm() and paintFootprint(). Only the constructor, the facts() accessor and the
  // members below change; paintFootprint()'s constant 1 and its comment are still
  // right, because nothing about what this panel draws has moved.

 private:
  enum Row { kCancel = 0, kDelete, kRowCount };
  void syncVm() override;
  Facts facts_;
  DeleteConfirmViewModel vm_;
};
```

In `core/src/screen_delete_confirm.cpp`, replace the constructor's name lookup:

```cpp
DeleteConfirmScreen::DeleteConfirmScreen(Facts facts)
    : FocusScreen(kRowCount, kRowCount), facts_(std::move(facts)) {
  const std::string name = upperLatin1(facts_.displayName);
  vm_.title = "DELETE \xE2\x80\x9C" + name + "\xE2\x80\x9D?";
```

and replace the `Gesture::Activate` branch's body:

```cpp
    case Gesture::Activate:
      if (vm_.focusedAction == kCancel) return Action::pop();
      // LATCHED, not done here. The removal's consequences are all the shell's --
      // forgetCardFacts, the Library's rescan, gHomeStale and gLibraryStale -- and
      // core/ has no filesystem. The shell reads the path off this screen while it
      // is still on top, then the dispatch pops to `returnTo`.
      //
      // The result is deliberately not branched on: FileSystem::remove reports the
      // END STATE, so a false means the file is still there and the list the reader
      // lands on already says so.
      return Action::del();
```

Delete the now-unused `#include "reader/screen_library.h"`.

- [ ] **Step 5: Make the shell's pop happen**

`Action::del()` latches but does not pop. The shell pops with the same
`popTo(facts().returnTo)` it reads the path from — that is Task 11, Step 3. Add a
note in `screen_delete_confirm.cpp` beside the `Action::del()` return saying so, so
the missing pop is not read as a bug.

- [ ] **Step 6: Update the factory's DeleteConfirm case**

In `core/src/screens.cpp`, replace `:339-341`:

```cpp
    case ScreenId::DeleteConfirm:
      // THE FACTS ARE CHECKED FIRST, and the `library_ == nullptr` guard that used to
      // sit above this line is GONE WITH THE REFERENCE. Leaving it would repeat the
      // exact defect recorded on BookDetails below: a change that replaces the
      // `return` and not the GUARD leaves the case refused for the very reason it
      // was meant to stop refusing. WHEN A CASE'S EARLY RETURN ENCODES AN ASSUMPTION
      // A CHANGE REMOVES, THE GUARD IS PART OF THE CHANGE.
      if (deleteFactsSet_) return std::make_unique<DeleteConfirmScreen>(deleteFacts_);
      // The Library is the fallback, and it is what the simulator and the goldens
      // use: it can answer both facts from its focused row.
      if (library_ == nullptr) return nullptr;
      {
        const LibraryItem* item = library_->focusedItem();
        if (item == nullptr || item->entry.isDir) return nullptr;
        std::string p = library_->path();
        if (p.empty() || p.back() != '/') p += '/';
        p += item->entry.name;
        return std::make_unique<DeleteConfirmScreen>(
            DeleteConfirmScreen::Facts{p, item->entry.title(), ScreenId::Library});
      }
```

with `setDeleteFacts`/`clearDeleteFacts` and members mirroring `setDetailsFacts`.

- [ ] **Step 7: Run**

Run: `make test 2>&1 | tail -20`
Expected: PASS, including `test_partial_repaint.cpp`'s every-ordered-pair walk over
both overlays, which must be untouched.

- [ ] **Step 8: Commit**

```bash
git add core/include/reader/app.h core/src/app.cpp \
        core/include/reader/screen_delete_confirm.h core/src/screen_delete_confirm.cpp \
        core/include/reader/screens.h core/src/screens.cpp \
        test/unit/test_screen_delete_confirm.cpp
git commit -m "refactor(delete-confirm): facts instead of a Library, and the delete becomes a latch

It held a LibraryScreen& and acted through deleteFocused(), so it was unreachable
from Home's CONTINUE -- and CONTINUE is the likeliest real corruption path,
because it is a book the reader was part-way through. Leaving DELETE FILE... dead
there would be a button that works only sometimes, which is worse than one that
never works, because nobody can learn the rule.

BookDetailsScreen::Facts solved the identical problem for the identical reason.
The removal joins Open/Retry/Finish as a latch, because a delete's consequences
-- forgetCardFacts, the rescan, gHomeStale, gLibraryStale -- are all the shell's.

The factory's \`library_ == nullptr\` guard goes WITH the reference. Keeping it is
the exact defect BookDetails records two cases below: a change that replaces the
return and not the guard leaves the case refused for the reason it was meant to
stop refusing.

Refs #5"
```

---

### Task 10: Raise the dialog from the shell

**Files:**
- Modify: `shell/src/main.cpp:2389-2410` (`openBookAt`'s refusal)

- [ ] **Step 1: Raise it on a user press only**

In `openBookAt`, replace the refusal block's `return false;` with:

```cpp
    logf("[open] %s REFUSED: %s (heap %u free, largest block %u)\n", path.c_str(),
         why, (unsigned)ESP.getFreeHeap(),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    logFlush();
    // A REFUSAL THE READER ASKED FOR GETS A SCREEN; A REFUSAL ON THE WAKE DOES NOT.
    // `push` is false only for the session restore, where App::restore already stops
    // short of a Reader it cannot build and leaves Home or the Library standing --
    // wrong in a way the reader can see through. Waking into a modal about a book
    // nobody just asked for replaces a calm landing with an interruption, seconds
    // after pressing power and with no context for it.
    if (push && gApp != nullptr) {
      // WHICH REFUSAL, in the only vocabulary the screen has. openBook's `why` is
      // developer English and stays in the log; what reaches glass is one of two
      // bounded shapes, because "cannot open the book file" is a file that is gone
      // or a card that is -- and openRead does not call noteCardGone(), so
      // pollCardPresence takes 2-25s to notice. Telling the reader a healthy book is
      // damaged for that whole window would be a false claim, which this firmware
      // refuses elsewhere for the battery gauge and the charging bolt.
      const bool unreadable = (why != nullptr && std::strcmp(why, "cannot open the book file") == 0);
      // The leaf name, not the path: the board's paragraph quotes a filename.
      const size_t slash = path.find_last_of('/');
      const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
      // WHERE A DELETE RETURNS TO is decided here, because this is the one place that
      // knows which screen asked. Home's CONTINUE has no Library to go back to.
      const reader::ScreenId returnTo = gApp->top().id() == reader::ScreenId::Home
                                            ? reader::ScreenId::Home
                                            : reader::ScreenId::Library;
      gFactory.setBookErrorFacts({path, leaf,
                                  unreadable ? reader::BookErrorReason::Unreadable
                                             : reader::BookErrorReason::Damaged,
                                  returnTo});
      if (!gApp->pushScreen(reader::ScreenId::BookError))
        logf("[open] ...and the dialog would not build\n");
    }
    return false;
```

Add `#include <cstring>` to `shell/src/main.cpp` if absent (check:
`grep -n '#include <cstring>' shell/src/main.cpp`). Note CLAUDE.md's record that a
missing `<cstring>` compiled on macOS for months and failed on the first Linux build.

- [ ] **Step 2: Build the firmware — the ESP32 toolchain is stricter**

Run: `make firmware 2>&1 | tail -20`
Expected: SUCCESS. If `PackageException: not a directory`, run
`git submodule update --init` first — a fresh worktree has an empty `freeink-sdk/`.
If "Failed to install Python dependencies into penv", retry; it is transient, and do
not run two builds at once.

- [ ] **Step 3: Commit**

```bash
git add shell/src/main.cpp
git commit -m "feat(shell): put the refusal on the panel instead of only in the log

openBookAt refused with a log line and NOTHING on glass, so Confirm on a book
that will not open was a press that produced no visible change -- on a device
where that is indistinguishable from a broken button.

Only when \`push\` is true, which is a user press. The wake restore is excluded:
it already stops short of a Reader it cannot build and leaves Home standing, and
waking into a modal about a book nobody just asked for is worse.

Which of the two copy shapes is decided HERE, because this is the one place that
knows both the reason and which screen asked -- and so is returnTo, because
Home's CONTINUE has no Library to go back to.

Refs #5"
```

---

### Task 11: Answer the delete latch

**Files:**
- Modify: `shell/src/main.cpp` (`loop()`, beside `openRequested()` at `:6031`)

- [ ] **Step 1: Write the handler**

Above `loop()`, beside `handleOpen`:

```cpp
// THE PATH IS READ WHILE DeleteConfirm IS STILL ON TOP, because the pop below is what
// takes it away and after that there is no screen left to ask. Contents' chosenSpine()
// has exactly this shape and for exactly this reason.
static void handleDelete() {
  gApp->clearDeleteRequest();  // first, so a failed removal does not re-fire

  const reader::Screen& top = gApp->top();
  if (top.id() != reader::ScreenId::DeleteConfirm) {
    logf("[delete] latched with no confirmation on top\n");
    return;
  }
  const reader::DeleteConfirmScreen::Facts facts =
      static_cast<const reader::DeleteConfirmScreen&>(top).facts();

  // THE RESULT IS NOT BRANCHED ON. FileSystem::remove reports the END STATE, so a
  // false means the file is still there -- and the list the reader is about to be
  // looking at has just been rescanned and already says which it was. An error panel
  // would be a screen with no board saying what the Library already shows.
  const bool gone = gSd.remove(facts.path);
  logf("[delete] %s -> %s\n", facts.path.c_str(), gone ? "gone" : "still there");
  logFlush();

  // BOTH FLAGS, SEPARATELY. Each is consumed when ITS screen is reachable, and one
  // shared flag lets Library, Back, Home clear it before Home has used it. Home's
  // CONTINUE may name the file just removed, and its LIBRARY count is keyed on
  // removals() -- which gSd.remove has just advanced.
  gHomeStale = true;
  gLibraryStale = true;
  if (reader::LibraryScreen* lib = gFactory.library(); lib != nullptr) lib->rescan();

  // Down to whatever asked. From the actions panel that is the Library; from
  // BookError it may be Home, and the BookError under this confirmation goes too --
  // it names a book that no longer exists. One Action, one screen change, however
  // deep the flow was.
  gApp->dispatch(reader::Action::popTo(facts.returnTo));
}
```

- [ ] **Step 2: Call it**

Beside `if (gApp->openRequested()) handleOpen();` at `:6031`:

```cpp
    if (gApp->deleteRequested()) handleDelete();
```

- [ ] **Step 3: Verify the pop lands and Home rebuilds**

Run: `grep -n "gHomeStale" shell/src/main.cpp | head -5`
Confirm `gHomeStale` is consumed where Home becomes reachable (the `buildHomeApp`
path). If a delete from BookError-over-Home does not rebuild Home, the CONTINUE block
still names the removed file — Home does check `last.json` against the card with an
`exists` call before drawing, so the failure is a fallback to the nothing-open variant
rather than a lie, but the LIBRARY count would stay wrong.

- [ ] **Step 4: Build both**

Run: `make test 2>&1 | tail -5 && make firmware 2>&1 | tail -5`
Expected: both SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add shell/src/main.cpp
git commit -m "feat(shell): answer the delete latch, from either parent

Reads the path off DeleteConfirm WHILE IT IS STILL ON TOP, because the pop is
what takes it away -- Contents' chosenSpine() has the same shape for the same
reason.

Sets gHomeStale and gLibraryStale separately, because each is consumed when its
own screen is reachable and one shared flag lets Library, Back, Home clear it
before Home has used it. Home's CONTINUE may name the file just removed.

Refs #5"
```

---

### Task 12: Compare, document, and hand over

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/superpowers/plans/2026-08-20-v1-roadmap.md`
- Modify: `docs/on-device-smoke-checklist.md`

- [ ] **Step 1: Compare against both boards**

```bash
make compare COMPARE_ARGS="--only book_error"
make compare COMPARE_ARGS="--only book_error_unreadable"
```
Expected: both render. Remember `make compare` prints `ok`, **not** a percentage
(#41), so `ok` means the simulator produced a frame and nothing more. Measure the real
figure over the exported panel PNGs:

```bash
make compare COMPARE_ARGS="--only book_error --export build/overlay"
```

- [ ] **Step 2: Run the full gate**

```bash
make test && make sim && make firmware && make conventions
```
Expected: all four green.

- [ ] **Step 3: Document in CLAUDE.md**

Add `Book error` to the screen table in **The chrome screens**:

```
| Book error | `BookError.dc.html` | An overlay over the Library **or Home** — the only overlay whose parent is not a list. Two copy shapes, because one of its four refusals is not damage. |
```

Add a short subsection recording: the silent-refusal defect it closes; that the
reason is bounded rather than `openBook`'s string; that `openRead` does not call
`noteCardGone`, which is why the second shape exists; that the wake is excluded; and
that `DeleteConfirmScreen` now takes facts. Update the **Storage** section's #42
paragraph to say it is CLOSED and how.

- [ ] **Step 4: Update the roadmap and the smoke checklist**

Strike `corrupt-book dialog` from the 3C-and-beyond scope line at `roadmap:790`, in
the style the typography entry uses (`~~...~~ **DONE 2026-09-06 — see CLAUDE.md ...**`).
Add the four glass questions from the spec to `docs/on-device-smoke-checklist.md`.

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md docs/superpowers/plans/2026-08-20-v1-roadmap.md \
        docs/on-device-smoke-checklist.md
git commit -m "docs(book-error): what the dialog closes, and the claim it must not make

Refs #5"
```

- [ ] **Step 6: Move the card to On glass — NOT Done**

```bash
gh project item-edit --id PVTI_lAHOAkvc3c4BhZ5gzg36kNU --project-id PVT_kwHOAkvc3c4BhZ5g \
  --field-id PVTSSF_lAHOAkvc3c4BhZ5gzhgVwC4 --single-select-option-id 5012a8f7
```

Then VERIFY — `item-edit` prints a GraphQL error and still exits 0:

```bash
gh project item-list 1 --owner Rukkaitto --format json --limit 100 \
  | python3 -c "import json,sys;[print(i.get('status')) for i in json.load(sys.stdin)['items'] if i.get('content',{}).get('number')==5]"
```
Expected: `On glass`.

**Do NOT write `Closes #5`** in any commit or PR. `On glass` -> `Done` needs device
evidence, flashing must be run by the user, and "the tests pass" is not evidence.
#42 is different — it has no hardware surface, so `Closes #42` is correct there.

- [ ] **Step 7: Hand the flash to the user**

Give them the command and name what only the panel can answer (from the spec):
the warning triangle's three diagonals under `Mono`; the dialog appearing on a real
refusal (a truncated `.epub` on the card); `DELETE FILE...` from Home's CONTINUE; and
the veil over Home, which no board draws.

---

## Self-review

**Spec coverage:** raised-on-press-only (Task 10); two copy shapes (Tasks 3, 5, 8);
bounded reason (Task 5); own board (Task 3); `kWarning` (Task 4); overlay/Mono/focus/
hints/footprint (Task 5); `DeleteConfirmScreen` facts + latch + `returnTo` +
`gHomeStale` (Tasks 9, 11); #42 sentinel with `Count` refused everywhere (Task 2);
sim + compare (Task 7); four goldens (Task 8); glass questions (Task 12).

**Type consistency:** `BookErrorScreen::Facts{path, displayName, reason, returnTo}` and
`DeleteConfirmScreen::Facts{path, displayName, returnTo}` are used with those field
names and that order in Tasks 5, 7, 9, 10 and 11. `BookErrorReason::{Damaged,
Unreadable}`, `Action::del()`/`Kind::Delete`, `deleteRequested()`/`clearDeleteRequest()`,
`setBookErrorFacts`/`clearBookErrorFacts`, `setDeleteFacts`/`clearDeleteFacts` and
`kBookErrorIconGap` are each defined once and spelled the same everywhere after.

**Known gap, deliberately left to the implementer:** Task 7 Step 2 says "follow
`delete_confirm`'s own branch structure" rather than quoting `sim/main.cpp`'s dispatch,
because that block is long, repetitive and its shape is set by the surrounding code
rather than by this screen. Task 7 Step 5 renders both screens, which is what proves
the branch is right.
