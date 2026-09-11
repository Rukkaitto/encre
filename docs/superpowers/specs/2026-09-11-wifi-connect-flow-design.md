# The Wi-Fi connect flow

**Status:** design approved 2026-09-11. Not implemented. Release **V1.1**. Promotes
the project board's `Wi-Fi manager and join flow` draft (`roadmap:1181`, Phase 4) to
an issue; the two drafts beside it — `HTTP upload server and Transfer screen` and
`Setup hotspot with QR` — stay drafts and stay V2.

**What this is:** the device learns a Wi-Fi network. Six screens — a settings hub, a
scan list, an on-device keyboard, a connecting dialog, three failure shapes sharing
one screen, and a one-row actions overlay — plus two `core/` primitives and a
credential store in NVS. Pressing `JOIN` brings the radio up, associates, says so,
and turns the radio off again.

**What it is not: there is nothing to transfer.** The HTTP server, the upload page,
`Transfer.dc.html`, the setup hotspot and both 960×620 web boards are out. That is
the boundary the project board already draws across three separate cards, and it is
the one that fits the heap.

## What this closes, and the honest cost of the boundary

`design/` has carried nine finished Wi-Fi boards since 2026-08-20 and no line of
Wi-Fi code has ever existed — `grep -rn -i wifi core/ shell/ sim/ test/` returns
only comments naming the parked boards. Commit `53c92d4` ("scope: Wi-Fi is out of V1
-- card transfer only", 2026-08-22) parked seven of them in `V2_SCREENS`
(`tools/compare-design.py:123-131`) with the instruction *"If Wi-Fi returns, move
these rows back into FLOW_SCREENS"*. This is that.

**THE STATED COST: `roadmap:1181-1186` GIVES PHASE 4 THE EXIT "drop an EPUB from a
browser onto the device over Wi-Fi", AND THIS BRANCH DOES NOT MEET IT.** Phase 4
splits into three; this is the first third. The roadmap is edited to say so rather
than left carrying an exit nobody is working toward — see **The record**.

**WHAT SAVES SIX SCREENS FROM BEING AN ELABORATE NO-OP** is that the join *verifies*.
The flow's product is not a stored string, it is a network the device has actually
associated with and said so about. `WifiConnect` and `WifiError` are that outcome, and
they are why the connect half is shippable alone. A branch that only wrote credentials
to NVS would be the dead-button defect at feature scale, and this file records three
instances of it at row scale.

## On-demand is forced by the heap, not chosen for battery

The policy was already written down — spec §3.3:151-153, *"WiFi stays OFF except
during transfer/sync/setup — battery first"*, and §4.1b:249, *Settings displays Wi-Fi
as **"on demand"** — never "connected"*. **What is new is that it is no longer a
preference.**

`~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/sdkconfig` fixes the
statically-allocated floor at `esp_wifi_init`:

| setting | value | cost |
|---|--:|---|
| `ESP_WIFI_STATIC_RX_BUFFER_NUM` | 8 | ~1.6 KB each, allocated at init and not freed until deinit — **~12.8 KB** |
| `ESP_WIFI_RX_MGMT_BUF_NUM_DEF` | 5 | ~500 B each — **~2.5 KB** |
| `ESP_WIFI_CSI_ENABLED` | y | *"about `STATIC_RX_BUFFER_NUM` KB"* — **~8 KB, and nothing here uses CSI** |

**~23 KB before lwIP, before the netif and DHCP client, before any dynamic buffer,
and before the driver's own control structures — which live in the closed-source
`libpp.a` and `libnet80211.a` and are UNKNOWN from this environment.** Plus two
tasks: `tcpip_thread` at 4,096 B of stack and the system event task at 2,048.

Against that, `docs/on-device-smoke-checklist.md:246-262` records the smallest free
heap this project has ever measured — **13,696 bytes**, reproducibly, opening a
66,843-byte chapter from a 232-entry Library, found as a `reason=4 PANIC` three times
on 2026-09-07:

| `/books` entries | heap at the Library | cost of the open | left over |
|---|--:|--:|--:|
| 7 | ~168,300 | 63,552 | ~105 KB |
| **232** | **96,272** | 63,552 | **13,696** |

**SO WI-FI AND AN OPEN BOOK CANNOT COEXIST ON A LARGE CARD, AND NO AMOUNT OF TUNING
REACHES IT.** The flow is entered from Settings, which is reached from Home, where the
Reader is not on the stack and the Library is not resident — roughly 133 KB free. That
is where the radio may come up and the only place it does.

**CSI's ~8 KB is the one lever visible from here and it is NOT assumed spent.**
arduino-esp32 ships precompiled, so whether `sdkconfig` can be changed at all is an
open question this spec does not answer. It is listed under **What only the panel can
answer** as a measurement, not a plan.

**Flash is a non-issue and was sized for this deliberately.** `firmware.bin` is
1,582,896 B of `app0`'s 6,553,600 — 24%. `partitions.csv`'s own header says the board
default's 1.31 MB would not do because *"the EPUB container, XHTML parser, layout
engine, Wi-Fi stack and HTTP server are all still to come"*.

**Nothing needs vendoring.** `WiFi`, `WebServer`, `DNSServer` and `ESPmDNS` all ship
with arduino-esp32 3.3.7 (platform `55.03.37`, ESP-IDF 5.5.2). Only `WiFi` is wanted
here. `ESPAsyncWebServer` is *not* bundled and would be a new external dependency —
relevant to the transfer card, not this one.

## The flow, and the stack

```
Home -> Settings -> WifiSettings -> WifiPicker -> WifiPassword
                          |               \             |
                          |                \ (open)     | JOIN
                          |                 v           v
                          |            [Action::replace] -> WifiConnect
                          |                                      |
                          |                              READY --+-- failure
                          |                                |          |
                          +<-------------------------------+     WifiError
                          |
                          +-- hold on a saved row --> WifiNetworkActions
```

**THE CONNECTING DIALOG REPLACES THE JOIN STACK RATHER THAN SITTING ON IT.**
`Action::replace` (`core/src/app.cpp:249-262`) pushes before it erases, so a factory
that refuses leaves the stack exactly as it was. Three things follow, and the third is
the reason:

- Both entry paths — a locked network through the keyboard and an open network
  straight from the picker — arrive identically, so the board's single veiled parent
  is truthful for both. An open network has no `WifiPassword` to veil.
- `WifiError` already veils `WifiSettings` on its board, so the pair is consistent
  with **no board change to the error screen's background**.
- Depth stays at 3 against `kMaxDepth` of 8, with the actions overlay never
  co-resident with the join path.

**AND IT HAS A CONSEQUENCE THAT MUST BE PAID FOR IN THE SHELL** — see
**The join attempt** below. At the moment of failure `WifiPassword` is gone, and
`EDIT PASSWORD`'s entire purpose is fixing a typo in something the user just typed.

**`WifiConnect`'s BOARD CURRENTLY VEILS THE ARTICLES LIST** — the cut Instapaper
screen. `c2468df` rewrote the dialog's two strings when Instapaper was cut and left
the background alone. It becomes `WifiSettings`.

## The radio is up for as little as possible

Two bring-ups, neither spanning a human.

| phase | radio | what is on screen |
|---|---|---|
| scan | **up** | `WifiPicker`, `drawStatusBar` reading `SCANNING` until results land |
| password entry | **down** | `WifiPassword` — minutes of keystrokes at ~520 ms a repaint |
| join | **up** | `WifiConnect`, `CONNECTING...` |
| after `READY` or a failure | **down** | `WifiSettings`, or `WifiError` |

**THE SCAN IS ASYNC AND POLLED FROM `loop()`'s QUIET WINDOW**, which is the pattern
every slow job in this firmware already uses — the deferred page count, the grayscale
refinement, the ring warm, the card-log flush and the quiet-window position save. A
blocking `WiFi.scanNetworks()` takes two to four seconds with the loop frozen, and on
e-ink a frozen loop is indistinguishable from a crash.

**`drawStatusBar` IS NOT NEW** (`core/src/components.cpp:234-260`): it replaces the
hint bar with one centred tracked line and reuses `hintBarHeight`, so the box is
identical and nothing below moves. `LibraryOpening.dc.html` boards the mechanism.

## `WifiSettings`

The hub. Board edits: **the `Start setup hotspot · SHOWS QR` row is removed.**

That row broke the rule that a row states a quantity **or** discloses a screen, never
both — and its screen is out of scope. The precedent is exact and recent: the reader
menu's `Names` row was cut from the board entirely rather than drawn and skipped,
because it waits on a *later release*, where a row waiting on its own screen inside
the same release is drawn and skipped. Removing it dissolves the `SHOWS QR`
inconsistency rather than codifying it, and leaves `SETUP` with one row,
`Join another network…`.

| element | behaviour |
|---|---|
| band | `WI-FI` / `ON DEMAND` — spec §4.1b:249 forbids ever showing `CONNECTED` |
| prose | *"Wi-Fi stays off. It connects only while receiving books — then turns off again."* Unchanged, and true in this release: the radio really is off. |
| `SAVED NETWORKS` | one row per stored network, value `AUTO` or `SAVED` |
| `SETUP` | `Join another network…`, chevron, pushes `WifiPicker` |

**`SELECT` on a saved network toggles `AUTO`/`SAVED`; the hold forgets.** The hold
was already on the board — `WifiSettings.dc.html`'s `SELECT` slot draws
`<circle … fill="none" stroke=…>`, the hollow ring — and spec §4.0:182-185 spells it
out: *"Long-press Confirm on any list item = contextual actions overlay … Saved Wi-Fi
networks: hold to forget."* This is the Library's exact binding
(`core/src/screen_library.cpp:74,90`, `vm_.holds = {false, true, false, false}`), and
it is the **second** hold in the firmware.

**`AUTO` IS A PREFERENCE, NOT A SWITCH**: at most one network holds it, and it means
*tried first when the radio comes up*.

- The **first** network saved becomes `AUTO`, because otherwise a one-network device
  shows a flag that reads as broken.
- Setting `AUTO` on another clears the previous one.
- **Forgetting the `AUTO` network leaves zero `AUTO`. Nothing is silently promoted** —
  a preference belongs to the user, and changing it behind their back with nothing on
  the glass to say so is the class of claim this project refuses for the battery gauge
  and the sleep badge.
- **Zero `AUTO` is legal and is not a dead end**: bring-up then tries every saved
  network in the order the last scan ranked them.

**THE EMPTY STATE IS ITS OWN BOARD.** First run has no saved networks at all, so
`SAVED NETWORKS` is a section header over nothing — a layout no existing board draws,
and it is the state every user meets first. `WifiSettingsEmpty.dc.html`, in
`HomeEmpty`/`NamesEmpty`/`SleepIdle`'s company.

## `WifiPicker`

**It is a real list, not the board's five rows.** A scan in a block of flats returns
thirty, and a dual-band router returns the same SSID twice at different strengths.

- **Scrolls, with the 14px gutter and rail.** Visible rows derived by the theme
  exactly as `libraryVisibleRows` is. CLAUDE.md's rail section says it *"governs every
  scrollable list, and today that is Library alone"* — and its own history named
  `WifiPicker` there until Wi-Fi was cut and the mention went with it. This is the
  second.
- **Sorted by signal, descending.** The network you are standing next to is first.
- **Deduplicated by SSID, keeping the strongest.** A router's two bands take the same
  credential, so two identical rows would be a choice with no meaning.
- **Capped at 20**, with `Rescan` as the final row so it stays reachable in a couple of
  held presses.
- **`Up`/`Down` declare auto-repeat**, as Library and Contents do
  (`core/src/screen_library.cpp:80,96`).

Each row carries the SSID, a padlock when locked, and a four-state signal mark.
**Confirm on an open row skips the keyboard entirely** and goes straight to the
connecting dialog — the board's own note says *"OPEN NETWORKS JOIN DIRECTLY; LOCKED
ONES ASK FOR A PASSWORD"*.

**Two states get boards of their own:** `WifiPickerScrolled.dc.html`, because the rail
takes 14px off every row and that geometry must be stated somewhere a comparison can
read it (`LibraryScrolled.dc.html` is the precedent), and `WifiPickerEmpty.dc.html`,
because zero results needs copy nobody has written — see **Open question for review**.
The scanning state is `drawStatusBar`, already boarded twice, and gets a golden rather
than a third board.

## `WifiPassword`, and the first text entry in this firmware

There is no caret, no editable string, no character set and no keyboard anywhere in
`core/`, `shell/` or `sim/` today. This brings all four.

**Three layers, all exactly 10×4.** A WPA2 passphrase is any printable ASCII, 8 to 63
characters, so all 95 have to be reachable or some passwords are untypeable on this
device.

| layer | contents | key |
|---|---|---|
| base | `a-j` / `k-t` / `u-z 0 1 2 3` / `4-9 . - _ !` — as boarded | — |
| upper | 26 capitals in the same cells | `SHIFT`, **one-shot**, key inverted while armed |
| symbols | the remaining punctuation | `#+=`, **latches** until pressed again |

**Every layer is the same 40 cells**, so the panel geometry never moves and the focus
model never copes with a changing grid. `SHIFT` is one-shot because a passphrase
usually needs one capital; the stated cost is that several capitals in a row cost a
`SHIFT` each. The two layers change glyphs only, so they are **goldens, not boards**.

The function row is `SHIFT`, `#+=`, `SPACE`, `JOIN` — four cells, giving 44 landing
spots in five visual rows.

**AMENDED IN IMPLEMENTATION — the Confirm hint follows the focused cell, and `JOIN`
has a floor.** The spec left the hint bar unstated and the first implementation drew a
constant `TYPE`, which is false on three of the four function keys and outright
misleading on `JOIN`: a Confirm labelled TYPE that leaves the screen and starts a join
is the misleading-button defect this project keeps recording. It reads `TYPE` on a
character cell **and on `SPACE`** (which types a character like any other), `SHIFT` on
`SHIFT`, `#+=` on `#+=`, and `JOIN` on `JOIN`. Settings is the precedent for a label
that varies within a screen; `WifiSettings` is the second instance and this is the
third.

And the *"8 to"* half of the bound above was never implemented. This keyboard is only
ever reached for a **locked** network — an open one joins directly — so `JOIN` under
eight characters cannot succeed: it would spend a radio round trip and a failure dialog
to report a length the counter is already showing. The cell goes inert and the Confirm
slot goes **empty**, which is this firmware's existing vocabulary for a button with no
action (and is 36px wide, not zero) rather than a new one. One expression,
`joinable()`, answers both the bar and the press, so they cannot drift into a cell that
promises `JOIN` and ignores Confirm.

`design/WifiPassword.dc.html` carries the rule; its rendered state does not move,
because the cell it focuses is a character.

**The input model already exists.** `declareSplitMovers` (`core/include/reader/app.h:397`)
is what `ReaderScreen` and `PeekScreen` use: with it, `Up`/`Down` become
`AltPrev`/`AltNext` and the side buttons become `Prev`/`Next`. That is exactly the
board's *"up and down move between rows; the side page buttons move along a row"*.
`shell/src/main.cpp:6102-6111` confirms `Button::Left`/`Right` really are the side
buttons. The hold ring on `DELETE` sits in the `Back` slot, which `gestureFor`
supports (`core/src/gesture.cpp:78-81`).

**The field:**

- **Clear text, always**, as the board's `SHOWN WHILE TYPING` says. At one character
  per ~520 ms repaint on a 44-cell grid, a typo you cannot see is punishing, and this
  is a device you hold.
- **Capped at 63.** Past the box width the text **scrolls** so the caret and the tail
  stay visible and the head runs off — which keeps the box's height fixed, and the box
  is a board element.
- **A wake does not come back here.** The factory refuses an unprimed `WifiPassword`,
  so `App::restore` stops early (`core/src/app.cpp:126-130`) and the reader lands on
  `WifiSettings`. That falls out of the existing rule rather than needing a special
  case, and it is the Peek's precedent. Restoring an empty field instead would
  silently discard what was typed, which reads as the device having eaten it.

## `GridFocus` and `GridFocusScreen`

**`core/include/reader/focus.h` is a single `int index_`** (`:145`) and grep finds no
row or column concept anywhere in `core/`. The gestures are not the problem; the walk
is.

**`GridFocus` composes a `Focus` and adds per-row widths** — `{10,10,10,10,4}` here.
That is `ScrollWindow`'s exact shape, which CLAUDE.md describes as *"a Focus plus the
window around it, so the two concerns are separable"* rather than a reimplementation
of one. Clamp, wrap, the `Gate` and the `set`-clamps/`move`-wraps split all come from
the `Focus` underneath, so none of it is written twice — and **the ragged final row
stops being a special case and becomes a row that is 4 wide**, which is what makes the
test table interesting only at its edges.

**Edges wrap per axis, and the column clamps:**

- Right off the end of a row returns to that row's start; down off the last row
  returns to the top. The two axes are independent, so **the side buttons only ever
  move along a row**, which is what the board promises in as many words. A serpentine
  walk would break that promise.
- Landing in a narrower row clamps the column, and **the original column is
  remembered**, so coming back out of the 4-wide function row returns to where you
  were.
- Held movement clamps, inherited from `Focus` — a hold rests at the end rather than
  rolling over.

**`GridFocusScreen` is a sibling of `FocusScreen`** with the same `final`
`focus()`/`setFocus()` pair, `syncVm()` pure virtual, and an optional `focusable(int)`
gate. One caller today, and that is not the Typography-formatters mistake: the `final`
pair is not a convenience, it is the mechanism that makes a whole class of bug
unwritable. CLAUDE.md records the one-way-screen defect shipping **three separate
times**, each behind a comment arguing why its screen was the exception — every
premise true, every conclusion wrong. **The tempting argument here is precisely the
discredited one**: a wake already refuses this screen, so why does the round trip
matter. Whether a wake can reach a screen is a fact about the factory and the restore
ladder, and encoding it in a `core/` header is how the previous three went one-way.

## `WifiConnect`

**The eight-cell progress ticker is dropped from the board.** It reads as three of a
known total, a join takes an unknown one to ten seconds, and nothing on this device
animates. The label is the whole indicator: `CONNECTING...` steps to `READY`, two
paints for a successful join. The ellipsis is already the indeterminate mark, and a
meter that never moves states a fraction it never measured — the same call as the
reader's `—` page total and `—%`, and as the battery gauge answering `-1` rather than
`0%`.

**Copy changes, because the current words name a transfer this release does not
have.** *"Joining "HOME" to receive books."* becomes *"Joining "HOME" to check the
password."*, and *"WI-FI TURNS OFF WHEN THE TRANSFER FINISHES."* becomes
*"WI-FI TURNS OFF AGAIN WHEN THIS FINISHES."* — which is true now **and stays true
when transfer lands**.

`CANCEL` is the only hint; three empty slots at `kHintEmptySlotW` (36px, not zero —
`core/include/reader/components.h:134`), which is how the board already authors them.

On `READY` the radio goes down and the dialog pops to `WifiSettings`, where the
network now appears in `SAVED NETWORKS`.

## `WifiError` — three shapes, because one sentence would be a lie

A join fails three distinguishable ways, and today's copy claims only the first:
*"PENDRAGON" rejected the password.* Telling someone their password was rejected by a
router that is not there is the false-claim shape this project refuses everywhere
else.

**The precedent is exact.** `BookError` is three boards — damaged, unreadable,
out-of-memory — *because one sentence would be a lie*, and `BookErrorMemory` **drops
its `DELETE FILE…` slab** because the file is fine.

| shape | when | slabs |
|---|---|---|
| **password rejected** | auth/handshake failure | `EDIT PASSWORD` (filled) · `TRY AGAIN` · `CANCEL` |
| **network not found** | the `NO_AP_FOUND` **family** — out of range, a stale scan result, or an AP whose security this device cannot meet | `TRY AGAIN` · `CANCEL` |
| **couldn't finish** | associated, no address; everything else | `TRY AGAIN` · `CANCEL` |

**AMENDED IN IMPLEMENTATION — `NO_AP_FOUND` IS FOUR CODES, NOT ONE.** This line said
`NO_AP_FOUND` and the first implementation mapped 201 alone, so Espressif's 210, 211
and 212 (`_W_COMPATIBLE_SECURITY`, `_IN_AUTHMODE_THRESHOLD`, `_IN_RSSI_THRESHOLD` —
verified in `esp_wifi_types_generic.h:175-177`) fell through to **couldn't finish**,
whose sentence says the network *took the password* — an association that never
happened. A WPA3-only router told the reader its password had been accepted, on the
screen whose three shapes exist so that cannot happen. 212 in particular means the AP
was **heard** and refused as too weak, which is exactly what this shape's *"it may be
out of range"* says.

**`EDIT PASSWORD` IS ABSENT ON THE LATTER TWO, NOT INERT.** The password is not what
went wrong. A slab that draws and does nothing is the works-only-sometimes trap this
project has shipped twice; a slab that is not there teaches nothing because there is
nothing to press. `BookErrorMemory` is the precedent, and its lesson — *do not make it
inert* — applies verbatim.

Two new boards, each one sentence different from the first, exactly as
`BookErrorUnreadable` and `BookErrorMemory` are. All three keep
*"Wi-Fi is off again."*, which is true on every path. The warning triangle is
`kWarning`, already generated for `BatteryEmpty` — reuse, not a new mark.

**The reason mapping lives in `core/`, beside the enum.** `wifiFailureFor(int reason)`
takes the vendor code the radio interface hands back and answers one of the three.
That is `bookErrorReasonFor`'s placement and its argument: a `strcmp` ladder in
`shell/` grows a fourth case nobody remembers to add, in the one directory where
nothing executes it. The cost is stated rather than hidden — `core/` carries about six
integer constants that came from `esp_wifi_types.h`.

## The join attempt, which the collapsed stack makes necessary

At failure the stack is `Settings > WifiSettings > WifiError`. `WifiPassword` is gone.

**The shell holds one join attempt** — SSID, passphrase, and whether the network is
locked — for the life of the flow, and all three slabs read from it:

| slab | does |
|---|---|
| `CANCEL` | drops the attempt, pops to `WifiSettings` |
| `TRY AGAIN` | re-runs the join unchanged |
| `EDIT PASSWORD` | pushes a fresh `WifiPassword` **primed with the SSID and the text already typed**, caret at the end |

Nothing is retyped. This is `openBookAt`'s shape — the one function both a press and a
wake go through, extracted precisely because they must agree — and the priming is the
factory pattern `setReaderDemo`/`setContentsDemo` already use, with a **refusal**
rather than a substitution when unprimed.

## `WifiNetworkActions`

Hold on a saved network opens a **one-row overlay** in `ItemActions`' boarded box:
`Forget network`. Confirm forgets and pops.

`ItemActions` reads the *Library's* focused row, so reuse is not possible — this is a
new `ScreenId` and a new board, and that takes the branch from five screens to six.
Stated rather than buried.

**No second confirmation.** Forgetting a network costs you retyping a password;
deleting a book is irreversible and gets `DeleteConfirm`. The overlay is itself the
step between the press and the loss, which is proportionate — and it matches the
spec's own *"contextual actions overlay"* wording. It is deliberately one row today,
and it is where `Connect now` or `Make automatic` would go later.

## The credential store

**NVS, not the card** — spec :124 and :279 both say so outright, and it is the only
answer that works: `core/include/reader/settings.h` has no string field, no
`<string>` include, and `validate()`'s three correction modes (clamp, snap-to-step,
reset-to-default) fit a credential none of the three ways.

