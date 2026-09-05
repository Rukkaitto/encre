# Corpus baseline — 2026-08-28

**Status:** Round 0's deliverable. Measured after the entity fix (`7a83774`) and before
any other leniency work. The next plan is written from this document.

**SUPERSEDED IN ONE ROW, AND THE NUMBERS BELOW ARE LEFT AS MEASURED.** The single
refusal is fixed — an attribute over `kMaxAttrBytes` reads as absent now, issue #35 —
so the corpus opens **225 of 225**, with the other 224 books byte-identical in every
field this document reports. A dated baseline is not rewritten when the thing it
measured moves; it is what the next measurement is compared against.

Spec: `docs/superpowers/specs/2026-08-28-epub-leniency-design.md`
Plan: `docs/superpowers/plans/2026-08-28-epub-leniency-round-0.md`

## The corpus

225 books, 320 MB, `tools/corpus.manifest`. Reproduce with
`python3 tools/corpus.py fetch`.

| source | books | what it is |
|---|---|---|
| gutenberg | 114 | ids spread over twenty years of its own generator |
| standardebooks | 95 | modern EPUB3, **19 of them Kobo `.kepub`** |
| local | 16 | the author's Calibre library: publisher and Calibre output |

**31 candidates were skipped, 20 of them HTTP 429.** One second between requests was
too fast for Standard Ebooks, which is one volunteer's server; the tool's delay is 3 s
now. The shortfall is Standard Ebooks' alone and does not change the shape below.

## The two numbers

```
  open rate     99.56%  (224 of 225)
  whole books   98.67%  (222 opened with nothing missing)
  chapter rate  99.97%  (6746 spine entries)
  text         126,100,595 bytes
```

**By source, which is the number that matters:**

| source | open | chapters | books |
|---|--:|--:|--:|
| gutenberg | 100.00% | 99.94% | 114 |
| standardebooks | 100.00% | 100.00% | 95 |
| **local** | **93.75%** | 100.00% | 16 |

## THE HEADLINE IS TRUE AND IT FLATTERS US

99.56% meets the target that started this work, and **the corpus is not the
population**. 209 of the 225 books are two clean, uniform toolchains that have every
incentive to produce valid EPUBs. The only source made of books somebody actually
bought or converted is the local library, and it scores **93.75%** — one refusal in
sixteen.

The spec predicted exactly this and it is now measured rather than argued. Quote a
per-source figure or none at all.

## Everything that failed, and where it maps

**One book refused, and one cause:**

| refusal | books | audit row |
|---|--:|---|
| `the OPF is malformed` | 1 | **Xml, attribute bytes over 512** — issue #35, **FIXED** |

`Le soleil et l'acier`, in the local library. Calibre writes a `user_metadata`
`<meta content="…">` of 720–848 bytes and `kMaxAttrBytes` is 512, so the OPF errors.

**AND THE CORPUS UNDER-COUNTED IT, WHICH IS THE FINDING RATHER THAN THE FIX.** One
book in 225 reads as a rounding error; a THIRD book off the same shelf
(`Walden ou la vie dans les bois`, 574 decoded bytes) hit it afterwards, and both are
Calibre output. **This corpus is 209 books of Gutenberg and Standard Ebooks against 16
of the population that complains** — the skew this document already warns about, and
here is a case where it hid a defect's real rate behind a denominator that was mostly
publisher output.

**Two chapters lost, and the cause is NOT IN THE AUDIT:**

| loss | chapters | audit row |
|---|--:|---|
| `block too long` | 2 | **none — new** |

`document.h`'s `kMaxBlockBytes` is 64 KB and a block over it sets `error_`, which stops
`BlockReader`, which ends the chapter. Both books are Gutenberg mathematics texts —
`The Number "e"` and `The 32nd Mersenne Prime` — where one paragraph is a hundred
thousand digits.

**It is the entity bug's exact shape**: an error that reports nothing, because
`next()` returning false is also how a chapter ends. It was not in the audit because
the audit was read off the refusal sites, and this one is a truncation site. That is
the single most valuable thing this round produced, and it is the argument for having
built the corpus rather than working straight down the audit table.

## What the audit predicted and the corpus did not hit

**Zero books** in 225 hit any of these:

| audit row | hits |
|---|--:|
| Zip — an unimplemented compression method | 0 |
| Zip — `kMaxEntries` (512) exceeded | 0 |
| Zip — directory ends mid-entry | 0 |
| Xml — more than 16 attributes | 0 |
| Xml — an unquoted attribute value | 0 |
| Epub — no `container.xml` / no rootfile / absent OPF | 0 |
| Epub — a manifest href that does not resolve | 0 |
| Epub — a percent-encoded href | 0 |
| Epub — a spine entry missing from manifest or archive | 0 |
| Epub — spine empty | 0 |

**This is the result that changes the plan.** The spec proposed working down that table
under one rule; the corpus says ten of its rows are unproven and two are real. Doing
them all would be work whose value is asserted rather than measured — which is the
habit this project keeps having to correct.

They are not disproven, and the skew above is the reason to be careful about the
distinction: percent-encoded hrefs and a 512-entry archive are far likelier in the
population the local library represents than in Gutenberg's output. **The honest
statement is "no evidence yet", not "does not happen".**

## What Round 1 should be

1. ~~**`kMaxAttrBytes` — truncate the value instead of erroring.**~~ **DONE, and as a
   DROP rather than a truncation** — a truncated `href` resolves to a path that is
   wrong rather than to nothing, where an absent one is a state every caller already
   handles. `kMaxAttrs` went with it, by the same rule. 225 of 225 open.
2. **`kMaxBlockBytes` — emit what fits instead of erroring.** Two chapters, and the
   same silent-truncation shape the entity fix removed. Needs a decision the corpus
   cannot make: split the block, or truncate it and say so.
3. **Nothing else, until a book demands it.** Grow the corpus toward the population
   that complains — Kobo files, Kindle conversions, publisher output — before spending
   a round on the ten unproven rows.

## What this baseline cannot tell you

- **Nothing here touched a device.** These are desktop runs; heap and flash are not
  measured, and `shell/` has no harness.
- **`truncated` counts chapters, not bytes.** A chapter that lost its last paragraph
  and one that lost everything after its first both count once. The entity fix's own
  before-and-after had to be read in bytes to be seen.
- **A book that opens and reads is not a book that renders.** Layout, pagination and
  the panel are all downstream of everything measured here.
