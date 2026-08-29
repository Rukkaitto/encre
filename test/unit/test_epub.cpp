#include <cstring>
#include <memory>
#include <string>

#include "doctest.h"
#include "epub_builder.h"
#include "epub_fixtures.h"
#include "fake_fs.h"
#include "reader/epub.h"

namespace {

struct Book {
  FakeFileSystem fs;
  std::unique_ptr<reader::FileHandle> handle;
  reader::Zip zip;
  reader::Epub epub;

  Book(const unsigned char* bytes, size_t len) {
    fs.writeAll("/b.epub", std::string_view(reinterpret_cast<const char*>(bytes), len));
    handle = fs.openRead("/b.epub");
  }
  bool open() {
    return handle != nullptr && zip.open(*handle) && epub.open(*handle, zip);
  }
};

#define BOOK(name) Book b(epubfix::name, epubfix::name##Len)

}  // namespace

TEST_CASE("a minimal EPUB gives its metadata and its spine in order") {
  BOOK(kEpubGood);
  REQUIRE(b.open());
  CHECK(b.epub.title() == "Middlemarch");
  CHECK(b.epub.author() == "George Eliot");
  CHECK(b.epub.identifier() == "urn:uuid:0000-1111");
  REQUIRE(b.epub.chapters().size() == 2);
  CHECK(b.epub.chapters()[0].id == "ch1");
  CHECK(b.epub.chapters()[1].id == "ch2");
}

TEST_CASE("manifest hrefs are resolved against the OPF's own directory") {
  // The OPF is at OEBPS/content.opf and its hrefs are bare filenames, so the
  // entries are OEBPS/*. Getting this wrong reports "not in the archive" for a
  // book that is fine.
  BOOK(kEpubGood);
  REQUIRE(b.open());
  CHECK(b.epub.chapters()[0].path == "OEBPS/ch1.xhtml");
  CHECK(b.epub.chapters()[1].path == "OEBPS/ch2.xhtml");
}

TEST_CASE("a chapter's bytes are reachable through the archive") {
  BOOK(kEpubGood);
  REQUIRE(b.open());
  const reader::Zip::Entry* e = b.zip.find(b.epub.chapters()[0].path);
  REQUIRE(e != nullptr);
  std::string xhtml;
  REQUIRE(b.zip.read(*b.handle, *e, xhtml));
  CHECK(xhtml.find("Miss Brooke") != std::string::npos);
}

// --- resolveHref, where a path bug hides ------------------------------------

TEST_CASE("resolveHref joins against the base's DIRECTORY, not the base") {
  std::string out;
  REQUIRE(reader::resolveHref("OEBPS/content.opf", "ch1.xhtml", out));
  CHECK(out == "OEBPS/ch1.xhtml");
  REQUIRE(reader::resolveHref("OEBPS/content.opf", "text/ch1.xhtml", out));
  CHECK(out == "OEBPS/text/ch1.xhtml");
  // An OPF at the root: no directory to join against.
  REQUIRE(reader::resolveHref("content.opf", "ch1.xhtml", out));
  CHECK(out == "ch1.xhtml");
}

TEST_CASE("resolveHref resolves .. and refuses one that climbs out") {
  std::string out;
  REQUIRE(reader::resolveHref("OEBPS/text/content.opf", "../images/a.png", out));
  CHECK(out == "OEBPS/images/a.png");
  REQUIRE(reader::resolveHref("OEBPS/a/b/x.opf", "../../c.xhtml", out));
  CHECK(out == "OEBPS/c.xhtml");
  // A zip entry name is not a filesystem path, and `../../etc` is not something
  // an EPUB may address. Refused rather than clamped to the root, because a book
  // asking for it is not a book we understand.
  CHECK_FALSE(reader::resolveHref("OEBPS/content.opf", "../../etc/passwd", out));
  CHECK_FALSE(reader::resolveHref("content.opf", "../x", out));
}

TEST_CASE("resolveHref drops a single dot and refuses an absolute href") {
  std::string out;
  REQUIRE(reader::resolveHref("OEBPS/content.opf", "./ch1.xhtml", out));
  CHECK(out == "OEBPS/ch1.xhtml");
  CHECK_FALSE(reader::resolveHref("OEBPS/content.opf", "/ch1.xhtml", out));
  CHECK_FALSE(reader::resolveHref("OEBPS/content.opf", "", out));
}

// --- Refusals ---------------------------------------------------------------

TEST_CASE("no container.xml is a refusal") {
  BOOK(kEpubNoContainer);
  CHECK_FALSE(b.open());
  CHECK(std::strlen(b.epub.reason()) > 0);
}

TEST_CASE("a container naming an OPF that is not there is a refusal") {
  BOOK(kEpubNoOpf);
  CHECK_FALSE(b.open());
  CHECK(std::strlen(b.epub.reason()) > 0);
}

TEST_CASE("a container pointing at a path nothing is at is a refusal") {
  BOOK(kEpubContainerPointsNowhere);
  CHECK_FALSE(b.open());
}