**`kSettingsVersion` does not move.** No Wi-Fi field enters `settings.json`.

### Split readable from secret

| where | holds |
|---|---|
| one payload key, `encre_wifi` | the readable list — SSID, `locked`, `auto` — percent-escaped, with a version key written **after** it |
| one key per network | the passphrase, keyed by a hash of the SSID |

**The readable half is `session_record`'s idiom verbatim**, and for its reasons: one
payload key rather than several, because two keys leave a valid-looking mixed record
when a write is cut; a version written afterwards so it is a real commit record;
percent-escaping because **an SSID can legally contain `;`, `:` and `%`**, exactly the
bytes the format uses; and it stays human-readable so `nvs_get encre_wifi list str`
diagnoses a device.

**THE SECRETS ARE SPLIT OUT BECAUSE `logToCard` EXISTS.** NVS is not encrypted on this
build, so the passphrase is in plaintext either way — what the split prevents is it
being *trivially printable*. One payload key holding everything would put a user's
Wi-Fi password into any serial capture or `/encre.log` that dumps it, and this project
tees its log to a removable card.

**Keyed by a hash of the SSID, not by index.** With index naming, forgetting network 2
renumbers 3 through 8 and every secret has to be rewritten — eight NVS writes, with a
window where a crash attaches passwords to the wrong networks. A hash is stable and a
forget is one delete. That is `/.reader/state/<hash>.json`'s exact mechanism including
its collision handling: the SSID is stored beside the secret and a mismatch means
**drop**, so a collision costs one network its password and can never hand another
network the wrong one. FNV-1a, 8 hex, plus a `p_` prefix — 10 characters against NVS's
15-character key cap.

