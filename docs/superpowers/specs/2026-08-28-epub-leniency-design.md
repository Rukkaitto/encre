# LENIENCY — opening the EPUBs people actually have, not the ones the spec describes

**Status:** design approved 2026-08-28. Not implemented. Prompted by a device
report and by the fix that followed it (`ae165d3`). No roadmap phase yet; this is
release-readiness work, and it should land before anyone but the author flashes a
build.

## What prompted it

A `Dune` volume was refused on the device with *"the OPF's unique-identifier names
an id no dc:identifier carries"*. The book was technically invalid and perfectly
readable — 55 spine entries, 197,330 words — and the refusal existed for a consumer
that never arrived. Measured against the reporter's own library the check refused
**4 of 16 books**.

That fix removed one refusal. This is the audit of all of them, and the question it
answers is the one that follows a release: *"my book doesn't open."*

## The rule

**A failure is fatal only when it leaves nothing to read.** Everything else degrades
to the smallest honest unit — an entry, an attribute, a chapter — and every
degradation is counted on the wire even though the panel stays quiet.

Dropping is not the substitution this repo forbids. A dropped chapter leaves a gap;
an invented one is `demoHomeVm()` waking the user into Middlemarch. The line does not
move.

## Two complaint classes, not one

The audit found a second failure shape on the way to the first, and it is the more
insidious of the two.

**Refusal.** The book does not open. Loud, diagnosable, and what the reporter saw.

**Silent truncation.** `document.cpp:279` returns false on an XML error, and
`advance()` returning false already means "the chapter's blocks are exhausted" — so a
malformed chapter *ends early, indistinguishably from a chapter that simply ended*.
That arrives as "my book is missing half a chapter", which is the same email. The
most likely cause of it is the entity gap below, which lands squarely here, so both
classes are in scope for one pass.

## The audit

Every refusal in the chain, and what it becomes. The rows in bold are the ones with
evidence behind them today.

### Zip

| site | today | after |
|---|---|---|
| no EOCD, unreadable directory | refuse | refuse — it is not a zip |
| encrypted flag | refuse | refuse — DRM, nothing to do but say so |
| **an unimplemented compression method** | refuse the whole archive | keep the entry, mark it unreadable; only what needs *that* file fails |
| **`kMaxEntries` (512) exceeded** | refuse | keep the first 512 and carry on; the cap itself does not move until the corpus says it must |
| directory ends mid-entry, a name runs past it | refuse | stop there, keep what parsed |

The cap is the one place leniency costs RAM. `Zip::Entry` is a `std::string` plus
three `uint32`s — ~48 bytes plus name heap — so 512 entries is already **25–40 KB of
transient allocation at open**, against ~133 KB free. Raising it is a heap decision,
not a format one: the corpus decides whether books commonly exceed 512, and whether
`Entry` needs its names packed into one buffer to afford a higher number.

### Xml

| site | today | after |
|---|---|---|
| **an unknown entity, in text or an attribute** | errors the whole document | a named table plus numeric refs; anything still unknown passes through as literal text |
| **attribute bytes over `kMaxAttrBytes` (512)** | errors the whole document | truncate the value, keep parsing — issue #35 |
| more than `kMaxAttrs` (16) attributes | errors | ignore the extras |
| an unquoted attribute value | errors | accept it |
| unterminated comment, CDATA, PI, tag | errors | still errors — but an error ends a *document*, never a book |

**The entity table is HTML 4's 252 named references**, not HTML 5's 2,231: a sorted
static table with a binary search, a few KB of flash against a 6.25 MB partition, and
it covers `&nbsp;` `&mdash;` `&eacute;` `&rsquo;` `&hellip;` — the ones real XHTML
actually carries. Numeric references already work. **Anything still unknown passes
through as the literal text it was**, which also fixes a bare `&`, and means no
future entity can ever cost a chapter again.

### Epub

| site | today | after |
|---|---|---|
| no `META-INF/container.xml` | refuse | refuse — the format's one hard requirement |
| container names no rootfile, or an OPF that is absent | refuse | fall back to the first `.opf` in the archive |
| **a manifest href that does not resolve** | refuse the book | skip that item — usually an image or a remote URL |
| **a percent-encoded href** | reads as absent | decode `%XX`, falling back to the raw name |
| manifest or spine over `kMaxChapters` | refuse | truncate |
| **a spine entry missing from the manifest or the archive** | refuse the book | keep its position, mark it unreadable |
| the spine is empty, or nothing in it is readable | refuse | refuse |

### A dropped chapter keeps its spine position

It is marked unreadable, never removed, and that detail is load-bearing twice.

- **The mechanism already exists.** `ChapterSpan::readable()`, `locate()` returning a
  location with no size, `ChapterReader` refusing it, and `ReaderScreen` skipping a
  pageless entry in whichever direction it was going are all shipped. This reaches for
  them rather than inventing anything.
- **The sidecar stores `spine` as an index into the OPF's own spine.** Compacting the
  list would silently change what every saved position means, so a firmware update
  would move readers to a different chapter — a data-format break disguised as a
  parser fix.

## The corpus

