#include "reader/epub.h"

#include "reader/xml.h"

namespace reader {
namespace {

constexpr std::string_view kContainerPath = "META-INF/container.xml";

// Read one archive entry as a string, or fail. Every step of this layer is
// "find an entry, read it, parse it", so the repetition is worth a name.
bool readEntry(FileHandle& file, Zip& zip, std::string_view path, std::string& out) {
  const Zip::Entry* e = zip.find(path);
  if (e == nullptr) return false;
  return zip.read(file, *e, out);
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
  return false;
}

bool Epub::open(FileHandle& file, Zip& zip) {
  fail("");
  reason_ = "";

  // 1. The container names the OPF. This is the one path in an EPUB that is fixed
  //    by the specification; everything else is discovered.
  std::string container;
  if (!readEntry(file, zip, kContainerPath, container))
    return fail("no META-INF/container.xml, so this is not an EPUB");

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
  if (!readEntry(file, zip, opfPath, opf))
    return fail("container.xml names an OPF that is not in the archive");

  // 2. The OPF. One pass, gathering four things at once: the metadata, the
  //    manifest (id -> resolved path), the spine order, and the id that the
  //    `unique-identifier` attribute points at.
  std::string uniqueIdRef;
  std::string identifierWithId;  // the dc:identifier whose id matches uniqueIdRef
  std::vector<std::pair<std::string, std::string>> manifest;  // id -> path
  std::vector<std::string> spine;
  std::string tocId;  // the spine's `toc` attribute, resolved against the manifest below

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
          manifest.emplace_back(std::string(x.attr("id")), std::move(resolved));
        } else if (tag == "spine") {
          // The spine's `toc` names the manifest id of the table of contents. It
          // takes precedence over the media-type guess above: it is the book saying
          // which of its parts is the contents, where the media type only says which
          // parts COULD be.
          if (x.hasAttr("toc")) tocId.assign(x.attr("toc"));
        } else if (tag == "itemref" && x.hasAttr("idref")) {
          if (spine.size() >= kMaxChapters) return fail("the spine is too long");
          spine.emplace_back(x.attr("idref"));
        }
        continue;
      }

      if (n == Xml::Node::Text) {
        // Only the FIRST of each is taken. A dc:title can legitimately appear more
        // than once (a subtitle, a collection), and the first is the book's.
        if (in == In::Title && title_.empty()) title_.assign(x.text());
        else if (in == In::Creator && author_.empty()) author_.assign(x.text());
        else if (in == In::Identifier && pendingIdentifierId == uniqueIdRef &&
                 identifierWithId.empty())
          identifierWithId.assign(x.text());
        continue;
      }

      if (n == Xml::Node::EndTag) in = In::None;
    }
  }

  // 3. The identifier has to RESOLVE. It parses fine when it does not, and then
  //    breaks everything keyed on it -- which is what per-book reading state will
  //    be. Refused here rather than discovered when a bookmark lands on the wrong
  //    book.
  if (uniqueIdRef.empty()) return fail("the OPF declares no unique-identifier");
  if (identifierWithId.empty())
    return fail("the OPF's unique-identifier names an id no dc:identifier carries");
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

  if (spine.empty()) return fail("the spine is empty, so there is nothing to read");
  chapters_.reserve(spine.size());
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
