# The caches

`CLAUDE.md` keeps a stub under this heading and is where the cross-references to it
point. Same standing as anything in that file.

This is the INDEX of every cache in the firmware and the policy each one holds. Where
the depth already exists somewhere else it is pointed at rather than copied —
**`docs/notes/reader.md`**'s *The glyph cache* prices the glyph arena and the pair kern
cache, **`docs/notes/storage.md`** has the card layout the card-side entries sit in, and
`core/include/reader/dir_cache.h` and `dir_counts.h` each carry their own whole argument
at the site. A second copy of a measurement is the defect this repo keeps recording.

## Why the Phase 5 card closed with no code

**`roadmap:1205` says "cache eviction", and what it meant was `/.reader/cache/` —
which was never built.** Spec §5 lists it in the card layout as *"pagination caches
(evicted LRU when space is low)"*, and that is the referent: an LRU policy over a
directory of pagination results. The directory does not exist. `grep -rn
'\.reader/cache'` over the whole tree finds **two** hits and neither is code —
`docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md:274`, which is the spec
line itself, and a comment in `shell/src/sd_selftest.cpp:140` explaining why the
contract harness probes a dot-prefixed parent. Nothing creates the directory, nothing
writes to it and nothing reads it.

**The pagination cache is issue #19, "Pagination + SD cache keyed by settings hash",
and it is open.** So the eviction card was a policy for a store that has no
implementation, and it could not be written without first writing #19. When #19 lands
it brings its own eviction question, and that question should ride on #19 rather than
on an inherited card: which entries are worth keeping cannot be decided before the
entries have a format, and this card outlived the store it was written for by a whole
phase.

**Meanwhile every cache the firmware actually has is already bounded**, and the audit
below is what says so. Some evict, some refuse, two are one-of by construction, and one
card-side store is unbounded on purpose with the reason written down. The sweep did
find one thing nobody had named, and it is a live defect rather than a missing policy:
*The one thing nobody had named: the NINTH network*, at the bottom.

## Everything held, and what holds it

| Cache | Where | Bound | Policy | At the limit |
|---|---|---|---|---|
| Glyph arena, per body face | RAM | 16 KB (roman) / 10 KB (italic) stated at ppem 32, derived per size, ceiling 150% | **FIFO by arena overwrite.** Not LRU | Wraps and re-rasterises. ~3,794 µs a glyph on the panel |
| Advance/gid cache, per body face | RAM | 256 entries, Latin-1 only | Direct-mapped; a collision overwrites | Nothing: `cp >= 256` never enters it |
| Pair kern cache, per body face | RAM | 512 slots, 1,536 B | Direct-mapped; a collision overwrites | Re-bisects the face's 6,064-pair table |
| Page ring (`ReaderScreen`) | RAM | Depth 3, ceiling 8; 1,471 B (X4) / 1,512 B (X3) a page | Drop the oldest; **whole ring dropped on any layout change** | A backward turn rewinds and re-decodes from the chapter start |
| Directory listings (`DirListingCache`) | RAM, device only | **2 slots**, 20 KB across both, min 32 entries | **Evict the SMALLEST held listing.** Refuses a listing below the minimum or over the ceiling | A miss costs the card: 2.90 ms an entry, 590 ms for 203 books |
| Folder book counts (`DirCountCache`) | RAM, device only | 64 folders, 2,048 path bytes, ~1.5 KB full | **REFUSES the 65th.** Never evicts | The folders past 64 pay a listing each, every time |
| Spine geometry (`ReaderScreen`) | RAM | One book; 12 B an entry, 1,104 B for 92 chapters | Read once per book, dropped with the book | n/a — it is the whole book by construction |
| Card log buffer (`CardLogBuffer`) | RAM | 4,096 B | **Refuses the NEWEST whole line** and counts the bytes dropped | A forced flush at 1,024 B free is what keeps it from getting there |
| `/encre.log` | Card | 256 KB (`kLogFileCapBytes`) | **Truncate and reopen** — loses the OLDEST | The log restarts empty; the count of lost bytes does not ride across |
| `/.reader/sleep.cover` | Card | **ONE file**, by construction | Overwritten when the last-read book changes | Alternating between two books re-decodes on each sleep |
| `/.reader/articles/*.epub`, `*.json` | Card | `articlesKeepOffline`, default 50 | Oldest first, **skipping starred and started** | See below — this is a floor, not a ceiling |
| `/.reader/articles/queue/` | Card | One marker per article per action | Cleared on ack; the opposite marker is REMOVED rather than contradicted | Self-clearing: a push acks on 2xx **or 404** |
| Saved Wi-Fi networks | NVS `encre_wifi` | 8 (`kMaxSavedNetworks`) | **REFUSES the ninth.** Never evicts | The reader is told, on `WifiError`'s fourth copy shape, and nothing is written — see below |
| Session record | NVS `encre_sess` | `App::kMaxDepth` = 8 stack entries | One record, overwritten | n/a |
| Home's `LIBRARY` count | RAM | One integer | Keyed on `SdFileSystem::removals()` | One listing plus one per folder to re-derive |
| `/.reader/state/<hash>.json` | Card | **None, deliberately** | Never pruned | See *Unbounded on purpose* |
| Wi-Fi passphrases | NVS `encre_wpsk` | One key per SSID | Deleted by `forget` only | A refused join no longer writes one; the older orphans stay — see below |

