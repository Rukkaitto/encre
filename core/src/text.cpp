#include <optional>

#include "reader/dither.h"
#include "reader/text.h"

#include "reader/framebuffer.h"
#include "reader/glyphsource.h"
#include "reader/profile.h"

namespace reader {

namespace {

// --- The glyph blit ----------------------------------------------------------
//
// WHAT THIS USED TO BE, and why it is worth the arithmetic below: a per-pixel
// loop calling font.coverage() then fb.setPixel(). Measured on the device, a
// reader page's render is 99% this -- 213 ms of a 215 ms render, 352 ms of a
// cold one -- which makes it the single most expensive drawing routine in the
// firmware, ahead of the veil this file's neighbour (dither.cpp) already
// rewrote for the same reason and in the same shape.
//
// Four costs per emitted pixel, none of which depends on the pixel:
//
//   - coverage() recomputed `bitmap + row * stride` per PIXEL, a multiply for a
//     row pointer that is constant across the row, and branched on the face's
//     bit depth per pixel;
//   - the Plane switch re-decided per pixel what the plane decides per SCREEN
//     (and per row, for the dither's phase);
//   - setPixel bounds-checked a coordinate the glyph's own box already settles;
//   - byteIndex()/bitMask() divided and took a modulus per pixel, and then did
//     a read-modify-write of one bit -- eight of them per byte.
//
// So: clip the glyph's box ONCE, hoist the plane decision to a four-entry table
// per physical row, hoist the row pointer, and accumulate a byte's worth of
// bits before touching memory. Nothing about the OUTPUT changes;
// test_text.cpp keeps the per-pixel form as its reference and asserts this
// produces the identical framebuffer for a spread of glyphs, all four planes,
// both rotations, both panel geometries and glyphs clipped at each edge.
//
// The notdef box is deliberately NOT routed through here. It is four setPixel
// runs on a face that has no glyph for a codepoint, which is a case a correct
// screen never hits at all -- and it is drawn from geometry rather than from a
// bitmap, so it shares none of the machinery below.

// WHICH COVERAGES INK, as a 4-bit set indexed by coverage: bit c set means a
// pixel of coverage c emits. That is the whole of the Plane switch, decided
// once instead of per pixel, and it is expressible as a table only because
// coverage is 0..3 for every face (glyphsource.h makes that structural: a 1bpp
// face reports 0 or 3, never 1 or 2).
constexpr uint8_t kEmitBw = 0b1100;   // cov >= 2
constexpr uint8_t kEmitLsb = 0b1010;  // cov & 1
constexpr uint8_t kEmitMsb = 0b1100;  // cov & 2 -- the same set as Bw, and not
                                      // by coincidence: `cov >= 2` and
                                      // `cov & 2` select {2,3} either way on a
                                      // 2-bit scale. Spelled separately so the
                                      // two planes stay two decisions.

// The same set for the dithered plane, where it depends on the pixel's Bayer
// rank as well as its coverage: `(cov * 16) / 3 > rank`, which is 0, 5, 10, 16
// for coverage 0..3. So coverage 0 never inks (0 > rank is false for every rank
// in 0..15) and coverage 3 always does (16 > 15) -- a glyph's interior is never
// stippled, and its blank surround never speckles -- and only the two edge
// coverages consult the tile.
constexpr uint8_t ditherEmitMask(int rank) {
  return static_cast<uint8_t>((5 > rank ? 0b0010 : 0) | (10 > rank ? 0b0100 : 0) | 0b1000);
}

// Coverage of one pixel of a glyph ROW, from a pointer to that row.
//
// A SECOND COPY OF GlyphSource::coverage's UNPACKING, and a deliberate one --
// this project's rule is that the second copy is the extraction point, so the
// reason has to be written down rather than assumed. The interface's coverage()
// takes (glyph, col, row) and so must recompute `bitmap + row * stride` on every
// call; hoisting that out of the inner loop is a third of what this rewrite is,
// and GlyphSource has no accessor that would let it be hoisted. The pin is a
// test rather than a shared function: test_text.cpp's reference implementation
// reads its coverage through GlyphSource::coverage and its frames are compared
// with these byte for byte, so an unpacking that disagreed by one pixel of one
// glyph fails there rather than drifting in silence.
//
// THE DEPTH IS A TEMPLATE PARAMETER RATHER THAN AN ARGUMENT, and the measurement
// is the only reason: at -Os on the desktop, a page of body text went 179 -> 151
// us unrotated with it and 147 -> 148 rotated, which is to say it buys ~15% on
// the path the goldens, the tests and `make compare` take and NOTHING on the
// device's own (Ccw) path, where the source walk strides and the unpacking is
// not what the loop is waiting for. Two instantiations of blitGlyphT is what it
// costs. Every face in the repo is 2bpp except literata_18, so the branch it
// removes was perfectly predicted -- what the template buys is the addressing
// being folded, not the branch.
template <int Bpp>
inline uint8_t coverageInRow(const uint8_t* row, int col) {
  if (Bpp == 1) return ((row[col >> 3] >> (7 - (col & 7))) & 1) ? 3 : 0;
  // 2bpp, MSB-first: two bits per pixel, four pixels per byte.
  return static_cast<uint8_t>((row[col >> 2] >> (6 - 2 * (col & 3))) & 0x3);
}

// One PHYSICAL row of the blit: `n` consecutive physical columns starting at
// `pc0`, taking their coverage from `cov(i)` for i in [0, n).
//
// The bits are accumulated into a byte and written once, which is the other
// half of the win: a bit-addressed read-modify-write per pixel becomes one per
// eight, and a byte no pixel of which inks is not touched at all -- which is
// most of a glyph's bounding box, since a glyph is mostly its surround.
//
// `masks[(phase0 + i) & 3]` is the emit set for pixel i. For every plane but
// BwDithered the four entries are equal and the indexing costs a load; for
// BwDithered they are the four phases of the Bayer tile along whichever axis
// this run walks, which is what keeps the stipple keyed on ABSOLUTE panel
// coordinates -- the phase must not shift with the glyph, or two words on one
// line stipple out of step with each other.
template <typename CovFn>
inline void blitPhysRun(uint8_t* prow, int pc0, int n, bool white, const uint8_t masks[4],
                        int phase0, CovFn cov) {
  int i = 0;
  int pc = pc0;
  while (i < n) {
    const int byte = pc >> 3;
    const int bit0 = pc & 7;
    int take = 8 - bit0;
    if (take > n - i) take = n - i;
    uint8_t m = 0;
    for (int k = 0; k < take; ++k) {
      const uint8_t c = cov(i + k);
      if ((masks[(phase0 + i + k) & 3] >> c) & 1)
        m = static_cast<uint8_t>(m | (0x80u >> (bit0 + k)));
    }
    // Skipping an all-zero mask is not an optimisation of the write, it is the
    // absence of one: `|= 0` and `&= ~0` are both no-ops, so the branch replaces
    // a load and a store with nothing.
    if (m != 0) {
      if (white) prow[byte] = static_cast<uint8_t>(prow[byte] | m);
      else prow[byte] = static_cast<uint8_t>(prow[byte] & ~m);
    }
    i += take;
    pc += take;
  }
}

// Blit one glyph's coverage with its bitmap's top-left at logical (gx, gy) --
// which the caller derives from the pen and the glyph's bearings, so the two
// halves of that arithmetic (the 26.6 pen, and where a bitmap sits relative to
// the baseline) stay where they were.
template <int Bpp>
void blitGlyphT(Framebuffer& fb, const Glyph& g, int gx, int gy, bool white, Plane plane) {
  const int fw = fb.width(), fh = fb.height();
  // An inert framebuffer (a non-positive geometry, or a refused view) reports
  // zero here and has no bytes behind data(); the clip below would already
  // reject every pixel, but this returns before a pointer is formed from null.
  if (fw <= 0 || fh <= 0) return;
  // CLIP THE BOX ONCE, which is what setPixel's own bounds check used to do one
  // pixel at a time. After this every coordinate the loops produce is in range,
  // which is also what makes the `& 3` phases below safe without the extra
  // rescue a negative coordinate would need.
  const int c0 = gx < 0 ? -gx : 0;
  const int c1 = (gx + g.bitmapW) > fw ? fw - gx : g.bitmapW;
  const int r0 = gy < 0 ? -gy : 0;
  const int r1 = (gy + g.bitmapH) > fh ? fh - gy : g.bitmapH;
  if (c0 >= c1 || r0 >= r1) return;

  uint8_t* const base = fb.data();
  const int stride = fb.physRowBytes();
  const int gstride = g.stride;
  const bool dithered = (plane == Plane::BwDithered);
  const uint8_t flat = plane == Plane::Lsb ? kEmitLsb : (plane == Plane::Msb ? kEmitMsb : kEmitBw);
  uint8_t masks[4] = {flat, flat, flat, flat};

  if (fb.rotation() == Rotation::Ccw) {
    // UNDER ROTATION A LOGICAL ROW IS A PHYSICAL COLUMN, so accumulating bits
    // along a glyph row would write one bit into each of eight different bytes
    // and, worse, into the wrong ones -- the mistake veilRect records, which
    // passes every desktop test and every golden and smears only on glass.
    // A logical COLUMN is a physical row (physX = logY, physY = width - 1 - logX
    // -- framebuffer.cpp's byteIndex), so the outer loop walks the glyph's
    // COLUMNS: each one is one physical row, whose consecutive bits are the
    // glyph's successive ROWS. The source walk is then a stride jump per pixel
    // instead of a shift within a byte, which costs nothing measurable because a
    // glyph's bitmap is a few hundred bytes and sits in L1 for the whole blit.
    for (int col = c0; col < c1; ++col) {
      const int px = gx + col;
      if (dithered)
        for (int p = 0; p < 4; ++p) masks[p] = ditherEmitMask(bayer4(px, p));
      uint8_t* const prow = base + static_cast<size_t>(fw - 1 - px) * static_cast<size_t>(stride);
      const uint8_t* const src = g.bitmap + static_cast<size_t>(r0) * static_cast<size_t>(gstride);
      blitPhysRun(prow, gy + r0, r1 - r0, white, masks, (gy + r0) & 3, [&](int i) {
        return coverageInRow<Bpp>(src + static_cast<size_t>(i) * static_cast<size_t>(gstride), col);
      });
    }
  } else {
    // Unrotated: a logical row IS a physical row, so this is the plain case and
    // the source walk is sequential within the glyph's own row.
    for (int row = r0; row < r1; ++row) {
      const int py = gy + row;
      if (dithered)
        for (int p = 0; p < 4; ++p) masks[p] = ditherEmitMask(bayer4(p, py));
      uint8_t* const prow = base + static_cast<size_t>(py) * static_cast<size_t>(stride);
      const uint8_t* const src = g.bitmap + static_cast<size_t>(row) * static_cast<size_t>(gstride);
      blitPhysRun(prow, gx + c0, c1 - c0, white, masks, (gx + c0) & 3,
                  [&](int i) { return coverageInRow<Bpp>(src, c0 + i); });
    }
  }
}

void blitGlyph(Framebuffer& fb, const Glyph& g, int gx, int gy, int bpp, bool white, Plane plane) {
  if (bpp == 1) blitGlyphT<1>(fb, g, gx, gy, white, plane);
  else blitGlyphT<2>(fb, g, gx, gy, white, plane);
}

}  // namespace

// THE ONE PEN LOOP. drawText and drawTextJustified are both this, differing by
// `extraPerGapF26` alone -- a second loop for justified text would be a second
// place for the fractional pen, the kern-before-glyph order, the notdef box and
// every fidelity fix this file has accumulated to be got subtly differently. The
// header says one text path; this is where that is true or not.
// THE PEN IS THE ARGUMENT AND THE RETURN, in 26.6. It used to take an integer x
// and return an integer advance, which was exactly right while a line was one run
// in one face -- and rounds at every boundary once a line can change face
// part-way. `drawRun` below keeps the integer signature for every caller that has
// one run, so nothing else moves.
static int drawRunF26(Framebuffer& fb, const GlyphSource& font, int penFIn, int baselineY,
                      std::string_view utf8, Ink ink, Tracking tracking, Plane plane,
                      int extraPerGapF26) {
  // ONE SPAN PER RUN, never per glyph: the clock would then cost more than the
  // blit it was timing. A run is a whole string at one face, so a page of body
  // text is ~12 of these and a chrome screen a few dozen.
  PhaseSpan sp(Phase::Glyph);
  const bool white = (ink == Ink::White);
  // The pen is 26.6 fixed point; `pen` below is only ever the *paint* position,
  // rounded off it. With integer tracking penF stays a multiple of 64 and every
  // glyph lands exactly where the old integer pen put it, so this is a strict
  // generalisation rather than a re-rounding of the untracked runs.
  int penF = penFIn;
  char32_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const char32_t cp = utf8Next(utf8, i);
    // KERNING IS LOOKED UP BEFORE THE GLYPH, and the order is the contract
    // rather than a preference. `Glyph::bitmap` is borrowed and valid only until
    // the next call into this same GlyphSource -- and `kerning()` is such a
    // call. Asking for it after the glyph was safe only because neither shipped
    // implementation's kerning() touches the cache; the day one does (kerning
    // synthesised from outlines is the obvious future), every kerned pair in
    // body text would blit from an evicted arena slot, and it would pass every
    // test in which the arena never happens to wrap mid-pair.
    //
    // It is applied below rather than here, so the notdef path still resets
    // `prev` and kerns across nothing -- which is what measure() does too, and
    // the two must not part company.
    const int kernF = prev ? pxToF26(font.kerning(prev, cp)) : 0;
    const std::optional<Glyph> g = font.glyph(cp);
    if (!g) {
      // No glyph for this codepoint: draw a hollow box so malformed or
      // out-of-subset text is visibly wrong instead of silently invisible.
      // Font::notdefAdvance() is the width this consumes, so Font::measure can
      // account for it without a second copy of the geometry.
      const int pen = f26ToPx(penF);
      const int h = font.ascent() * 2 / 3;
      const int w = h / 2 + 1;
      const int top = baselineY - h;
      for (int col = 0; col < w; ++col) {
        fb.setPixel(pen + col, top, white);
        fb.setPixel(pen + col, baselineY - 1, white);
      }
      for (int row = 0; row < h; ++row) {
        fb.setPixel(pen, top + row, white);
        fb.setPixel(pen + w - 1, top + row, white);
      }
      penF += pxToF26(font.notdefAdvance()) + tracking.f26();
      prev = 0;
      continue;
    }
    penF += kernF;
    const int pen = f26ToPx(penF);
    // The coverage blit, which is 99% of a reader page's render on the device --
    // see blitGlyph above for what it does and what it used to do. What the
    // plane means, including the dither's `(cov * 16) / 3 > bayer4(px, py)`
    // stipple, lives there now: it is decided per row rather than per pixel, so
    // it cannot be spelled here as well without being spelled twice.
    blitGlyph(fb, *g, pen + g->xOff, baselineY - g->yOff, font.bpp(), white, plane);
    penF += pxToF26(g->advance) + tracking.f26();
    // The word gap stretch, applied to ASCII space and nothing else. The set has
    // to match layout.cpp's gap COUNT exactly -- it divides the line's slack by
    // that count -- so both name the same single codepoint rather than each
    // deciding what a gap is.
    if (cp == U' ') penF += extraPerGapF26;
    prev = cp;
  }
  return penF;
}

