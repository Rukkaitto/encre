#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace reader {

class Epub;
class FileHandle;
class Zip;

// --- WHICH CLASS NAMES MEAN ITALIC -------------------------------------------
//
// `document.cpp` reads `<em>`, `<i>` and `<cite>`, and a real book does not
// necessarily use any of them. Measured on the user's own card: one chapter of
// `Le Fleau` carries **609 classed inline tags and not one emphasis tag** -- its
// italics are `<span class="...">` against a publisher stylesheet, which is what a
// converted EPUB usually emits. So without this, the italics of most books on a
// real card do not render at all, silently.
//
// THIS IS NOT A CSS ENGINE AND MUST NOT BECOME ONE. It answers exactly one
// question -- which class names carry `font-style: italic` -- and everything else
// in the stylesheet is skipped without being understood. That bound is what keeps
// it affordable on a part where the reader's floor is 42 KB: the OUTPUT is a
// handful of short names, and the input is scanned in one forward pass with no
// tree, no cascade and no specificity.
//
// WHAT IT DELIBERATELY DOES NOT DO, each because the cost is real and the benefit
// is not:
//   * SPECIFICITY AND THE CASCADE. A later rule turning italic back off is not
//     modelled; the class stays in the set. Over-matching italicises a run that
//     should be roman, which is a typographic wrong; under-matching is invisible,
//     which is worse, and today everything is under-matched.
//   * ELEMENT AND ID SELECTORS. `p { font-style: italic }` would italicise a whole
//     chapter, and an id names one element. Only classes are collected.
//   * @media, @supports AND THE REST. An at-rule's body is scanned exactly like any
//     other text, so a class inside one is collected. That is the over-matching
//     choice again, taken knowingly.
//
// Case: class names in HTML are case-sensitive, so the comparison is exact. CSS
// property names are not, so `FONT-STYLE` is matched case-insensitively.

// The most class names worth keeping from one book. A stylesheet naming more italic
// classes than this is not a book, and the cap is what stops a malformed file
// turning into an unbounded allocation on the reader's path.
inline constexpr size_t kMaxItalicClasses = 64;
// Longer than any class name a generator emits; measured samples are 4-12 bytes.
inline constexpr size_t kMaxClassNameBytes = 48;

// Scan one stylesheet and add every class name whose rule asks for italics.
// Appends to `out`, de-duplicated, and stops at the cap. Never allocates per rule:
// the selector and the body are string_views into `css`.
void collectItalicClasses(std::string_view css, std::vector<std::string>& out);

// The same, over every stylesheet an already-opened book declares. Takes the open
// handles rather than a path because it shares them with loadToc: reading the
// contents and reading the styles are both "what the book says about itself", both
// wanted at the same moment, and a second archive open costs ~100 ms and a second
// central-directory parse for nothing.
//
// A stylesheet that will not read is SKIPPED, not fatal. Missing italics is a
// typographic loss; refusing to open the book over it would be a reader that will
// not show a book because of its CSS.
void readItalicClasses(FileHandle& file, Zip& zip, const Epub& epub,
                       std::vector<std::string>& out);

// True when `classAttr` -- the raw `class` attribute, which may name several
// classes separated by whitespace -- includes any name in `italics`.
bool classAttrIsItalic(std::string_view classAttr, const std::vector<std::string>& italics);

}  // namespace reader
