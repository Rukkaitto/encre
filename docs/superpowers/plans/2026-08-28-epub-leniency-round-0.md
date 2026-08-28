# EPUB Leniency — the entity fix, and Round 0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop a named entity throwing away the rest of a chapter, then build the
corpus and the probe that order every remaining leniency fix by measured frequency.

**Architecture:** Part A fixes `Xml::decodeEntity` — a generated HTML 4 table for the
252 names, and literal passthrough for anything still unknown, so no future entity can
ever cost a chapter again. Part B builds `tools/corpus.py` (fetch a manifest of public
EPUBs), `tools/corpus_probe.cpp` (run the real `openBook` over them and emit JSONL) and
`tools/corpus_report.py` (two metrics, a histogram by reason and by generator), then
records a baseline.

**Tech Stack:** C++20 in `core/`, doctest, CMake; Python 3 for the generators, matching
`mkzip.py` and `iconc.py`; `curl`-free fetching via `urllib` from the standard library.

**Spec:** `docs/superpowers/specs/2026-08-28-epub-leniency-design.md`

---

## Deviation from the spec, and why

The spec puts Round 0 first and says *"the frequencies are what order the work"*. Part A
jumps that queue because the frequencies for it are already in hand, measured on the
author's own library before this plan was written:

| finding | number |
|---|---|
| distinct named entities across 16 real books | **7** — `rsquo` `nbsp` `mdash` `ndash` `ldquo` `rdquo` `lsquo` |
| occurrences | 50,245 |
| `Dark Plagueis — Ultimate Edition` | **177 of 183 chapters truncated**, 3,214 bytes of text recovered |
| `Darkly Dreaming Dexter` | 1 of 34 chapters truncated |

`Dark Plagueis` opens today and is effectively empty. This is silent data loss in
shipped firmware, it is measured rather than guessed, and it is cheap — so it goes
first. If you would rather hold to the spec's order, do Part B first; nothing in Part B
depends on Part A.

---

## File Structure

**Part A**

- Create: `tools/entities.py` — generates the named-entity table from Python's
  `html.entities.name2codepoint`. Nothing is vendored and nothing is typed by hand.
- Create: `core/src/entity_table.h` — generated, committed, the same relationship
  `iconc.py` has with `icons_data.h`.
- Modify: `core/src/xml.cpp` — `decodeEntity` consults the table, then falls back to
  passthrough; its two callers widen their capacity reserve.
- Modify: `test/unit/test_xml.cpp` — invert the "an unknown entity is malformed" case,
  add table and passthrough cases.
- Modify: `test/unit/test_document.cpp` — the chapter-level proof that text after an
  entity survives.
- Modify: `Makefile` — an `entities` target beside `zips`.

**Part B**

- Create: `tools/corpus.py` — `discover`, `fetch` and `verify` over a manifest.
- Create: `tools/corpus.manifest` — committed. URL, sha256, size, source. No books.
- Create: `tools/corpus_probe.cpp` — one JSON object per book on stdout.
- Create: `tools/corpus_report.py` — histogram and the two metrics.
- Modify: `CMakeLists.txt` — a `corpus_probe` target beside `name_probe`.
- Create: `docs/superpowers/plans/2026-08-28-corpus-baseline.md` — the baseline report,
  committed so the after-run has a before to be compared against.

---

## Task 1: A chapter must survive a named entity

**Files:**
- Test: `test/unit/test_xml.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `test/unit/test_xml.cpp`. Note `\xC2\xA0` is U+00A0 and `\xE2\x80\x94` is an em
dash — the same spelling the existing numeric-reference test uses.

```cpp
TEST_CASE("the HTML 4 named entities decode, in text and in attributes") {
  // MEASURED, not chosen: seven distinct named entities appear across sixteen real
  // books -- rsquo, nbsp, mdash, ndash, ldquo, rdquo, lsquo, 50,245 occurrences --
  // and every one is HTML 4 punctuation. The table is all 252 because the whole set
  // costs 1,416 bytes of names and typing a subset invites a second pass.
  Xml x("<p title='a&nbsp;b'>x&rsquo;y &mdash; &ndash; &ldquo;q&rdquo;</p>");
  REQUIRE(x.next() == Node::StartTag);
  CHECK(x.attr("title") == "a\xC2\xA0" "b");
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "x\xE2\x80\x99" "y \xE2\x80\x94 \xE2\x80\x93 \xE2\x80\x9C" "q\xE2\x80\x9D");
}

