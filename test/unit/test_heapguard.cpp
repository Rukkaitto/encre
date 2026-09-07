// THE OPEN PATH REFUSES INSTEAD OF ABORTING, WITH FAILURE INJECTED.
//
// Reported off an X3: opening a book crashed the firmware and rebooted to Home, and
// the same book opened normally on the next press. Under `-fno-exceptions` a
// container growth that cannot allocate is `abort()` with no message and no stack,
// and CLAUDE.md already records that exact symptom being read as a navigation bug
// twice. The successful open of that book left 13,696 bytes of heap with a 232-book
// library resident underneath it.
//
// WHY FAILURE HAS TO BE INJECTED: the desktop cannot be made to fail an 8 KB
// allocation. A 64-bit host has gigabytes, `new` there does not return null, and the
// OOM killer is not a return value. So `Heap::install` swaps the allocator question
// for one this file controls -- `Profile`'s injected clock, in the one other place
// `core/` needs something it must not acquire for itself.
//
// THE CENTRAL TEST IS A PROPERTY, NOT A LIST OF SITES: refuse the Nth probe, for
// every N the open path makes, and every one of them must come back as a refusal
// whose reason maps to `BookErrorReason::OutOfMemory`. A test naming sites would go
// stale the moment a site was added; this one counts them and would notice.
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "epub_builder.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/book.h"
#include "reader/css.h"
#include "reader/heapguard.h"
#include "reader/screen_book_error.h"
#include "reader/toc.h"

namespace {

std::string_view asBytes(const unsigned char* p, size_t n) {
  return std::string_view(reinterpret_cast<const char*>(p), n);
}

// HOW MANY TIMES THE PROBE WAS ASKED, and which of those to refuse. File scope
// because a `Heap::Probe` is a plain function pointer -- there is nothing to capture
// into, which is deliberate: this ships in the firmware and a `std::function` does
// not belong on a path that exists to survive a full heap.
int gAsked = 0;
// REFUSE FROM THE Nth CALL ONWARD, not just the Nth -- and the difference is the
// whole point. `ensureRoom` falls back to the exact size when a doubling is refused,
// so a single refused probe is legitimately recoverable; a heap that has run out has
// not un-run-out by the next statement. Refusing from N on is the honest model and
// it is what makes the property below exact.
int gRefuseFrom = -1;
size_t gCeiling = 0;  // 0 for no ceiling; otherwise refuse anything larger

bool countingProbe(size_t bytes) {
  const int n = gAsked++;
  if (gRefuseFrom >= 0 && n >= gRefuseFrom) return false;
  if (gCeiling != 0 && bytes > gCeiling) return false;
  return true;
}

// RAII, because a test that leaves an injected probe installed has changed every
// case after it -- and doctest's ordering is not something to rely on.
struct Injected {
  Injected() {
    gAsked = 0;
    gRefuseFrom = -1;
    gCeiling = 0;
    reader::Heap::install(&countingProbe);
  }
  ~Injected() { reader::Heap::install(nullptr); }
};

FakeFileSystem cardWith(std::string_view bytes) {
  FakeFileSystem fs;
  fs.writeAll("/books/book.epub", bytes);
  return fs;
}

}  // namespace

// --- the primitives -----------------------------------------------------------

TEST_CASE("ensureRoom reserves what it was asked for and answers true") {
  Injected inj;
  std::vector<int> v;
  REQUIRE(reader::ensureRoom(v, 300));
  CHECK(v.capacity() >= 300);
  CHECK(v.empty());
  // Already big enough: no probe, no reserve.
  const int asked = gAsked;
  CHECK(reader::ensureRoom(v, 10));
  CHECK(gAsked == asked);
}

TEST_CASE("a refused growth leaves the container exactly as it was") {
  Injected inj;
  std::vector<int> v{1, 2, 3};
  const size_t cap = v.capacity();
  gCeiling = 4;  // smaller than anything a vector of ints will want
  CHECK_FALSE(reader::ensureRoom(v, cap + 1));
  CHECK_FALSE(reader::pushOrRefuse(v, 4));
  CHECK(v.size() == 3);
  CHECK(v.capacity() == cap);

  // PAST THE SMALL-STRING BUFFER, which is 22 bytes on libc++ and 15 on libstdc++:
  // an append that fits inline allocates nothing, so there is nothing for a probe to
  // refuse and `true` is the right answer. Filling the capacity first is what makes
  // this case about the guard rather than about the standard library.
  std::string s(64, 'a');
  s.resize(s.capacity());
  const std::string before = s;
  const size_t scap = s.capacity();
  CHECK_FALSE(reader::appendOrRefuse(s, "0123456789", 10));
  CHECK(s == before);
  CHECK(s.capacity() == scap);
}