TEST_CASE("an unresolvable unique-identifier is metadata, not a reason to refuse") {
  // THE REFUSAL THIS REPLACED WAS WRITTEN FOR A CONSUMER THAT NEVER ARRIVED. It said
  // an unresolvable identifier "breaks everything keyed on the identifier -- which is
  // what per-book reading state will be"; reading state ended up card-side under
  // /.reader/state, keyed on a hash of the book's PATH with its byte size as the
  // identity check, and nothing outside this file has ever read Epub::identifier().
  // Measured against one real library the check refused 4 of 16 books, every one of
  // which reads -- including a 55-chapter novel that walks 197,330 words once the id
  // resolves.
  //
  // It is the call the spine's `toc` attribute already got: a cross-reference inside
  // the OPF that does not resolve costs the book the thing it named and nothing else.
  // A spine itemref is different, and still a refusal, because a spine is a reading
  // ORDER -- see the test below.
  BOOK(kEpubIdMismatch);
  REQUIRE(b.open());
  CHECK(b.epub.identifier().empty());
  CHECK(b.epub.title() == "Middlemarch");
  REQUIRE(b.epub.chapters().size() == 2);
}

TEST_CASE("a book naming no unique-identifier does not adopt an unnamed one") {
  // THE TRAP THE OLD REFUSAL WAS HIDING. An absent `unique-identifier` and a
  // dc:identifier with no id are both the empty string, so "does this identifier
  // carry the id the package named" answers YES for a book that named nothing --
  // and the identifier would then be reported as the book's own. That is a
  // substitution where empty is the honest answer, and it only became reachable
  // when the refusal in front of it went.
  BOOK(kEpubNoUniqueId);
  REQUIRE(b.open());
  CHECK(b.epub.identifier().empty());
  REQUIRE(b.epub.chapters().size() == 2);
}

TEST_CASE("an empty spine is a refusal -- a book with no chapters is not a book") {
  BOOK(kEpubEmptySpine);
  CHECK_FALSE(b.open());
}

TEST_CASE("a spine itemref with no manifest item is a refusal") {
  // Not "skip the bad one": a spine is a reading ORDER, and silently dropping an
  // entry from it gives the reader a book missing a chapter with no way to know.
  BOOK(kEpubSpineRefMissing);
  CHECK_FALSE(b.open());
}

TEST_CASE("a manifest href that is not in the archive is a refusal") {
  BOOK(kEpubHrefMissing);
  CHECK_FALSE(b.open());
}

TEST_CASE("every truncation of a good EPUB is survivable") {
  for (size_t cut = 0; cut <= epubfix::kEpubGoodLen; cut += 7) {
    FakeFileSystem fs;
    fs.writeAll("/t.epub", std::string_view(
                               reinterpret_cast<const char*>(epubfix::kEpubGood), cut));
    auto h = fs.openRead("/t.epub");
    REQUIRE(h != nullptr);
    reader::Zip z;
    if (!z.open(*h)) continue;
    reader::Epub e;
    if (!e.open(*h, z)) continue;
    // If it opened, everything it claims must be internally consistent. NOT the
    // identifier: it is best-effort metadata, so a non-empty one is not something
    // open() promises and asserting it here would pin the fixture, not the contract.
    CHECK_FALSE(e.chapters().empty());
    for (const auto& c : e.chapters()) CHECK(z.find(c.path) != nullptr);
  }
}

TEST_CASE("a second open() over the same Epub keeps nothing from the first book") {
  // fail("") IS THE RESET, and it is what open() begins with -- so this covers the
  // success path too, not only a refusal.
  //
  // It cleared the metadata and the chapters and NOT the three fields noted during
  // the OPF walk, so an Epub reused over a second book reported the FIRST book's NCX
  // and stylesheets. Nothing in the firmware reuses one today -- openBook builds a
  // local -- which is exactly why this needs a test rather than a caller: it can
  // regress with nothing to notice, and adding a cover field that behaved either way
  // would have made the inconsistency structural.
  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/rich.epub", epubbuild::withEverythingNoted()));
  REQUIRE(fs.writeAll("/plain.epub", epubbuild::minimalEpub()));

  reader::Epub epub;  // ONE Epub, opened twice. That is the whole test.

  std::unique_ptr<reader::FileHandle> rich = fs.openRead("/rich.epub");
  REQUIRE(rich != nullptr);
  reader::Zip zr;
  REQUIRE(zr.open(*rich));
  REQUIRE(epub.open(*rich, zr));
  REQUIRE(epub.tocPath() == "OEBPS/toc.ncx");
  REQUIRE(epub.cssPaths().size() == 1);
  REQUIRE(epub.coverPath() == "OEBPS/images/cover.jpg");

  std::unique_ptr<reader::FileHandle> plain = fs.openRead("/plain.epub");
  REQUIRE(plain != nullptr);
  reader::Zip zp;
  REQUIRE(zp.open(*plain));
  REQUIRE(epub.open(*plain, zp));
  // The second book declares none of the three. Every one of these was the first
  // book's answer before fail() cleared them.
  CHECK(epub.tocPath().empty());
  CHECK(epub.cssPaths().empty());
  CHECK(epub.coverPath().empty());
}