TEST_CASE("an entity we still do not know passes through as its own text") {
  // THE RULE THIS REVERSES is in decodeEntity's own header: an unknown entity meant
  // "we are wrong about the file, not that the file is being casual". Half of that
  // is right and the table above is the half that acts on it. The other half was
  // measured and is false: `Dark Plagueis` lost 177 of its 183 chapters this way,
  // because erroring here truncates the chapter at that byte.
  //
  // A visible wrong beats an invisible one, which is the call css.h already makes
  // for over-matched italics: a literal "&unknown;" on the page is a typographic
  // error you can see and report, where a silently discarded chapter is not.
  Xml x("<p>Tom &unknown; Jerry</p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "Tom &unknown; Jerry");
}

TEST_CASE("a bare ampersand survives, because real books contain them") {
  // Not an entity at all: no ';' before the run ends. Today this errors and takes
  // the chapter with it.
  Xml x("<p>Tom & Jerry</p>");
  REQUIRE(x.next() == Node::StartTag);
  REQUIRE(x.next() == Node::Text);
  CHECK(x.text() == "Tom & Jerry");
}
```

- [ ] **Step 2: Delete the test that pins the old rule**

Remove the whole `TEST_CASE("an unknown entity is malformed, not passed through")` from
`test/unit/test_xml.cpp`. It asserts the behaviour being reversed, and leaving it means
the suite contradicts itself.

- [ ] **Step 3: Run the tests to verify they fail**

```bash
cmake --build build -j --target unit_tests && ./build/unit_tests -tc="*named entities*,*passes through*,*bare ampersand*"
```

Expected: 3 test cases, 3 failed. The first two fail on the entity being rejected; the
third fails because a bare `&` is treated as the start of a reference.

- [ ] **Step 4: Commit the failing tests**

```bash
git add test/unit/test_xml.cpp
git commit -m "test: a chapter must survive an entity it does not know"
```

---

## Task 2: Generate the entity table

**Files:**
- Create: `tools/entities.py`
- Create: `core/src/entity_table.h`
- Modify: `Makefile`

- [ ] **Step 1: Write the generator**

Create `tools/entities.py`:

```python
#!/usr/bin/env python3
"""Generate the named-entity table for Xml::decodeEntity, as a C++ header.

Why a generator: the same reason mkzip.py and iconc.py are generators. 252 names
typed by hand is 252 chances to transpose a code point, and the authority is already
on the machine -- Python's `html.entities.name2codepoint` IS the HTML 4.01 list.

Why HTML 4's 252 and not HTML 5's 2,125: the longest HTML 4 name is 8 bytes, which
fits xml.cpp's existing 16-byte entity scratch with room to spare, and the measured
need is seven names. The passthrough in decodeEntity is what makes that a safe
choice rather than a bet -- an entity outside this table costs its own five
characters of ugliness, never a chapter.

Usage:
    python3 tools/entities.py --out core/src/entity_table.h
"""

import argparse
from html.entities import name2codepoint


def emit(out_path):
    rows = sorted(name2codepoint.items())
    longest = max(len(n) for n, _ in rows)
    lines = [
        "#pragma once",
        "// GENERATED by tools/entities.py -- do not edit.",
        "//",
        "// HTML 4.01's named character references, sorted by name so decodeEntity can",
        "// binary-search them. An entity NOT in here is passed through as its own text,",
        "// which is why this table being incomplete can never cost a chapter.",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace reader {",
        "",
        "struct NamedEntity {",
        "  const char* name;",
        "  uint32_t cp;",
        "};",
        "",
        "// The longest name here is %d bytes, which is why xml.cpp's 16-byte entity" % longest,
        "// scratch is enough to hold any of them.",
        "inline constexpr NamedEntity kNamedEntities[] = {",
    ]
    for name, cp in rows:
        lines.append('    {"%s", 0x%04X},' % (name, cp))
    lines += [
        "};",
        "",
        "inline constexpr size_t kNamedEntityCount =",
        "    sizeof(kNamedEntities) / sizeof(kNamedEntities[0]);",
        "",
        "}  // namespace reader",
        "",
    ]
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines))
    print("%s: %d entities, longest name %d bytes" % (out_path, len(rows), longest))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the entity table header")
    emit(ap.parse_args().out)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Generate the header and check what landed**

```bash
python3 tools/entities.py --out core/src/entity_table.h
```

Expected: `core/src/entity_table.h: 252 entities, longest name 8 bytes`

```bash
grep -c '^    {"' core/src/entity_table.h && grep -E '"(nbsp|rsquo|mdash|ndash|ldquo|rdquo|lsquo)"' core/src/entity_table.h
```

Expected: `252`, then seven rows — `{"ldquo", 0x201C},` `{"lsquo", 0x2018},`
`{"mdash", 0x2014},` `{"nbsp", 0x00A0},` `{"ndash", 0x2013},` `{"rdquo", 0x201D},`
`{"rsquo", 0x2019},`. These are the seven the library measured; if any is missing the
generator is wrong, not the measurement.

- [ ] **Step 3: Add the Makefile target**

In `Makefile`, directly after the `zips:` target, add:

```make
# The named-entity table for the XML tokenizer, generated from Python's own HTML 4
# list -- the same relationship iconc.py has with icons_data.h. Regenerate and commit;
# nothing in the build runs this.
entities:
	$(PYTHON) tools/entities.py --out core/src/entity_table.h
```

- [ ] **Step 4: Verify the generator is deterministic**

```bash
python3 tools/entities.py --out /tmp/et.h && diff -q /tmp/et.h core/src/entity_table.h && echo "deterministic"
```

Expected: `deterministic`. A generated file that does not reproduce itself cannot be
trusted to be in sync with its generator.

- [ ] **Step 5: Commit**

```bash
git add tools/entities.py core/src/entity_table.h Makefile
git commit -m "feat: generate the HTML 4 entity table rather than typing it"
```

---

## Task 3: Look the entity up, and pass through what is left

**Files:**
- Modify: `core/src/xml.cpp:159-200` (`decodeEntity`), `:247-251` and `:375-383` (its
  two callers)

- [ ] **Step 1: Replace decodeEntity's header comment**

The comment at `core/src/xml.cpp:155-158` argues for the behaviour being removed. Replace
those four lines with:

