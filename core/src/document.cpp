#include "reader/document.h"

#include "reader/css.h"

// `<memory>` for the nothrow probe's unique_ptr and `<utility>` for the swap in
// take(). SPELLED OUT because a transitively-satisfied include is a bug only the other
// toolchain can see: this project's first Linux CI run died on a `std::memcmp` with no
// `<cstring>`, which libc++ pulls in and libstdc++ does not, after the file had
// compiled on macOS for months.
#include <memory>
#include <new>
#include <utility>

#include "reader/xml.h"

namespace reader {

// AT MOST ONE CUT PER TEXT NODE, which is what lets the cut defer its return to the
// end of the node: `out` holds the piece that was handed over, and a second cut in the
// same node would overwrite it. A node is at most `Xml::kTextBytes` and a cut needs
// `kMaxBlockBytes` of text, so this is the inequality that makes it impossible -- as an
// assert rather than a comment, because the two constants live in different headers and
// nothing else ties them together.
static_assert(Xml::kTextBytes < kMaxBlockBytes,
              "a text node must be smaller than a block, or one node could cut twice");

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

// EMPHASIS. `<cite>` is here because a cited title is set in italics by every
// convention this face was designed for, and books use it that way.
//
// `<strong>` AND `<b>` ARE ABSENT ON PURPOSE, and the measurement is in document.h:
// 17 runs and 220 characters across eight real books. They keep contributing their
// text with no marker, which is what they always did.
bool isEmphasis(std::string_view t) {
  return t == "em" || t == "i" || t == "cite";
}

// See document.h. Diagnostic only -- nothing reads these to decide anything.
MarkupHints gHints;

// Whether a `style` value asks for italics. Deliberately a substring search and not
// a CSS parse: this only has to answer "is there something here we are not reading",
// and a false positive costs a log line.
bool mentionsItalic(std::string_view style) {
  return style.find("italic") != std::string_view::npos ||
         style.find("oblique") != std::string_view::npos;
}

// NOT CONTENT. Their text must never reach a page: a stylesheet rendered as a
// paragraph is the most obvious way a reader can look broken.
bool isSuppressed(std::string_view t) {
  return t == "head" || t == "style" || t == "script";
}

// ONE TAG NAME, HELD BY VALUE.
//
// The stack used to hold `std::string_view`s of the names, which worked only while
// Xml handed out views into the caller's whole document. It reads a stream now, so
// a name lives in a buffer the next token overwrites -- and the two tests that
// caught it are worth naming, because neither looks like a lifetime bug:
// `<blockquote><p>x</p></blockquote>` came out as a plain paragraph (the stack's
// "blockquote" had become "p"), and `<a><b></a></b>` was ACCEPTED, because the
// mismatch check was comparing two views into the same buffer and they are always
// equal. A dangling view does not crash here; it silently agrees with itself.
//
// TRUNCATED AT 24 BYTES, which is longer than every element name that exists in
// the vocabularies an EPUB can contain: `blockquote` and `figcaption` are 10,
// MathML's `annotation-xml` is 14, and the longest name measured in a real book is
// 14. SVG's `feComponentTransfer` is 19. So two names collide only if they share
// their first 24 bytes AND their length, which no real pair does -- and the cost of
// getting that wrong is one accepted mis-nesting, not a corrupt page.
constexpr size_t kTagNameBytes = 24;

struct TagName {
  char bytes[kTagNameBytes];
  uint8_t len = 0;
  // DID THIS ELEMENT OPEN AN EMPHASIS RUN. Recorded at the start tag, read at the
  // matching end tag -- because by then the attributes are gone, and a `</span>`
  // cannot say whether its `<span>` carried an italic class. The tag-based path
  // (`<em>`, `<i>`, `<cite>`) rides the same flag rather than re-testing the name,
  // so the two cannot disagree about which close ends which run.
  bool openedEmphasis = false;

  void set(std::string_view s) {
    len = static_cast<uint8_t>(s.size() < kTagNameBytes ? s.size() : kTagNameBytes);
    for (size_t i = 0; i < len; ++i) bytes[i] = s[i];
    // The FULL length is what makes truncation safe: two names sharing a prefix
    // but differing in length still compare unequal.
    full = static_cast<uint16_t>(s.size());
  }
  bool operator==(std::string_view s) const {
    if (s.size() != full) return false;
    const size_t n = s.size() < kTagNameBytes ? s.size() : kTagNameBytes;
    for (size_t i = 0; i < n; ++i)
      if (bytes[i] != s[i]) return false;
    return true;
  }
  std::string_view view() const { return {bytes, len}; }