static int drawRun(Framebuffer& fb, const GlyphSource& font, int x, int baselineY,
                   std::string_view utf8, Ink ink, Tracking tracking, Plane plane,
                   int extraPerGapF26) {
  const int penF =
      drawRunF26(fb, font, pxToF26(x), baselineY, utf8, ink, tracking, plane, extraPerGapF26);
  // f26ToPx is exact-linear in x (x is a whole pixel, so it factors out of the
  // rounding), which is why this equals Font::measure of the same run.
  return f26ToPx(penF) - x;
}

const GlyphSource& StyledFace::at(size_t off) const {
  return anyEmphasis() && emphasisedAt(*emphasis, off) ? *italic : *roman;
}

int drawTextStyled(Framebuffer& fb, const StyledFace& face, int x, int baselineY,
                   std::string_view utf8, const std::vector<Span>& emphasis,
                   int extraPerGapF26, Ink ink, Tracking tracking, Plane plane) {
  // The common case is ONE piece in ONE face, and it takes the same call the
  // unstyled path takes -- so a page with no emphasis on it costs what it did.
  if (face.italic == nullptr || emphasis.empty())
    return drawRun(fb, *face.roman, x, baselineY, utf8, ink, tracking, plane, extraPerGapF26);

  // A LOCAL FACE OVER THE LINE'S OWN SPANS. `face.emphasis` indexes whatever text
  // the WRAP was given -- a whole block -- and this function is handed one LINE
  // with spans re-based onto it. Using the caller's face here would read the block's
  // offsets against the line's bytes, which is the same class of error as a stale
  // guard: both operands look right and mean different things.
  const StyledFace local{face.roman, face.italic, &emphasis};

  int penF = pxToF26(x);
  size_t pos = 0;
  while (pos < utf8.size()) {
    const size_t next = nextStyleBoundary(emphasis, pos, utf8.size());
    const size_t stop = (next > pos && next <= utf8.size()) ? next : utf8.size();
    penF = drawRunF26(fb, local.at(pos), penF, baselineY, utf8.substr(pos, stop - pos), ink,
                      tracking, plane, extraPerGapF26);
    pos = stop;
  }
  return f26ToPx(penF) - x;
}

