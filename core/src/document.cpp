#include "reader/document.h"

#include "reader/xml.h"

namespace reader {
namespace {

// THREE CATEGORIES OF ELEMENT, and naming them is the whole design. Every tag an
// EPUB can contain falls into one of these or into "transparent", and transparent
// is the default -- so an element nobody listed contributes its text and no
// structure, which is the DROPPED-not-approximated rule from document.h expressed
// as code.

// Starts a block. The text after it belongs to a new paragraph-like thing.
bool startsBlock(std::string_view t) {
  return t == "p" || t == "div" || t == "blockquote" || t == "li" || t == "pre" ||
         t == "tr" ||  // a table row reads as a paragraph; see below
         t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6";
}

// Does not start a block, but forces a word boundary. `<td>a</td><td>b</td>` has
// no whitespace between the cells and must not read as "ab" -- whereas
// `<em>b</em><i>c</i>` must read as "bc", because that is one word a generator
// split for styling. The difference is exactly this list.
bool separatesWords(std::string_view t) {
  return t == "td" || t == "th" || t == "br";
}

// NOT CONTENT. Their text must never reach a page: a stylesheet rendered as a
// paragraph is the most obvious way a reader can look broken.
bool isSuppressed(std::string_view t) {
  return t == "head" || t == "style" || t == "script";
}

// The kind a block gets, read from the WHOLE STACK rather than from the tag that
// started it -- outermost wins. `<blockquote><p>x</p></blockquote>` is a quoted
// paragraph, not a paragraph that happens to sit inside a quote, and the same for
// `<li><p>`: what the reader should see is decided by the outer element. Reading
// only the innermost tag makes every `<blockquote><p>` a plain paragraph, which is
// how the quote styling silently disappears from a book.
BlockKind kindFromStack(const std::string_view* stack, size_t depth) {
  for (size_t i = 0; i < depth; ++i) {
    const std::string_view t = stack[i];
    if (t == "blockquote") return BlockKind::Blockquote;
    if (t == "li") return BlockKind::ListItem;
    if (t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6")
      return BlockKind::Heading;
  }
  return BlockKind::Paragraph;
}

}  // namespace

bool buildDocument(std::string_view xhtml, Document& out, const char** reason) {
  out.blocks.clear();

  std::string_view stack[kMaxNestDepth];
  size_t depth = 0;

  // The depth at which suppression began, 0 for "not suppressing". Nested
  // suppression (a `<style>` inside a `<head>`) needs no counter: the outer one is
  // already in force and the inner close is not at the recorded depth.
  size_t suppressAt = 0;

  Block cur;
  bool open = false;

  // Whitespace collapses on the way IN rather than in a pass afterwards, because
  // the alternative is holding a chapter's raw text with every source newline in it
  // and then rewriting it -- two copies of the largest string in the program, on a
  // device with 300 KB of heap.
  const auto appendSpace = [&]() {
    if (!cur.text.empty() && cur.text.back() != ' ') cur.text.push_back(' ');
  };

  const auto flush = [&]() -> bool {
    if (open) {
      while (!cur.text.empty() && cur.text.back() == ' ') cur.text.pop_back();
      // An empty block is DROPPED, not emitted blank: `<p></p>` between chapters is
      // a generator's artifact and a blank block would cost a line of the page.
      if (!cur.text.empty()) {
        if (out.blocks.size() >= kMaxBlocks) {
          *reason = "too many blocks";
          return false;
        }
        out.blocks.push_back(std::move(cur));
      }
    }
    cur = Block{};
    open = false;
    return true;
  };

  const auto begin = [&]() -> bool {
    if (!flush()) return false;
    cur.kind = kindFromStack(stack, depth);
    open = true;
    return true;
  };

  Xml xml(xhtml);
  for (;;) {
    const Xml::Node n = xml.next();

    if (n == Xml::Node::Error) {
      *reason = xml.error();
      return false;
    }

    if (n == Xml::Node::Eof) {
      // UNCLOSED TAGS SURFACE HERE, for free, off the stack this layer needs
      // anyway -- which is why xml.h deliberately keeps no stack of its own.
      if (depth != 0) {
        *reason = "unclosed tag at end of document";
        return false;
      }
      return flush();
    }

    if (n == Xml::Node::StartTag) {
      if (depth >= kMaxNestDepth) {
        *reason = "nesting too deep";
        return false;
      }
      stack[depth++] = xml.name();

      if (suppressAt != 0) continue;
      if (isSuppressed(xml.name())) {
        suppressAt = depth;
        // Whatever block was being built ends at the boundary rather than
        // absorbing the text after the suppressed element.
        if (!flush()) return false;
        continue;
      }
      if (startsBlock(xml.name())) {
        if (!begin()) return false;
      } else if (separatesWords(xml.name())) {
        if (!open && !begin()) return false;
        appendSpace();
      }
      continue;
    }

    if (n == Xml::Node::EndTag) {
      if (depth == 0) {
        *reason = "end tag with no start tag";
        return false;
      }
      if (stack[depth - 1] != xml.name()) {
        *reason = "mismatched end tag";
        return false;
      }
      --depth;

      if (suppressAt != 0) {
        if (depth + 1 == suppressAt) suppressAt = 0;
        continue;
      }
      if (startsBlock(xml.name())) {
        if (!flush()) return false;
      } else if (separatesWords(xml.name())) {
        appendSpace();
      }
      continue;
    }

    // Text.
    if (suppressAt != 0) continue;
    // A bare text node with no block around it is still the book's words, so it
    // opens one rather than being lost.
    if (!open && !begin()) return false;
    for (const char c : xml.text()) {
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        appendSpace();
        continue;
      }
      if (cur.text.size() >= kMaxBlockBytes) {
        *reason = "block too long";
        return false;
      }
      cur.text.push_back(c);
    }
  }
}

}  // namespace reader