TEST_CASE("doubling that is refused falls back to the exact size asked for") {
  // THE CASE THIS EXISTS FOR: geometric growth asks for TWICE what is needed, so
  // near the limit it would refuse a book that fits. Removing the fallback in
  // heapguard.h fails this and nothing else -- the corpus never comes close enough
  // to the ceiling for the difference to show.
  Injected inj;
  std::vector<char> v;
  REQUIRE(reader::ensureRoom(v, 1000));
  REQUIRE(v.capacity() >= 1000);
  v.resize(v.capacity());
  const size_t cap = v.capacity();
  // Room for one more element, and not for a doubling.
  gCeiling = cap + 1;
  const int asked = gAsked;
  REQUIRE(reader::pushOrRefuse(v, 'x'));
  CHECK(v.size() == cap + 1);
  // TWO probes: the doubling, refused, then the exact size.
  CHECK(gAsked == asked + 2);
}

// --- the mapping onto the screen's vocabulary ---------------------------------

TEST_CASE("bookErrorReasonFor puts each class of refusal on the right board") {
  using reader::BookErrorReason;
  CHECK(reader::bookErrorReasonFor(reader::kOpenCannotOpen) == BookErrorReason::Unreadable);
  CHECK(reader::bookErrorReasonFor("not enough memory to read the manifest") ==
        BookErrorReason::OutOfMemory);
  CHECK(reader::bookErrorReasonFor("the spine names no chapters") == BookErrorReason::Damaged);
  // A refusal that will not say why is Damaged, which is the claim that asserts
  // least about the card and about the heap.
  CHECK(reader::bookErrorReasonFor("") == BookErrorReason::Damaged);
  CHECK(reader::bookErrorReasonFor(nullptr) == BookErrorReason::Damaged);
}

// --- the property, over the whole open path -----------------------------------

TEST_CASE("every probe on the open path, refused in turn, is a refusal and not an abort") {
  Injected inj;
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubToc, epubfix::kEpubTocLen));

  // FIRST, THE UNRESTRICTED RUN -- which is what says the probe is consulted at all.
  // A property over "every probe" is vacuous if there are none, which is this
  // project's own rule about a mutation telling you about your input first.
  reader::OpenedBook book;
  const char* why = "";
  REQUIRE(reader::openBook(fs, "/books/book.epub", book, &why));
  const int probes = gAsked;
  // Ten probes over six distinct sites on this fixture. The one guarded site it does
  // NOT reach is the entry NAME: every name in an EPUB fixture fits the
  // small-string buffer, so nothing is allocated and nothing is asked. That guard is
  // for a 65,535-byte name the format permits and no book writes, and it is stated
  // here as uncovered rather than left looking covered.
  REQUIRE(probes >= 4);

  // WHICH SITE OWNED PROBE k, collected. The refusal from a sticky failure at k is
  // the message of the site holding probe k, so walking k over every probe collects
  // one message per guarded site.
  std::vector<std::string> seen;
  for (int k = 0; k < probes; ++k) {
    gAsked = 0;
    gRefuseFrom = k;
    reader::OpenedBook out;
    const char* reason = "";
    CAPTURE(k);
    const bool ok = reader::openBook(fs, "/books/book.epub", out, &reason);
    REQUIRE_FALSE(ok);
    CAPTURE(std::string(reason));
    // AND NOT `Damaged`, which is the half that was already wrong before the guards
    // landed: an out-of-memory inside `Epub::open`'s container read was reported as
    // "this is not an EPUB", so a healthy book was named damaged on the panel.
    CHECK(reader::bookErrorReasonFor(reason) == reader::BookErrorReason::OutOfMemory);
    seen.push_back(reason);
  }

  // AND EVERY SITE IS STILL GUARDED, which the property above cannot see on its own:
  // a REMOVED guard makes no probe, so the walk simply has one fewer element and
  // every remaining one still refuses correctly. Proved by mutation -- deleting the
  // entry-list guard passed all 1,403 cases before this block existed, which is this
  // project's own "reports on less than it claims" shape inside the test written to
  // stop it.
  //
  // Each site's words rather than a count, because a count is a fact about the
  // standard library's growth ladder (libc++ and libstdc++ double from different
  // starting capacities) and would differ between the dev machine and CI.
  const auto sawSite = [&seen](const char* words) {
    for (const std::string& one : seen)
      if (one == words) return true;
    return false;
  };
  CHECK(sawSite("not enough memory to hold the archive's entry list"));
  // The two `Zip::read` probes -- the container and the OPF, both deflated in this
  // fixture. They have been guarded since 3A; what is new is that `Epub::open`
  // REPORTS them, where it used to answer "this is not an EPUB" and put a healthy
  // book on the `appears damaged` board.
  CHECK(sawSite("not enough memory to inflate an archive entry"));
  CHECK(sawSite("not enough memory to read the manifest"));
  CHECK(sawSite("not enough memory to read the spine"));
  CHECK(sawSite("not enough memory to hold the spine"));
  CHECK(sawSite("not enough memory to hold the book's chapter list"));
}