**Open networks carry an explicit flag and no secret key at all.** Not "absence of a
key means open": a secret write that failed and a genuinely open network would then be
indistinguishable, and that mistake joins a locked network with no password and fails
confusingly. The invariant is checked on load — **a record marked locked with no
secret key is broken and is dropped**, not trusted. There is no warning screen for
joining an open network; the missing padlock says it, and this release sends nothing.

**Cap 8, and the ninth is refused rather than evicting silently.**

### Where the code lives

**Format and invariants in `core/` (`reader/wifi_store.h`); NVS I/O in `shell/`.**
That is `session_record.h`'s split exactly, and for its stated reason — *"`shell/` has
no test harness and this is the only pure logic on the resume path"*. Escaping, the
version, the cap, `locked ⟹ a secret exists` and `at most one AUTO` all get unit tests
driven against malformed input.

**A property falls out of the split: `core/` only ever handles the readable list, so a
passphrase never passes through the serialiser.** The keyboard screen holds the typed
string because it must draw it; nothing that encodes, decodes or logs ever sees one.

## The radio seam

**`reader::WifiRadio`, an interface in `core/` with a fake** — `FileSystem`'s shape,
and `SettingsSink`'s and `CoverSource`'s argument.

The deciding reason is not testability in the abstract. **It is that two of the three
error boards cannot otherwise be rendered from the state they exist for**: you cannot
ask a router to answer `NO_AP_FOUND` on demand, so without injection those boards ship
having only ever been drawn by hand. `Heap::install` exists for exactly this and is the
precedent for injecting a failure the desktop cannot produce.

