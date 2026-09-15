# Articles over wallabag — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A reader whose wallabag credentials sit in `/.reader/wallabag.json` presses
`ARTICLES` on Home, presses `Sync now`, and reads their unread articles offline as
EPUBs — archiving and starring from the device, with those actions pushed on the next
sync.

**Architecture:** Five cards, one feature, four shippable phases after a design pass.
The screens are built first on fixture data, because every one of them is a shape this
firmware already draws (the Library, `ItemActions`, `BookEnd`, `WifiConnect`,
`WifiError`, Settings). The card-side store comes second and is pure `core/` over
`FileSystem`: a per-article metadata sidecar in the flat JSON `settings.json` already
uses, marker files for the offline queue, and the credentials file with its boot seed.
The wallabag client comes third, entirely in `core/` against an injected, poll-shaped
`HttpTransport` — `WifiRadio`'s seam exactly — with a bounded pull JSON scanner for
the listing, because the existing parser cannot read arrays. The shell arrives last
and owns only what the desktop cannot test: the Arduino transport, a card file sink
that streams an EPUB to the card, the NVS token store, and the loop that brings Wi-Fi
up around a sync. An article is read by `openBook` with nothing new below it — proved
against a real export in `docs/notes/wallabag-api.md` §6.

**Tech Stack:** C++20 (`core/`, no Arduino/ESP/host-OS), doctest, CMake, the
`.dc.html` design boards, `tools/compare-design.py`, `make canvas`, PlatformIO for the
ESP32-C3 shell, arduino-esp32's `HTTPClient`/`WiFiClient`, `Preferences` (NVS), SdFat.

**Spec:** `docs/notes/wallabag-api.md` (the decision and the API surface, §2 and §5
are the load-bearing sections). Cards: #108 (parent), #111, #112, #113, #114, #140,
#141. #124 is the board pass and is done except for what Phase 0 adds.

**Read first:** `CLAUDE.md` sections *The rule that governs UI work*, *The rule that
governs COPY*, *Overlays and lists*, *Storage*, *What the shell owes a flow*, *The
board*, and the whole of *Editing this repo with scripts*. Then
`docs/superpowers/specs/2026-09-11-wifi-connect-flow-design.md` sections *The flow,
and the stack*, *The radio seam* and *Testing* — this feature is that flow's shape one
subsystem over, and every decision below that looks arbitrary is copied from there on
purpose.

**Three rules that apply to every task and are not repeated in each:**

1. **A screen change goes into the board first.** If a task makes you want to move a
   pixel, stop, change the `.dc.html`, render it at both geometries, then follow it.
2. **Every scripted edit follows *Editing this repo with scripts***: read fully, mutate
   in memory, assert the anchor is present and unique, write once, check `git diff
   --stat`, grep for a marker from the new text. An anchor is not what you remember
   writing.
3. **`make test` is the gate before every commit, and the goldens are never
   re-blessed to make it green.** A failing golden writes `build/<name>_candidate.png`;
   look at it and say what you see before blessing. **CMake uses `file(GLOB ...)`**, so
   run `cmake -S . -B build` after creating any source file.

---

## What this plan does NOT do, and why

- **No HTML tokenizer.** `export.epub` makes the article a file this reader already
  reads. The `content`-field fallback stays off the bill unless a real export fails
  `docs/notes/wallabag-api.md` §7's three checks.