## The policies that are not the obvious one

**THE GLYPH ARENA IS FIFO AND THE HEADER ARGUES THE CASE AT THE SITE.** A
malloc-per-glyph cache with LRU eviction fragments a 240 KB heap, and on this part an
allocation that cannot be served is `abort()` with nothing on the serial line. A single
arena written strictly forwards cannot fragment: every eviction is "the bytes I am
about to overwrite". `core/src/scalablefont.cpp` says so where the ring lives, and
**`docs/notes/reader.md`**'s *The glyph cache* carries the sizing table, the `u(p) =
39p² + 300p` curve and the 150% ceiling. Two things about it belong here rather than
there, because they are policy rather than sizing:

- **A glyph too big for the whole arena goes through a BYPASS buffer.** It is not
  refused and it does not evict anything — `reserve()` answers `kNoRoom` and the
  rasteriser writes into a separately grown scratch block. A memo may miss; it may not
  lie.
- **`release()` HANDS THE WHOLE ARENA BACK, and one flow takes it.** The two body faces
  hold 26 KB at ppem 32 and nothing outside the Reader draws with them, so a wallabag
  sync gives both back for its duration. **`docs/notes/articles.md`** records what that
  bought: without it the fourth request aborted with 716 bytes free.

**THE ADVANCE AND KERN CACHES ARE DROPPED WHOLESALE BY `init()`, WHICH IS THE HALF THAT
IS EASY TO MISS.** Neither is evicted by policy — a collision simply overwrites — but
both hold values in PIXELS, so a size change invalidates every entry at once. A cached
kern outliving its ppem is text that is uniformly, subtly mis-spaced with no glyph
wrong, which is invisible to every golden. `ScalableFont::init()` is what makes the
Typography panel's `Size` row safe.

**THE LISTING CACHE EVICTS THE SMALLEST, AND THAT IS THE WHOLE REASON IT WORKS ON A
REAL CARD.** `LibraryScreen::rescan()` lists two directories — `/books`, which is
expensive, and `/.reader/state`, which is not — so LRU or FIFO over two slots would
hand `/books` to whichever listing came last. "Do not throw away the expensive one" has
to be a rule rather than a hope, and smallest-first is that rule. `dir_cache.h` has the
measurement the cache exists for and the SdFat walk it could not make faster.

- **`openRead()` clears the listings and `forgetCardFacts()` clears both**, and the
  distinction is deliberate rather than an oversight. Opening a file changes nothing
  about what is on the card, so the listings have not become WRONG — they have become
  expensive at the wrong moment, against the 45,840-byte floor a book open measures.
  That is eviction. `writeAll`, `mkdirs` and `remove` call `forgetCardFacts()` instead,
  which is invalidation, and they call it above their own refusals. The ~1.5 KB of
  folder counts survive a book open, which is what makes Home → book → Back → Library
  cost no folder listings.

**THE FOLDER COUNTS REFUSE RATHER THAN EVICT, AND THE ARGUMENT IS STABILITY.** A card
past 64 folders keeps a stable subset that is cheap, where an evicting memo of the same
size would thrash and be cheap for nobody. The Wi-Fi list refuses in the same shape for
a different reason, which is that what it would evict cannot be recovered.

**THE TWO HALVES OF THE LOG HOLD OPPOSITE POLICIES, ON PURPOSE.** `/encre.log`
truncates and reopens, losing the OLDEST, because `sd_fs.h` states the trade at the
site: refusing to write once full loses the newest, which is the half you want. The RAM
buffer in front of it does exactly the opposite — `CardLogBuffer::append` refuses a
whole line rather than truncating one, and counts the bytes. Half a log line reads as a
corrupt log where a missing one plus a count reads as what it is, and a buffer that
dropped its oldest would need a ring the shell cannot afford to memmove. What reconciles
them is `mustFlush(kLogLineReserveBytes)`: the reserve is restored every loop iteration
rather than once per idle window, so a drop needs more than 1,024 bytes to arrive inside
one iteration. #83 is what that is worth: measured on an X3 on 2026-09-07, the one-shot
reserve was overrun by **407 B** and the log lost whole lines in the window a fault is
most interesting.

**THE SLEEP COVER IS ONE FILE AND ITS HEADER NAMES THIS CARD.**
`core/include/reader/sleep_cover.h:34` says, in as many words, that being scoped like
`last.json` "makes cache eviction — its own Phase 5 card — a non-problem by
construction", and states the cost: alternating between two books re-decodes on each
sleep. The file is a 160-byte header plus two planes of `Framebuffer::sizeBytes()`,
which is `rowBytes × physHeight` — 52,272 bytes a plane at 528×792, so 104,704 bytes in
all. That arithmetic is done here and is stated nowhere else.
**`docs/notes/covers.md`** has the decode path.

**THE ARTICLE QUEUE IS MARKER FILES AND IT CLEARS ITSELF.** One empty file per pending
action under `/.reader/articles/queue/`; starring and then unstarring REMOVES the
opposite marker rather than appending a contradiction, so the server is owed one action.
The push acks on **2xx or 404**, so a marker for an article the server no longer has is
cleared rather than retried for ever. The queue is therefore bounded by the actions a
reader took since the last successful push, which is a quantity that returns to zero.
**`docs/notes/articles.md`** has the rest.

## `prune` is a floor, not a ceiling

**`ArticleStore::prune(keep)` does NOT guarantee `keep` articles, and this is the one
row of the audit whose obvious reading is wrong.** It walks oldest-first and skips
anything starred or with a reading position (`core/src/article_store.cpp:244-245`), so a
card of 200 started articles against `articlesKeepOffline = 50` keeps all 200. The
skipping is right — a star is the reader saying so and a reading position is attention
already spent, and taking either away to make room for something unopened is the wrong
trade — and the consequence is that `keep` is a target the policy may decline to reach.

**Nothing about that is a defect, and it is written here so the number is not read as a
bound.** The articles a reader has starred or started are ones they chose; a card fills
with them at the rate a reader chooses them, which is slow, and the remedy exists
already in `removeAll()` behind the account screen. It is worth a card only if a real
card is ever seen filling this way, and none has been.

## Unbounded on purpose

**`/.reader/state/` GROWS ONE SIDECAR PER BOOK EVER OPENED AND IS DELIBERATELY NEVER
PRUNED.** Spec §4.0 (`docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md:184`)
says deleting a book "never erases reading progress", so a book that comes back should
still know where the reader was. `forgetLastRead` removes the POINTER for a book that is
gone and says at the site that it deliberately does not touch the sidecars. A record is
one small JSON keyed by an FNV-1a hash of the book path; the path is stored inside it, so
a hash collision costs one book its position and can never hand another book the wrong
one.

- **The RAM consequence is a transient and is worth stating because it is the part that
  scales.** `loadProgressIndex` builds one `ProgressEntry` — two `std::string`s, an int
  and a bool — for every sidecar on the card, and it is called on every Library push,
  every Articles list build and every Book details open. It is a LOCAL in each caller
  and is released when the screen finishes building, so it is not resident and it is not
  at the reading floor. **It is unmeasured on the device**; the figure this project has
  is the Library's own residency (~291 bytes a book, `CLAUDE.md`), which is a different
  vector. If a card is ever seen with hundreds of started books, this is the allocation
  to measure before anything else.
- `percentFor` is a linear scan and `reading_store.h` says so, with "if that ever stops
  being true this is the line to change" beside it.

**THE WI-FI LIST REFUSES THE NINTH RATHER THAN EVICTING THE OLDEST, AND THE REASON IS
THAT EVICTION LOSES SOMETHING THE DEVICE CANNOT RECOVER.** A passphrase was typed on a
six-button e-reader; dropping one silently costs the reader that work and gives them no
way to know it happened. Eight covers home, work and a cafe, and the whole list at that
cap is under a kilobyte of a 20 KB NVS partition (`wifi_store.h`). **The policy is
right and its one caller does not honour it** — see the section below.

## The one thing nobody had named: the NINTH network

**FIXED IN #162, AND THE REFUSAL NOW REACHES THE GLASS.** What this section reported is
below, unchanged, because the diagnosis is what makes the fix legible; what follows it
is what shipped.

**THE WI-FI LIST REFUSES A NINTH NETWORK AND THE SHELL THREW THE REFUSAL AWAY, SO THE
PASSPHRASE WAS WRITTEN AND ORPHANED ON THE SPOT.** The refusal is the right policy and
`wifi_store.h` argues it well; what nobody had looked at is what the one caller did
with the answer. Three lines, all in the `JoinState::Ok` branch of
`shell/src/main.cpp`:

```
gWifiNets.remember(gJoinSsid, gJoinLocked);   // returned false when full() -- DISCARDED
if (gJoinLocked) shellwifi::putSecret(gJoinSsid, gJoinPsk);   // wrote anyway
shellwifi::save(gWifiNets);                   // wrote the UNCHANGED eight
```

`SavedNetworks::remember` answered `false` on `full()` and that is its ONLY caller in
the firmware. So on a device already holding eight networks, a ninth join that SUCCEEDS
was not saved, its passphrase landed in `encre_wpsk` under a key no list entry hashes
to, and NVS has no delete-by-prefix to find it again.

**THE FLOW'S OWN CONFIRMATION IS THE THING THAT WENT MISSING.** The block's comment
says the flow leaves for the saved-network hub rather than stepping the dialog to
READY, because "the hub opening with the network in it IS the confirmation". On the
ninth network the hub opened without it. The reader joined, was shown a success, and
landed on a list that did not contain what they had just joined.

**And the orphan has a second, quieter route, which is NOT fixed.** `wifi_store_nvs.h`
states at the site that "`remember` adds a row before a passphrase exists and `forget`
removes the row, so the two move independently", and that `save()` does not touch the
secrets. Only `forget()` knows which secret has become unreachable. So a list that
fails to DECODE is read as "no saved networks" — the correct reading of an unparseable
record — and abandons up to eight secrets in one step, with nothing left that could
name them.

### What shipped

**THE ANSWER CANNOT BE DROPPED ANY MORE, and that is the half that is structural.**
`remember()` returns `Remembered` — `Yes`, `ListFull` or `BadSsid` — and is
`[[nodiscard]]`, so the statement form that caused this fails to COMPILE, on every
build including the firmware's. That matters here more than it usually would: `shell/`
has no desktop harness, so the compiler is the only thing that reads that branch at
all.

**IT IS THREE VALUES BECAUSE `false` WAS TWO FACTS.** A dialog saying the list is full
would be a lie about an SSID no list would have taken, whatever its length. `BadSsid`
cannot arrive from a join — `rankScanResults` drops blank SSIDs and 802.11 caps the
field at 32 bytes — so it has no copy and no board, and the shell logs it.

**AND THE READER IS TOLD.** `JoinFailure::ListFull` is a fourth copy shape on
`WifiError` (`design/WifiErrorListFull.dc.html`), captioned `COULDN'T SAVE IT` rather
than `COULDN'T JOIN` because the join SUCCEEDED, with one `OK` slab: `EDIT PASSWORD`
points at a password that worked and `TRY AGAIN` would be refused identically, so both
are absent rather than inert. The sentence names the cap and the remedy, because a
refusal with no remedy reads as a fault.

