#include "reader/toc.h"

#include "reader/css.h"

#include <cstdio>
#include <memory>

#include "reader/epub.h"
#include "reader/heapguard.h"
#include "reader/inflate_stream.h"
#include "reader/xml.h"
#include "reader/zip.h"

namespace reader {

namespace {

// `ch3.xhtml#part2` names a file and a place in it. The reader positions by spine
// entry, so the fragment is dropped -- see the header's note on what that costs.
std::string_view withoutFragment(std::string_view src) {
  const size_t hash = src.find('#');
  return hash == std::string_view::npos ? src : src.substr(0, hash);
}

}  // namespace

bool loadToc(FileSystem& fs, std::string_view bookPath, std::vector<TocEntry>& out,
             const char** reason, std::vector<std::string>* italicClassesOut) {
  out.clear();
  const auto say = [reason](const char* why) {
    if (reason != nullptr) *reason = why;
    return false;
  };
  if (reason != nullptr) *reason = "";

  std::unique_ptr<FileHandle> file = fs.openRead(bookPath);
  if (file == nullptr) return say("cannot open the book file");
  Zip zip;
  if (!zip.open(*file)) return say(zip.reason());
  Epub epub;
  if (!epub.open(*file, zip)) return say(epub.reason());

  // THE STYLES FIRST, because a book with no contents still has italics and the
  // early return below would skip them. Failures are swallowed: see readItalicClasses.
  if (italicClassesOut != nullptr) {
    italicClassesOut->clear();
    readItalicClasses(*file, zip, epub, *italicClassesOut);
  }

  // NO TABLE OF CONTENTS IS NOT A FAILURE. The book reads perfectly well; it just
  // cannot name its chapters, and every caller's answer to that is to fall back on
  // the spine position it already shows.
  if (epub.tocPath().empty()) return true;

  const Zip::Entry* entry = zip.find(epub.tocPath());
  if (entry == nullptr) return say("the table of contents is not in the archive");

  // Streamed through the same chain a chapter uses, because an NCX is 20 KB of XML
  // and there is no reason to hold it whole when the parser reads a ByteSource.
  uint32_t dataOffset = 0;
  if (!Zip::locateData(*file, entry->localHeaderOffset, entry->compressedSize, dataOffset))
    return say("the table of contents' local header will not parse");
  EntrySource bytes;
  bytes.reset(*file, dataOffset, entry->compressedSize);

  Inflater inflater;
  std::unique_ptr<InflateSource> inflated;
  ByteSource* src = &bytes;
  if (entry->deflated) {
    if (!inflater.begin(bytes)) return say(inflater.error());
    inflated.reset(new (std::nothrow) InflateSource(inflater));
    if (inflated == nullptr) return say("not enough memory to read the table of contents");
    src = inflated.get();
  }

  // ON THE HEAP, because sizeof(Xml) is 2,560 and this runs inside a book open that
  // already reported `loopTask free at worst: 2632 bytes of 16384`. The reader's own
  // path holds its Xml inside a heap-allocated BlockReader for the same reason; a
  // stack-allocated one here spends a tenth of the task's whole budget for the length
  // of a parse.
  std::unique_ptr<Xml> xml(new (std::nothrow) Xml(*src));
  if (xml == nullptr) return say("not enough memory to read the table of contents");
  Xml& x = *xml;
  // The NCX's shape, and only the parts that carry meaning here:
  //
  //   <navPoint> <navLabel> <text>LABEL</text> </navLabel> <content src="..."/> </navPoint>
  //
  // A label and a target arrive in either order and either may be missing, so both are
  // accumulated and the entry is committed when the navPoint CLOSES. Committing on
  // `content` would drop a label that follows it.
  bool inNavPoint = false, inText = false;
  std::string label, target;
  // HOW DEEP THE POINT BEING READ SITS. One book of the four measured is three levels
  // deep (ten section headers over eighty-four chapters), so this is the board's
  // grouping and not a hypothetical -- see the header.
  int depth = 0;

  // COMMITTING IS ITS OWN STEP, because there are TWO moments a navPoint is complete:
  // when it closes, and when a CHILD navPoint opens. Only handling the close lost every
  // nested point's parent -- the child's start cleared the label the parent had already
  // read, so `<navPoint>A<navPoint>B</navPoint></navPoint>` yielded B alone. A test
  // caught it; the comment here previously claimed the parent's label "has already been
  // read by the time an inner one begins", which was true and beside the point.
  //
  // WHY A COMMIT REFUSED, because there are now two reasons and they are not the
  // same claim. `out.size() >= kMaxTocEntries` is the book's list being longer than
  // this will read; a refused growth is the device being out of memory for a book
  // that is fine. Both call sites reported the first, so the second would have
  // arrived in the log as "the table of contents is too long" -- a false statement
  // about the file, which is the one thing this project refuses everywhere.
  const char* commitWhy = "";
  const auto commit = [&]() -> bool {
    if (label.empty() || target.empty()) return true;
    // RESOLVED AGAINST THE NCX'S OWN PATH, not the OPF's. An NCX at the archive root
    // and one under OEBPS/ produce different absolute targets from the same relative
    // src, and real books have both -- three of four measured are under OEBPS/ and one
    // is at the root.
    std::string absolute;
    if (!resolveHref(epub.tocPath(), withoutFragment(target), absolute)) {
      label.clear();
      target.clear();
      return true;
    }
    // The spine's paths are already resolved, so this is a string match rather than a
    // second path calculation. An entry naming a file the spine does not read is
    // skipped: the NCX may point at anything in the archive, and a row that cannot be
    // opened is worse than a missing row.
    int spine = -1;
    const std::vector<Epub::Chapter>& chapters = epub.chapters();
    for (size_t i = 0; i < chapters.size(); ++i)
      if (chapters[i].path == absolute) {
        spine = static_cast<int>(i);
        break;
      }
    if (spine < 0) {
      label.clear();
      target.clear();
      return true;
    }
    if (out.size() >= kMaxTocEntries) {
      commitWhy = "the table of contents is too long";
      return false;
    }
    // AN IDENTICAL ROW TWICE IS NOISE; A DIFFERENT NAME FOR ONE TARGET IS CONTENT.
    //
    // Real books produce both. Le Fleau's NCX names `spine 3` twice with the SAME
    // label ("Pour Tabby") -- two rows a reader cannot tell apart, going to the same
    // place -- and names `spine 4` twice with DIFFERENT labels ("PREMIERE PARTIE",
    // "DEUXIEME PARTIE"), which is the book telling you about two sections of one
    // file. So the exact repeat is dropped and the distinct label is kept.
    //
    // Only the PREVIOUS entry is compared, not the whole list: an NCX is authored in
    // reading order (0 out-of-order entries across the four measured books), so a
    // repeat is adjacent, and a full scan would be quadratic on a 96-entry list for a
    // case that cannot happen far apart.
    if (!out.empty() && out.back().spine == spine && out.back().label == label) {
      label.clear();
      target.clear();
      return true;
    }
    // 32 bytes an entry against a 1,024-entry cap, and the corpus really does reach
    // for it: one book's NCX has 299 entries, whose vector took a single
    // 32,768-byte request -- the largest unguarded one in this phase, and this phase
    // is the PEAK of a book open (measured at 47.6-91.8 KB across the user's own 16
    // books, against a flat ~48-51 KB for the chapter walk).
    //
    // REFUSED THE WAY A TOO-LONG LIST IS, one clause up, because it is the same
    // event from the reader's side: `loadToc`'s failure is not a book failure. The
    // shell logs it and the book opens without chapter names.
    if (!pushOrRefuse(out, TocEntry{spine, depth < 1 ? 1 : depth, label})) {
      commitWhy = "not enough memory to read the table of contents";
      return false;
    }
    // CLEARED, so a close after a child's commit adds nothing. Without this the outer
    // point's close committed the CHILD's label a second time and the duplicate rule
    // happened to drop it -- correct by accident, and only while that rule exists.
    label.clear();
    target.clear();
    return true;
  };
  for (;;) {
    const Xml::Node n = x.next();
    if (n == Xml::Node::Eof) break;
    if (n == Xml::Node::Error) return say(x.error());

    if (n == Xml::Node::StartTag) {
      const std::string_view tag = x.name();
      if (tag == "navPoint") {
        // NESTING IS FLATTENED, not tracked: an inner navPoint starts a new entry and
        // the one it interrupts is committed first, so a parent keeps its own row. No
        // measured book nests, so a depth counter would be state kept for nothing --
        // but losing the parent silently would not be.
        if (inNavPoint && !commit()) return say(commitWhy);
        inNavPoint = true;
        ++depth;
        label.clear();
        target.clear();
      } else if (inNavPoint && tag == "text") {
        inText = true;
      } else if (inNavPoint && tag == "content" && x.hasAttr("src")) {
        target.assign(x.attr("src"));
      }
      continue;
    }

    if (n == Xml::Node::Text && inText) {
      // Several pieces for one run: Xml chunks at kTextBytes, so a long label arrives
      // in parts. Capped rather than grown without limit -- a file chooses the length.
      const std::string_view piece = x.text();
      const size_t room = kMaxTocLabelBytes > label.size() ? kMaxTocLabelBytes - label.size() : 0;
      label.append(piece.substr(0, piece.size() < room ? piece.size() : room));
      continue;
    }

    if (n != Xml::Node::EndTag) continue;
    const std::string_view tag = x.name();
    if (tag == "text") {
      inText = false;
    } else if (tag == "navPoint") {
      // A close commits whatever this point gathered. A point whose child already
      // committed has been cleared, so this adds nothing -- see commit().
      // Committed BEFORE the depth drops, so the entry records the level it was
      // authored at rather than its parent's.
      if (!commit()) return say(commitWhy);
      if (depth > 0) --depth;
      // Still inside a parent if one is open: a child's close returns to it, and the
      // parent may have siblings after this.
      inNavPoint = depth > 0;
    }
  }
  return true;
}

std::string chapterPositionLabel(int spine) {
  // `CH. %02d`, one-based, which is the form ReaderScreen has put in the header band
  // since it stopped being able to name the chapter. Two callers now, so it is one
  // function -- see toc.h.
  char buf[16];
  std::snprintf(buf, sizeof(buf), "CH. %02d", spine + 1);
  return std::string(buf);
}

size_t fillTocGaps(std::vector<TocEntry>& toc, int spineCount) {
  if (toc.empty() || spineCount <= 0) return 0;

  // THE NAMED RANGE, which is what bounds the fill -- front and back matter the book
  // itself did not list are not gaps. See toc.h for the measurement that settled this
  // against a size floor.
  //
  // ONLY THE LOWER HALF OF THAT BOUND IS WRITTEN DOWN, because the upper half is
  // structural: the walk below emits a gap only in front of an entry that already
  // exists, so it can never reach past the last one. A `next < hi` term here read as
  // load-bearing and was implied by the loop's own `next < e.spine` -- caught by a
  // mutation that removed it and failed nothing.
  int lo = spineCount;
  std::vector<bool> named(static_cast<size_t>(spineCount), false);
  for (const TocEntry& e : toc) {
    if (e.spine < 0 || e.spine >= spineCount) continue;
    named[static_cast<size_t>(e.spine)] = true;
    if (e.spine < lo) lo = e.spine;
  }
  if (lo >= spineCount) return 0;  // nothing the book names is in range

  // THE SECOND LIST IS GUARDED LIKE THE FIRST. `out` grows from a number a FILE
  // states -- the spine's length -- on the book-open path, and there is no nothrow
  // spelling of `reserve` or `push_back`, so an unguarded one is an `abort()` with no
  // diagnostic under -fno-exceptions. `loadToc` already builds its own list through
  // `pushOrRefuse` for exactly this reason, two functions up.
  //
  // A REFUSAL COSTS THE ROWS AND NOTHING ELSE: `toc` is left as the book wrote it and
  // this answers 0, which is the behaviour before this function existed. A PARTIAL
  // fill would be worse than none -- which chapters got a row would depend on where
  // the heap ran out, so the same book would offer a different list on different days.
  std::vector<TocEntry> out;
  if (!ensureRoom(out, toc.size())) return 0;
  size_t added = 0;
  // ONE PASS IN DOCUMENT ORDER, starting one past the first spine entry the book
  // names -- which is the whole of the lower bound. A gap is emitted immediately
  // before the first entry that sits past it, and takes THAT entry's depth -- which is what makes a
  // synthesised row structurally incapable of becoming a section header, since
  // `isHeaderAt` asks whether the next entry is DEEPER and equal depths are not.
  // The strictly-between bound guarantees such an entry exists.
  int next = lo + 1;
  for (const TocEntry& e : toc) {
    while (next < e.spine) {
      if (!named[static_cast<size_t>(next)] && out.size() + 1 < kMaxTocEntries) {
        if (!pushOrRefuse(out, TocEntry{next, e.depth, chapterPositionLabel(next)})) return 0;
        ++added;
      }
      ++next;
    }
    if (!pushOrRefuse(out, e)) return 0;
    if (e.spine >= next) next = e.spine + 1;
  }
  if (added != 0) toc = std::move(out);
  return added;
}

int tocIndexForSpine(const std::vector<TocEntry>& toc, int spine) {
  // THE FIRST MATCH. This kept the LAST for two phases -- see the header for why that
  // was the wrong end of the group, and for the corpus figures that say how common a
  // group is.
  for (size_t i = 0; i < toc.size(); ++i)
    if (toc[i].spine == spine) return static_cast<int>(i);
  return -1;
}

}  // namespace reader