**IT IS POLL-SHAPED, NOT CALLBACK-SHAPED, AND THAT IS LOAD-BEARING.** Arduino's Wi-Fi
events arrive on the system event task, and `shell/src/main.cpp` creates **no tasks at
all** — `xTaskCreate` appears zero times. A callback mutating `App` state from another
task would be the riskiest thing in this branch, and it buys nothing: every slow job
here already polls from `loop()`'s quiet window.

```
beginScan()  -> scanState() -> scanResults()   // {ssid, rssi, locked}
beginJoin(ssid, psk) -> joinState() -> joinReason()   // vendor int
down()
```

Three implementations, as `FileSystem` has three:

| implementation | in which build |
|---|---|
| `FakeWifiRadio` | unit tests — injectable results, injectable failure reasons |
| a demo radio | the simulator and the goldens, primed like `setContentsDemo` |
| the real one | `shell/`, over Arduino `WiFi` |

The factory **refuses** an unprimed picker rather than substituting demo networks —
the rule that exists because a substituting factory once woke a device into
Middlemarch.

**What this makes testable on the desktop, none of which is today:** the scan's
dedupe and ranking, the reason-to-shape mapping, the `AUTO` invariant, the retry that
must not lose the typed password, and all three error renders.

## Settings regains `CONNECTIONS`