```cpp
// AN UNKNOWN ENTITY IS PASSED THROUGH, AND THAT REVERSES WHAT THIS SAID.
//
// It said a literal "&nbsp;" reaching a paragraph "reads as a rendering bug and is
// really a parsing one", and that an unknown entity means "we are wrong about the
// file, not that the file is being casual". The first half was right and the table
// below is what acts on it -- 252 names, generated, so `&nbsp;` and `&rsquo;` are no
// longer unknown.
//
// The second half was measured and is false. Erroring here does not report anything:
// document.cpp stops on Node::Error and ChapterReader::next() then returns false,
// which is INDISTINGUISHABLE from the chapter ending. Across sixteen real books that
// cost `Dark Plagueis` 177 of its 183 chapters -- a book that opens, and is empty.
//
// So a visible wrong beats an invisible one, which is the call css.h already makes
// for over-matched italics. A stray "&unknown;" on the page is a typographic error a
// reader can see and report; a discarded chapter is not.
```

- [ ] **Step 2: Add the table lookup and the passthrough**

In `core/src/xml.cpp`, add the include beside the others at the top of the file:

```cpp
#include "entity_table.h"
```

Then in `decodeEntity`, replace the five `if (r == "amp")`-style blocks *and* the
`if (r[0] != '#') return false;` line with the following. The five predefined names stay
first: they are in the generated table too, but they are the overwhelmingly common case
and a compare beats a binary search for them.

```cpp
  if (r == "amp") { out[0] = '&'; outLen = 1; return true; }
  if (r == "lt")  { out[0] = '<'; outLen = 1; return true; }
  if (r == "gt")  { out[0] = '>'; outLen = 1; return true; }
  if (r == "quot") { out[0] = '"'; outLen = 1; return true; }
  if (r == "apos") { out[0] = '\''; outLen = 1; return true; }

  if (r[0] != '#') {
    // The generated HTML 4 table, binary-searched. std::lower_bound would need
    // <algorithm> for four lines that are clearer written out.
    size_t lo = 0, hi = kNamedEntityCount;
    while (lo < hi) {
      const size_t mid = lo + (hi - lo) / 2;
      const int cmp = r.compare(kNamedEntities[mid].name);
      if (cmp == 0) {
        outLen = appendUtf8(kNamedEntities[mid].cp, out);
        return true;
      }
      if (cmp < 0) hi = mid; else lo = mid + 1;
    }
    // PASSTHROUGH. `&` + the reference + `;`, exactly as it was written. The caller
    // reserves kMaxEntityBytes + 2, so this only fails on a reference longer than the
    // scratch that held it -- which the loop above already refused.
    if (refLen + 2 > cap) return false;
    out[0] = '&';
    std::memcpy(out + 1, ref, refLen);
    out[refLen + 1] = ';';
    outLen = refLen + 2;
    return true;
  }
```

- [ ] **Step 3: Pass through an unterminated reference too**

A bare `&` never reaches the code above: the scan loop returns false when the source ends
or the reference outgrows `ref`. Replace the loop at the top of `decodeEntity` with one
that remembers where it started and rewinds:

```cpp
  char ref[kMaxEntityBytes];
  size_t refLen = 0;
  bump(1);  // '&'
  bool terminated = false;
  for (;;) {
    if (ensure(1) < 1) break;  // the source ended inside a reference
    const char c = at(0);
    if (c == ';') { bump(1); terminated = true; break; }
    // A reference holds name characters or a numeric form. Anything else -- a space,
    // a '<' -- means this '&' was never a reference at all, which is what "Tom &
    // Jerry" is, and real books are full of them.
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '#')) break;
    if (refLen >= sizeof(ref)) break;  // longer than any entity we accept
    ref[refLen++] = c;
    bump(1);
  }
  if (!terminated) {
    // NOT an entity. Emit the '&' and whatever was consumed looking for one, so no
    // byte of the document is lost. The characters after it are still in the input
    // and will be read as ordinary text.
    if (refLen + 1 > cap) return false;
    out[0] = '&';
    std::memcpy(out + 1, ref, refLen);
    outLen = refLen + 1;
    return true;
  }
  if (refLen == 0 || cap < 4) return false;
```

- [ ] **Step 4: Widen both callers' capacity reserve**

Both call sites reserve 4 bytes, which was enough for a decoded character and is not
enough for a passthrough. In `core/src/xml.cpp`:

At the text caller (`:247`), change

```cpp
        if (textLen_ + 4 > kTextBytes) break;
```

to

```cpp
        // kMaxEntityBytes + 2 because a passthrough writes '&' + the reference + ';'.
        if (textLen_ + kMaxEntityBytes + 2 > kTextBytes) break;
```

At the attribute caller (`:377`), change

```cpp
        if (attrUsed_ + 4 > kMaxAttrBytes)
```

to

```cpp
        if (attrUsed_ + kMaxEntityBytes + 2 > kMaxAttrBytes)
```

- [ ] **Step 5: Run the tests**

```bash
cmake --build build -j --target unit_tests && ./build/unit_tests -tc="*named entities*,*passes through*,*bare ampersand*,*predefined entities*,*numeric character*"
```

Expected: 5 test cases, all passed.

- [ ] **Step 6: Run the whole suite**

```bash
make test
```

Expected: `100% tests passed out of 9`, and the unit binary reporting all test cases
passed. If a golden moved, stop: nothing here should change a rendered pixel, because no
fixture in the repo contains an entity outside the predefined five.

- [ ] **Step 7: Commit**

```bash
git add core/src/xml.cpp
git commit -m "fix: an entity we do not know costs its own five characters, not the chapter"
```

