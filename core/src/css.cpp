#include "reader/css.h"

#include <cctype>
#include <memory>
#include <new>

#include "reader/epub.h"
#include "reader/filesystem.h"
#include "reader/heapguard.h"
#include "reader/inflate_stream.h"
#include "reader/zip.h"

namespace reader {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// A class name's bytes: what CSS allows without escaping, which is what generators
// emit. A hyphen and an underscore are in; a digit is in after the first byte.
bool isNameByte(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '_';
}

// Case-insensitive search, for property names only -- CSS is case-insensitive there
// and class names are not.
size_t findNoCase(std::string_view hay, std::string_view needle, size_t from) {
  if (needle.empty() || hay.size() < needle.size()) return std::string_view::npos;
  for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
    size_t j = 0;
    for (; j < needle.size(); ++j) {
      const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(hay[i + j])));
      const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
      if (a != b) break;
    }
    if (j == needle.size()) return i;
  }
  return std::string_view::npos;
}

// Does this declaration block ask for italics? `font-style: italic` and its
// shorthand-free cousins; `oblique` counts because a face without a true italic is
// asked for the same way and books use it interchangeably.
//
// THE VALUE MUST FOLLOW THE PROPERTY, which is why this is not two independent
// searches: `font-family: "Italic Garamond"` mentions both words and asks for
// neither. The colon after the property is what separates them.
bool bodyAsksForItalic(std::string_view body) {
  size_t at = 0;
  for (;;) {
    const size_t prop = findNoCase(body, "font-style", at);
    if (prop == std::string_view::npos) return false;
    size_t i = prop + 10;
    while (i < body.size() && isSpace(body[i])) ++i;
    if (i < body.size() && body[i] == ':') {
      ++i;
      while (i < body.size() && isSpace(body[i])) ++i;
      const std::string_view rest = body.substr(i);
      if (findNoCase(rest, "italic", 0) == 0 || findNoCase(rest, "oblique", 0) == 0) return true;
    }
    at = prop + 1;
  }
}

void addClass(std::string_view name, std::vector<std::string>& out) {
  if (name.empty() || name.size() > kMaxClassNameBytes) return;
  if (out.size() >= kMaxItalicClasses) return;
  for (const std::string& have : out)
    if (have == name) return;
  out.emplace_back(name);
}

// Every `.name` in a selector list. `span.foo, .bar > em` yields foo and bar.
void collectFromSelector(std::string_view sel, std::vector<std::string>& out) {
  for (size_t i = 0; i < sel.size(); ++i) {
    if (sel[i] != '.') continue;
    size_t j = i + 1;
    while (j < sel.size() && isNameByte(sel[j])) ++j;
    addClass(sel.substr(i + 1, j - i - 1), out);
    i = j - 1;
  }
}

}  // namespace

void collectItalicClasses(std::string_view css, std::vector<std::string>& out) {
  size_t at = 0;
  size_t selStart = 0;
  while (at < css.size()) {
    const char c = css[at];
    // COMMENTS ARE SKIPPED rather than parsed, because a commented-out rule is not a
    // rule and a `{` inside one would otherwise close a block that never opened.
    if (c == '/' && at + 1 < css.size() && css[at + 1] == '*') {
      const size_t end = css.find("*/", at + 2);
      at = (end == std::string_view::npos) ? css.size() : end + 2;
      continue;
    }
    if (c == '}') {  // a stray close, or the end of an at-rule's wrapper
      ++at;
      selStart = at;
      continue;
    }
    if (c != '{') {
      ++at;
      continue;
    }
    const std::string_view selector = css.substr(selStart, at - selStart);
    const size_t bodyStart = at + 1;
    const size_t close = css.find('}', bodyStart);
    // AN AT-RULE'S WRAPPER IS ENTERED, NOT SKIPPED: `@media { .x { font-style:
    // italic } }` has its inner rules scanned, because the first `}` found closes the
    // INNER block and the selector for it starts after this `{`. That is what makes a
    // class inside a media query collectable, and it is the over-matching choice this
    // file's header takes knowingly.
    if (close == std::string_view::npos) {
      selStart = bodyStart;
      at = bodyStart;
      continue;
    }
    const std::string_view body = css.substr(bodyStart, close - bodyStart);
    if (body.find('{') != std::string_view::npos) {
      // A wrapper: step inside rather than treating its contents as declarations.
      selStart = bodyStart;
      at = bodyStart;
      continue;
    }
    if (bodyAsksForItalic(body)) collectFromSelector(selector, out);
    at = close + 1;
    selStart = at;
  }
}