int drawText(Framebuffer& fb, const GlyphSource& font, int x, int baselineY, std::string_view utf8,
             Ink ink, Tracking tracking, Plane plane) {
  return drawRun(fb, font, x, baselineY, utf8, ink, tracking, plane, 0);
}

int drawTextJustified(Framebuffer& fb, const GlyphSource& font, int x, int baselineY,
                      std::string_view utf8, int extraPerGapF26, Ink ink, Tracking tracking,
                      Plane plane) {
  return drawRun(fb, font, x, baselineY, utf8, ink, tracking, plane, extraPerGapF26);
}

int stretchFor(const GlyphSource& font, std::string_view line, int availW, Tracking tracking) {
  int gaps = 0;
  for (const char c : line)
    if (c == ' ') ++gaps;
  if (gaps == 0) return 0;  // one long word: nothing to distribute across

  const int naturalW = font.measure(line, tracking);
  // Negative slack is a word wider than the column, which the wrap deliberately
  // let overhang. Pulling the gaps tighter to compensate would compress a line
  // that is already wrong, in a way that looks like a different bug.
  if (naturalW >= availW) return 0;

  // The ragged fallback, tested on how full the LINE is rather than on how far a
  // gap would stretch -- see kMinJustifyFillPercent for why that distinction is
  // the whole of it. Multiplied out rather than divided, so a narrow column needs
  // no rounding rule of its own.
  if (naturalW * 100 < availW * kMinJustifyFillPercent) return 0;
  return pxToF26(availW - naturalW) / gaps;
}