One section header and one `Wi-Fi` row, chevron, **no value** — because
`SettingsScreen`'s own test asserts every drawn row either discloses a screen or states
a value from `settings_`, never both, so the tempting `Wi-Fi · ON DEMAND` is the shape
that rule forbids.

**It does not start Settings scrolling.** `53c92d4` removed `CONNECTIONS` and that
removal is what stopped the list scrolling — *"eleven items where twelve fit"*. The
`SLEEP SCREEN` section has since taken it to nine; a header plus one row makes eleven.
`renderSettings` reads `totalRows > rows` rather than assuming, so **no code change**,
and the rail and its 14px gutter stay off.

This closes the project board's `Restore Settings' CONNECTIONS section` card.
**`Restore HomeEmpty's action slab` stays open and must not be closed with it** — there
is still no way to send books, so a primary action that cannot work is still worse than
none.

## Icons

| mark | source | note |
|---|---|---|
| `kLock` | `WifiPicker.dc.html` | locked rows |
| `kSignal1`..`kSignal3` | `WifiPicker.dc.html` | a family, one per fill state — see below |
| `kRescan` | `WifiPicker.dc.html` | the circular arrow on the last row |
| `kWifi` | `WifiConnect.dc.html` | the arc in the dialog |
| `kWarning` | — | **reuse**, generated for `BatteryEmpty` |