bool classAttrIsItalic(std::string_view classAttr, const std::vector<std::string>& italics) {
  if (italics.empty() || classAttr.empty()) return false;
  size_t i = 0;
  while (i < classAttr.size()) {
    while (i < classAttr.size() && isSpace(classAttr[i])) ++i;
    size_t j = i;
    while (j < classAttr.size() && !isSpace(classAttr[j])) ++j;
    if (j > i) {
      const std::string_view one = classAttr.substr(i, j - i);
      for (const std::string& have : italics)
        if (have.size() == one.size() && have == one) return true;
    }
    i = j;
  }
  return false;
}

namespace {

// A stylesheet larger than this is not one a book needs read, and the number is
// sized against the HEAP AT BOOK OPEN rather than against what CSS can be. This runs
// where loadToc runs -- openBook has released its archive and the Reader's 32 KB
// inflater does not exist yet, so there is ~133 KB free -- and it is a transient
// std::string on top of that. 32 KB is a quarter of the headroom for a file that
// measures 2-10 KB in every book checked.
//
// It is a CAP, not a buffer size: the read streams and stops here, so a pathological
// sheet costs a bounded read and yields whatever rules came first rather than being
// refused. collectItalicClasses is safe on a truncated file -- an unterminated rule
// simply ends the scan, which its tests pin.
constexpr size_t kMaxStylesheetBytes = 32u * 1024u;

// Inflate one archive entry into `out`, stopping at the cap. False means the entry
// could not be read at all, which the caller treats as "this book has no styles"
// rather than as a failure to open it.
bool readEntry(FileHandle& file, const Zip::Entry& entry, std::string& out) {
  uint32_t dataOffset = 0;
  if (!Zip::locateData(file, entry.localHeaderOffset, entry.compressedSize, dataOffset))
    return false;
  EntrySource bytes;
  bytes.reset(file, dataOffset, entry.compressedSize);

  Inflater inflater;
  std::unique_ptr<InflateSource> inflated;
  ByteSource* src = &bytes;
  if (entry.deflated) {
    if (!inflater.begin(bytes)) return false;
    inflated.reset(new (std::nothrow) InflateSource(inflater));
    if (inflated == nullptr) return false;
    src = inflated.get();
  }
  out.clear();
  uint8_t buf[512];
  // A CAP IS NOT A GUARD, which is the lesson `kMaxBlockBytes` taught one layer
  // down. 32 KB of `append` climbs a geometric ladder and its largest single
  // request was measured at 16,640 bytes on a real book -- taken while the 36,956
  // byte inflate window above it is still held, inside the phase that is the PEAK of
  // a whole book open. `append` cannot refuse; `appendOrRefuse` probes for the block
  // the growth will want and answers false instead of `abort()`.
  //
  // A REFUSAL HERE IS NOT A BOOK FAILURE. `readEntry` returning false already means
  // "this book has no styles" to its one caller, so a stylesheet that will not fit
  // costs the book its italics and nothing else -- which is the same call this file
  // already makes for a sheet that will not inflate.
  for (;;) {
    const size_t got = src->read(buf, sizeof(buf));
    if (got == 0) break;
    if (out.size() + got > kMaxStylesheetBytes) {
      return appendOrRefuse(out, reinterpret_cast<const char*>(buf),
                            kMaxStylesheetBytes - out.size());
    }
    if (!appendOrRefuse(out, reinterpret_cast<const char*>(buf), got)) return false;
  }
  return true;
}

}  // namespace

void readItalicClasses(FileHandle& file, Zip& zip, const Epub& epub,
                       std::vector<std::string>& out) {
  std::string css;
  for (const std::string& path : epub.cssPaths()) {
    if (out.size() >= kMaxItalicClasses) break;
    const Zip::Entry* entry = zip.find(path);
    if (entry == nullptr) continue;  // declared and absent: the book's problem, not ours
    if (!readEntry(file, *entry, css)) continue;
    collectItalicClasses(css, out);
  }
}

}  // namespace reader