std::string elideToWidth(const GlyphSource& font, std::string_view utf8, int maxW, Tracking tracking) {
  if (font.measure(utf8, tracking) <= maxW) return std::string(utf8);
  const int ellipsisW = font.measure(kEllipsis, tracking);
  // Not even the mark fits. See the header: nothing, rather than something that
  // overhangs the box this whole function exists to respect.
  if (ellipsisW > maxW) return std::string();

  // The pen is accumulated exactly as Font::measure accumulates it -- 1/64 px,
  // kerning included, rounded once -- and after each codepoint we ask what the
  // run WOULD measure with the ellipsis joined on. That join has to be part of
  // the measurement rather than added afterwards: `measure(prefix) +
  // measure(kEllipsis)` misses the kern across the join, and the kern is where a
  // one-pixel overhang would come from.
  // advance(), not glyph(): an elide is a MEASUREMENT, and on a scalable face
  // reaching for glyph() here would rasterise every codepoint of a run in order
  // to decide how much of it to draw -- and rasterise the ones it then discards.
  // The whole of this function is design decision 3's second caller.
  constexpr char32_t kEllipsisCp = 0x2026;
  const std::optional<int> ea = font.advance(kEllipsisCp);
  const int ellipsisAdvanceF = pxToF26(ea ? *ea : font.notdefAdvance());

  int penF = 0;
  char32_t prev = 0;
  size_t fits = 0;  // byte length of the longest prefix that fits with the mark
  size_t i = 0;
  while (i < utf8.size()) {
    const char32_t cp = utf8Next(utf8, i);
    const std::optional<int> adv = font.advance(cp);
    if (!adv) {
      penF += pxToF26(font.notdefAdvance()) + tracking.f26();
      prev = 0;
    } else {
      if (prev) penF += pxToF26(font.kerning(prev, cp));
      penF += pxToF26(*adv) + tracking.f26();
      prev = cp;
    }
    int joinedF = penF;
    if (prev) joinedF += pxToF26(font.kerning(prev, kEllipsisCp));
    joinedF += ellipsisAdvanceF + tracking.f26();
    if (f26ToPx(joinedF) > maxW) break;
    fits = i;
  }
  // `i` is already on a codepoint boundary at every iteration -- utf8Next only
  // ever leaves it on one -- so this substr can never split a sequence.
  std::string out(utf8.substr(0, fits));
  out += kEllipsis;
  return out;
}