---

## Task 4: The chapter-level proof

**Files:**
- Test: `test/unit/test_document.cpp`

The tokenizer tests prove the bytes. This proves the thing that was actually broken —
that a block *after* an entity still arrives — and it is the test that would have caught
`Dark Plagueis`.

- [ ] **Step 1: Write the test**

Add to `test/unit/test_document.cpp`:

```cpp
TEST_CASE("a named entity does not truncate the blocks after it") {
  // `Dark Plagueis` lost 177 of 183 chapters to this: BlockReader stops on
  // Node::Error, ChapterReader::next() then returns false, and a caller cannot tell
  // that from the chapter ending. Every block after the first entity was discarded.
  reader::ChapterReader cr;
  REQUIRE(cr.beginBuffer(
      "<html><body><p>One</p><p>Two&nbsp;three</p><p>Four</p></body></html>"));
  std::vector<std::string> texts;
  reader::Block b;
  while (cr.next(b)) texts.push_back(b.text);
  REQUIRE(texts.size() == 3);
  CHECK(texts[0] == "One");
  CHECK(texts[1] == "Two\xC2\xA0" "three");
  CHECK(texts[2] == "Four");
  CHECK(cr.ok());
}
```

If `test/unit/test_document.cpp` does not already include them, add
`#include "reader/chapter.h"` and `#include <vector>` at the top.

- [ ] **Step 2: Run it**

```bash
cmake --build build -j --target unit_tests && ./build/unit_tests -tc="*does not truncate*"
```

Expected: PASS — Task 3 already fixed it. This test exists to keep it fixed.

- [ ] **Step 3: Prove the test bites**

Temporarily revert the passthrough by making the table lookup fall through: in
`core/src/xml.cpp`, change `if (r[0] != '#') {` to `if (false) {`, rebuild, and run the
test.

Expected: FAIL with `texts.size() == 3` reporting 1. Then restore the line. This repo has
had a mutation that was a no-op and read as a passing test; check the mutation lands
before believing what it tells you.

- [ ] **Step 4: Commit**

```bash
git add test/unit/test_document.cpp
git commit -m "test: the blocks after an entity are the thing that was lost"
```

---

## Task 5: Re-measure the library

**Files:**
- Create: `/tmp/lossprobe.cpp` (scratch, not committed)

- [ ] **Step 1: Write the probe**

```cpp
#include <cstdio>
#include <string>
#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/host_fs.h"
int main(int argc, char** argv) {
  reader::HostFileSystem fs("/");
  reader::OpenedBook book;
  const char* reason = "";
  if (!reader::openBook(fs, argv[1], book, &reason)) { std::printf("REFUSED\t%s\n", reason); return 1; }
  int bad = 0, blocks = 0; long bytes = 0;
  std::string firstErr;
  reader::ChapterReader cr;
  for (int c = 0; c < book.chapterCount(); ++c) {
    const reader::ChapterLocation loc = book.locate(c);
    if (loc.compressedSize == 0) continue;
    if (!cr.begin(fs, loc)) { ++bad; continue; }
    reader::Block b;
    while (cr.next(b)) { ++blocks; bytes += (long)b.text.size(); }
    if (!cr.ok()) { ++bad; if (firstErr.empty()) firstErr = cr.error(); }
  }
  std::printf("%d/%d truncated\tblocks=%d\tbytes=%ld\t%s\n", bad, book.chapterCount(), blocks, bytes,
              firstErr.c_str());
  return 0;
}
```

- [ ] **Step 2: Build and run it over the library**

```bash
c++ -std=c++20 -O1 -I core/include -DREADER_DESKTOP=1 /tmp/lossprobe.cpp build/libreader_core.a -o /tmp/lossprobe
```

Then over every EPUB in the Calibre library. The numbers to beat, taken before the fix:

| book | before |
|---|---|
| `Dark Plagueis - Ultimate Edition` | 177/183 truncated, 164 blocks, 3,214 bytes |
| `Darkly Dreaming Dexter` | 1/34 truncated, 2,052 blocks, 406,709 bytes |
| every other book that opened | 0 truncated |

Expected after: **0 truncated everywhere**, `Dark Plagueis` recovering thousands of
blocks and megabytes of text rather than 3,214 bytes, and every previously-clean book
reporting **exactly the same** block and byte counts as before — an entity fix must not
move a book that had no entities.

- [ ] **Step 3: Record the result in the roadmap**

Append the before/after table to the FIXED paragraph at
`docs/superpowers/plans/2026-08-20-v1-roadmap.md:1080`, under the heading
`**AND THE SAME AUDIT FOUND SILENT TEXT LOSS**`. Two sentences and the table; the
reasoning lives in the spec.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-08-20-v1-roadmap.md
git commit -m "docs: what the entity fix recovered, measured on sixteen real books"
```

---

## Task 6: Fetch a manifest

**Files:**
- Create: `tools/corpus.py`
- Create: `tools/corpus.manifest`

- [ ] **Step 1: Write the fetcher**

Create `tools/corpus.py`. This step is `fetch` and `verify` only; discovery is Task 7, so
that a broken source later costs books rather than the tool.

```python
#!/usr/bin/env python3
"""Fetch a corpus of real EPUBs, and verify it against a committed manifest.

WHY A MANIFEST AND NOT A DIRECTORY: a refusal rate is a measurement, and a
measurement that cannot be repeated is an anecdote. The manifest is committed so a
run today and a run in three months are over the same books; the books themselves
are hundreds of megabytes and are never committed.

The cache defaults to ~/.cache/encre-corpus, deliberately outside the repo.

Usage:
    python3 tools/corpus.py fetch                     # everything in the manifest
    python3 tools/corpus.py verify                    # re-hash what is on disk
    python3 tools/corpus.py fetch --cache /some/dir
"""