`tools/corpus.py` fetches public-domain EPUBs into a cache **outside the repo**; a few
hundred books is a few hundred MB and none of it gets committed. What is committed is
a **manifest of URL and sha256**, so a measurement taken today is reproducible next
month — the same discipline as a µs figure naming its build type.

Sources are chosen for **generator diversity**, which is what a public corpus is short
on. The books that generate complaints are Kindle conversions, Kobo files and
publisher tools; Standard Ebooks and Gutenberg are clean, uniform output.

| source | what it contributes |
|---|---|
| Standard Ebooks | modern EPUB3, one careful toolchain |
| Project Gutenberg | volume, one generator |
| Feedbooks, archive.org | older EPUB2, mixed publisher output |
| the author's Calibre library | Calibre and NordCompo — both already produced findings |

The probe records each book's `<meta name="generator">` and its
`dc:contributor opf:role="bkp"`, so **the histogram is per toolchain rather than per
book**. If 300 books are four generators, the report says four rather than implying
300 independent samples.

## The harness

`tools/corpus_probe.cpp`, beside `name_probe` and out of ctest for the same reason —
it needs real novels, and the repo's generated EPUBs are 1,400-word stubs. It runs the
real `openBook`, walks every chapter, and emits **one JSON object per book**: opened or
not, the refusal reason, spine length, unreadable spans, blocks, text bytes, whether
any block ended on an error, and the generator. Same code the device runs, so a finding
transfers; runs before and after a fix are directly comparable.

## Two metrics, because one of them lies

- **Open rate** — books yielding at least one readable chapter.
- **Integrity** — readable chapters over spine length, and blocks ending on an error
  rather than on EOF.

A book that opens with 3 of its 92 chapters scores as a success on the first number
alone. Reporting only that is the shape of the card probe answered from cache and of
`make compare` defaulting to seven boards: **a check that reports on less than it
claims is worse than no check, because it is trusted.**

## Order of work

**Round 0** builds `corpus.py` and `corpus_probe`, fetches the corpus, and publishes a
baseline histogram *before any fix* — including which of the audit's rows the corpus
actually hits, and how often. Round 0 also verifies two behaviours this design assumes
but has not confirmed: that a text run over `kTextBytes` continues rather than losing
its tail, and exactly what a mid-chapter XML error costs today.

**Round 0 is its own deliverable and stops for review.** It changes no parser code, so
it can land on its own, and the histogram it produces is what orders everything after
it — planning the fixes before knowing their frequencies would be the same guess this
whole document exists to replace.

**Rounds 1..n** land fixes in frequency order, re-measuring after each.

**The deliverable is the histogram and the residue, not the number 99.** If it settles
at 96.4% with the remainder being DRM and three files that are not zips, that is a
better answer than a 99 nobody can audit.

## Tests

Every mechanism gets a minimal fixture through `mkzip.py` — deterministic, regenerated
into a committed header, byte-identical for the fixtures it already holds — and a unit
test beside it. **The corpus never enters CI**: a test that needs the network is not a
test, and `make test` stays at nine seconds.

**Every new fixture is proved by breaking the code it defends.** This repo has already
had a mutation that was a no-op and read as a passing test, and a golden test that
filled a field with the ink's own colour so every comparison was trivially true.

## What this does not do

- **DRM stays refused.** There is nothing to do with an encrypted archive but say so.
  Improving the *message* is fair game; decrypting is not.
- **Zip64, and anything needing a real HTML5 or CSS parser**, wait for the corpus to
  demand them.
- **No UI.** A book that cannot open still shows what it shows today, and dropped
  chapters are invisible on the panel. That was decided deliberately: the panel stays
  quiet, the wire does not. If the counters later say users need telling, that is a
  board and a screen, and it goes design-first like everything else.
- **No case-folded name lookup, unless the corpus asks for one.** `Zip::find` compares
  bytes because an EPUB names its parts exactly: percent-decoding is a defined
  transformation of a name, where case-folding is a guess about a filesystem this code
  never sees. If books turn out to disagree with their own manifests in case, it
  returns as a *fallback* after an exact match fails, never as the primary lookup.

## Risks

**Leniency masks bugs.** A parser that never refuses can hand back half a book and
call it a success. The counters are the whole defence: `[open] … dropped 3 of 92
chapters` is a bug report, not a success line, and it is the difference between
shipping tolerance and shipping silence.

**The corpus is not the population.** Public-domain books skew clean. The
per-generator histogram is what keeps this visible instead of flattering; the author's
own library is the only sample of publisher output we have, and it is 16 books.

**Heap.** The entity table is flash and cheap. `kMaxEntries` is transient heap at
exactly the moment a book opens, which is the tightest moment there is — 42,152 bytes
free through the Library, where 203 books sit resident underneath. Any move on that
number is a measurement, not a preference.

## Verification

- `make test` green, with a fixture and a mutation-proved test per mechanism.
- A before-and-after histogram over the same manifest, reporting both metrics and the
  per-generator breakdown.
- The four books that already read must keep reading, byte-for-byte identical block
  counts: `Le Fléau`, and the three `Dune` volumes.
- **On glass:** the largest book in the corpus, opened on the device, with `[open]`'s
  heap figures and the `[stack]` line read off the log. Everything else here is
  desktop-measurable; the heap is not, and `shell/` has no harness.