int drawTextElided(Framebuffer& fb, const GlyphSource& font, int x, int baselineY,
                   std::string_view utf8, int maxW, Ink ink, Tracking tracking, Plane plane) {
  if (font.measure(utf8, tracking) <= maxW)
    return drawText(fb, font, x, baselineY, utf8, ink, tracking, plane);
  const std::string cut = elideToWidth(font, utf8, maxW, tracking);
  return drawText(fb, font, x, baselineY, cut, ink, tracking, plane);
}

// descent() is negative, so `ascent - descent` is the run's full extent and
// `(ascent + descent) / 2` is the signed distance from the baseline up to the
// extent's midpoint.
//
// There is ONE implementation, and it is the fractional one. The whole-pixel
// entry point is a unit conversion in front of it, not a second rule: it used to
// compute `boxTop + (boxH - extent) / 2 + ascent`, which rounds the half-leading
// AND then lands on a whole baseline -- two roundings -- and so answered 1px
// higher than this one on every box whose slack (boxH - extent) is odd and
// positive. That is the "round once" invariant, and the two spellings disagreeing
// by a pixel was a trap: a screen picked whichever helper its neighbour used and
// the defect was too small to see in review.
//
// Exactly two boxes on the implemented screens have odd positive slack, and both
// set Value700 (33px extent): a menu row's VALUE in the 80px row content box
// (slack 47) and the action block's label in its 68px block (slack 35). Both
// moved down a pixel and both now land where Chrome puts them, measured off the
// rasterised boards -- Home's "12" on rows 606..623 against the board's 606..623,
// and SdMissing's RETRY 25px above and 25px below inside its slab where it used
// to be 24 and 26. Six goldens were re-blessed onto those numbers. Every other
// box is even-slack (a row's LABEL, the header band's label, the CONTINUE block)
// or negative-slack (the boards tighten the title, the numeral and every
// line-height-1 box below its own extent), where the two spellings always agreed.
int baselineInF26(const GlyphSource& font, int boxTopF26, int boxHF26) {
  const int extentF26 = pxToF26(font.ascent() - font.descent());
  // Arithmetic shift rather than / 2, so a box shorter than the run it holds
  // (the boards do tighten line boxes below their content) halves the same way
  // on either side of zero instead of truncating toward it.
  const int halfLeading = (boxHF26 - extentF26) >> 1;
  return f26ToPx(boxTopF26 + halfLeading + pxToF26(font.ascent()));
}