import argparse
import hashlib
import os
import time
import urllib.error
import urllib.parse
import urllib.request

MANIFEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus.manifest")
DEFAULT_CACHE = os.path.expanduser("~/.cache/encre-corpus")
# Gutenberg asks not to be crawled hard, and Standard Ebooks is one volunteer's
# server. One second between requests to the same host, and a User-Agent that says
# who is calling, is the price of using them at all.
USER_AGENT = "encre-corpus/1.0 (+https://github.com/Rukkaitto/encre)"
POLITE_DELAY_S = 1.0


def read_manifest(path=MANIFEST):
    """source, sha256, size, url -- tab separated, '#' comments."""
    out = []
    if not os.path.exists(path):
        return out
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            source, digest, size, url = line.split("\t")
            out.append({"source": source, "sha256": digest, "size": int(size), "url": url})
    return out


def cache_path(cache, entry):
    """Named by digest, so two sources naming one book cost one file and one fetch."""
    return os.path.join(cache, entry["source"], entry["sha256"][:16] + ".epub")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def fetch(cache):
    entries = read_manifest()
    if not entries:
        print("manifest is empty -- run `corpus.py discover` first")
        return 1
    have = missing = failed = 0
    last_host = None
    for e in entries:
        dst = cache_path(cache, e)
        if os.path.exists(dst) and sha256_of(dst) == e["sha256"]:
            have += 1
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        host = urllib.parse.urlsplit(e["url"]).netloc
        if host == last_host:
            time.sleep(POLITE_DELAY_S)
        last_host = host
        try:
            data = download(e["url"])
        except (urllib.error.URLError, OSError) as err:
            print("FAILED %s: %s" % (e["url"], err))
            failed += 1
            continue
        got = hashlib.sha256(data).hexdigest()
        if got != e["sha256"]:
            # NOT a warning. The manifest is what makes the measurement repeatable,
            # so a book whose bytes changed is a different book and must not silently
            # enter the corpus under the old name.
            print("MISMATCH %s: manifest %s, got %s" % (e["url"], e["sha256"][:16], got[:16]))
            failed += 1
            continue
        with open(dst, "wb") as fh:
            fh.write(data)
        missing += 1
    print("%d already had, %d fetched, %d failed, of %d" % (have, missing, failed, len(entries)))
    return 1 if failed else 0