  uint16_t full = 0;
};

// Whether a block holds nothing but whitespace, WITH U+00A0 COUNTING AS
// WHITESPACE.
//
// `<p>&nbsp;</p>` is how an ebook makes vertical space, and it is everywhere: the
// first text chapter of a real EPUB opens with THREE of them. Trimming only ASCII
// space left each one as a two-byte block, which took a line of the page and drew
// blank -- the device showed a page whose first line was a space.
//
// NBSP is only whitespace for THIS question. It is kept inside text, because there
// it is deliberate: French sets a non-breaking space before a colon, and collapsing
// that to an ordinary one would let the line break in the wrong place.
bool onlyWhitespace(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }
    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
      i += 2;
      continue;
    }
    return false;
  }
  return true;
}

// The byte length of a dialogue dash opening `s`, or 0 if there is none.
//
// THREE MARKS, AND THE CORPUS CHOSE THEM. Across 225 real books, a paragraph opens
// with U+2014 EM DASH (45 books), ASCII '-' (7) or U+2013 EN DASH (4) and with
// nothing else -- U+2010, U+2011, U+2012, U+2015 and U+2212 open none. Two of those
// are not in tools/fontc.py's CODEPOINTS either, so a rule for them would be a rule
// for a notdef box.
size_t dialogueDashLen(const std::string& s) {
  if (s.empty()) return 0;
  if (s[0] == '-') return 1;
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xE2 &&
      static_cast<unsigned char>(s[1]) == 0x80) {
    const unsigned char c = static_cast<unsigned char>(s[2]);
    if (c == 0x93 || c == 0x94) return 3;  // U+2013 EN DASH, U+2014 EM DASH
  }
  return 0;
}

// A DIALOGUE DASH IS GLUED TO ITS FIRST WORD, by replacing the ordinary space after
// it with U+00A0.
//
// A dash opening a paragraph is direct speech, and it belongs to the words after
// it -- so that space must be neither elastic nor a break opportunity. U+00A0 is
// exactly that on this device and needs no new mechanism: `wrapProseLead`,
// `stretchFor` and `drawRunF26` all key on U+0020 and nothing else, so a
// non-breaking space is already unbreakable and already unstretchable in all three.
//
// THIS IS THE MAJORITY FORM, SUPPLIED FOR THE BOOKS THAT OMITTED IT, which is what
// makes it a normalisation rather than a style this layer invented. Of the corpus's
// dash-opening paragraphs, 17,435 already carry the non-breaking space themselves
// (15,053 with an em dash, 2,382 with an en dash) and 8,095 carry a plain space. The
// publishers state the rule; a third of them just do not encode it.
//
// WHAT IT LOOKED LIKE WITHOUT THIS. `Le Fleau` is 6,837 of the plain-space ones and
// is what reported it: the gap after the dash was justified along with every other
// gap on its line, so across four consecutive lines of one exchange it was 1, 1, 5.5
// and 5.7 spaces wide and read as the paragraph indent moving at random. The indent
// never moved -- it is 48px on every one of them. On a line holding only the dash and
// one long word (`- Brrrrrrrrrroum...`) the single gap took all 164px of the line's
// slack: 28 spaces.
//
// CHROME DOES NOT DO THIS, and that is deliberate rather than drift. It stretches
// U+00A0 exactly as it stretches U+0020, so it opens the same gap -- the firmware
// already declines to, in the three functions named above, and this extends that
// existing decision to the books that wrote the wrong character. No board states it
// because no board states the U+00A0 rule it rides on.
//
// THE SPANS MOVE WITH IT. U+00A0 is two bytes where the space was one, so every
// emphasis span after the seam shifts and every span across it grows. Getting that
// wrong italicises from one byte early -- and that byte is a space, so it is
// INVISIBLE. test_document.cpp asserts the spans against the bytes they cover.
void glueDialogueDash(Block& b) {
  const size_t d = dialogueDashLen(b.text);
  if (d == 0 || d >= b.text.size() || b.text[d] != ' ') return;
  b.text.replace(d, 1, "\xC2\xA0");
  for (Span& s : b.emphasis) {
    if (static_cast<size_t>(s.off) > d) ++s.off;
    else if (static_cast<size_t>(s.off) + s.len > d) ++s.len;
  }
}