int baselineIn(const GlyphSource& font, int boxTop, int boxH) {
  return baselineInF26(font, pxToF26(boxTop), pxToF26(boxH));
}

// One division, at the end, halves up -- and a floor that behaves the same
// either side of zero, so an item taller than its box (the boards do tighten
// line boxes below their content) overhangs symmetrically instead of being
// pulled back toward the origin by integer truncation.
int centreIn(int boxStart, int boxSize, int itemSize) {
  const int slack = boxSize - itemSize;
  const int half = slack >= 0 ? (slack + 1) / 2 : -((-slack) / 2);
  return boxStart + half;
}

int iconTopIn(int boxTop, int boxH, int itemH) { return centreIn(boxTop, boxH, itemH); }


std::string upperLatin1(std::string_view s) {
  std::string out(s);
  for (size_t i = 0; i < out.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(out[i]);
    if (c >= 'a' && c <= 'z') {
      out[i] = static_cast<char>(c - 'a' + 'A');
      continue;
    }
    // THE LATIN-1 SUPPLEMENT, in UTF-8: U+00E0..U+00FE is `C3 A0`..`C3 BE`, and the
    // uppercase U+00C0..U+00DE is `C3 80`..`C3 9E` -- so the SECOND byte drops by
    // 0x20, exactly as an ASCII letter's only byte does. One subtraction, no table.
    if (c != 0xC3 || i + 1 >= out.size()) continue;
    const unsigned char lo = static_cast<unsigned char>(out[i + 1]);
    // THREE EXCLUSIONS, and each is a character whose uppercase is not one byte away:
    //
    //   0xB7 (U+00F7 division sign) is not a letter at all, and 0xB7-0x20 is 0x97 --
    //        U+00D7, the MULTIPLICATION sign. Shouting a divide into a times.
    //   0xBF (U+00FF y-diaeresis) uppercases to U+0178, which is outside Latin-1 and
    //        outside every font subset this project builds -- so it would render as a
    //        notdef box, which is worse than a lowercase letter.
    //   0x9F (U+00DF sharp s) is already outside the lowercase range below, and its
    //        uppercase is two letters (SS) or U+1E9E. Left alone by the bounds.
    //
    // The range therefore stops at 0xBE and skips 0xB7.
    if (lo >= 0xA0 && lo <= 0xBE && lo != 0xB7) {
      out[i + 1] = static_cast<char>(lo - 0x20);
      ++i;  // the pair is done; do not re-examine the byte just written
    }
  }
  return out;
}

}  // namespace reader