TEST_CASE("a refused table of contents costs the chapter names and never claims the book is too long") {
  Injected inj;
  FakeFileSystem fs = cardWith(asBytes(epubfix::kEpubToc, epubfix::kEpubTocLen));

  std::vector<reader::TocEntry> toc;
  const char* why = "";
  REQUIRE(reader::loadToc(fs, "/books/book.epub", toc, &why));
  REQUIRE(toc.size() == 2);
  const int probes = gAsked;
  REQUIRE(probes >= 4);

  bool sawTocSite = false;
  for (int k = 0; k < probes; ++k) {
    gAsked = 0;
    gRefuseFrom = k;
    std::vector<reader::TocEntry> out;
    const char* reason = "";
    CAPTURE(k);
    const bool ok = reader::loadToc(fs, "/books/book.epub", out, &reason);
    // A refusal here is NOT a book failure -- the shell logs it and the book opens
    // without chapter names -- so `true` is a legitimate answer for a probe the
    // styles swallowed. What may never happen is the wrong claim.
    if (!ok) {
      CAPTURE(std::string(reason));
      CHECK(reader::bookErrorReasonFor(reason) == reader::BookErrorReason::OutOfMemory);
      // AND NEVER THE OTHER REFUSAL'S WORDS. `commit()` had one failure mode and now
      // has two, and both call sites reported the first -- so without `commitWhy` an
      // out-of-memory would have gone into the log as a claim about the book's list
      // being too long.
      CHECK(std::string(reason) != "the table of contents is too long");
      if (std::string(reason) == "not enough memory to read the table of contents")
        sawTocSite = true;
    }
  }
  // THE ENTRY VECTOR IS STILL GUARDED. Same hole as the open path's: a removed guard
  // makes no probe, so the loop shrinks by one and every remaining case still passes.
  // Proved by mutation -- deleting this guard passed all 1,403 cases before this line.
  CHECK(sawTocSite);
}

TEST_CASE("a stylesheet that will not fit costs the italics and nothing else") {
  // `readItalicClasses` swallows its failures by design -- `readEntry` returning
  // false already means "this book has no styles" to its one caller -- so the
  // observable is the LIST rather than a reason.
  //
  // THE FIXTURE HAS TO CARRY A REAL STYLESHEET. Written first against a book whose
  // CSS is declared in the manifest and absent from the archive, which every other
  // fixture in the suite is, this case passed with the guard deleted: `italics` was
  // empty because nothing had been read, not because the read refused. That is this
  // project's own rule about a mutation telling you about your input first.
  FakeFileSystem fs = cardWith(epubbuild::withRealStylesheet());

  // The class is found when there is room for it, which is what makes the refusal
  // below mean something.
  {
    std::vector<reader::TocEntry> toc;
    std::vector<std::string> italics;
    const char* reason = "";
    reader::loadToc(fs, "/books/book.epub", toc, &reason, &italics);
    REQUIRE(italics.size() == 1);
    REQUIRE(italics.front() == "ital");
  }

  // A CEILING THAT REFUSES THE SHEET AND NOTHING ELSE. Everything else the open path
  // asks for on this fixture is under 2 KB -- the entry list, the container, the OPF,
  // the manifest, the spine -- and the sheet is padded past it, so a refusal here is
  // this site's and not an archive that never opened. Set to 8 first, which refused
  // `Zip::open` and left `italics` empty for the wrong reason.
  Injected inj;
  gCeiling = 2048;
  std::vector<reader::TocEntry> toc;
  std::vector<std::string> italics;
  const char* reason = "";
  reader::loadToc(fs, "/books/book.epub", toc, &reason, &italics);
  CHECK(italics.empty());
}

// --- what reaches the glass ---------------------------------------------------

TEST_CASE("the out-of-memory shape says memory and does not say damaged") {
  const auto message = [](reader::BookErrorReason why) {
    return reader::BookErrorScreen(
               {"/books/book.epub", "book.epub", why, reader::ScreenId::Library})
        .vm()
        .message;
  };
  const std::string mem = message(reader::BookErrorReason::OutOfMemory);
  CHECK(mem.find("more memory") != std::string::npos);
  // THE TWO CLAIMS IT MUST NOT MAKE. The book is fine and the card answered.
  CHECK(mem.find("damaged") == std::string::npos);
  CHECK(mem.find("could not be read") == std::string::npos);
  // The transience is the one thing the firmware actually knows.
  CHECK(mem.find("right now") != std::string::npos);
  // And it is a THIRD shape rather than one of the two.
  CHECK(mem != message(reader::BookErrorReason::Damaged));
  CHECK(mem != message(reader::BookErrorReason::Unreadable));
}