**NOTHING IS WRITTEN ON A REFUSAL** — not the list, not the secret. `dropSecret` is
deliberately not called either: it is `forget`'s tool for a key this device PUT there,
and on this path none was put, so the call could only delete whatever a colliding hash
happens to name.

**THE SWEEP IS STILL NOT DONE, and it is still worth a card of its own.** Deleting any
`p_` key in `encre_wpsk` that no entry hashes to, after a successful `save()`, is the
larger half: it would be the first code in this firmware to enumerate NVS keys, and it
buys back bytes rather than behaviour. It is what would collect the orphans already on
a device, and the decode-failure route above, which #162 does not touch.

**STILL NOT OBSERVED ON GLASS, and the suite cannot make it so.** The fix is
unit-tested in `core/` — the refusal's name, the copy, the slab list and two goldens —
and the shell branch that reads it is executed by nothing on the desktop, which is the
whole reason `[[nodiscard]]` is doing the work rather than a test. Nobody has taken a
device to nine networks. Confirming it needs a device and nine joins, or a build with
`kMaxSavedNetworks` temporarily at 2.

## What is NOT a cache, checked so the sweep reads as complete

- **The chrome type ramp.** The eleven `.rfnt` roles are pre-rendered and live in
  memory-mapped flash as `const` arrays. `core/include/reader/font.h` holds no mutable
  state and no cache at all; only the scalable body faces cost RAM. **`CLAUDE.md`**'s
  **Type** section has the ramp.
