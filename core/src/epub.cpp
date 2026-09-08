#include "reader/epub.h"

#include "reader/heapguard.h"
#include "reader/xml.h"

namespace reader {
namespace {

constexpr std::string_view kContainerPath = "META-INF/container.xml";

// Read one archive entry as a string, or fail. Every step of this layer is
// "find an entry, read it, parse it", so the repetition is worth a name.
//
// ABSENT AND UNREADABLE ARE TWO ANSWERS, and collapsing them was a false claim about
// the file. `zip.read` refuses an entry it cannot hold -- a nothrow buffer and a
// probe, both there since 3A -- and both call sites below reported that as "this is
// not an EPUB" / "names an OPF that is not in the archive", so an out-of-memory
// inside the container read arrived on the panel as `appears damaged`. `*why` is null
// when the entry is simply not there and the archive's own reason otherwise.
bool readEntry(FileHandle& file, Zip& zip, std::string_view path, std::string& out,
               const char** why) {
  *why = nullptr;
  const Zip::Entry* e = zip.find(path);
  if (e == nullptr) return false;
  if (zip.read(file, *e, out)) return true;
  // Zip::read leaves `reason()` empty on some refusals (a local header that will not
  // parse, an entry over kMaxEntryBytes), so reporting it blindly could report
  // nothing at all.
  const char* r = zip.reason();
  *why = (r != nullptr && r[0] != '\0') ? r : "an archive entry would not read";
  return false;
}

// Does a space-separated attribute value carry this exact token?
//
// AN EPUB 3 `properties` IS A SET, NOT A STRING, so `find()` is the wrong tool: it
// answers yes for `not-cover-image`, which declares no cover at all, and for any
// future vocabulary term that happens to end in one we look for. The whole attribute
// is at most a handful of short words, so walking it costs nothing.
bool hasToken(std::string_view list, std::string_view token) {
  size_t at = 0;
  while (at < list.size()) {
    // XML attribute values may be separated by any whitespace, not only a space.
    while (at < list.size() && (list[at] == ' ' || list[at] == '\t' || list[at] == '\n' ||
                                list[at] == '\r'))
      ++at;
    size_t end = at;
    while (end < list.size() && list[end] != ' ' && list[end] != '\t' &&
           list[end] != '\n' && list[end] != '\r')
      ++end;
    if (end > at && list.substr(at, end - at) == token) return true;
    at = end;
  }
  return false;
}

}  // namespace

bool resolveHref(std::string_view base, std::string_view href, std::string& out) {
  out.clear();
  if (href.empty()) return false;
  // AN ABSOLUTE HREF IS REFUSED. A zip entry name has no leading slash, so `/x`
  // addresses nothing -- and treating it as root-relative would be inventing a
  // filesystem this code never sees.
  if (href.front() == '/') return false;

  // The base's DIRECTORY, which is the part that trips people: an OPF at
  // `OEBPS/content.opf` with an href of `ch1.xhtml` means `OEBPS/ch1.xhtml`.
  const size_t slash = base.rfind('/');
  std::vector<std::string_view> parts;
  if (slash != std::string_view::npos) {
    std::string_view dir = base.substr(0, slash);
    size_t at = 0;
    while (at <= dir.size()) {
      const size_t next = dir.find('/', at);
      const size_t end = next == std::string_view::npos ? dir.size() : next;
      if (end > at) parts.push_back(dir.substr(at, end - at));
      if (next == std::string_view::npos) break;
      at = next + 1;
    }
  }

  size_t at = 0;
  while (at <= href.size()) {
    const size_t next = href.find('/', at);
    const size_t end = next == std::string_view::npos ? href.size() : next;
    const std::string_view seg = href.substr(at, end - at);
    if (seg == "..") {
      // REFUSED rather than clamped at the root. A book asking to climb above the
      // archive is not a book we understand, and clamping would silently turn
      // `../../x` into `x` -- which might exist, and would then be the wrong file.
      if (parts.empty()) return false;
      parts.pop_back();
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    if (next == std::string_view::npos) break;
    at = next + 1;
  }

  if (parts.empty()) return false;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out.push_back('/');
    out.append(parts[i]);
  }
  return true;
}

bool Epub::fail(const char* why) {
  reason_ = why;
  title_.clear();
  author_.clear();
  identifier_.clear();
  chapters_.clear();
  // ...AND THE THREE FIELDS NOTED DURING THE OPF WALK. tocPath_ and cssPaths_ were
  // not reset here, so a SECOND open() on the same Epub reported the first book's
  // NCX and stylesheets when the second book declared none. Nothing in the firmware
  // reuses an Epub today -- openBook builds a local -- so it has never bitten; adding
  // a third field that behaved either way would have made the inconsistency
  // structural, which is why it is fixed rather than matched.
  tocPath_.clear();
  cssPaths_.clear();
  coverPath_.clear();
  return false;
}

bool Epub::open(FileHandle& file, Zip& zip) {
  fail("");
  reason_ = "";

  // 1. The container names the OPF. This is the one path in an EPUB that is fixed
  //    by the specification; everything else is discovered.
  std::string container;
  const char* readWhy = nullptr;
  if (!readEntry(file, zip, kContainerPath, container, &readWhy))
    return fail(readWhy != nullptr ? readWhy
                                   : "no META-INF/container.xml, so this is not an EPUB");

  std::string opfPath;
  {
    Xml x(container);
    for (;;) {
      const Xml::Node n = x.next();
      if (n == Xml::Node::Error) return fail("META-INF/container.xml is malformed");
      if (n == Xml::Node::Eof) break;
      if (n == Xml::Node::StartTag && x.name() == "rootfile" && x.hasAttr("full-path")) {
        opfPath.assign(x.attr("full-path"));
        break;  // the FIRST rootfile: a container may list more, and the first is
                // the one a reader is meant to render.
      }
    }
  }
  if (opfPath.empty()) return fail("container.xml names no rootfile");

  std::string opf;
  if (!readEntry(file, zip, opfPath, opf, &readWhy))
    return fail(readWhy != nullptr ? readWhy
                                   : "container.xml names an OPF that is not in the archive");

  // 2. The OPF. One pass, gathering four things at once: the metadata, the
  //    manifest (id -> resolved path), the spine order, and the id that the
  //    `unique-identifier` attribute points at.
  std::string uniqueIdRef;
  std::string identifierWithId;  // the dc:identifier whose id matches uniqueIdRef
  std::vector<std::pair<std::string, std::string>> manifest;  // id -> path
  std::vector<std::string> spine;
  std::string tocId;  // the spine's `toc` attribute, resolved against the manifest below
  std::string coverId;  // `<meta name="cover">`'s value, resolved the same way

  {
    Xml x(opf);
    enum class In { None, Title, Creator, Identifier } in = In::None;
    std::string pendingIdentifierId;
    for (;;) {
      const Xml::Node n = x.next();
      if (n == Xml::Node::Error) return fail("the OPF is malformed");
      if (n == Xml::Node::Eof) break;

      if (n == Xml::Node::StartTag) {
        const std::string_view tag = x.name();
        if (tag == "package" && x.hasAttr("unique-identifier")) {
          uniqueIdRef.assign(x.attr("unique-identifier"));
        } else if (tag == "title") {
          in = In::Title;
        } else if (tag == "creator") {
          in = In::Creator;
        } else if (tag == "identifier") {
          in = In::Identifier;
          pendingIdentifierId.assign(x.attr("id"));
        } else if (tag == "item" && x.hasAttr("id") && x.hasAttr("href")) {
          std::string resolved;
          if (!resolveHref(opfPath, x.attr("href"), resolved))
            return fail("a manifest item's href does not resolve inside the archive");
          if (manifest.size() >= kMaxChapters) return fail("the manifest is too large");
          // THE NCX, NOTED IN PASSING. A table of contents lives in a manifest item
          // like any other part, so the one walk that resolves every href is the
          // cheapest place to notice it -- finding it later would mean re-parsing the
          // OPF, and scanning the archive for `*.ncx` would be a guess where the
          // manifest is a statement.
          //
          // BY MEDIA TYPE, which is the reliable half. The formal route is the
          // spine's `toc` attribute naming a manifest id, and that is honoured below
          // where the spine is read -- but the attribute is optional and real files
          // omit it, while the media type is what makes an NCX an NCX.
          if (x.attr("media-type") == "application/x-dtbncx+xml" && tocPath_.empty())
            tocPath_ = resolved;
          // THE STYLESHEETS, NOTED IN PASSING for the same reason the NCX is: the
          // manifest is the book STATING what its parts are, where scanning the
          // archive for `*.css` would be a guess. They are what says which class
          // names mean italic -- see reader/css.h for why that matters at all.
          if (x.attr("media-type") == "text/css" && cssPaths_.size() < kMaxStylesheets)
            cssPaths_.push_back(resolved);
          // THE COVER, NOTED IN PASSING for the third time and the same reason: this
          // is the one walk that resolves every manifest href, and a cover href is
          // relative to the OPF's directory exactly as a chapter's is. Finding it
          // afterwards would re-parse the OPF -- ~100 ms and ~32 KB of transient on
          // the device -- for a fact this walk already has in its hand.
          //
          // EPUB 3's ROUTE, and it wins over the EPUB 2 one resolved below: this is
          // the manifest declaring which of its items IS the cover, and it is
          // NORMATIVE, where `<meta name="cover">` is a convention that predates any
          // spec saying so and that real books often point at the cover PAGE.
          //
          // THE OPPOSITE WAY ROUND FROM THE NCX ABOVE, which is worth stating because
          // the two sit ten lines apart and the analogy reads as obvious and is
          // inverted: the NCX lets the pointer-by-id (the spine's `toc`) beat the
          // property on the item (its media type). Here the property wins.
          // FIRST WINS, spelled as the NCX line above spells it. A manifest with two
          // cover-image items is malformed either way; what matters is that the two
          // noted-in-passing fields next to each other do not answer that differently.
          if (coverPath_.empty() && hasToken(x.attr("properties"), "cover-image"))
            coverPath_ = resolved;
          // THE LARGEST UNGUARDED ALLOCATION THIS FUNCTION MADE, measured: the
          // manifest is 48 bytes an entry plus two path strings, capped at
          // kMaxChapters, and over 225 real EPUBs its vector reached a single
          // 24,576-byte request -- second only to the OPF string that `Zip::read`
          // has probed since 3A. `push_back` cannot refuse; this can.
          if (!pushOrRefuse(manifest,
                            std::pair<std::string, std::string>(std::string(x.attr("id")),
                                                                std::move(resolved))))
            return fail("not enough memory to read the manifest");
        } else if (tag == "meta" && x.attr("name") == "cover") {
          // EPUB 2'S ROUTE, and the one the corpus overwhelmingly uses: a bare
          // convention, in no specification, naming a manifest id. Resolved after the
          // walk rather than here, because the manifest item it names may not have
          // been read yet -- the same shape as the spine's `toc` attribute below.
          coverId.assign(x.attr("content"));
        } else if (tag == "spine") {
          // The spine's `toc` names the manifest id of the table of contents. It
          // takes precedence over the media-type guess above: it is the book saying
          // which of its parts is the contents, where the media type only says which
          // parts COULD be.
          if (x.hasAttr("toc")) tocId.assign(x.attr("toc"));
        } else if (tag == "itemref" && x.hasAttr("idref")) {
          if (spine.size() >= kMaxChapters) return fail("the spine is too long");
          if (!pushOrRefuse(spine, std::string(x.attr("idref"))))
            return fail("not enough memory to read the spine");
        }
        continue;
      }

      if (n == Xml::Node::Text) {
        // Only the FIRST of each is taken. A dc:title can legitimately appear more
        // than once (a subtitle, a collection), and the first is the book's.
        if (in == In::Title && title_.empty()) title_.assign(x.text());
        else if (in == In::Creator && author_.empty()) author_.assign(x.text());
        // AN UNNAMED IDENTIFIER MATCHES AN UNNAMED REFERENCE, so the emptiness check
        // is load-bearing: a package with no `unique-identifier` attribute and a
        // dc:identifier with no id are both "", and without it such a book would
        // adopt the first identifier it happens to carry and report it as the one it
        // designated. It designated none.
        else if (in == In::Identifier && !uniqueIdRef.empty() &&
                 pendingIdentifierId == uniqueIdRef && identifierWithId.empty())
          identifierWithId.assign(x.text());
        continue;
      }

      if (n == Xml::Node::EndTag) in = In::None;
    }
  }

  // 3. The identifier, WHERE IT RESOLVES -- and EMPTY IS NOT A FAILURE.
  //
  //    An OPF's `unique-identifier` names the id of the dc:identifier that is the
  //    book's own, and real files get that wrong constantly: 4 of 16 EPUBs in one
  //    measured library name an id nothing carries, publisher and Calibre output
  //    alike, and every one of them reads. This was a REFUSAL until one of them was
  //    reported from the device, on the stated grounds that an unresolved identifier
  //    "breaks everything keyed on it -- which is what per-book reading state will
  //    be". That consumer never arrived: reading state lives on the card under
  //    /.reader/state, keyed on a hash of the book's PATH with its byte size as the
  //    identity check, and nothing in the firmware reads identifier() at all. A
  //    prediction written into a comment outlived its truth and cost a quarter of a
  //    real shelf.
  //
  //    So it degrades the way the spine's `toc` attribute does below: a
  //    cross-reference inside the OPF that does not resolve costs the book the thing
  //    it named and nothing else. A spine itemref is the one that stays a refusal,
  //    because a spine is a reading ORDER rather than a fact about the book.
  identifier_ = std::move(identifierWithId);

  // 4. The spine, resolved through the manifest. A missing item is a refusal and
  //    not a skip: a spine is a reading ORDER, and dropping an entry gives the
  //    reader a book missing a chapter with no way to know it happened.
  // The spine's own answer wins over the media-type one, and a `toc` naming an id the
  // manifest does not list is simply ignored -- a missing table of contents is not a
  // reason to refuse a book, which is the same call this layer makes nowhere else
  // (a missing spine item IS a refusal, because a spine is a reading order).
  if (!tocId.empty()) {
    for (const auto& item : manifest)
      if (item.first == tocId) {
        tocPath_ = item.second;
        break;
      }
  }

  // THE COVER, IF THE MANIFEST DID NOT ALREADY SAY. `coverPath_` is non-empty only
  // when an item declared `properties="cover-image"`, and that is the book stating it
  // outright; this is the older convention filling in when it did not.
  //
  // A `<meta name="cover">` NAMING AN ID THE MANIFEST DOES NOT LIST COSTS THE BOOK
  // ITS COVER AND NOTHING ELSE -- the same call this layer makes for the `toc`
  // attribute and for an unresolved `unique-identifier`. A cover is metadata a reader
  // does not need, and refusing a book over it is how a quarter of one measured
  // library was lost once already.
  //
  // NO MEDIA-TYPE FILTER, deliberately. An `image/*` check would refuse a cover whose
  // manifest spells its type oddly, where a wrong pointer costs only a decoder that
  // sniffs the bytes, refuses them and falls back to the card. Losing a real cover is
  // the more expensive mistake of the two.
  if (coverPath_.empty() && !coverId.empty()) {
    for (const auto& item : manifest)
      if (item.first == coverId) {
        coverPath_ = item.second;
        break;
      }
  }

  if (spine.empty()) return fail("the spine is empty, so there is nothing to read");
  // ONE ALLOCATION FOR THE WHOLE LIST, taken while `manifest` and `spine` are both
  // still held -- which is the state that decides whether it fits, and the state the
  // probe therefore runs in. 48 bytes an entry: 15,408 for the longest spine in a
  // 225-book corpus.
  //
  // `ensureRoom` RESERVES as well as asking, so there is no second `reserve` under
  // it -- one that stayed would be a second, unguarded request for the same block.
  if (!ensureRoom(chapters_, spine.size()))
    return fail("not enough memory to hold the spine");
  for (const std::string& idref : spine) {
    const std::string* path = nullptr;
    for (const auto& item : manifest)
      if (item.first == idref) {
        path = &item.second;
        break;
      }
    if (path == nullptr) return fail("the spine references an item the manifest does not list");
    // ...and the manifest can name a file the archive does not hold, which is a
    // different failure and worth its own message.
    if (zip.find(*path) == nullptr)
      return fail("a spine item's file is not in the archive");
    chapters_.push_back(Chapter{idref, *path});
  }

  return true;
}

}  // namespace reader