Generated by `iconc.py` from the boards, because assets are generated from the design
and not transcribed — the rule `iconc.py` exists to enforce after it once held copies
and swallowed a design fix. **The four signal states must be drawn distinctly on the
board**, or two collide on identical path data and need the `source` tie-breaker that
already separates `kBook` from `kBookLarge` and `kBattery` from `kBatteryCharging`.

**THE GLYPH IS THREE BARS, NOT FOUR, and this line said four until the board was
counted.** `WifiPicker.dc.html` draws one `<svg viewBox="0 0 17 13">` holding exactly
three `<rect>`s, and three fill states across its rows: 3-of-3, 2-of-3, 1-of-3. So the
family is `kSignal1..kSignal3`. Whether a zero-bar state is wanted is a question the
board does not answer — a scan result with no signal at all is not a row any board
draws.

**AND THE FULL-STRENGTH MARK EXISTS ONLY IN WHITE.** The list is sorted by signal
descending, so 3-of-3 lands on the top row, and the top row is the FOCUSED one, which
is inverted — meaning there is no black 3-of-3 anywhere on either picker board.
Whether `iconc.py` can generate a mark from an SVG whose rects are `fill="#ffffff"`
is an implementation-time question for `make icons`, not a defect in the boards: they
draw what the screens draw. If it cannot, the fix is a board change (an unfocused row
carrying the full-strength mark) and not a hand-written asset — assets are generated
from the design, never transcribed.