// The kind a block gets, read from the WHOLE STACK rather than from the tag that
// started it -- outermost wins. `<blockquote><p>x</p></blockquote>` is a quoted
// paragraph, not a paragraph that happens to sit inside a quote, and the same for
// `<li><p>`: what the reader should see is decided by the outer element. Reading
// only the innermost tag makes every `<blockquote><p>` a plain paragraph, which is
// how the quote styling silently disappears from a book.
BlockKind kindFromStack(const TagName* stack, size_t depth) {
  for (size_t i = 0; i < depth; ++i) {
    const std::string_view t = stack[i].view();
    if (t == "blockquote") return BlockKind::Blockquote;
    if (t == "li") return BlockKind::ListItem;
    if (t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6")
      return BlockKind::Heading;
  }
  return BlockKind::Paragraph;
}

}  // namespace

// All of the builder's state, in one heap allocation -- the reason Inflater's
// Scratch gives: an Xml is 2,560 bytes and a 64-deep tag stack is another 1,792, so
// a BlockReader as a local would put 4.3 KB in a frame, and this project has
// already had one stack-protection panic from arrays in the wrong place.
struct BlockReader::State {
  Xml xml;
  TagName stack[kMaxNestDepth];
  size_t depth = 0;
  // The depth at which suppression began, 0 for "not suppressing". Nested
  // suppression (a `<style>` inside a `<head>`) needs no counter: the outer one is
  // already in force and the inner close is not at the recorded depth.
  size_t suppressAt = 0;
  Block cur;
  bool open = false;
  bool finished = false;
  // HOW DEEP INSIDE EMPHASIS THE PARSER IS, and a depth rather than a flag because
  // `<em><cite>x</cite></em>` is one emphasised run and not two nested ones. Only
  // the transitions 0->1 and 1->0 open and close a span.
  size_t emDepth = 0;
  // Where the open span started, as an offset into `cur.text`. Meaningful only
  // while emDepth > 0.
  size_t emStart = 0;
  // The book's italic class names, or null. Not owned -- see BlockReader::
  // setItalicClasses.
  const std::vector<std::string>* italicClasses = nullptr;

  explicit State(ByteSource& src) : xml(src) {}
};

BlockReader::BlockReader(ByteSource& src) : st_(new (std::nothrow) State(src)) {
  if (st_ == nullptr) error_ = "not enough memory to read this chapter";
}

void BlockReader::setItalicClasses(const std::vector<std::string>* classes) {
  if (st_ != nullptr) st_->italicClasses = classes;
}

BlockReader::~BlockReader() { delete st_; }

void BlockReader::restart(ByteSource& src) {
  if (st_ == nullptr) return;
  st_->xml.restart(src);
  st_->depth = 0;
  st_->suppressAt = 0;
  st_->cur = Block{};
  st_->open = false;
  st_->finished = false;
  st_->emDepth = 0;
  st_->emStart = 0;
  emitted_ = 0;
  blocksSplit_ = 0;
  error_ = "";
}