- **The icons.** All thirteen marks are generated at build time into `core/src/icons.cpp`
  and are `const` data.
- **`/fonts/` and `/sleep/`.** Both appear in spec §5 and neither is read by any code in
  this tree. User-installed fonts and user sleep images are V2 and the roadmap cuts the
  second explicitly.
- **`SdFileSystem::readAll`'s 64 KB cap** is a refusal, not a cache: it turns "someone
  put a 300 MB file where a settings file goes" into a clean false under
  `-fno-exceptions`. `openRead` needs no equivalent because the handle allocates a fixed
  few dozen bytes whatever the file's size.
- **`starts_`** in `ReaderScreen` is one `Cursor` per page of the OPEN chapter. It grows
  with the chapter as it is read and is cleared on a chapter change or a layout change,
  so it is the chapter's index rather than a cache of anything. **`docs/notes/reader.md`**
  has the deferred page count that produces it.

## What was checked, and what was not

**Checked by reading the code**, every row of the table above, plus `grep -rn
'\.reader/cache'` over the tree (two hits, both prose), `grep -rn 'Preferences'` over
`shell/` for the NVS namespaces (`encre_wifi`, `encre_wpsk`, `encre_wbg`, `encre_sess`,
`encre_diag`), and a sweep of `core/include/reader/*.h` for members that only grow.

**NOT checked on glass.** No figure in this note was measured for it. Every number is
quoted from the code that states it or from a note that measured it, with ONE exception:
the sleep cover's 52,272 and 104,704 are arithmetic over `Framebuffer::sizeBytes()` at
528×792, done here and not found anywhere else. The ninth-network case has never been
OBSERVED either; it is read off `wifi_store.cpp:180` and `main.cpp:4246-4248`, and
nobody has taken a device to nine networks.

**A CALLER LIST WAS THE THING THAT FOUND IT, and the first version of this note had the
row wrong because it skipped one.** The table said of the Wi-Fi cap that "the reader is
told; nothing typed is silently lost", which was an inference from a good policy rather
than a fact about a caller. `grep` for `full()` across `shell/` and the Wi-Fi screens
returned nothing, which looked like confirmation that the limit never surfaces and was
instead a grep that had missed `remember()`'s own use of it one directory over. Reading
`remember()` gave the refusal; reading its ONE caller gave the defect. **Naming the
caller is the check**, which is `CLAUDE.md`'s own rule arriving again.

**NOT swept: `sim/`, `tools/` and `test/`.** They hold caches of their own (the
simulator's font loading, the compare tool's render cache) and none of them ships.