def verify(cache):
    entries = read_manifest()
    ok = bad = absent = 0
    for e in entries:
        dst = cache_path(cache, e)
        if not os.path.exists(dst):
            absent += 1
        elif sha256_of(dst) == e["sha256"]:
            ok += 1
        else:
            bad += 1
            print("CORRUPT %s" % dst)
    print("%d verified, %d corrupt, %d not fetched, of %d" % (ok, bad, absent, len(entries)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["fetch", "verify"])
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    a = ap.parse_args()
    return fetch(a.cache) if a.command == "fetch" else verify(a.cache)


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Write a three-line manifest by hand and fetch it**

Both URLs below were verified before this plan was written: Gutenberg returns
`application/epub+zip`, and Standard Ebooks needs the `?source=download` suffix or it
serves an HTML interstitial instead of the book.

```bash
printf 'gutenberg\t0000\t0\thttps://www.gutenberg.org/ebooks/1342.epub3.images\n' > /tmp/m.manifest
```

Fetching with a wrong digest must fail loudly:

```bash
python3 tools/corpus.py fetch --cache /tmp/corpus-test
```

Expected: `MISMATCH ...`, and `0 already had, 0 fetched, 1 failed, of 1`. That is the
check that keeps a changed book from entering the corpus under an old name.

- [ ] **Step 3: Commit**

```bash
git add tools/corpus.py
git commit -m "feat: a corpus is a manifest, because a rate that cannot be repeated is an anecdote"
```

---

## Task 7: Discover books from three sources

**Files:**
- Modify: `tools/corpus.py`
- Create: `tools/corpus.manifest`

- [ ] **Step 1: Add the discoverers**

Add to `tools/corpus.py`. Each returns a list of URLs and each is allowed to fail on its
own — a source that changes its scheme costs its books, not the corpus.

```python
# Gutenberg ids spread across the whole range on purpose: its EPUB generator has
# changed over twenty years, so an id from 1994 and one from last year are different
# toolchains wearing the same name.
GUTENBERG_IDS = list(range(1, 200)) + list(range(20000, 20100)) + list(range(60000, 60100))


def discover_gutenberg(limit):
    return ["https://www.gutenberg.org/ebooks/%d.epub3.images" % i
            for i in GUTENBERG_IDS[:limit]]


def discover_standardebooks(limit):
    """Standard Ebooks serves an HTML interstitial on the bare download URL; the
    `?source=download` its own meta-refresh points at is the file. Two formats per
    book, and the .kepub is the point -- it is Kobo's own dialect, which is a
    toolchain this corpus otherwise has no sample of."""
    import re
    urls = []
    for page in range(1, 12):
        try:
            html = download("https://standardebooks.org/ebooks?page=%d" % page).decode("utf-8")
        except Exception as err:
            print("standardebooks: page %d unavailable (%s)" % (page, err))
            break
        for path in re.findall(r'href="(/ebooks/[^"?#]+)"', html):
            if path.count("/") != 3:
                continue
            slug = path.rsplit("/", 2)
            name = "%s_%s" % (slug[-2], slug[-1])
            urls.append("https://standardebooks.org%s/downloads/%s.epub?source=download"
                        % (path, name))
            if len(urls) % 4 == 0:
                urls.append("https://standardebooks.org%s/downloads/%s.kepub.epub?source=download"
                            % (path, name))
        time.sleep(POLITE_DELAY_S)
        if len(urls) >= limit:
            break
    return urls[:limit]


def discover_local(root, limit):
    """The author's own library: publisher output and Calibre, which is the only
    sample of the population this firmware will actually meet."""
    out = []
    for dirpath, _, names in os.walk(os.path.expanduser(root)):
        for n in names:
            if n.lower().endswith(".epub"):
                out.append("file://" + os.path.join(dirpath, n))
    return out[:limit]
```

- [ ] **Step 2: Add the `discover` command**

```python
def discover(cache, sources, limit, root):
    found = []
    if "gutenberg" in sources:
        found += [("gutenberg", u) for u in discover_gutenberg(limit)]
    if "standardebooks" in sources:
        found += [("standardebooks", u) for u in discover_standardebooks(limit)]
    if "local" in sources and root:
        found += [("local", u) for u in discover_local(root, limit)]
    rows, failed = [], 0
    last_host = None
    for source, url in found:
        try:
            if url.startswith("file://"):
                data = open(url[7:], "rb").read()
            else:
                host = urllib.parse.urlsplit(url).netloc
                if host == last_host:
                    time.sleep(POLITE_DELAY_S)
                last_host = host
                data = download(url)
        except Exception as err:
            print("skip %s (%s)" % (url, err))
            failed += 1
            continue
        # IT MUST BE A ZIP. Every source here has served an HTML page in place of a
        # book at least once, and a corpus quietly full of error pages would report a
        # refusal rate for a parser that was handed no books.
        if not data.startswith(b"PK\x03\x04"):
            print("skip %s (not a zip: %r)" % (url, data[:16]))
            failed += 1
            continue
        rows.append((source, hashlib.sha256(data).hexdigest(), len(data), url))
    rows.sort()
    with open(MANIFEST, "w") as fh:
        fh.write("# GENERATED by tools/corpus.py discover. source, sha256, bytes, url.\n")
        fh.write("# The books are NOT committed; `corpus.py fetch` puts them in the cache.\n")
        for r in rows:
            fh.write("%s\t%s\t%d\t%s\n" % r)
    print("manifest: %d books, %d skipped" % (len(rows), failed))
    return 0
```

Then replace `main()` with the three-command form:

```python
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["discover", "fetch", "verify"])
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    ap.add_argument("--sources", default="gutenberg,standardebooks,local",
                    help="comma separated")
    ap.add_argument("--limit", type=int, default=120, help="books per source")
    ap.add_argument("--root",
                    default="~/Library/Mobile Documents/com~apple~CloudDocs/Calibre Library",
                    help="the local library, for --sources local")
    a = ap.parse_args()
    if a.command == "discover":
        return discover(a.cache, set(a.sources.split(",")), a.limit, a.root)
    return fetch(a.cache) if a.command == "fetch" else verify(a.cache)
```

- [ ] **Step 2b: Verify each discoverer before trusting it**

```bash
python3 tools/corpus.py discover --sources gutenberg --limit 5
```

Expected: `manifest: 5 books, 0 skipped`.

```bash
python3 tools/corpus.py discover --sources standardebooks --limit 5
```

Expected: `manifest: 5 books, 0 skipped`. **If this reports 0 books, the site's listing
markup has changed** — fix `discover_standardebooks`'s regex against the live page before
going further, and do not let the corpus silently become Gutenberg-only.

- [ ] **Step 3: Build the real manifest**

```bash
python3 tools/corpus.py discover --sources gutenberg,standardebooks,local --limit 120
```

Expected: roughly 250–260 books, and the manifest naming all three sources. Check that:

```bash
cut -f1 tools/corpus.manifest | sort | uniq -c
```

Expected: three lines, none of them zero.

- [ ] **Step 4: Commit the manifest, not the books**

```bash
git add tools/corpus.py tools/corpus.manifest
git commit -m "feat: three sources, three toolchains, one reproducible manifest"
```

---

## Task 8: The probe

**Files:**
- Create: `tools/corpus_probe.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the probe**

Create `tools/corpus_probe.cpp`. One JSON object per book on stdout, one book per
argument, so a crash on book 40 does not cost the first 39.

```cpp
// EVERY BOOK IN THE CORPUS, THROUGH THE REAL OPEN PATH.
//
// Not in ctest, and deliberately, for the reason name_probe is not: it needs real
// books, and the repo's generated EPUBs are 1,400-word stubs. It runs `openBook` and
// walks every chapter, so what it measures is what the device does, minus the card
// and the heap.
//
// TWO METRICS, because one of them lies. "Did it open" scores a book that yields
// three of its ninety-two chapters as a success; `truncated` and `unreadable` are
// what stop that being reported as a win.
//
//   build: see CMakeLists.txt (target corpus_probe)
//   run:   build/corpus_probe book1.epub book2.epub ... > run.jsonl

#include <cstdio>
#include <string>

#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/host_fs.h"

namespace {

// JSON needs its quotes and backslashes escaped, and a book title is arbitrary bytes
// off somebody's card -- including control characters.
std::string esc(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (const unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
    else out += static_cast<char>(c);
  }
  return out;
}

void probe(reader::FileSystem& fs, const char* path) {
  reader::OpenedBook book;
  const char* reason = "";
  if (!reader::openBook(fs, path, book, &reason)) {
    std::printf("{\"path\":\"%s\",\"opened\":false,\"reason\":\"%s\"}\n",
                esc(path).c_str(), esc(reason).c_str());
    return;
  }
  int unreadable = 0, truncated = 0, blocks = 0;
  long bytes = 0;
  std::string firstError;
  reader::ChapterReader cr;
  for (int c = 0; c < book.chapterCount(); ++c) {
    const reader::ChapterLocation loc = book.locate(c);
    if (loc.compressedSize == 0) { ++unreadable; continue; }
    if (!cr.begin(fs, loc)) { ++unreadable; continue; }
    reader::Block b;
    while (cr.next(b)) { ++blocks; bytes += static_cast<long>(b.text.size()); }
    if (!cr.ok()) {
      ++truncated;
      if (firstError.empty()) firstError = cr.error();
    }
  }
  std::printf("{\"path\":\"%s\",\"opened\":true,\"title\":\"%s\",\"author\":\"%s\","
              "\"spine\":%d,\"unreadable\":%d,\"truncated\":%d,\"blocks\":%d,"
              "\"textBytes\":%ld,\"firstError\":\"%s\"}\n",
              esc(path).c_str(), esc(book.title).c_str(), esc(book.author).c_str(),
              book.chapterCount(), unreadable, truncated, blocks, bytes,
              esc(firstError).c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: corpus_probe <book.epub> [more.epub ...] > run.jsonl\n");
    return 2;
  }
  reader::HostFileSystem fs("/");
  for (int i = 1; i < argc; ++i) probe(fs, argv[i]);
  return 0;
}
```

- [ ] **Step 2: Wire it into CMake**

In `CMakeLists.txt`, directly after the `name_probe` block, add:

```cmake
# The CORPUS probe: every book in tools/corpus.manifest through the real open path,
# as JSONL. Not a test -- it needs real books, same as name_probe.
add_executable(corpus_probe ${CMAKE_SOURCE_DIR}/tools/corpus_probe.cpp)
target_link_libraries(corpus_probe PRIVATE reader_core)
target_compile_options(corpus_probe PRIVATE ${READER_WARNINGS})
target_compile_definitions(corpus_probe PRIVATE READER_DESKTOP=1)
```

- [ ] **Step 3: Build and smoke-test it**

CMake globs its sources, so reconfigure rather than just rebuilding:

```bash
cmake -S . -B build && cmake --build build -j --target corpus_probe
```

Then over two books whose answers are already known:

```bash
./build/corpus_probe "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Calibre Library/Frank Herbert/Dune - Tome 5 _ Les heretiques de Dune (84)/Dune - Tome 5 _ Les heretiques de Dune - Frank Herbert.epub"
```

Expected: one JSON line, `"opened":true`, `"spine":55`, `"truncated":0`,
`"blocks":7227`. If `blocks` is not 7227 the probe disagrees with the measurement this
plan was built on; stop and find out which is wrong.

- [ ] **Step 4: Commit**

```bash
git add tools/corpus_probe.cpp CMakeLists.txt
git commit -m "feat: every book through the real open path, as JSONL"
```

---

## Task 9: The report

**Files:**
- Create: `tools/corpus_report.py`

- [ ] **Step 1: Write it**

```python
#!/usr/bin/env python3
"""Turn corpus_probe's JSONL into the two numbers and the histogram.

TWO METRICS, because one of them lies. A book that opens with three of its
ninety-two chapters scores as a success on the open rate alone, and reporting only
that is the shape of the card probe answered from cache: a check that reports on less
than it claims is worse than no check, because it is trusted.

Usage:
    python3 tools/corpus_report.py run.jsonl
    python3 tools/corpus_report.py before.jsonl after.jsonl   # a delta
"""

import collections
import json
import sys


def load(path):
    with open(path) as fh:
        return [json.loads(line) for line in fh if line.strip()]


def summarise(rows):
    opened = [r for r in rows if r["opened"]]
    spine = sum(r["spine"] for r in opened)
    unreadable = sum(r["unreadable"] for r in opened)
    truncated = sum(r["truncated"] for r in opened)
    return {
        "books": len(rows),
        "opened": len(opened),
        "openRate": 100.0 * len(opened) / max(1, len(rows)),
        "clean": sum(1 for r in opened if not r["unreadable"] and not r["truncated"]),
        "spine": spine,
        "chapterRate": 100.0 * (spine - unreadable - truncated) / max(1, spine),
        "textBytes": sum(r["textBytes"] for r in opened),
    }


def report(path):
    rows = load(path)
    s = summarise(rows)
    print("%s: %d books" % (path, s["books"]))
    print("  open rate    %6.2f%%  (%d of %d)" % (s["openRate"], s["opened"], s["books"]))
    print("  whole books  %6.2f%%  (%d opened with nothing missing)"
          % (100.0 * s["clean"] / max(1, s["books"]), s["clean"]))
    print("  chapter rate %6.2f%%  (%d spine entries)" % (s["chapterRate"], s["spine"]))
    print("  text         %d bytes" % s["textBytes"])

    refusals = collections.Counter(r["reason"] for r in rows if not r["opened"])
    if refusals:
        print("\n  why books were refused")
        for reason, n in refusals.most_common():
            print("    %4d  %s" % (n, reason))

    errors = collections.Counter(r["firstError"] for r in rows
                                 if r["opened"] and r.get("firstError"))
    if errors:
        print("\n  why chapters were truncated")
        for err, n in errors.most_common():
            print("    %4d  %s" % (n, err))

    # PER SOURCE, because a corpus of 250 books from three toolchains is three
    # samples, not 250, and a rate that hides that flatters itself.
    per = collections.defaultdict(list)
    for r in rows:
        per[r["path"].split("/")[-2] if "/" in r["path"] else "?"].append(r)
    print("\n  by source")
    for src in sorted(per):
        s2 = summarise(per[src])
        print("    %-16s %6.2f%% open, %6.2f%% of chapters, %d books"
              % (src, s2["openRate"], s2["chapterRate"], s2["books"]))


def main():
    if len(sys.argv) == 2:
        report(sys.argv[1])
        return 0
    if len(sys.argv) == 3:
        before, after = summarise(load(sys.argv[1])), summarise(load(sys.argv[2]))
        print("               before    after    delta")
        for k, fmt in (("openRate", "%6.2f%%"), ("chapterRate", "%6.2f%%")):
            print("  %-12s " % k + fmt % before[k] + "  " + fmt % after[k] +
                  "  " + fmt % (after[k] - before[k]))
        print("  %-12s %8d %8d %8d" % ("textBytes", before["textBytes"],
                                       after["textBytes"],
                                       after["textBytes"] - before["textBytes"]))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Smoke-test it on two lines**

```bash
./build/corpus_probe "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Calibre Library"/*/*/*.epub > /tmp/local.jsonl 2>/dev/null; python3 tools/corpus_report.py /tmp/local.jsonl
```

Expected: 16 books, an open rate of 93.75% (15 of 16), one refusal reading *"the OPF is
malformed"*, and — after Task 3 — a chapter rate of 100.00% with no truncation lines.

- [ ] **Step 3: Commit**

```bash
git add tools/corpus_report.py
git commit -m "feat: two metrics, because the open rate alone flatters a broken book"
```

---

## Task 10: The baseline

**Files:**
- Create: `docs/superpowers/plans/2026-08-28-corpus-baseline.md`

- [ ] **Step 1: Fetch the corpus**

```bash
python3 tools/corpus.py fetch
```

Expected: `0 already had, ~250 fetched, 0 failed, of ~250`, taking 10–20 minutes at the
one-second politeness delay. A handful of failures is fine and is recorded; a whole
source failing is not — go back to Task 7 Step 2b.

- [ ] **Step 2: Run the probe over everything**

```bash
find ~/.cache/encre-corpus -name '*.epub' -print0 | xargs -0 ./build/corpus_probe > /tmp/baseline.jsonl
python3 tools/corpus_report.py /tmp/baseline.jsonl | tee /tmp/baseline.txt
```

- [ ] **Step 3: Write the baseline document**

Create `docs/superpowers/plans/2026-08-28-corpus-baseline.md` holding: the manifest's
book and source counts, the full report output, and — the part that matters — **the
refusal histogram read against the spec's audit table**, saying for each row of that
table how many books in the corpus hit it. That mapping is what orders every remaining
fix, and it is the deliverable of Round 0.

Where a refusal reason in the histogram matches no row of the audit, say so explicitly.
An unanticipated failure mode is the most valuable thing this round can produce, and it
is exactly what a plan written from the audit alone would have missed.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-08-28-corpus-baseline.md
git commit -m "docs: the baseline, and which audit rows the corpus actually hits"
```

- [ ] **Step 5: Stop for review**

Round 0 ends here. The next plan is written **from the baseline document**, in the order
the histogram gives, and not before — planning those fixes now would be the guess this
whole exercise exists to replace.

---

## What is deliberately not in this plan

- **Nothing else from the audit.** The entity fix is here because it was measured; the
  attribute-byte cap (issue #35), the entry cap, percent-decoding, per-entry
  compression, the manifest-href skip and the spine-position change all wait for the
  baseline to say how often they matter.

## Two of Round 0's questions are already answered

The spec asks Round 0 to verify two behaviours it assumed. Both were measured while this
plan was being written, so they are answers rather than tasks — recorded here so nobody
spends a round rediscovering them:

- **A text run longer than `kTextBytes` (1024) loses nothing.** A 3,000-character
  paragraph comes back whole in one block, and the following block arrives normally. The
  buffer flushes and continues; it does not truncate.
- **A mid-chapter XML error costs the whole rest of the chapter, and the reader already
  knows.** `ChapterReader::ok()` returns false and `error()` names the reason — but
  `next()` returning false is how a chapter ends normally, so no caller asks. That is why
  `corpus_probe` reads `ok()` after the walk, and it is the entire basis of the
  `truncated` metric.
- **No device work.** Part A changes flash by ~2 KB of table and touches no heap path,
  but it does change what the tokenizer accepts, so the `On glass` check is: flash it,
  open `Dark Plagueis` on the device, and confirm the chapters are there. That is
  yours — flashing cannot be run from an agent.
- **No UI.** A refused book still shows what it shows today. That was decided in the
  spec and it stays decided.