`iconc.py` matches on an arbitrary substring rather than on `<path>` data specifically
(`"match": '<rect x="19.5"'` is how `kBattery` is already keyed), so rect-built marks
are supported — but the three states differ only in the `fill` of their second and
third rects, so each `match` has to include that attribute to be distinguishing.

All six are chrome at `Fidelity::Mono`, so they are hard-thresholded. **`kSignal*`'s
bars are axis-aligned and `kLock`'s shackle is a curve** — the thin-diagonal warning
this project records applies to the latter and is a glass question.

## What gets built

| | |
|---|---|
| screens | **6** new `ScreenId`s — `WifiSettings`, `WifiPicker`, `WifiPassword`, `WifiConnect`, `WifiError`, `WifiNetworkActions`. Appended before `Count`, never inserted. |
| bounds | **all six** hand-maintained bounds already name `ScreenId::Count`, so `kNames`, `kAllScreens` and the three every-id walks fail the build until they grow. Run `grep -n "ScreenId::Count" core/src/session_record.cpp test/unit/test_focus_restore.cpp test/unit/test_session_record.cpp` before trusting that. |
| primitives | `reader/gridfocus.h` + `.cpp`, `reader/grid_focus_screen.h` |
| store | `reader/wifi_store.h` + `.cpp` (`core/`); NVS I/O in `shell/` |
| radio | `reader/wifi_radio.h` (interface), `FakeWifiRadio` in `test/unit/`, a demo in `sim/`, the real one in `shell/` |
| view-models | six structs in `viewmodel.h`. No geometry. |
| theme | six `Theme::render*` virtuals, `QuietTheme` implements, built from `drawSectionHeader`, `drawDetailRow`, `drawScrollRail`, `drawPanelCaption`, `drawActionButton`, `wrapProse`/`drawProse`, `drawStatusBar`, `drawHintBar`, `veilRect` |
| icons | `kLock`, `kSignal1-4`, `kRescan`, `kWifi` via `iconc.py` |
| new boards | **6** — `WifiErrorNotFound`, `WifiErrorFailed`, `WifiSettingsEmpty`, `WifiPickerEmpty`, `WifiPickerScrolled`, `WifiNetworkActions` |
| edited boards | **4** — `WifiSettings` (hotspot row out), `WifiConnect` (veil, copy, ticker out), `WifiError` (veil rows 72→80px, the missing SETUP section, and copy off the wrap boundary), `Settings` (`CONNECTIONS` back) |
| simulator | one subcommand per screen and per boarded state |
| compare | move 5 rows `V2_SCREENS` → `FLOW_SCREENS`, add 6 more; denominator **37 → 48** |
| goldens | every screen and boarded state at both geometries, plus the two keyboard layers and `READY` |
| build | `cmake -S . -B build` **after adding sources** — `file(GLOB)` silently ignores them otherwise |

**`ScreenId` goes from 16 members to 22.** The sentinel is what makes that safe: #42's
own history is that the same append-past-a-named-member defect shipped three times,
and the last time it was caught only by a merge in which two screens landed at once.

## Copy is measured against the wrap boundary, in both directions

#76 made this a rule for this panel family after two `BookError` shapes shipped
one and three pixels from it: the firmware's `.rfnt` faces measure ~3% wider than
Chrome's, so a line that merely fits on the board wraps differently on glass, the
centred panel grows, and every rule inside it lands out of register. That defect
measured 11.12%/11.70% against a 3.58% sibling.

**`test_book_error_copy.cpp` CHECKS ONE DIRECTION AND THERE ARE TWO.** It asserts
the NEXT word overflowed by at least 12px, which stops a word coming **up** into a
line. It says nothing about a line sitting flush at the column width, whose own
last word is pushed **down** the moment the face widens — and that is a real state:
`"PENDRAGON" refused the password.` puts line one at **exactly 340px** in a 340px
column, which passes the overflow test and breaks on the device.

Every string in this flow was measured both ways, against a floor of 4% of its own
column. Five were inside it and all five were reworded:

| string | was | now |
|---|---|---|
| `WifiError`'s sentence | +6.3px up | `Wrong password for "PENDRAGON".` — +14 up, 26 down |
| `WifiConnect`'s line | +2.7px up | `…to test the password.` — +86 up, 21 down |
| `WifiConnect`'s note | +11.5px up | `WI-FI TURNS OFF AGAIN AFTERWARDS.` — +93 up, 57 down |
| `WifiSettingsEmpty`'s prose | +11.3px up | `Whichever you save first…` — +46 up, 30 down |
| `WifiPickerEmpty`'s prose | 7px down | split into two paragraphs — +18 up, 32 down |

**One cost is stated rather than hidden:** `WifiError`'s sentence no longer leads
with the SSID, where the other two shapes do. Every SSID-first wording tried sat
inside the floor in one direction or the other, so this is a trade forced by
measurement.

**AND THE HARNESS HAS A LIMIT WORTH KNOWING.** Canvas `measureText` is accurate for
plain text and **disagrees with Chrome's own layout for letter-spaced runs** — it
put the shipped `OPEN NETWORKS JOIN DIRECTLY;…` footer at three lines where the
render draws two. So the tracked-caps footers were **not** cleared by this method,
and the option of moving the 2.4 GHz caveat into `WifiPicker`'s footer could not be
measured. That is part of why the caveat stayed in the empty state's body, and it
is the second half of the open question below.