bool BlockReader::next(Block& out) {
  if (st_ == nullptr || !ok() || st_->finished) return false;
  State& st = *st_;

  // Whitespace collapses on the way IN rather than in a pass afterwards, because
  // the alternative is holding a chapter's raw text with every source newline in it
  // and then rewriting it.
  const auto appendSpace = [&]() {
    if (!st.cur.text.empty() && st.cur.text.back() != ' ') st.cur.text.push_back(' ');
  };

  // Closes the emphasis span that is open, if one is, at the text's current end.
  // Returns false only on the cap.
  //
  // AN EMPTY SPAN IS DROPPED. `<em></em>`, and `<em> </em>` once the space collapses
  // away, would otherwise leave a zero-length range that every walk has to skip.
  const auto closeSpan = [&]() -> bool {
    if (st.emStart >= st.cur.text.size()) return true;
    if (st.cur.emphasis.size() >= kMaxEmphasisPerBlock) {
      error_ = "too much emphasis in one block";
      return false;
    }
    st.cur.emphasis.push_back(
        Span{static_cast<uint32_t>(st.emStart),
             static_cast<uint32_t>(st.cur.text.size() - st.emStart)});
    return true;
  };

  // THE BLOCK'S BUFFER IS RESERVED, NOT GROWN -- issue #90, and it is what makes
  // `kMaxBlockBytes` a bound the heap can honour rather than an argument about the
  // growth factor of a standard library this project does not ship.
  //
  // `push_back` grows GEOMETRICALLY, so a cap of N costs far more than N in flight:
  // libstdc++'s ladder is 15*2^k, so the block's last reallocation holds the old
  // buffer and a new one of twice the size at once, and the peak is ~2.5x the final
  // capacity. document.h has the table; the short version is that seven of the
  // corpus's 225 books could not be read on this device and none of them was near the
  // cap. Reserved once and never grown, the peak is exactly two buffers -- the piece
  // handed to the caller and the one being built.
  //
  // NOTHROW-PROBED, BECAUSE `reserve` CANNOT BE. There is no std::nothrow spelling of
  // std::string::reserve, and under -fno-exceptions a request the heap cannot serve is
  // `abort()` with no diagnostic. So the probe asks the heap the same question first
  // and the reserve that follows takes the block it just released -- imagefit.cpp's
  // shape, and legal here for its reason: this is the single-threaded loop task, and
  // the request is the same size, issued immediately.
  //
  // THE REFUSAL NEEDS NO NEW WORDS. It is the message BlockReader's own State
  // allocation already answers with, so there is no new BookErrorReason and no new
  // copy shape on BookError.dc.html: "this chapter needs more memory than this device
  // has" is one fact however it is discovered.
  //
  // AND THE RESERVE IS EXACT, which needs an argument because libstdc++'s `_M_create`
  // rounds a request up to twice the OLD capacity: reserving the cap on a buffer
  // already half the cap would give twice the cap. It cannot happen here, and the
  // reason is worth writing down rather than re-deriving. The reserve only ever fires
  // on a buffer this function has not seen, and there are two of those: the caller's
  // Block on the first block of a walk (its own internal buffer, 15 bytes on
  // libstdc++), and the buffer handed back by the swap in take() after the FIRST cut,
  // which likewise came from the caller. Between that cut and the next text node the
  // block can only take the remainder of one text node, so it is at most
  // `Xml::kTextBytes` and its capacity at most 1,920 -- a quarter of the cap, so the
  // request is more than twice it and lands exactly.
  //
  // THAT 1,920 IS ALSO THE ONLY TERM ABOVE TWO BUFFERS, so the real peak is
  // 2 * (kMaxBlockBytes + 2) + 1,920 = 18,308 bytes, reached at the one moment after
  // the first cut when the piece just handed over, the short buffer and its
  // replacement are all live. It could be removed by reserving at the cut site
  // instead, which needs `next()` to hand the piece back while carrying an error, and
  // 1,920 bytes is not worth that control flow.
  //
  // NOT A FRESH STRING SWAPPED IN, WHICH IS HOW THIS WAS WRITTEN FIRST AND IT LOST TEXT.
  // `roomFor` runs once per text NODE, and a block spans many, so `st.cur.text` is
  // routinely non-empty here -- swapping a fresh buffer in threw away everything
  // accumulated since the last reserve. Two of #37's own tests caught it, one of them
  // as a hole in the middle of a rejoined digit run. `reserve` copies the content
  // across by definition, which is the whole reason to use it.
  const auto roomFor = [&]() -> bool {
    if (st.cur.text.capacity() >= kMaxBlockBytes + 2) return true;
    {
      std::unique_ptr<char[]> probe(new (std::nothrow) char[kMaxBlockBytes + 2]);
      if (probe == nullptr) {
        error_ = "not enough memory to read this chapter";
        return false;
      }
    }
    st.cur.text.reserve(kMaxBlockBytes + 2);
    return true;
  };

  // Moves the block being built into `out`, if it has anything in it. An empty
  // block is DROPPED, not emitted blank: `<p></p>` between chapters is a
  // generator's artifact and a blank block would take a line of the page.
  const auto take = [&](bool& have) -> bool {
    have = false;
    if (st.open) {
      // EMPHASIS STILL OPEN AT A BLOCK BOUNDARY IS CLOSED AT IT. `<em><p>a</p>
      // <p>b</p></em>` is markup a book really does contain, and the alternative --
      // carrying the span across the boundary -- would mean a span indexing a
      // string it does not belong to. It reopens against the next block below.
      if (st.emDepth > 0 && !closeSpan()) return false;
      const size_t before = st.cur.text.size();
      while (!st.cur.text.empty() && st.cur.text.back() == ' ') st.cur.text.pop_back();
      // THE TRIM CAN LAND INSIDE A SPAN, and an offset past the end of the string is
      // the kind of thing that reads fine in every test whose text has no trailing
      // space. `<p>a <em>b </em></p>` trims one byte off a span that ended there.
      if (before != st.cur.text.size())
        st.cur.emphasis = clipTo(st.cur.emphasis, 0, st.cur.text.size());
      // AFTER THE TRIM, so a block that is nothing but a dash and a space has had
      // the space removed and has no gap left to glue.
      glueDialogueDash(st.cur);
      if (!onlyWhitespace(st.cur.text)) {
        if (emitted_ >= static_cast<int>(kMaxBlocks)) {
          error_ = "too many blocks";
          return false;
        }
        // SWAPPED, NOT MOVED, so the reserved buffer COMES BACK. A move hands the
        // buffer to the caller and leaves `st.cur` on its small internal one, so the
        // next block would have to reserve again -- once per paragraph, 537,474 times
        // over the corpus. A caller that reuses one Block (which is every caller in
        // this repo: `Block b; while (cr.next(b))`) therefore reserves ONCE per
        // reader, and one that moves out of `out` gets a fresh reserve per block,
        // which is still fewer allocations than the nine-step ladder this replaces.
        //
        // WHAT THE CALLER GIVES UP: `out`'s previous value is handed to the reader
        // rather than destroyed, and is cleared below. Nothing can be holding a view
        // into it -- `LaidLine::text` is OWNED precisely so a Page can outlive the
        // blocks it was laid from -- and the caller has by definition already consumed
        // it, because it is what the previous next() returned. This is also what
        // restart()'s "reusing the buffers" has claimed since it was written.
        std::swap(out, st.cur);
        ++emitted_;
        have = true;
      }
    }
    // NOT `st.cur = Block{}`, which would throw the buffer away again. Every field
    // Block's default constructor sets, set by hand, so a field added to Block has to
    // be considered here -- the price of keeping the capacity.
    //
    // AND THE `kind` RESET IS DEAD TODAY, which is written down rather than left for
    // someone to discover: commenting it out fails NOTHING, because `st.open` is set
    // only by `beginBlock`, `take` emits only when `st.open`, and `beginBlock` re-reads
    // the kind off the stack -- so no block can be emitted with a kind this line would
    // have corrected. It stays because the reset has to be COMPLETE: the two lines
    // below it are load-bearing (see the emphasis one, which needed a three-block
    // fixture to catch at all), and a partial reset is what invites the next field
    // added to Block to be forgotten.
    st.cur.kind = BlockKind::Paragraph;
    st.cur.text.clear();
    st.cur.emphasis.clear();
    st.open = false;
    return true;
  };

  const auto beginBlock = [&]() {
    st.cur.kind = kindFromStack(st.stack, st.depth);
    st.open = true;
    // Emphasis that was open across the boundary starts again at byte 0 of the new
    // block -- the other half of the rule take() states.
    if (st.emDepth > 0) st.emStart = 0;
  };

  for (;;) {
    const Xml::Node n = st.xml.next();

    if (n == Xml::Node::Error) {
      error_ = st.xml.error();
      return false;
    }

    if (n == Xml::Node::Eof) {
      // UNCLOSED TAGS SURFACE HERE, for free, off the stack this layer needs
      // anyway -- which is why xml.h deliberately keeps no stack of its own.
      if (st.depth != 0) {
        error_ = "unclosed tag at end of document";
        return false;
      }
      st.finished = true;
      bool have = false;
      if (!take(have)) return false;
      return have;
    }

    if (n == Xml::Node::StartTag) {
      if (st.depth >= kMaxNestDepth) {
        error_ = "nesting too deep";
        return false;
      }
      st.stack[st.depth].openedEmphasis = false;
      st.stack[st.depth++].set(st.xml.name());

      if (st.suppressAt != 0) continue;
      if (isSuppressed(st.xml.name())) {
        st.suppressAt = st.depth;
        // Whatever block was being built ends at the boundary rather than
        // absorbing the text after the suppressed element.
        bool have = false;
        if (!take(have)) return false;
        if (have) return true;
        continue;
      }
      if (startsBlock(st.xml.name())) {
        // The PREVIOUS block is what goes out; the new one opens against the stack
        // as it stands now, which is why beginBlock runs before the return.
        bool have = false;
        if (!take(have)) return false;
        beginBlock();
        if (have) return true;
        continue;
      }
      if (separatesWords(st.xml.name())) {
        if (!st.open) beginBlock();
        appendSpace();
      }
      // COUNTED BEFORE THE TAG IS ACTED ON, so it sees every inline tag whether or
      // not this parser understands it. That is the whole point: the tags it does
      // NOT understand are the question.
      // ITALIC BY CLASS, from the book's own stylesheet. The tag-based test comes
      // first so a `<em class="x">` is one run and not two.
      const bool italicByClass =
          st.italicClasses != nullptr && !isEmphasis(st.xml.name()) &&
          classAttrIsItalic(st.xml.attr("class"), *st.italicClasses);
      if (isEmphasis(st.xml.name())) {
        ++gHints.emphasisTags;
      } else {
        const std::string_view style = st.xml.attr("style");
        if (!style.empty()) {
          ++gHints.styledSpans;
          if (mentionsItalic(style)) ++gHints.italicStyles;
        }
        const std::string_view cls = st.xml.attr("class");
        if (!cls.empty()) {
          ++gHints.classedSpans;
          if (gHints.sampleClass[0] == '\0') {
            const size_t take = cls.size() < sizeof(gHints.sampleClass) - 1
                                    ? cls.size()
                                    : sizeof(gHints.sampleClass) - 1;
            for (size_t i = 0; i < take; ++i) gHints.sampleClass[i] = cls[i];
            gHints.sampleClass[take] = '\0';
          }
        }
      }
      if (isEmphasis(st.xml.name()) || italicByClass) {
        // A bare `<em>` with no block around it is still the book's words, exactly
        // as a bare text node is -- so it opens one, or `emStart` would index a
        // block that beginBlock is about to replace.
        if (!st.open) beginBlock();
        if (st.emDepth++ == 0) st.emStart = st.cur.text.size();
        // RECORDED ON THE ELEMENT, because the close cannot work it out again: by
        // then the attributes are gone and `</span>` says nothing about which
        // `<span>` it ends. depth-1 is this tag's slot, pushed just above.
        st.stack[st.depth - 1].openedEmphasis = true;
      }
      continue;
    }

    if (n == Xml::Node::EndTag) {
      if (st.depth == 0) {
        error_ = "end tag with no start tag";
        return false;
      }
      if (!(st.stack[st.depth - 1] == st.xml.name())) {
        error_ = "mismatched end tag";
        return false;
      }
      --st.depth;

      if (st.suppressAt != 0) {
        if (st.depth + 1 == st.suppressAt) st.suppressAt = 0;
        continue;
      }
      if (startsBlock(st.xml.name())) {
        bool have = false;
        if (!take(have)) return false;
        if (have) return true;
        continue;
      }
      if (separatesWords(st.xml.name())) appendSpace();
      // THE ELEMENT'S OWN FLAG, not its name. A class-italic run is closed by a
      // `</span>` that is indistinguishable from every other one, so the name cannot
      // answer this -- and reading the flag makes the tag-based path stricter too: an
      // `</em>` whose `<em>` was inside a suppressed element no longer decrements a
      // depth it never incremented.
      if (st.stack[st.depth].openedEmphasis && st.emDepth > 0) {
        // Only the OUTERMOST close ends the run: `<em><cite>x</cite></em>` is one
        // emphasised phrase.
        if (--st.emDepth == 0 && !closeSpan()) return false;
      }
      continue;
    }

    // Text.
    if (st.suppressAt != 0) continue;
    // A bare text node with no block around it is still the book's words, so it
    // opens one rather than being lost.
    if (!st.open) beginBlock();
    // ONE SITE, AND IT IS ENOUGH BECAUSE IT IS THE ONLY PLACE TEXT CAN START. The
    // push_back below is one of two writers; the other is `appendSpace`, which refuses
    // an empty string, so a space can only ever follow a text node that came through
    // here -- and `glueDialogueDash`'s extra byte in take() likewise needs text to
    // already be there. Per text NODE rather than per character: a capacity comparison
    // and an early return, and a node is at most Xml::kTextBytes.
    if (!roomFor()) return false;
    // WHETHER A CUT PUT A FINISHED BLOCK IN `out`. The return is deferred to the end
    // of the text node so the bytes AFTER the cut land in the continuation instead of
    // being dropped -- returning from inside the loop would lose the rest of the node,
    // because the next call moves the tokenizer on.
    bool cutHere = false;
    for (const char c : st.xml.text()) {
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        appendSpace();
        continue;
      }
      // A BLOCK OVER THE CAP IS CUT, NOT REFUSED -- issue #37, and the same
      // correction `kMaxAttrBytes` got in #35.
      //
      // This used to set `error_`, which stops BlockReader, which ends the chapter --
      // and `next()` returning false is ALSO how a chapter ends normally, so nothing
      // reported it. Two Gutenberg mathematics texts in the 225-book corpus lost
      // everything after one paragraph of a hundred thousand digits, and the book read
      // as though it simply stopped there. It is the named-entity bug's exact shape.
      //
      // SPLIT RATHER THAN TRUNCATE-AND-RECORD, which was the other candidate. What the
      // cap protects is the size of ONE block -- the peak this layer exists to bound --
      // and both halves are under it, so splitting keeps the bound exactly and loses no
      // text. Truncating would have kept the reporting and thrown the bytes away, and
      // its magnitude is unbounded rather than academic: a chapter that is one giant
      // <div> with no <p> is ONE block, and a real book's longest chapter is 228,849
      // bytes of blocks, so truncation there would drop 72% of it. CLAUDE.md's rule
      // decides between them -- A VISIBLE WRONG BEATS AN INVISIBLE ONE.
      //
      // WHAT IT COSTS, stated rather than discovered: `indentedAfter(Paragraph,
      // Paragraph)` is true, so a continuation gets the 1.5em paragraph indent and a
      // split blockquote or heading gets a blank row above its second half. At the cap
      // #90 derived that is one spurious paragraph break per 8 KB of unbroken text --
      // once per ~15 pages of it, against #37's once per ~121 at 64 KB -- and 190 cuts
      // across 17 of the corpus's 225 books, against 4 cuts in 2. On the other 208 the
      // rate is zero: they write no paragraph that long.
      //
      // RAISING THE CAP IS STILL NOT THE FIX -- "the failure mode was the bug and the
      // number is fine" was #35's finding twice, and #90 is the case where the NUMBER
      // was also wrong, in the other direction. It was above what the heap can serve,
      // so it protected nothing: see document.h.
      if (st.cur.text.size() >= kMaxBlockBytes) {
        bool have = false;
        if (!take(have)) return false;
        ++blocksSplit_;
        // The kind is RE-DERIVED from the tag stack rather than remembered, and the
        // emphasis run open across the seam closes and reopens -- both are the rule
        // `<em><p>a</p><p>b</p></em>` already states, reached from the other direction.
        beginBlock();
        cutHere = cutHere || have;
      }
      st.cur.text.push_back(c);
    }
    if (cutHere) return true;
  }
}

bool buildDocument(std::string_view xhtml, Document& out, const char** reason) {
  out.blocks.clear();
  BufferSource src(xhtml);
  BlockReader r(src);
  if (!r.ok()) {
    *reason = r.error();
    return false;
  }
  Block b;
  while (r.next(b)) out.blocks.push_back(std::move(b));
  if (!r.ok()) {
    *reason = r.error();
    out.blocks.clear();
    return false;
  }
  return true;
}

const MarkupHints& lastMarkupHints() { return gHints; }
void resetMarkupHints() { gHints = MarkupHints{}; }

}  // namespace reader