- **No ages anywhere.** The boards say `WALLABAG · 2 H AGO` and `Last sync · 2 H AGO`
  and **this device has no clock** (#132, closed with the verification). A deep sleep
  on battery is a full power-down, so nothing can measure elapsed time across one, and
  the server's `Date` header is the server's clock in GMT. Phase 0 replaces every age
  on the boards with the last sync's *outcome*, which is a fact the device holds.
- **No TLS work until the probe says whether it is needed.** A self-hosted instance
  on the LAN is plain HTTP; `wallabag.it` is not. Phase 4 measures both before the
  transport is finished, and the plan states what a failing TLS probe changes.
- **No Wi-Fi joining from the Articles flow.** A sync uses the saved network marked
  `AUTO` and nothing else. If there is none, the flow says so and sends the reader to
  Settings — one new copy shape, boarded in Phase 0. Joining is #107's, and it is done.
- **Articles are never in the Library.** They live under `/.reader/articles/` and the
  Library lists `/books`. An archived article's file is removed by the sync, and the
  Library's delete never sees it.

---

## File structure

| File | Responsibility | New? |
|---|---|---|
| `design/Settings.dc.html` | a `wallabag` row under CONNECTIONS, the door to the account screen | modify |
| `design/WallabagErrorNoNetwork.dc.html` | the third failure shape: no saved network to sync over | create |
| `design/WallabagFetching.dc.html` | the connecting dialog's second stage, a count of files fetched | create |
| `design/Articles.dc.html`, `design/SyncDone.dc.html`, `design/WallabagAccount.dc.html`, `design/ArticleEnd.dc.html` | stamps and rows reworded for a device with no clock | modify |
| `design/ArticlesRemoveConfirm.dc.html` | the confirmation `Remove downloaded articles…` opens | create |
| `design/canvas.json` | artboard entries for the three new boards | modify |
| `core/include/reader/app.h` | six `ScreenId`s appended before `Count`; `Action::Kind::Article` and `Action::article()`; the latch getters | modify |
| `core/src/app.cpp` | six `kRestorability` rows; `screenUsesRadio` cases; `screenName` cases; the latch in `dispatch` | modify |
| `core/src/session_record.cpp` | six wire names | modify |
| `core/include/reader/viewmodel.h` | `ArticlesViewModel`, `ArticleActionsViewModel`, `ArticleEndViewModel`, `WallabagAccountViewModel`, `WallabagConnectingViewModel`, `WallabagErrorViewModel` | modify |
| `core/include/reader/screen_articles.h`, `core/src/screen_articles.cpp` | the list, its not-set-up variant and its sync stamp | create |
| `core/include/reader/screen_article_actions.h`, `core/src/screen_article_actions.cpp` | the Archive / Star overlay | create |
| `core/include/reader/screen_article_end.h`, `core/src/screen_article_end.cpp` | the end-of-article screen | create |
| `core/include/reader/screen_wallabag_account.h`, `core/src/screen_wallabag_account.cpp` | the account screen | create |
| `core/include/reader/screen_wallabag_connecting.h`, `core/src/screen_wallabag_connecting.cpp` | the in-flight dialog, two stages | create |
| `core/include/reader/screen_wallabag_error.h`, `core/src/screen_wallabag_error.cpp` | the failure dialog, three shapes | create |
| `core/include/reader/theme.h`, `core/src/theme_quiet.cpp` | one `render*` virtual per new screen | modify |
| `core/include/reader/screens.h`, `core/src/screens.cpp` | factory cases, priming setters, demo view-models, `demoHomeTargets` | modify |
| `core/include/reader/screen_home.h`, `core/src/screens.cpp` | the `ARTICLES` menu row (#141) | modify |
| `core/include/reader/screen_reader.h`, `core/src/screen_reader.cpp` | which end screen a last page turns into | modify |
| `core/include/reader/settings.h`, `core/src/settings.cpp` | `articlesKeepOffline` | modify |
| `core/include/reader/wallabag_credentials.h`, `core/src/wallabag_credentials.cpp` | `/.reader/wallabag.json`: parse, seed, configured-or-not | create |
| `core/include/reader/article_store.h`, `core/src/article_store.cpp` | the article directory: metadata sidecars, the queue's marker files, the watermark, pruning | create |
| `core/include/reader/json_stream.h`, `core/src/json_stream.cpp` | a bounded pull scanner over a `ByteSource` for nested JSON | create |
| `core/include/reader/http_transport.h` | the injected, poll-shaped transport interface and the `BodySink` interface | create |
| `core/include/reader/wallabag_client.h`, `core/src/wallabag_client.cpp` | request building, the token exchange and refresh-on-401, response classification | create |
| `core/include/reader/sync_engine.h`, `core/src/sync_engine.cpp` | the sync as a poll-driven state machine over client + store | create |
| `core/include/reader/token_store.h` | the interface the shell's NVS store implements | create |
| `sim/main.cpp` | one subcommand per new board state | modify |
| `tools/compare-design.py` | one `FLOW_SCREENS` row per new board state | modify |
| `test/unit/fake_http_transport.h` | scripted responses, injectable failures, a recorded request log | create |
| `test/unit/test_screen_articles.cpp`, `test_screen_article_actions.cpp`, `test_screen_article_end.cpp`, `test_screen_wallabag_account.cpp`, `test_wallabag_dialogs.cpp` | the screens' behaviour | create |
| `test/unit/test_theme_articles_golden.cpp` | every boarded state at both geometries | create |
| `test/unit/test_article_outcomes.cpp` | the latch contract through a real `App::dispatch` | create |
| `test/unit/test_wallabag_credentials.cpp`, `test_article_store.cpp`, `test_json_stream.cpp`, `test_wallabag_client.cpp`, `test_sync_engine.cpp` | the engine | create |
| `test/unit/test_focus_restore.cpp`, `test_session_record.cpp`, `test_screen_home.cpp`, `test_home_rebuild.cpp` | catalogue rows, counts, the third menu row | modify |
| `shell/src/http_transport_arduino.h`, `shell/src/http_transport_arduino.cpp` | the real transport over `HTTPClient` | create |
| `shell/src/card_file_sink.h`, `shell/src/card_file_sink.cpp` | a `BodySink` that streams to a card file over SdFat, `.part` then rename | create |
| `shell/src/wallabag_store_nvs.h`, `shell/src/wallabag_store_nvs.cpp` | the token store, `wifi_store_nvs`'s shape | create |
| `shell/src/main.cpp` | the credentials seed at mount; the sync driver in `loop()`; `handleArticle()`; `openBookAt` from the Articles list; Home's count; the restore walk | modify |
| `docs/on-device-smoke-checklist.md` | §13, Articles | modify |
| `CLAUDE.md` | the chrome-screens table rows and one section on the sync | modify |

---

## Phase 0 — The design decisions the boards do not yet make

Every task here is board-only. Nothing compiles. Each ends with `make canvas`,
`make canvas-check`, and a render at both geometries through
`tools/compare-design.py`'s `render_board` (the way the Wallabag boards were reviewed
in `65b1ecd`), with the render looked at. Every new or changed string goes through
`/humanizer` before it is committed — *The rule that governs COPY*.

### Task 0.1: Ages become outcomes on four boards

**Why:** the device has no clock (#132). `2 H AGO` is a claim it cannot make.

**Files:** `design/Articles.dc.html`, `design/SyncDone.dc.html`,
`design/WallabagAccount.dc.html`, `design/ArticleEnd.dc.html`

- [ ] **Step 1:** In `Articles.dc.html`, change the sync row's stamp `WALLABAG · 2 H AGO`
  to `WALLABAG · UP TO DATE`. This is the specimen for a device whose last sync
  found nothing new. Add a note above the row stating the four values the stamp can
  take and that they are outcomes, not ages: `NEVER SYNCED` (no sync has ever
  completed on this card), `UP TO DATE` (the last sync fetched nothing), `3 NEW` (the
  last sync fetched articles — the count), `FAILED` (the last sync did not finish).
  State #132 as the reason an age is impossible.
- [ ] **Step 2:** In `SyncDone.dc.html`, change `WALLABAG · NOW` to `WALLABAG · 3 NEW`,
  and the status line `SYNC COMPLETE · 3 NEW ARTICLES · 1 ARCHIVE PUSHED` stays —
  it is the one place the push count is stated. Note that `SyncDone` is the same
  screen as `Articles` with the stamp's `N NEW` value and the status line, so it is a
  **variant**, not a `ScreenId`.
- [ ] **Step 3:** In `WallabagAccount.dc.html`, change `Last sync · 2 H AGO` to
  `Last sync · UP TO DATE`, and note the same four values. Change `Unread · 3 ARTICLES`
  to keep as is — that is a count the device holds.
- [ ] **Step 4:** In `ArticleEnd.dc.html`, confirm `2 LEFT` in the band's right slot is
  the count of unread articles remaining on the card after this one, and add a note
  saying so; the value is drawn from the store, never derived from the server.
- [ ] **Step 5:** Render all four at 480×800 and 528×792; check no stamp wraps or
  collides with the `Sync now` label. `WALLABAG · NEVER SYNCED` is the widest value
  — measure it in Chrome against the row's remaining width the way `65b1ecd`
  measured the dialog messages, and record the clearance in the board note.
- [ ] **Step 6:** `make canvas && make canvas-check`; commit
  `feat(articles): the sync stamps state an outcome, because this device has no clock`.

### Task 0.2: Settings gets its `wallabag` row back

**Why:** `WallabagAccount.dc.html` is reachable from nothing. `c2468df` cut an
`Instapaper / SIGNED IN` row from Settings' CONNECTIONS alongside Home's; this
restores it as the door to the account screen. Home's row is the door to the *list*
(#141, done); this is the door to *setup and status*, which is what Settings is for.

**Files:** `design/Settings.dc.html`

- [ ] **Step 1:** Add a row under CONNECTIONS, after `Wi-Fi`, label `wallabag`, value
  `NOT SET UP`, drawn exactly as the `Wi-Fi` row is drawn (copy that row's markup, swap
  the two runs). The value is `SIGNED IN` once `/.reader/wallabag.json` carries all five
  values and a sync has completed at least once; `NOT SET UP` otherwise. Note that
  unlike Home this is a screen visited deliberately, so a setup state here is
  information rather than a nag — Home's row stays a chevron for the reason
  `Main.dc.html`'s menu note gives.
- [ ] **Step 2:** Count the items. Settings is eleven today where twelve fit
  (`CLAUDE.md`, *Overlays and lists*); this makes it **twelve**. Render at both
  geometries and confirm no rail and no 14px gutter appears. Record the count in the
  board note and in `CLAUDE.md`'s "eleven items where twelve fit" sentence when Phase
  1 lands the row.
- [ ] **Step 3:** Confirm the hint bar's Confirm slot reads `OPEN` when this row is
  focused — it discloses a screen, like `Typography` and `Wi-Fi`.
- [ ] **Step 4:** `make canvas && make canvas-check`; commit
  `feat(settings): the wallabag row returns under CONNECTIONS, the door to the account screen`.

### Task 0.3: A third failure shape — no network to sync over

**Why:** a sync needs a saved Wi-Fi network marked `AUTO`. A device with none can
reach neither of the two dialogs truthfully: `COULDN'T SIGN IN` blames the
credentials, `COULDN'T CONNECT` says the server did not answer. Neither was asked.

**Files:** `design/WallabagErrorNoNetwork.dc.html` (create from
`design/WallabagError.dc.html`), `design/canvas.json`

- [ ] **Step 1:** Copy `WallabagError.dc.html`. Caption `COULDN'T CONNECT`. Message:
  `No saved Wi-Fi network. Join one in Settings first.` One slab, `OK` — `TRY AGAIN`
  is absent on `WallabagError`'s own argument (pressing it could never succeed until
  the reader has been to Settings). Hint bar `CANCEL · OK` and two 36px dead slots.
- [ ] **Step 2:** Measure the message's wrap in Chrome: tightest break at least 12px
  (#76's floor), no line wider than the 336px column. Record the numbers in the note.
- [ ] **Step 3:** Add the artboard to `canvas.json` on page 6 (`V1.1 · Articles`) at the
  slot `make canvas` names when it refuses.
- [ ] **Step 4:** `make canvas && make canvas-check`; commit
  `feat(articles): a third failure shape, for a device with no network to sync over`.

### Task 0.4: The connecting dialog's second stage

**Why:** `WallabagConnecting.dc.html` says `CONNECTING…` with one message, and a sync
of fifty EPUBs at ~60 KB each over an ESP32 radio is a minute or more. The board's
own note says the caption is *"the one thing a still panel on e-ink owes a reader"*
— one caption for ninety seconds reads as frozen.

**Files:** `design/WallabagFetching.dc.html` (create from
`design/WallabagConnecting.dc.html`), `design/canvas.json`

- [ ] **Step 1:** Copy `WallabagConnecting.dc.html`. Caption `SYNCING…`. Message:
  `Fetching article 3 of 12.` Footnote unchanged. Same 340px panel, same `CANCEL`.
  Note that the count advances per file and each advance is a ~520 ms waveform — the
  same price a page turn pays — so this is a paint per article and never per byte.
- [ ] **Step 2:** Note the cancel contract for this stage: cancel stops after the file
  in flight, the partial file is removed, everything already fetched stays, and the
  watermark is **not** advanced — so the next sync fetches what this one did not.
- [ ] **Step 3:** Measure the message's widest form (`Fetching article 50 of 50.`)
  against the 296px column; record it.
- [ ] **Step 4:** Add to `canvas.json`; `make canvas && make canvas-check`; commit
  `feat(articles): the sync dialog's fetching stage, so a long sync does not read as frozen`.

### Task 0.5: The confirmation `Remove downloaded articles…` opens

**Why:** the ellipsis on that row promises a further step, as `Delete…` and the old
`Sign out…` did, and no board draws it.

**Files:** `design/ArticlesRemoveConfirm.dc.html` (create from
`design/DeleteConfirm.dc.html`), `design/canvas.json`

- [ ] **Step 1:** Copy `DeleteConfirm.dc.html` with `WallabagAccount.dc.html` regenerated
  as its veiled parent. Caption `REMOVE DOWNLOADED ARTICLES`. Message: `Every article
  on the card is deleted. Your wallabag is untouched, and the next sync fetches them
  again.` Slabs: `REMOVE` filled, `CANCEL` outlined. Hint bar as `DeleteConfirm`'s.
- [ ] **Step 2:** Note it is an **overlay** over the account screen; the veil is
  generated from `WallabagAccount.dc.html`, not transcribed.
- [ ] **Step 3:** Measure the wrap; record. Add to `canvas.json`; `make canvas &&
  make canvas-check`; commit
  `feat(articles): the remove-downloads confirmation, which the row's ellipsis promised`.

### Task 0.6: Register the new boards with the comparison sheet

**Files:** `tools/compare-design.py`

- [ ] **Step 1:** Add `FLOW_SCREENS` rows, one per boarded state, after the `wifi_*`
  rows: `articles`, `articles_setup`, `articles_sync_done`, `article_actions`,
  `article_end`, `wallabag_account`, `wallabag_connecting`, `wallabag_fetching`,
  `wallabag_error`, `wallabag_error_offline`, `wallabag_error_no_network`,
  `articles_remove_confirm`. Each names its `.dc.html` and a label.
- [ ] **Step 2:** Run `python3 tools/test_compare_design.py` — the table checks (no
  duplicate id, every board on disk) must pass. Run `make compare
  COMPARE_ARGS="--only articles,wallabag_account"` and confirm each prints `design ok
  firmware not implemented` — the boards exist, the screens do not yet.
- [ ] **Step 3:** Commit `chore(compare): rows for the twelve Articles board states`.

---

## Phase 1 — The screens, on fixture data

Everything in this phase is `core/` plus `sim/`, renders in the simulator and is
pinned by goldens. Nothing touches the card or the network. **At the end of this
phase the firmware builds and the six new screens are reachable in the simulator**;
on the device, Home's `ARTICLES` row opens a list built from an empty store.

### Task 1.1: Six `ScreenId`s, appended together, and every guard that names them

This is the `BatteryEmpty` append done six at once — the case the `Count` sentinel
was built for. Expect the build to fail at each guard in turn until every table has
grown; that is the guards working.

**Files:**
- Modify: `core/include/reader/app.h` (the enum, before `Count`)
- Modify: `core/src/app.cpp` (`kRestorability`, `screenUsesRadio`, `screenName`)
- Modify: `core/src/session_record.cpp` (`kNames`)
- Modify: `test/unit/test_focus_restore.cpp` (`kAllScreens`, the three counts)
- Test: `test/unit/test_session_record.cpp` (already walks every id)

- [ ] **Step 1:** Append `Articles`, `ArticleActions`, `ArticleEnd`, `WallabagAccount`,
  `WallabagConnecting`, `WallabagError` to `ScreenId` immediately before `Count`, each
  with a comment naming its board. Build. Expected: the build fails at
  `session_record.cpp`'s table assert and at `test_focus_restore.cpp`'s catalogue
  assert, and `-Wswitch` warns in `screenUsesRadio` and `screenName`. Write down which
  guards fired — that list is evidence for the commit message.
- [ ] **Step 2:** Add six wire names to `kNames`: `articles`, `article-actions`,
  `article-end`, `wallabag-account`, `wallabag-connecting`, `wallabag-error`.
- [ ] **Step 3:** Add six `kRestorability` rows with their reasons as comments:
  `Articles` → `Ready` (built from the card, as the Library is); `ArticleActions` →
  `Never` (`WifiNetworkActions`' argument: its facts belong to the press);
  `ArticleEnd` → `NeedsPriming` (`BookEnd`'s: built from the open book);
  `WallabagAccount` → `Ready`; `WallabagConnecting` → `Never` and `WallabagError` →
  `Never` (a sync in flight does not survive a sleep, and waking into a dialog about
  one nobody remembers is `Peek`'s argument).
- [ ] **Step 4:** `screenUsesRadio`: `WallabagConnecting` returns **true**; the other
  five return false, each listed explicitly. The sync runs behind the connecting
  dialog and nowhere else — this is what `pollWifi()`'s backstop sweeps against.
- [ ] **Step 5:** `screenName`: six upper-case labels.
- [ ] **Step 6:** `test_focus_restore.cpp`: six rows in `kAllScreens`; raise the
  `Ready` count by two, `NeedsPriming` by one, `Never` by three; leave `movable` and
  `wrapping` for Task 1.9, which is when the screens exist to be built. The `build()`
  helper `REQUIRE`s a non-null screen, so this file will not go green until the
  factory has cases — expected, and the reason Task 1.9 exists.
- [ ] **Step 7:** Build. Expected: `core/` compiles; `unit_tests` fails only in
  `test_focus_restore.cpp` with the six unbuildable ids. Commit
  `feat(articles): six ScreenIds appended together, and the six guards that had to grow`.

### Task 1.2: The `Article` latch

The screens in this feature latch outcomes the shell must act on — a sync request, an
archive, a star, an open, a remove — and `Action::wifi()`'s contract is the one to
copy: nothing is popped, the shell reads the outcome off the screen still on top.

**Files:**
- Modify: `core/include/reader/app.h` (`Action::Kind::Article`, `Action::article()`,
  `App::articleRequested()` / `clearArticleRequest()`)
- Modify: `core/src/app.cpp` (`dispatch` sets the latch)
- Test: `test/unit/test_app.cpp`

- [ ] **Step 1:** Write the failing test: a stub screen whose `onGesture` returns
  `Action::article()`; after `dispatch`, `articleRequested()` is true, the stack depth
  is unchanged, and the same screen is still on top. After `clearArticleRequest()` it
  is false. Model it on the existing `wifiRequested` case in that file.
- [ ] **Step 2:** Run `cmake --build build -j8 && build/unit_tests -tc="*article
  latch*"`. Expected: compile failure, `article` is not a member of `Action`.
- [ ] **Step 3:** Add the kind, the factory, the two `App` members, and the `dispatch`
  case, each with the one-line comment `wifi()` carries about carrying no payload.
- [ ] **Step 4:** Run the test. Expected: PASS. Run `make test`. Expected: green
  except `test_focus_restore.cpp` (still waiting on Task 1.9).
- [ ] **Step 5:** Commit `feat(app): the Article latch, wifi()'s contract for the Articles flow`.

### Task 1.3: View-models and the theme's six renderers

**Files:**
- Modify: `core/include/reader/viewmodel.h`
- Modify: `core/include/reader/theme.h`, `core/src/theme_quiet.cpp`

- [ ] **Step 1:** Add the six view-models. `ArticlesViewModel`: a band value string
  (`3 UNREAD` / `NOT SET UP`), a `notSetUp` flag, the sync row's stamp string, an
  optional status line for the sync-done variant, a visible slice of rows (title,
  source-and-minutes meta string, a read flag, focused index) mirroring
  `LibraryViewModel`'s slice shape, and the four hints. `ArticleActionsViewModel`: the
  caption (the article's title, elided), two rows `Archive` / `Star` (the second reads
  `Unstar` when starred), focus. `ArticleEndViewModel`: title, meta, four slabs, the
  `N LEFT` value, the footnote. `WallabagAccountViewModel`: five value rows, one
  section header, one disclosing row, the note. `WallabagConnectingViewModel`: caption,
  message, footnote — the fetching stage is the same view-model with a different
  caption and message. `WallabagErrorViewModel`: caption, message, a slab list whose
  length is the shape (one slab for `SignIn` and `NoNetwork`, two for `Offline`).
- [ ] **Step 2:** Add six `render*` virtuals to `theme.h` and implement each in
  `theme_quiet.cpp` **by assembly, not new geometry**: `renderArticles` is
  `renderLibrary`'s band, rows and rail with a sync row after the band;
  `renderArticleActions` is `renderItemActions`; `renderArticleEnd` is
  `renderBookEnd` with a fourth slab; `renderWallabagAccount` is `renderSettings`'s
  rows and section header; `renderWallabagConnecting` is `renderWifiConnect`;
  `renderWallabagError` is `renderWifiError`. Where a primitive already exists in
  `components.h`, call it; where the second copy of something appears, extract it then
  — *the second copy is the extraction point*.
- [ ] **Step 3:** Build. Expected: compiles; nothing calls the renderers yet.
  Commit `feat(articles): six view-models and their renderers, assembled from shipped primitives`.

### Task 1.4: `ArticlesScreen` — the list, its not-set-up variant and its stamp

**Files:**
- Create: `core/include/reader/screen_articles.h`, `core/src/screen_articles.cpp`
- Test: `test/unit/test_screen_articles.cpp`

- [ ] **Step 1:** Write the failing tests. Construct the screen from a vector of
  fixture rows (title, source, minutes, read flag, an id) plus a stamp string and a
  `notSetUp` flag. Assert: with rows, row 0 of the focus ring is the `Sync now` row
  and Confirm on it returns `Action::article()` with the screen's `chosen()` reading
  `Sync`; Down then Confirm on an article row returns `Action::open()`; a **long**
  Confirm on an article row returns `Action::push(ArticleActions)`; the hint bar is
  `BACK / READ / UP / DOWN` with a hold ring on Confirm; the band's value is
  `N UNREAD` counting only unread rows. With `notSetUp` true: no rows, no sync row,
  the band reads `NOT SET UP`, the hint bar is `BACK` plus three empty slots, and every
  gesture but Back returns `Action::none()`. With rows and `visibleRows` smaller than
  the count: the slice moves as the Library's does (copy the shape of
  `test_screen_library.cpp`'s scrolling case).
- [ ] **Step 2:** Run the file's tests. Expected: compile failure, no such header.
- [ ] **Step 3:** Implement as a `FocusScreen` over a `ScrollWindow`, `id()` →
  `Articles`, `Fidelity::Mono`, `declareRepeat` on Up/Down, `holds` on Confirm only.
  `focusedId()` and `focusedTitle()` getters for the shell and the overlay.
  `setStamp()` and `setStatusLine()` for the sync-done variant. The not-set-up variant
  is a constructor flag, not a second screen — `HomeEmpty`'s rule.
- [ ] **Step 4:** Run the tests. Expected: PASS. Commit
  `feat(articles): the Articles list, its not-set-up variant and its sync stamp`.

### Task 1.5: `ArticleActionsScreen` — Archive and Star

**Files:**
- Create: `core/include/reader/screen_article_actions.h`, `core/src/screen_article_actions.cpp`
- Test: `test/unit/test_screen_article_actions.cpp`

- [ ] **Step 1:** Write the failing tests. Constructed from `Facts` (id, title,
  starred flag) — **not** from an `ArticlesScreen&`, for `DeleteConfirmScreen`'s
  reason (a screen reference makes the overlay reachable from one parent only).
  Assert: `isOverlay()`; two rows; Confirm on `Archive` returns `Action::article()`
  and `chosen()` reads `Archive`; on `Star` it reads `Star`; the second row's label is
  `Unstar` when `Facts::starred` is true; Back returns `Action::pop()`; the focus wraps
  between the two rows; `paintFootprint()` is constant across both focus states (two
  rows of one height, the last row's rule already absent — verify by rendering both
  states and comparing the panel's top border row, the `ItemActions` one-pixel lesson).
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement on `ItemActionsScreen`'s shape.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(articles): the Archive / Star overlay, built from facts rather than a parent`.

### Task 1.6: `ArticleEndScreen`

**Files:**
- Create: `core/include/reader/screen_article_end.h`, `core/src/screen_article_end.cpp`
- Test: `test/unit/test_screen_article_end.cpp`

- [ ] **Step 1:** Write the failing tests. `Facts`: id, title, source, minutes,
  starred, unread-remaining count, whether a next unread article exists. Assert four
  slabs `ARCHIVE / STAR / NEXT ARTICLE / BACK TO LIST`; when no next article exists the
  `NEXT ARTICLE` slab is **absent** rather than inert (`WifiError`'s rule: the slab list
  is the shape); `STAR` reads `UNSTAR` when starred; Confirm on each returns
  `Action::article()` with `chosen()` naming it; Back returns `Action::pop()` (back to
  the last page, `BookEnd`'s rule); the band's right slot is `N LEFT`; the footnote is
  `SYNCS ON THE NEXT CONNECTION.`; `restorability` is `NeedsPriming`.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement on `BookEndScreen`'s shape.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(articles): the end-of-article screen, BookEnd's shape with a fourth slab`.

### Task 1.7: `WallabagAccountScreen`

**Files:**
- Create: `core/include/reader/screen_wallabag_account.h`, `core/src/screen_wallabag_account.cpp`
- Test: `test/unit/test_screen_wallabag_account.cpp`

- [ ] **Step 1:** Write the failing tests. `Facts`: username, unread count, last-sync
  outcome string, keep-offline value, pending count, configured flag. Assert: the
  focus starts on `Keep offline` and **skips** `Account`, `Unread`, `Last sync` and
  `Pending actions` (Settings' rule — a row that cannot act is not focusable); Confirm
  on `Keep offline` cycles `NEWEST 20 → 50 → 100 → 20` and returns `Action::article()`
  with `chosen()` reading `KeepOffline`; Confirm on `Remove downloaded articles…`
  returns `Action::push(...)` for the confirmation (the id lands in Task 1.8); the
  Confirm hint reads `CHANGE` on the cycling row and `OPEN` on the disclosing one —
  Settings' own varying-hint precedent; when `configured` is false the band reads
  `NOT SET UP` and `Keep offline` is the only focusable row.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement as a `FocusScreen` with a `focusable()` gate, on
  `SettingsScreen`'s shape.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(articles): the account screen, Settings' shape with one cycling row`.

### Task 1.8: The three dialogs — connecting, fetching, and three failure shapes

**Files:**
- Create: `core/include/reader/screen_wallabag_connecting.h`, `core/src/screen_wallabag_connecting.cpp`
- Create: `core/include/reader/screen_wallabag_error.h`, `core/src/screen_wallabag_error.cpp`
- Modify: `core/include/reader/app.h` — append `ArticlesRemoveConfirm` to `ScreenId`
  (and grow every table Task 1.1 grew; the guards will name each one)
- Test: `test/unit/test_wallabag_dialogs.cpp`

- [ ] **Step 1:** Write the failing tests. Connecting: constructed with the host;
  `isOverlay()`; no focus; the message names the host unquoted; Back returns
  `Action::article()` with `cancelled()` true; `setFetching(3, 12)` switches the
  caption to `SYNCING…` and the message to `Fetching article 3 of 12.` and marks the
  screen dirty. Error: constructed with a `Shape` enum (`SignIn`, `Offline`,
  `NoNetwork`); the slab list is one for `SignIn` and `NoNetwork`, two for `Offline`;
  `TRY AGAIN` is present only on `Offline`; Confirm on a slab returns
  `Action::article()` with `chosen()` naming it; the three captions and messages are
  the boards' strings verbatim. Remove-confirm: `DeleteConfirmScreen`'s shape — `REMOVE`
  returns `Action::article()` with `chosen()` reading `RemoveAll`, `CANCEL` pops.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement on `WifiConnectScreen`'s and `WifiErrorScreen`'s shapes;
  the remove-confirm on `DeleteConfirmScreen`'s.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(articles): the sync dialogs, WifiConnect's and WifiError's shapes one subsystem over`.

### Task 1.9: The factory, the simulator, and the catalogue goes green

**Files:**
- Modify: `core/include/reader/screens.h`, `core/src/screens.cpp`
- Modify: `sim/main.cpp`
- Modify: `test/unit/test_focus_restore.cpp`
- Test: `test/unit/test_screens.cpp`

- [ ] **Step 1:** Factory setters, each with a primed flag rather than "the data is
  non-empty" (`contentsPrimed_`'s rule): `setArticles(rows, stamp, notSetUp)`,
  `setArticleActionsFacts` / `clearArticleActionsFacts`, `setArticleEndFacts`,
  `setWallabagAccountFacts`, `setWallabagHost`, `setWallabagFailure(shape)`,
  `setArticlesDemo()` for the simulator. Factory cases for all seven ids; each refuses
  (returns null) when unprimed — **never substitutes demo data**.
- [ ] **Step 2:** Demo view-models in `screens.cpp` with the boards' specimen strings:
  the five article rows, `3 UNREAD`, `WALLABAG · UP TO DATE`, the account's `LUCASG`.
- [ ] **Step 3:** Home: `demoHomeTargets()` returns `Library, Articles, Settings`; the
  three `demoHome*Vm()` menus gain the `ARTICLES` row — `3 UNREAD` on `Main` and
  `HomeUnopened`, an empty value on `HomeEmpty` (the empty value draws the chevron,
  `SETTINGS`' own mechanism). This is #141's whole implementation; the budget already
  reads `vm.menu.size()`.
- [ ] **Step 4:** `sim/main.cpp`: subcommands `articles`, `articles_setup`,
  `articles_sync_done`, `article_actions`, `article_end`, `wallabag_account`,
  `wallabag_connecting`, `wallabag_fetching`, `wallabag_error`,
  `wallabag_error_offline`, `wallabag_error_no_network`, `articles_remove_confirm`,
  each priming the factory and pushing the stack the board draws (the overlays over
  their veiled parent: the dialogs over the list, the actions over the list, the
  remove-confirm over the account).
- [ ] **Step 5:** `test_focus_restore.cpp`: raise `movable` and `wrapping` by the
  screens whose focus moves — `Articles`, `ArticleActions`, `ArticleEnd`,
  `WallabagAccount`, `ArticlesRemoveConfirm`, `WallabagError` (its slabs) — and set the
  `Ready`/`NeedsPriming`/`Never` counts to what the seven rows say. Run
  `build/unit_tests -tf="*focus_restore*"`. Expected: PASS, with the counts read off the
  run and then written down rather than guessed.
- [ ] **Step 6:** `test_screens.cpp`: a case that walks Home → Down ×2 → Confirm and
  lands on `Articles`, then Back to Home at depth 1.
- [ ] **Step 7:** `make test`. Expected: **green**, including every Home golden going
  red first — inspect each candidate (a third menu row, the title block moved up by
  81px, nothing else) and bless. Ten Home goldens move; record which rows differ.
- [ ] **Step 8:** Commit `feat(articles): the factory builds all seven screens, and Home's ARTICLES row opens the list (#141)`.

### Task 1.10: Goldens for every boarded state, at both geometries

**Files:**
- Create: `test/unit/test_theme_articles_golden.cpp`

- [ ] **Step 1:** One golden per simulator subcommand from Task 1.9, at 480×800 and
  528×792 — twenty-four PNGs. Model the file on `test_theme_wifi_golden.cpp`.
- [ ] **Step 2:** Run once to generate candidates; **look at all twenty-four** and say
  what you see. The things to catch: the sync row's stamp colliding with `Sync now` on
  the X4; the connecting dialog's host wrapping; `2 LEFT` in the band; the veil under
  each overlay being the right parent.
- [ ] **Step 3:** Bless; `make test` green; commit
  `test(articles): every boarded Articles state pinned per pixel at both geometries`.

### Task 1.11: The comparison sheet reads the screens

- [ ] **Step 1:** `make compare COMPARE_ARGS="--only
  articles,articles_setup,article_actions,article_end,wallabag_account,wallabag_connecting,wallabag_error"`.
  Expected: every row `design ok firmware ok mismatch N%`. Read the percentages and
  compare like with like — the three dialogs against `wifi_connect` and `wifi_error`
  (~3–4%), the list against `library` (~2%). Anything over 8% is a board/screen
  disagreement to find before moving on, not a number to record.
- [ ] **Step 2:** `home`, `home_empty`, `home_unopened` should have returned to about
  1.2%/1.1%, 1.3%/1.2% and their old figures from ~21% — `Main.dc.html`'s menu note
  says to expect exactly that. Record the three pairs in the note.
- [ ] **Step 3:** Commit `docs(design): the Home boards' compare figures, back where the note said they would be`.

---

## Phase 2 — The card-side store, in `core/`

Pure logic over `FileSystem`, tested with `FakeFileSystem`. **At the end of this
phase a card pre-loaded with `/.reader/articles/<id>.epub` and `<id>.json` files reads
on the device with no network at all**, Home counts them, and archiving from the end
screen writes a marker the sync will push later.

### Task 2.1: The credentials file — parse, seed, and "is it configured"

**Files:**
- Create: `core/include/reader/wallabag_credentials.h`, `core/src/wallabag_credentials.cpp`
- Test: `test/unit/test_wallabag_credentials.cpp`

- [ ] **Step 1:** Write the failing tests over `FakeFileSystem`. Path constant
  `/.reader/wallabag.json`. `load()` returns a struct of five strings (`server`,
  `clientId`, `clientSecret`, `username`, `password`) and a `configured` answer that is
  true only when all five are non-empty. A missing file loads as unconfigured, **not**
  as an error. A file with any empty value is unconfigured, not an error — a seeded
  file is the normal state of a device nobody has set up. A malformed file is an error
  distinct from both, with a reason string for the log. `server` is normalised: a
  trailing slash is dropped; a value with no scheme gets `http://` — and the test
  asserts a value with `https://` keeps it. `seed()` writes the five keys with empty
  values **only when the file is absent**, returns whether it wrote, and never touches
  an existing file however malformed (`loadAndApplySettings`' rule). The flat `Json`
  parser is the parser; assert a `\uXXXX` escape in the file reads as malformed, and
  say in the header that a hand-editor's accented password must be typed as UTF-8.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement. The header states the security position in
  `docs/notes/wallabag-api.md` §5's words: plaintext on a removable card, bounded by
  the deployment and not by anything clever.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(wallabag): the credentials file, seeded when absent and never overwritten`.

### Task 2.2: The article store — metadata sidecars, queue markers, watermark, pruning

**Files:**
- Create: `core/include/reader/article_store.h`, `core/src/article_store.cpp`
- Test: `test/unit/test_article_store.cpp`

- [ ] **Step 1:** Write the failing tests over `FakeFileSystem`. The directory is
  `/.reader/articles/`. **Per article, two files named by the server's integer id**:
  `<id>.epub` and `<id>.json` — the sidecar holding `title`, `domain`, `readingTime`,
  `starred`, `archived`, `updatedAt` (the server's string, stored verbatim), all
  through the flat `Json` object — the same one-object-per-file shape as
  `/.reader/state/<hash>.json`, and for the same reason: the parser cannot hold an
  array and one corrupt file costs one row. `list()` lists the directory once and
  returns one entry per sidecar whose `.epub` exists, sorted newest `updatedAt` first
  (string comparison on wallabag's ISO-8601 is chronological). `unreadCount()` counts
  entries not archived and whose reading sidecar under `/.reader/state/` has no
  `finished` mark — the store asks `reading_store.h`, never re-derives. `writeMeta()`,
  `hasEpub()`, `epubPath(id)`. **The queue is marker files**: `/.reader/articles/queue/
  <id>.archive` and `<id>.star` / `<id>.unstar`, empty files; `queueArchive(id)`,
  `queueStar(id, bool)`, `pendingCount()` (a directory count), `pending()` (the list),
  `ack(id, kind)` (a remove). Queueing archive **also removes the local `.epub` and
  sidecar** and drops the article's reading sidecar via `reading_store` — the file is
  gone the moment the reader archives, and the server learns later. The watermark:
  `/.reader/articles/sync.json`, flat, keys `since` (the largest `updatedAt` seen, the
  server's clock) and `lastOutcome` (`never` / `upToDate` / `new:<n>` / `failed`).
  `prune(keep)` removes the oldest **unread, unstarted** articles beyond `keep` and
  never one with a reading position (`percentFor` > -1). `removeAll()` deletes every
  `.epub` and sidecar and every reading sidecar for them, and leaves the queue and
  the watermark alone.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement. Every write goes through `writeAll`, which drops the
  listing cache by construction (`SdFileSystem::forgetCardFacts`). Nothing here reads a
  clock.
- [ ] **Step 4:** Run. Expected: PASS. Add a case that a sidecar with no `.epub` is
  skipped by `list()` and reported by a `stray()` count, so a download that died
  between the two writes shows in the log rather than as a row that will not open.
- [ ] **Step 5:** Commit `feat(articles): the card-side store -- sidecars, marker-file queue, watermark, pruning`.

### Task 2.3: `articlesKeepOffline` in Settings

**Files:**
- Modify: `core/include/reader/settings.h`, `core/src/settings.cpp`
- Test: `test/unit/test_settings.cpp`

- [ ] **Step 1:** Write the failing test: a new field `articlesKeepOffline`, default
  50, steps `{20, 50, 100}`, clamped to the table on load (an out-of-range value is
  `CORRECTED`, not `DEFAULTED`), round-trips through `saveSettings`/`loadSettings`, and
  an older file without the key loads with the default. Assert `kSettingsVersion` did
  **not** move — the default is today's behaviour.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement beside the other tables in `settings.h`, with the
  `ascending` assert the others carry.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(settings): articlesKeepOffline, the account screen's one persisted row`.

### Task 2.4: The list, the account, Home and the end screen read the store

**Files:**
- Modify: `core/src/screens.cpp` (a `setArticleStore(FileSystem*)` prime that builds
  `Articles` and `WallabagAccount` from the card the way `Library` is built from `fs`)
- Modify: `core/include/reader/screen_articles.h` — a `rescan()` and
  `refreshProgress()` pair on `LibraryScreen`'s model
- Test: `test/unit/test_screen_articles.cpp`, `test/unit/test_home_rebuild.cpp`

- [ ] **Step 1:** Write the failing tests: an `ArticlesScreen` constructed over a
  `FakeFileSystem` holding three sidecars lists three rows newest first, marks the one
  with a `finished` reading sidecar `READ`, and its band reads `2 UNREAD`; with no
  credentials file it is the not-set-up variant; with credentials but no articles it
  lists only the sync row and the band reads `0 UNREAD`. A `WallabagAccountScreen` over
  the same fake reads `Unread · 2 ARTICLES`, `Pending actions · 1 TO PUSH` after one
  `queueArchive`, and `Last sync` from the watermark's outcome. `homeVmForCard`'s
  logic is in the shell, so pin the **pure** half here: a helper in
  `article_store.h` that returns the menu value — `N UNREAD` when configured, empty
  when not — and test both.
- [ ] **Step 2:** Run. Expected: failures.
- [ ] **Step 3:** Implement. The factory holds a `FileSystem*` for the store exactly as
  it holds one for the Library, and `Articles` is `Ready` because of it.
- [ ] **Step 4:** Run; `make test` green. Commit
  `feat(articles): the list, the account and Home's row read the card store`.

---

## Phase 3 — The wallabag client, in `core/`, against a fake transport

Everything here is desktop-tested. **At the end of this phase a `SyncEngine` driven by
a scripted `FakeHttpTransport` completes a full sync into a `FakeFileSystem`**: token,
push, listing, downloads, watermark — and every failure shape is reachable on demand.

### Task 3.1: The transport interface and the body sink

**Files:**
- Create: `core/include/reader/http_transport.h`
- Create: `test/unit/fake_http_transport.h`

- [ ] **Step 1:** Write the interface, poll-shaped, on `WifiRadio`'s argument (the
  spec's *The radio seam*, verbatim reasoning in the header): `begin(request, sink)`
  returns whether the request was accepted; `state()` is `Idle / Running / Done /
  Failed`; `status()` is the HTTP status once `Done`; `failure()` names why once
  `Failed` (`NoNetwork`, `Dns`, `Refused`, `Timeout`, `Tls`, `SinkRefused`); `cancel()`.
  A `Request` is method, path (relative to the server), a small header list, and an
  optional body string (the token form is ~200 bytes; nothing else has a body). A
  `BodySink` is `write(bytes) -> bool` plus `finish()`; a `false` from `write` fails
  the request with `SinkRefused`. Two sinks live in `core/`: `BufferSink`, bounded at
  a cap the constructor takes (**32 KB** for a listing page; the header says why:
  `perPage=20` × ~1 KB of metadata with `detail=metadata`, and refusing past the cap
  is the JSON parser's own "large is malformed" rule), and `NullSink` for `PATCH`
  responses nobody reads. The card sink is the shell's.
- [ ] **Step 2:** Write `FakeHttpTransport`: a queue of scripted responses (status,
  body, or a failure), a log of every request made (method, path, headers, body), and
  `step()` to move `Running → Done` under the test's control so a poll loop can be
  asserted mid-flight. Model on `fake_wifi_radio.h`.
- [ ] **Step 3:** Build. Commit `feat(wallabag): the injected HTTP transport, poll-shaped, and its fake`.

### Task 3.2: A bounded pull scanner for nested JSON

**Why:** `json.h` reads one flat object. A listing page is an object holding
`_embedded.items`, an **array** of objects each holding a `tags` array. Nothing in the
repo can read it, and vendoring is refused here.

**Files:**
- Create: `core/include/reader/json_stream.h`, `core/src/json_stream.cpp`
- Test: `test/unit/test_json_stream.cpp`

- [ ] **Step 1:** Write the failing tests. A pull tokenizer over the same `ByteSource`
  `xml.h` uses: `next()` yields `ObjectStart / ObjectEnd / ArrayStart / ArrayEnd / Key
  / String / Number / Bool / Null / End / Error`, with the current key and scalar
  readable after each. Strings decode the JSON escapes **including `\uXXXX` and
  surrogate pairs to UTF-8** — wallabag serialises non-ASCII titles that way, and a
  title the subset cannot draw is a notdef box (assert `“` decodes to the curly
  quote `fontc.py` carries). Bounds are the grammar: a string past
  `kJsonMaxStringBytes` (reuse `json.h`'s 256) is truncated at a UTF-8 boundary and
  flagged, never an error — a long title is content, and cutting it is the Library's
  own ellipsis; nesting past 8 is `Error`. Every malformed input is a clean `Error`
  and the scanner never reads past `End`. A helper `skipValue()` skips any value
  including a nested one, so a consumer can ignore `tags` and `preview_picture`
  without knowing their shape. Test it on a byte-at-a-time source (grain 1 is the
  load-bearing case, `inflate_stream.h`'s lesson).
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement. No allocation beyond the one bounded string buffer.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(json): a bounded pull scanner for nested JSON, because a listing is an array`.

### Task 3.3: The client — requests, the token, refresh on 401, and three answers

**Files:**
- Create: `core/include/reader/wallabag_client.h`, `core/src/wallabag_client.cpp`
- Create: `core/include/reader/token_store.h` (an interface: `load(access, refresh)`,
  `save(access, refresh)`, `clear()` — the shell implements it over NVS, the tests
  over a struct)
- Test: `test/unit/test_wallabag_client.cpp`

- [ ] **Step 1:** Write the failing tests against `FakeHttpTransport` and a fake token
  store. The client builds exactly the six requests in `docs/notes/wallabag-api.md`
  §2 and the test asserts each path and query verbatim from the fake's request log:
  `GET /api/info` with no `Authorization` header; `POST /oauth/v2/token` as a form body
  with `grant_type=password` and the four credentials; the same with
  `grant_type=refresh_token`; `GET /api/entries?detail=metadata&perPage=20&page=N`
  with `&archive=0` on a **first** sync and `&since=<watermark>` **without**
  `archive=0` on a later one (the header explains: an entry archived on the server
  must come back so its local file can be removed, and `archive=0` would hide it);
  `GET /api/entries/<id>/export.epub`; `PATCH /api/entries/<id>?archive=1`;
  `PATCH /api/entries/<id>?starred=1` and `=0`. Every authenticated call carries
  `Authorization: Bearer <access>`. **Refresh on 401, never on a predicted expiry**: a
  401 on any call triggers one refresh and one retry; a 401 on the refresh triggers one
  password grant and one retry; a 401 on the password grant is `CredentialsRefused`.
  Three classified answers, and the test reaches each: `NotAWallabag` (`/api/info` did
  not answer 200 with a body carrying `appname`), `CredentialsRefused`, `Unreachable`
  (any transport failure). Tokens are saved on every successful grant and cleared on
  `CredentialsRefused`.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement as a poll-shaped state machine too: `beginX()`, `poll()`,
  `result()` — because the transport is, and the loop that drives it is `loop()`.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(wallabag): the client -- six requests, refresh on 401, three honest answers`.

### Task 3.4: The listing parser

**Files:**
- Modify: `core/src/wallabag_client.cpp`
- Test: `test/unit/test_wallabag_client.cpp`

- [ ] **Step 1:** Write the failing tests over a fixture page captured from the spec's
  shape: `page`, `pages`, `total`, and `_embedded.items[]` with `id`, `title`,
  `domain_name`, `reading_time`, `is_archived`, `is_starred`, `updated_at`, plus
  `tags` and `preview_picture` to be skipped. `parseListing(source, out)` yields one
  `ListingEntry` per item and the page count, through `json_stream`, skipping every
  key not named. A title over the cap arrives truncated and flagged. A malformed page
  is `Error` with nothing partial handed back. Test at grain 1.
- [ ] **Step 2:** Run. Expected: failures.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run. Expected: PASS. Commit
  `feat(wallabag): the listing parser, over the pull scanner, ignoring what it does not draw`.

### Task 3.5: The sync engine

**Files:**
- Create: `core/include/reader/sync_engine.h`, `core/src/sync_engine.cpp`
- Test: `test/unit/test_sync_engine.cpp`

- [ ] **Step 1:** Write the failing tests over `FakeHttpTransport`, `FakeFileSystem`,
  the store and a fake token store. The engine is a state machine the shell polls:
  `begin()`, `poll()`, `state()`, `progress()` (files fetched of files to fetch, for
  the dialog's second stage), `outcome()`, `cancel()`. The order of a sync, asserted
  by the fake's request log: `GET /api/info` → token (only if the store has none, else
  straight on and refresh on 401) → **push the queue first** (every marker, oldest
  first; each 2xx acks its marker; a `PATCH` on an id the server no longer has — 404 —
  acks too, because the desired state is already true) → listing, page by page → for
  each entry: write the sidecar; if `is_archived` is now true and the file exists,
  remove the local files; if not archived and no `.epub` exists, `GET export.epub`
  into a sink the engine is **given** (a `SinkFactory` taking the id, so the shell
  hands over a card file sink and the test a buffer) → advance the watermark to the
  largest `updated_at` seen **only after every download landed** → `prune(keep)` →
  outcome `new:<n>` or `upToDate`. Failure shapes: `NotAWallabag` and
  `CredentialsRefused` end the sync with those outcomes and nothing written;
  `Unreachable` mid-download ends with `failed`, the partial file removed, every
  completed download kept, the watermark **not** advanced. `cancel()` during a
  download behaves as `Unreachable` for the file in flight but the outcome is
  `cancelled`. A test drives every state transition through `poll()` with the fake's
  `step()`.
- [ ] **Step 2:** Run. Expected: compile failure.
- [ ] **Step 3:** Implement. The engine owns no clock and no radio; it is handed a
  transport that is already connected.
- [ ] **Step 4:** Run. Expected: PASS. Add one property: a sync run twice against the
  same fake yields an identical store — idempotence is what makes a cancelled sync safe
  to retry.
- [ ] **Step 5:** Commit `feat(wallabag): the sync engine, a poll-driven state machine that pushes before it pulls`.

### Task 3.6: The outcome contract through a real `App`

**Files:**
- Create: `test/unit/test_article_outcomes.cpp`

- [ ] **Step 1:** Write it on `test_wifi_outcomes.cpp`'s model, through `App::dispatch`
  with a primed factory: Confirm on `Sync now` leaves the list on top at the same depth
  with `articleRequested()` true; a long Confirm on a row pushes `ArticleActions` and
  Confirm on `Archive` leaves the overlay standing with the latch set; on `ArticleEnd`
  each slab likewise; Back on `WallabagConnecting` sets the latch and does not pop;
  `screenUsesRadio` is true for exactly `WifiPicker`, `WifiConnect` and
  `WallabagConnecting` — the count is **three** now and the test says so.
- [ ] **Step 2:** Run. Expected: PASS if Phase 1 was done right; any failure here is a
  screen returning `pop()` where it should latch, which is the defect that file exists
  to catch.
- [ ] **Step 3:** Commit `test(articles): the latch contract through a real dispatch, wifi's outcome test one flow over`.

---

## Phase 4 — The shell, and the glass

Nothing here can be tested on the desktop. Every task ends at `On glass`, and the card
moves to `Done` only on the owner's evidence. **Flashing is the owner's step**; the
plan gives the command each time.

### Task 4.1: The probe, before the transport

**Why:** #140 says do this first, at the point where the answer can still change the
design. The reading floor with Wi-Fi linked is **28,508 bytes** on glass; an mbedTLS
handshake on a C3 with no PSRAM is the one cost this plan cannot price from the desktop.

**Files:**
- Modify: `shell/src/main.cpp` (behind `ENCRE_WALLABAG_PROBE`, `ENCRE_FS_SELFTEST`'s idiom)

- [ ] **Step 1:** Behind the flag, at the end of `setup()`: bring the radio up on the
  `AUTO` network, `GET /api/info` over plain HTTP against the host in
  `/.reader/wallabag.json`, print status, body length, and `mark()` the heap before,
  during and after; then the same over `https://` against `app.wallabag.it`; then
  `GET export.epub` for one entry over plain HTTP streamed to `/.reader/articles/probe.epub`,
  with `mark()` around it and the largest free block printed. Radio down after.
- [ ] **Step 2:** Build with `PLATFORMIO_BUILD_FLAGS="-DENCRE_WALLABAG_PROBE=1" make firmware`.
  Hand the owner the flash command and ask for `run.log`.
- [ ] **Step 3:** Read the log. Record in `docs/notes/wallabag-api.md` a new §8: the
  heap spent by plain HTTP, by TLS, and by one streamed download; whether TLS fits at
  all with no book open. **Decision rule, written before the numbers arrive:** if TLS
  leaves less than 40 KB free with the radio up and no book open, the transport
  supports plain HTTP only in this release and the account screen's band says
  `HTTP ONLY` as a stated limit; if it fits, TLS is enabled and nothing else changes.
- [ ] **Step 4:** Remove the probe (`ENCRE_COVER_PROBE` was removed after it answered);
  commit `docs(wallabag): what a plain and a TLS round trip cost on the C3, measured`.

### Task 4.2: The Arduino transport and the card file sink

**Files:**
- Create: `shell/src/http_transport_arduino.h`, `shell/src/http_transport_arduino.cpp`
- Create: `shell/src/card_file_sink.h`, `shell/src/card_file_sink.cpp`

- [ ] **Step 1:** `ArduinoHttpTransport` implements `HttpTransport` over `HTTPClient`
  and `WiFiClient` (and `WiFiClientSecure` only if Task 4.1 said yes, with the
  well-known root bundle arduino-esp32 ships and no per-host pinning). **Poll-shaped
  over a blocking library**: `begin()` opens the connection and sends headers;
  `poll()` reads at most one chunk (4 KB) from the stream into the sink and returns,
  so `loop()` keeps ticking between chunks and a cancel lands within one chunk. A
  read that yields nothing for 15 s is `Timeout`. Every state the fake has, the real
  one reports.
- [ ] **Step 2:** `CardFileSink` writes to `<path>.part` over SdFat directly — a free
  function pair like `appendToCard`, for `sd_fs.h`'s stated reason (the `FileSystem`
  contract has no write handle and must not grow one for a caller `core/` will never
  be) — under `SpiBusGuard` per write, and `finish()` renames `.part` to the final
  name; a failure removes the `.part`. Sizes are checked as `writeAll` checks them: a
  short write is a failure, not a success.
- [ ] **Step 3:** `make firmware` builds. Commit
  `feat(shell): the Arduino HTTP transport, chunked per poll, and a card file sink that streams`.

### Task 4.3: The NVS token store

**Files:**
- Create: `shell/src/wallabag_store_nvs.h`, `shell/src/wallabag_store_nvs.cpp`

- [ ] **Step 1:** Implement `TokenStore` over `Preferences`, namespace `encre_wbg`,
  keys `ver`, `access`, `refresh`, in `wifi_store_nvs.cpp`'s idiom: the version checked
  before the payload, a missing namespace a quiet empty, a wrong version discarded
  whole with a log line. NVS caps a string at 4000 bytes; a wallabag bearer token is
  ~40 characters, assert the bound in the header rather than trusting it.
- [ ] **Step 2:** `make firmware` builds. Commit
  `feat(shell): the wallabag token store in NVS, wifi_store_nvs's shape`.

### Task 4.4: The seed at mount, Home's count, and the Articles list on the card

**Files:**
- Modify: `shell/src/main.cpp`

- [ ] **Step 1:** In `armCardProbes()`, immediately after the settings seed and with its
  exact three-way log idiom, seed `/.reader/wallabag.json` when absent. **It does not
  join the probe's reason** — the settings file is the probe target and this file must
  never become a second one (`docs/notes/wallabag-api.md` §5's fourth bullet). Log
  `written`, `could NOT be written`, or nothing when present.
- [ ] **Step 2:** `homeVmForCard()`: fill `vm.menu[1].value` from the store's helper
  (Task 2.4) — `N UNREAD` when configured, empty when not — and log it on the
  `[boot] Home's ARTICLES row:` line beside the LIBRARY one. `gHomeStale` is set by any
  store write the shell makes (a sync, an archive, a remove-all), so Home rebuilds.
- [ ] **Step 3:** Prime the factory's store pointer at mount (`setArticleStore(&gSd)`)
  beside `primeWifi()`, so `Articles` and `WallabagAccount` are buildable from boot —
  `loadWifi()`'s argument about the dead SETUP row, one door over.
- [ ] **Step 4:** Flash; on glass: Home shows `ARTICLES ›`; pressing it shows
  `NOT SET UP` with the instruction; the card now carries a seeded `wallabag.json`;
  fill it in on a computer, reinsert, boot — Home shows `ARTICLES  0 UNREAD` and the
  list shows only `Sync now`. Commit
  `feat(shell): the credentials seed at mount, Home's unread count, the list from the card`.

### Task 4.5: The sync driver in `loop()`

**Files:**
- Modify: `shell/src/main.cpp` (`handleArticle()`, `pollSync()`, `beginSyncFlow()`,
  `endSyncSession()`)

- [ ] **Step 1:** `handleArticle()`, called beside `handleWifi()` on the dispatch's own
  pass (the outcome is read off a screen still on top): on `Articles` with `chosen() ==
  Sync` → `beginSyncFlow()`; on `WallabagConnecting` cancelled → `engine.cancel()`,
  which the poll below finishes; on `WallabagError` → `TryAgain` re-enters
  `beginSyncFlow()`, `Ok`/`Cancel` pop; on `ArticleActions` → `queueArchive` or
  `queueStar` on the store, pop the overlay, replace the list (its rows changed —
  the Wi-Fi hub's own reason for a replace over a refresh), set `gHomeStale`; on
  `ArticleEnd` → `Archive` queues and pops to the list, `Star` toggles and redraws,
  `NextArticle` opens the next unread through `openBookAt`, `BackToList` pops to the
  list; on `WallabagAccount` → `KeepOffline` commits the setting and `prune`s; on
  `ArticlesRemoveConfirm` → `removeAll()`, pop, replace the account screen.
- [ ] **Step 2:** `beginSyncFlow()`: if the credentials do not load as configured →
  push `WallabagError(SignIn)`; if `gWifiNets.automatic()` is null → push
  `WallabagError(NoNetwork)`; else `gRadio.beginJoin()` on that network with its
  secret, prime the host, push `WallabagConnecting`. Reuses `gRadio`, `shellwifi::
  secret`, and `endWifiSession()`'s discipline.
- [ ] **Step 3:** `pollSync()`, called from the quiet window beside `pollWifi()`: while
  `WallabagConnecting` is on top — if the join is `Running`, wait; if `Failed`,
  `WallabagError(Offline)` and radio down; if `Ok` and the engine has not begun,
  construct the transport and the engine and `begin()`; then `engine.poll()` each
  iteration; when `progress()` changes, call the dialog's `setFetching()` and
  `markDirty()` (one paint per file); on a terminal state: radio down
  (`endWifiSession()`), write the watermark's outcome, and leave — success unwinds to
  the list and **replaces** it with the sync-done variant (stamp `N NEW` and the status
  line) exactly as a Wi-Fi join replaces the hub; `NotAWallabag` and
  `CredentialsRefused` replace the dialog with `WallabagError(SignIn)`; `Unreachable`
  and `failed` with `WallabagError(Offline)`; `cancelled` pops to the list. Set
  `gHomeStale` on any outcome that wrote.
- [ ] **Step 4:** The `screenUsesRadio` backstop in `pollWifi()` already takes the
  radio down under any screen that does not declare it; confirm by reading that no
  path out of the sync leaves without `endWifiSession()` — and that the backstop
  would catch it if one did.
- [ ] **Step 5:** `make firmware`; flash; on glass with a real instance: `Sync now` →
  `CONNECTING…` → `SYNCING… Fetching article 1 of N` → the list with `WALLABAG · N NEW`.
  Pull the router's plug mid-fetch → `COULDN'T CONNECT`, `TRY AGAIN` resumes and
  fetches only what is missing. Wrong password in the file → `COULDN'T SIGN IN`.
  Delete the `AUTO` network in Settings → the no-network shape. Read `run.log` for
  `[sync]` lines and the `mark()` heap trail across the whole sync. Commit
  `feat(shell): the sync driver -- Wi-Fi up, the engine polled from the quiet window, the dialogs`.

### Task 4.6: Reading an article, and the end screen

**Files:**
- Modify: `core/include/reader/screen_reader.h`, `core/src/screen_reader.cpp`
  (`setEndScreen(ScreenId)` — the page turn off the last page pushes it; default
  `BookEnd`)
- Modify: `core/src/screens.cpp` (`setReaderEndScreen`)
- Modify: `shell/src/main.cpp` (`handleOpen`, `openBookAt`, the restore walk)
- Test: `test/unit/test_screen_reader_bookend.cpp`

- [ ] **Step 1:** Write the failing test: a `ReaderScreen` told `setEndScreen(ArticleEnd)`
  pushes `ArticleEnd` off its last page where the default pushes `BookEnd`. Run;
  expected failure; implement; pass.
- [ ] **Step 2:** `handleOpen()`: a third asker beside the Library and Home — when the
  top is `Articles`, the path is `epubPath(focusedId())` and the bytes come from the
  store. `openBookAt()` gains an `isArticle` fact (derived from the path prefix
  `/.reader/articles/`, one place): when true it primes `ArticleEndFacts` from the
  sidecar (title, source, minutes, starred, unread-remaining, whether a next unread
  exists) and tells the Reader `ArticleEnd`; otherwise everything is as today.
  `last.json` carries the article through `openBook` unchanged — Home's CONTINUE
  offers it, the sleep card names it; that is decision 3 of the note, taken.
- [ ] **Step 3:** The restore walk: `ArticleEnd` joins `Reader`, `ReaderMenu`,
  `Contents` and `BookEnd` as a screen whose priming is `openBookAt`'s — the switch
  gains one case and the comment's "four screens and one open" becomes five.
- [ ] **Step 4:** Flash; on glass: open an article from the list, page to its end,
  `ARTICLE FINISHED · 2 LEFT`, `ARCHIVE` returns to a list one row shorter with the
  file gone from the card, `NEXT ARTICLE` opens the next; sleep in an article and wake
  onto its page; sleep on `ArticleEnd` and wake onto it. Commit
  `feat(articles): an article reads through openBook and its last page turns into ArticleEnd`.

### Task 4.7: The smoke checklist, CLAUDE.md, and the cards

**Files:**
- Modify: `docs/on-device-smoke-checklist.md` (§13)
- Modify: `CLAUDE.md`
- The board

- [ ] **Step 1:** §13 *Articles*, in the checklist's numbered idiom, covering: the
  seeded file appears on a fresh card and is not overwritten once edited; the three
  failure shapes each reachable on demand (wrong password, unplugged router,
  no `AUTO` network); a cancel mid-fetch leaves no `.part` and the next sync resumes;
  the radio is down after every exit (`[wifi] radio was up under` must **never**
  print); the `mark()` heap trail across a sync with the largest free block; an
  archive from the end screen removes the file and the next sync's log shows the
  `PATCH` and the ack; `ARTICLES` on Home reads the right count after each.
- [ ] **Step 2:** `CLAUDE.md`: seven rows in *The chrome screens* table; the Settings
  item count sentence to twelve; a short *Articles* section stating the six decisions
  this plan made that the code does not explain by itself — no ages (#132), marker
  files as the queue, per-article sidecars over the flat parser, push before pull,
  the watermark advanced only after every download, and `since` dropping `archive=0`.
- [ ] **Step 3:** Move #113, #114, #111, #112, #140, #141 to `On glass` with the
  checklist section named in a comment on each. **Not `Done`** — that is the owner's
  move on the evidence above.
- [ ] **Step 4:** Commit `docs(articles): smoke checklist §13, the CLAUDE.md section, and the cards to On glass`.

---

## Self-review against the spec

**Coverage, section by section of `docs/notes/wallabag-api.md`:** §2's six calls —
Task 3.3 asserts each path verbatim. §3 and §5's credentials on the card, seeded, never
overwritten, empty means unconfigured — Tasks 2.1 and 4.4. §5's "what the device does
with it" — `/api/info` first, tokens in NVS, `since` from the server's clock, refresh
on 401 never on a predicted expiry — Tasks 3.3, 3.5, 4.3. §5's four open questions:
(1) fetched at sync, Task 3.5; (2) `/.reader/articles/`, Task 2.2; (3) `last.json`
carries them, Task 4.6; (4) archiving removes the local file, Task 2.2. §6's
one-entry-per-EPUB — the per-entry route is what Task 3.5 fetches; a whole-list export
is not used. #112's queue with `SYNCS ON THE NEXT CONNECTION` and `1 TO PUSH` — Tasks
2.2, 2.4, 3.5. #114's end screen and "should an article appear in the Library" —
Task 4.6 and *What this plan does NOT do*. #140's probe before the client — Task 4.1.
#141 — Task 1.9 Step 3.

**Gaps found and closed while reviewing:** the account screen had no door (Task 0.2);
the confirmation the ellipsis promised had no board (Task 0.5); the no-network case
had no truthful dialog (Task 0.3); a ninety-second sync had one caption (Task 0.4);
every age on the boards was a claim the device cannot make (Task 0.1); an entry
archived on the server would never have been learned about under `archive=0` (Task
3.3's `since` rule).

**Names used consistently across tasks:** `Action::article()` / `articleRequested()`
/ `clearArticleRequest()` (1.2, 1.4–1.8, 3.6, 4.5); `chosen()` on every latching
screen; `setFetching(n, of)` (1.8, 4.5); `HttpTransport` / `BodySink` / `BufferSink` /
`CardFileSink` / `FakeHttpTransport` (3.1, 3.5, 4.2); `TokenStore` (3.3, 4.3);
`SyncEngine` with `begin/poll/state/progress/outcome/cancel` (3.5, 4.5); the store's
`list/unreadCount/queueArchive/queueStar/pendingCount/ack/prune/removeAll/epubPath`
(2.2, 2.4, 4.5, 4.6); `setArticleStore`, `setArticleEndFacts`,
`setArticleActionsFacts`, `setWallabagHost`, `setWallabagFailure`, `setReaderEndScreen`
(1.9, 2.4, 4.5, 4.6); the seven `ScreenId`s (1.1, 1.8) and their wire names.

**Placeholder scan:** no step defers a decision to later, names a type no task
defines, or says "handle errors" without naming the error and the screen it lands on.