## Testing

`make test` covers, on the desktop, everything the fake makes reachable:

- **`GridFocus`** — the ragged row, per-axis wrap, column memory, held-clamps, the
  gate. Pinned to the ungated arithmetic by an equivalence property, as
  `test_focus.cpp` does for `Focus`'s gated walk.
- **`wifi_store`** — round trips, an SSID containing `;`/`:`/`%`, a malformed escape,
  a version bump, the cap, `locked ⟹ secret`, `at most one AUTO`, the forget-the-AUTO
  case leaving zero.
- **the flow**, over `FakeWifiRadio` — dedupe, ranking, each failure reason mapping to
  its shape, `EDIT PASSWORD` preserving the typed text, the open-network fork.
- **focus round trip** — every `ScreenId`, including the six new ones, through
  `test_focus_restore.cpp`'s walk and its `movable`/`wrapping` counts.

**Every golden and every new test is proved by mutation, not by passing.** This file
records four distinct ways a mutation lies — it can land on a line the input never
reaches, the fixture can be unable to reach the defect, `cp` plus a same-second compile
can leave a stale object, and `git checkout` to undo a mutation in an uncommitted file
destroys the work being tested. **Commit before mutating.**

## What only the panel can answer

`shell/` has no harness, so not one line of the radio path is executed by the desktop
suite. Named here so the card's move from `On glass` to `Done` has something to be
evidence of. A new section in `docs/on-device-smoke-checklist.md`, grouped by failure
class as that file is:

- **Free heap either side of bring-up, against the derived figure.** This is the one to
  falsify first: the ~23 KB is inferred from `sdkconfig` buffer counts with the
  driver's own structures listed as UNKNOWN, and **this project has been wrong
  reasoning about this hardware from desktop evidence three times.**
- **The flash delta from linking the stack**, which has never been measured here.
- **Whether `ESP_WIFI_CSI_ENABLED` can be turned off at all.** arduino-esp32 ships
  precompiled; ~8 KB says it is worth one attempt, and the answer may be no.
- **All three error shapes reached**, via the flag below.
- **A real scan's duration**, that `SCANNING` shows, and that buttons stay live through
  it.
- **The keyboard typed end to end with a real passphrase.** Whether 44 cells at ~520 ms
  a keystroke is usable is the single question no test can answer, and it is the
  feature's biggest risk.
- **The radio confirmed down** after a successful join *and* after a cancel.
- **A join against WPA3**, and against a hidden SSID.

**`ENCRE_WIFI_FAKE_FAIL=<reason>` is how the failure shapes are reached**, in
`ENCRE_BATTERY_FAKE_PERCENT`'s shape and absent by default. You cannot ask a router to
reject you on demand, and CLAUDE.md is explicit that the battery ladder could not have
been walked on glass without its equivalent — this has three shapes where that had one.

## The record

Five things in the paper trail are wrong, none of them caused by this work. This branch
repairs four and deliberately leaves the fifth.

- **CLAUDE.md's "What V1 is, and is not"** — rewritten: the connect flow is V1.1,
  transfer and the hotspot stay cut. **Its `HomeEmpty` consequence stays true and must
  say so explicitly**, or someone will restore the slab on the strength of Wi-Fi
  existing when there is still no way to send a book.
- **`design/canvas.json`** — `Transfer.dc.html` still sits on the page named
  *"V1 Screens"*; move it to a V2 page as Instapaper's boards were, and add the
  annotation recording the cut that Instapaper got and Wi-Fi never did. Also correct
  `note-wifi-join`, which still claims the hotspot carries Instapaper sign-in that
  `c2468df` deleted. **`make canvas` refuses to place a board itself** — which page it
  belongs on is a design decision, and it prints the next free slot — so this is a hand
  edit, then `make canvas`, then `make canvas-check`.
- **`roadmap:1181-1186`** — Phase 4 split into the three pieces the project board
  already splits it into, so the contradiction with `roadmap:292` stops regenerating.
- **`tools/compare-design.py`** — the five rows move, six are added, and `transfer` and
  `setup_hotspot` stay in `V2_SCREENS`.
- **The spec is left alone.** §3.3 and §4.1b are what this is being implemented *from*.
  They were never edited for the cut and are therefore ahead of the code rather than
  wrong about it; removing them would delete the most complete description of the
  feature that exists.

## Open question for review

**The ESP32-C3 is 2.4 GHz only, and nothing on any board says so.** A 5 GHz-only
network simply does not appear in the scan, and the reader's conclusion will be that
the device is broken rather than that the band is unsupported.

`WifiPickerEmpty.dc.html` needs copy nobody has written, and it is the place this
either gets explained or does not. The question is whether "no networks found" should
name the limitation — *"Encre joins 2.4 GHz networks only."* — or whether a band
caveat on an empty-state screen is noise for the overwhelming majority of readers whose
router broadcasts both.

**It also reaches a screen that is not empty**: a dual-band household sees its network
listed and joins fine, while a 5 GHz-only household sees a list that is missing the one
name they are looking for — which `WifiPickerEmpty` never renders. If the caveat is
worth saying, the honest place may be `WifiPicker`'s footer rather than its empty
state, and that is a board decision before it is a copy one.
