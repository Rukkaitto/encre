#include "reader/scalablefont.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

// --- Configuring stb_truetype for this part -----------------------------------
//
// The ESP32-C3 is RISC-V with **no FPU**, so every float is soft-float and every
// `double` is soft-double at roughly twice the cost. stb_truetype's defaults
// pull `<math.h>` in five places and use its double-precision floor, sqrt, pow,
// cos, acos and fmod on float arguments -- each call widening to double, doing
// the work in double, and narrowing back. Every one of them is a macro the
// header lets us replace, so:
//
//   - ifloor/iceil become integer truncation with a correction, no libm at all.
//     These are on the GLYPH path (stbtt_GetGlyphBitmapBoxSubpixel and the
//     rasteriser's edge setup), so they are the two that matter.
//   - sqrt is on the glyph path too, though only for COMPOSITE glyphs: stb takes
//     the norm of the component's transform matrix unconditionally, so every
//     accented Latin letter -- which body text has plenty of -- reaches it. It
//     becomes sqrtf.
//   - pow, cos, acos and fmod are reached only from the SDF functions, which
//     nothing here calls; they become the float forms so that, if the linker's
//     --gc-sections ever fails to drop them, what survives is single precision
//     rather than a double-precision libm.
//
// STBTT_assert is the other one worth stating. stb asserts on accumulated
// rasteriser error (`STBTT_fabs(area) <= 1.01f`), and on the device an assert is
// an abort with no diagnostic -- a font edge case in a book would take the
// firmware down. So it is live on the desktop, where the test suite and the
// sanitiser build are what it is *for*, and a no-op on the device. The
// divergence is deliberate and in the safe direction.
#define STBTT_STATIC  // every stb symbol stays internal to this translation unit

namespace {
// Integer floor/ceil of a float without libm. The argument is evaluated once
// because these are functions, which matters: stb passes expressions such as
// `-y1 * scale_y + shift_y`.
inline int stbttIFloor(float x) {
  const int i = static_cast<int>(x);
  return (x < 0.0f && static_cast<float>(i) != x) ? i - 1 : i;
}
inline int stbttICeil(float x) {
  const int i = static_cast<int>(x);
  return (x > 0.0f && static_cast<float>(i) != x) ? i + 1 : i;
}
}  // namespace

#define STBTT_ifloor(x) stbttIFloor(static_cast<float>(x))
#define STBTT_iceil(x) stbttICeil(static_cast<float>(x))
#define STBTT_sqrt(x) std::sqrt(static_cast<float>(x))
#define STBTT_pow(x, y) std::pow(static_cast<float>(x), static_cast<float>(y))
#define STBTT_fmod(x, y) std::fmod(static_cast<float>(x), static_cast<float>(y))
#define STBTT_cos(x) std::cos(static_cast<float>(x))
#define STBTT_acos(x) std::acos(static_cast<float>(x))
#define STBTT_fabs(x) std::fabs(static_cast<float>(x))
#define STBTT_malloc(x, u) ((void)(u), std::malloc(x))
#define STBTT_free(x, u) ((void)(u), std::free(x))
#define STBTT_strlen(x) std::strlen(x)
#define STBTT_memcpy std::memcpy
#define STBTT_memset std::memset
#if defined(READER_DESKTOP)
#include <cassert>
#define STBTT_assert(x) assert(x)
#else
#define STBTT_assert(x) ((void)0)
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace reader {

namespace {

// 8-bit linear coverage -> 2-bit level, built once and shared by every face.
//
// Character for character the same expression tools/fontc.py's coverage_lut()
// evaluates -- `min(3, int((v / 255) ** (1 / gamma) * 3 + 0.5))` -- because the
// two pipelines producing the same level for the same coverage is the whole
// reason this table exists. See ScalableFont::kCoverageGamma.
const uint8_t* coverageLut() {
  static uint8_t lut[256];
  static const bool built = [] {
    for (int v = 0; v < 256; ++v) {
      const float t = std::pow(static_cast<float>(v) / 255.0f,
                               1.0f / ScalableFont::kCoverageGamma);
      const int level = static_cast<int>(t * 3.0f + 0.5f);
      lut[v] = static_cast<uint8_t>(level < 3 ? level : 3);
    }
    return true;
  }();
  (void)built;
  return lut;
}

// Round a scaled font-unit quantity to whole pixels, halves away from zero.
// Both the advance and the kern go through it, so a run's pen accumulates one
// rule rather than two.
int roundPx(float v) {
  return v >= 0.0f ? static_cast<int>(v + 0.5f) : -static_cast<int>(-v + 0.5f);
}

uint16_t rdU16(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
uint32_t rdU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// usWeightClass out of OS/2, so weight() means something on a scalable face too.
// stb_truetype's own table lookup is static to its implementation and it does
// not expose OS/2, so this walks the directory itself -- fifteen lines against a
// face that reports "undeclared" for the one question FontSet::load asks.
// Bounds-checked against `len` at every step: this is a flash blob we trust, but
// a truncated one must not read past it.
int readWeightClass(const uint8_t* data, size_t len, int fontstart) {
  const size_t base = static_cast<size_t>(fontstart);
  if (base + 12 > len) return 0;
  const uint16_t numTables = rdU16(data + base + 4);
  for (uint16_t i = 0; i < numTables; ++i) {
    const size_t rec = base + 12 + static_cast<size_t>(i) * 16;
    if (rec + 16 > len) return 0;
    if (std::memcmp(data + rec, "OS/2", 4) != 0) continue;
    const uint32_t off = rdU32(data + rec + 8);
    if (static_cast<size_t>(off) + 6 > len) return 0;
    return rdU16(data + off + 4);  // usWeightClass
  }
  return 0;
}

// Is this blob structurally an sfnt whose table directory lies inside `len`?
//
// **stb_truetype does no range checking**, and says so in capitals at the top of
// its own header: "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT
// FILES ... an attacker can use it to read arbitrary memory". Handing it 4 KB of
// 0x5A takes the process down with a bus error, because the table count it reads
// out of the noise is 23,130 and it walks every one of them.
//
// Today's blob is a `const` array in flash and is as trusted as the firmware
// itself, so this is not defence against an attacker. It is defence against a
// TRUNCATED or WRONG one -- a regenerated asset that got cut, a symbol wired to
// the wrong array -- where `-fno-exceptions` turns "crash" into a boot loop with
// nothing on the serial line, and where a `false` from init() is a log line
// instead.
//
// It is NOT enough to make an untrusted font safe, and nothing here should be
// read as saying it is: it checks the directory, not the glyph offsets stb
// dereferences later. **An EPUB may embed a font** (@font-face), so if 3B or 3C
// ever rasterises a face that came off the card, this function is not the answer
// -- a real validator, or a face allow-list, is.
bool looksLikeSfnt(const uint8_t* d, size_t len) {
  if (d == nullptr || len < 12) return false;
  const uint32_t tag = rdU32(d);
  // 0x00010000 TrueType outlines, 'true' the Apple spelling, 'OTTO' CFF, 'ttcf'
  // a collection (which stbtt_GetFontOffsetForIndex resolves for us).
  if (tag != 0x00010000u && tag != 0x74727565u && tag != 0x4F54544Fu &&
      tag != 0x74746366u)
    return false;
  if (tag == 0x74746366u) return len >= 16;  // the collection header; stb walks it
  const uint16_t numTables = rdU16(d + 4);
  if (numTables == 0) return false;
  const size_t dirEnd = 12 + static_cast<size_t>(numTables) * 16;
  if (dirEnd > len) return false;
  for (uint16_t i = 0; i < numTables; ++i) {
    const uint8_t* rec = d + 12 + static_cast<size_t>(i) * 16;
    const size_t off = rdU32(rec + 8);
    const size_t sz = rdU32(rec + 12);
    if (off > len || sz > len || off + sz > len) return false;
  }
  return true;
}

}  // namespace

// --- The cache ---------------------------------------------------------------
//
// A RING arena plus a ring of entry records, and the reason it is a ring rather
// than an LRU over a free list is fragmentation. Glyph bitmaps are variable
// sized; a malloc-per-glyph cache with LRU eviction fragments a 240 KB heap over
// a reading session, and `-fno-exceptions` means the allocation that finally
// fails does so as an abort() with nothing on the serial line. A single arena
// written strictly forwards cannot fragment: every eviction is "the bytes I am
// about to overwrite", which is bounded work and needs no compaction.
//
// The eviction order is therefore FIFO, not LRU. For the access pattern a page
// render actually has -- a repeated sweep over the ~70 distinct characters of a
// paragraph -- the two behave alike, because nothing is re-used at a distance
// greater than the alphabet. It is worth knowing that they are not the same
// thing if 3C ever measures a pattern where they diverge.
// A DIRECT-MAPPED CACHE FOR LATIN-1, which is the lever the roadmap recorded:
// "each char pays a cmap binary search plus two more for the kern pair lookup, and a
// 256-entry direct-mapped advance/gid cache for Latin-1 would be a few hundred bytes
// against most of that".
//
// It is the wrap that made it worth taking. wrapProseLead grows a line greedily and
// MEASURES EACH CANDIDATE, so every glyph of a chapter is measured several times
// over, and measure() calls advance() and kerning() per character -- three cmap
// binary searches each. Indexing a 40-page chapter therefore walked the cmap tens of
// thousands of times for answers that never change.
//
// 1 KB, resolved lazily, cleared by init() because the advance is in PIXELS and so
// depends on the size. `gid == kUnresolved` means "not asked yet" and `gid == 0`
// means "this face has no glyph", which is a real answer worth caching too.
struct LatinCache {
  static constexpr uint16_t kUnresolved = 0xFFFF;
  uint16_t gid[256];
  int16_t advancePx[256];

  void clear() {
    for (size_t i = 0; i < 256; ++i) gid[i] = kUnresolved;
  }
};

struct ScalableFont::Impl {
  LatinCache latin{};

  stbtt_fontinfo info{};
  bool fontOk = false;
  float scale = 0.0f;  // font units -> pixels, i.e. sizePx / unitsPerEm

  // The arena, allocated ONCE at construction and never resized. Null if the
  // budget was zero or the allocation failed, in which case every glyph takes
  // the bypass path below: slower, still correct.
  std::unique_ptr<uint8_t[]> arena;
  size_t arenaCap = 0;
  size_t head = 0;  // next write offset; wraps to 0 when a glyph will not fit

  struct Entry {
    char32_t cp = 0;
    uint32_t offset = 0;
    uint32_t bytes = 0;
    int16_t advance = 0, w = 0, h = 0, xOff = 0, yOff = 0, stride = 0;
    bool live = false;
  };
  std::unique_ptr<Entry[]> entries;
  size_t entryCap = 0;
  size_t entryHead = 0;

  // For a glyph that does not fit the arena at all. Grown on demand and reused;
  // its contents are valid exactly as long as a cached bitmap's are, which is
  // until the next call into this GlyphSource (see Glyph).
  std::unique_ptr<uint8_t[]> bypass;
  size_t bypassCap = 0;

  mutable CacheStats stats{};

  // 8-bit rasteriser output, packed MSB-first two bits per pixel into `dst`,
  // four pixels per byte -- the identical layout fontc.py writes and
  // GlyphSource::coverage reads, which is what lets one coverage() serve both
  // faces and one Bayer dither serve both.
  // `srcStride` is the rasteriser's row pitch, `w`/`h` the extent to pack --
  // which is clamped to what `dst` was sized for and so may be smaller.
  static void pack2bpp(const uint8_t* src, int srcStride, int w, int h, uint8_t* dst,
                       int stride) {
    // `dst` is zeroed by the caller for its FULL byte length, not here for h
    // rows: h may be clamped below the height dst was sized for, and rows this
    // loop never reaches would otherwise render as uninitialised garbage --
    // nondeterministically, which is the worst way for a golden to fail.
    const uint8_t* lut = coverageLut();
    for (int row = 0; row < h; ++row) {
      const uint8_t* s = src + static_cast<size_t>(row) * static_cast<size_t>(srcStride);
      uint8_t* d = dst + static_cast<size_t>(row) * static_cast<size_t>(stride);
      for (int col = 0; col < w; ++col) {
        const int shift = 6 - 2 * (col % 4);
        d[col / 4] = static_cast<uint8_t>(d[col / 4] | (lut[s[col]] << shift));
      }
    }
  }

  void flush() {
    head = 0;
    entryHead = 0;
    for (size_t i = 0; i < entryCap; ++i) entries[i].live = false;
  }

  const Entry* find(char32_t cp) const {
    for (size_t i = 0; i < entryCap; ++i)
      if (entries[i].live && entries[i].cp == cp) return &entries[i];
    return nullptr;
  }

  // Reserve `n` arena bytes, evicting whatever they will overwrite. Returns the
  // offset, or npos when `n` cannot fit the whole arena.
  static constexpr size_t kNoRoom = static_cast<size_t>(-1);
  size_t reserve(size_t n) {
    if (arena == nullptr || n > arenaCap) return kNoRoom;
    if (head + n > arenaCap) {
      head = 0;
      ++stats.wraps;
    }
    const size_t begin = head, end = head + n;
    for (size_t i = 0; i < entryCap; ++i) {
      Entry& e = entries[i];
      // A zero-byte entry (a space, which has metrics and no bitmap) occupies no
      // arena bytes and so is never evicted by a write; only entry-table
      // pressure removes it. `e.bytes > 0` is what keeps the half-open
      // intersection test from matching it.
      if (!e.live || e.bytes == 0) continue;
      if (e.offset < end && static_cast<size_t>(e.offset) + e.bytes > begin) {
        e.live = false;
        ++stats.evictions;
      }
    }
    head = end;
    return begin;
  }

  Entry& claimEntry() {
    Entry& e = entries[entryHead];
    if (e.live) {
      e.live = false;
      ++stats.evictions;
    }
    entryHead = (entryHead + 1) % entryCap;
    return e;
  }
};

ScalableFont::ScalableFont(size_t cacheBudgetBytes) : impl_(new (std::nothrow) Impl) {
  if (!impl_) return;
  // The arena is the caller's budget; the entry table is derived from it and is
  // reported separately (CacheStats::overheadBytes) rather than hidden, because
  // a budget that quietly cost more than it said would defeat the point of
  // having one. 48 bytes per entry is a small glyph at a reading size, so this
  // sizes the table to roughly "as many entries as the arena could hold", and
  // the cap keeps the table itself from becoming the expensive part.
  constexpr size_t kBytesPerEntry = 48;
  constexpr size_t kMinEntries = 8, kMaxEntries = 128;
  size_t n = cacheBudgetBytes / kBytesPerEntry;
  if (n < kMinEntries) n = kMinEntries;
  if (n > kMaxEntries) n = kMaxEntries;
  impl_->entries.reset(new (std::nothrow) Impl::Entry[n]);
  if (impl_->entries) impl_->entryCap = n;
  if (cacheBudgetBytes > 0) {
    impl_->arena.reset(new (std::nothrow) uint8_t[cacheBudgetBytes]);
    if (impl_->arena) impl_->arenaCap = cacheBudgetBytes;
  }
  impl_->stats.capacityBytes = impl_->arenaCap;
  impl_->stats.capacityEntries = static_cast<int>(impl_->entryCap);
  impl_->stats.overheadBytes = impl_->entryCap * sizeof(Impl::Entry);
}

ScalableFont::~ScalableFont() = default;

bool ScalableFont::init(const uint8_t* ttf, size_t len, int sizePx) {
  if (!impl_ || impl_->entryCap == 0) return false;
  impl_->fontOk = false;
  impl_->flush();
  // The cached advances are in pixels, so they belong to the size being replaced.
  impl_->latin.clear();
  ascent_ = descent_ = lineGap_ = 0;
  ppem_ = weight_ = 0;
  bpp_ = 2;
  if (ttf == nullptr || len == 0 || sizePx <= 0) return false;
  // BEFORE stb sees it: see looksLikeSfnt. stb range-checks nothing, so a
  // truncated asset is a bus error rather than a refusal unless something asks
  // this question first.
  if (!looksLikeSfnt(ttf, len)) return false;

  // stb_truetype keeps a pointer, not a copy: the blob is the flash array and
  // must outlive this object, exactly as an .rfnt blob must outlive a Font.
  const int offset = stbtt_GetFontOffsetForIndex(ttf, 0);
  if (offset < 0) return false;
  if (!stbtt_InitFont(&impl_->info, ttf, offset)) return false;

  // ScaleForMappingEmToPixels, not ScaleForPixelHeight. `sizePx` is an EM size,
  // because that is what a stylesheet's `font-size: 16px` means and what the
  // fixtures' 16/32/41/51/64 are; ScaleForPixelHeight instead makes
  // ascent-descent come to sizePx, which is a different and larger number, so
  // using it would draw every heading over the box the layout reserved for it.
  impl_->scale = stbtt_ScaleForMappingEmToPixels(&impl_->info, static_cast<float>(sizePx));

  int a = 0, d = 0, g = 0;
  stbtt_GetFontVMetrics(&impl_->info, &a, &d, &g);
  // Away from the baseline on both sides, so a line box can never clip the
  // extent it was sized for; the gap rounds to nearest, being neither.
  ascent_ = static_cast<int>(std::ceil(static_cast<float>(a) * impl_->scale));
  descent_ = static_cast<int>(std::floor(static_cast<float>(d) * impl_->scale));
  lineGap_ = roundPx(static_cast<float>(g) * impl_->scale);
  ppem_ = sizePx;
  weight_ = readWeightClass(ttf, len, impl_->info.fontstart);
  impl_->fontOk = true;
  return true;
}

bool ScalableFont::ready() const { return impl_ && impl_->fontOk; }

int ScalableFont::gidFor(char32_t cp) const {
  if (cp >= 256) return stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
  LatinCache& lc = impl_->latin;
  const size_t at = static_cast<size_t>(cp);
  if (lc.gid[at] == LatinCache::kUnresolved) {
    const int g = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
    lc.gid[at] = static_cast<uint16_t>(g);
    int aw = 0, lsb = 0;
    if (g != 0) stbtt_GetGlyphHMetrics(&impl_->info, g, &aw, &lsb);
    lc.advancePx[at] =
        static_cast<int16_t>(g == 0 ? 0 : roundPx(static_cast<float>(aw) * impl_->scale));
  }
  return static_cast<int>(lc.gid[at]);
}

std::optional<int> ScalableFont::advance(char32_t cp) const {
  if (!ready()) return std::nullopt;
  // Glyph index 0 is .notdef, and in most faces it has an outline -- a hollow
  // box. Treating it as a hit would draw the FONT's box at the FONT's advance
  // while measure() and drawText agreed about neither with Font's notdef path.
  // So an unmapped codepoint is nullopt here, and GlyphSource::notdefAdvance is
  // what both then use: one missing-glyph rule for both faces.
  // Latin-1 comes out of the table; anything above it pays the cmap search. Body
  // text is overwhelmingly below 256, and what is not (curly quotes, an em dash) is
  // rare enough that a bigger table would be memory for nothing.
  if (cp < 256) {
    if (gidFor(cp) == 0) return std::nullopt;
    return static_cast<int>(impl_->latin.advancePx[static_cast<size_t>(cp)]);
  }

  const int gid = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
  if (gid == 0) return std::nullopt;
  int aw = 0, lsb = 0;
  // `hmtx`, not the outline: this is the read that makes design decision 3 free
  // to honour rather than a sacrifice.
  stbtt_GetGlyphHMetrics(&impl_->info, gid, &aw, &lsb);
  return roundPx(static_cast<float>(aw) * impl_->scale);
}

int ScalableFont::kerning(char32_t left, char32_t right) const {
  if (!ready()) return 0;
  // TWO cmap searches per PAIR, which measure() does for every character after the
  // first -- so this was two thirds of the cmap work in a wrap. A cached gid is the
  // same number the advance lookup already resolved.
  const int l = gidFor(left);
  const int r = gidFor(right);
  if (l == 0 || r == 0) return 0;
  const int k = stbtt_GetGlyphKernAdvance(&impl_->info, l, r);
  if (k == 0) return 0;
  return roundPx(static_cast<float>(k) * impl_->scale);
}

std::optional<Glyph> ScalableFont::glyph(char32_t cp) const {
  if (!ready()) return std::nullopt;
  Impl& m = *impl_;

  if (const Impl::Entry* e = m.find(cp)) {
    ++m.stats.hits;
    Glyph gl;
    gl.advance = e->advance;
    gl.bitmapW = e->w;
    gl.bitmapH = e->h;
    gl.xOff = e->xOff;
    gl.yOff = e->yOff;
    gl.stride = e->stride;
    // Borrowed from the arena. Valid until the next call into this object, which
    // is the contract Glyph states and the reason glyph() returns by value.
    gl.bitmap = m.arena ? m.arena.get() + e->offset : m.bypass.get();
    return gl;
  }
  ++m.stats.misses;

  const int gid = stbtt_FindGlyphIndex(&m.info, static_cast<int>(cp));
  if (gid == 0) return std::nullopt;
  int aw = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(&m.info, gid, &aw, &lsb);
  const int advance = roundPx(static_cast<float>(aw) * m.scale);

  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  stbtt_GetGlyphBitmapBox(&m.info, gid, m.scale, m.scale, &x0, &y0, &x1, &y1);
  const int w = x1 - x0, h = y1 - y0;

  Glyph gl;
  gl.advance = static_cast<int16_t>(advance);
  gl.bitmapW = static_cast<int16_t>(w > 0 ? w : 0);
  gl.bitmapH = static_cast<int16_t>(h > 0 ? h : 0);
  gl.xOff = static_cast<int16_t>(x0);
  // stb's box is y-down with the baseline at 0, so its top edge y0 is negative
  // above the baseline; Glyph::yOff is the baseline-to-top distance measured UP.
  gl.yOff = static_cast<int16_t>(-y0);
  gl.stride = static_cast<int16_t>((gl.bitmapW * 2 + 7) / 8);

  // A space, or any glyph with no ink: metrics and no bitmap. It is CACHED, with
  // zero bytes -- a space is the commonest character in a paragraph and there is
  // no reason to re-derive its box every time. The bitmap pointer is never
  // dereferenced (both of drawText's loops are empty at zero extent) but it is
  // still a valid pointer rather than null.
  const size_t bytes = static_cast<size_t>(gl.stride) * static_cast<size_t>(gl.bitmapH);
  if (bytes == 0) {
    static const uint8_t kNoInk = 0;  // a real address for a bitmap nothing reads
    gl.bitmap = m.arena ? m.arena.get() : &kNoInk;
    Impl::Entry& e = m.claimEntry();
    e.cp = cp;
    e.offset = 0;
    e.bytes = 0;
    e.advance = gl.advance;
    e.w = gl.bitmapW;
    e.h = gl.bitmapH;
    e.xOff = gl.xOff;
    e.yOff = gl.yOff;
    e.stride = gl.stride;
    e.live = true;
    return gl;
  }

  // stb rasterises 8 bits of coverage per pixel and takes a transient
  // allocation to do it -- the edge list and this bitmap. That allocation is
  // NOT what the budget bounds: the budget bounds what the cache RETAINS. stb
  // is null-safe on every failure path here (it checks each malloc and returns
  // a null bitmap), so an out-of-memory raster arrives as nullopt rather than
  // as an abort, which is the whole reason `-fno-exceptions` forces this
  // discipline.
  int rw = 0, rh = 0, rx = 0, ry = 0;
  uint8_t* raw = stbtt_GetGlyphBitmap(&m.info, m.scale, m.scale, gid, &rw, &rh, &rx, &ry);
  if (raw == nullptr) return std::nullopt;

  uint8_t* dst = nullptr;
  size_t offset = 0;
  const size_t at = m.reserve(bytes);
  if (at != Impl::kNoRoom) {
    offset = at;
    dst = m.arena.get() + at;
  } else {
    // Bigger than the whole arena (a 64px glyph against a 1 KB budget, or a
    // budget of zero). Rasterise into scratch and do not cache it: the caller
    // gets the right pixels, just no reuse. Refusing would draw a notdef box,
    // which is not "slower", it is wrong.
    ++m.stats.bypasses;
    if (m.bypassCap < bytes) {
      m.bypass.reset(new (std::nothrow) uint8_t[bytes]);
      m.bypassCap = m.bypass ? bytes : 0;
    }
    if (!m.bypass) {
      stbtt_FreeBitmap(raw, nullptr);
      return std::nullopt;
    }
    dst = m.bypass.get();
  }

  // rw/rh come from the same box computation as w/h, so they agree -- but `dst`
  // was sized from w/h, so a disagreement would be a write past the end of the
  // arena rather than a wrong pixel. Clamped rather than asserted: this is the
  // one place in the class where being wrong corrupts memory outside the buffer,
  // and a vendored rasteriser is not something to take on trust at that price.
  const int packW = rw < w ? rw : w;
  const int packH = rh < h ? rh : h;
  std::memset(dst, 0, bytes);
  Impl::pack2bpp(raw, rw, packW, packH, dst, gl.stride);
  stbtt_FreeBitmap(raw, nullptr);
  ++m.stats.rasterisations;
  gl.bitmap = dst;

  if (at != Impl::kNoRoom) {
    Impl::Entry& e = m.claimEntry();
    e.cp = cp;
    e.offset = static_cast<uint32_t>(offset);
    e.bytes = static_cast<uint32_t>(bytes);
    e.advance = gl.advance;
    e.w = gl.bitmapW;
    e.h = gl.bitmapH;
    e.xOff = gl.xOff;
    e.yOff = gl.yOff;
    e.stride = gl.stride;
    e.live = true;
  }
  return gl;
}

ScalableFont::CacheStats ScalableFont::cacheStats() const {
  if (!impl_) return {};
  CacheStats s = impl_->stats;
  s.entries = 0;
  s.usedBytes = 0;
  for (size_t i = 0; i < impl_->entryCap; ++i)
    if (impl_->entries[i].live) {
      ++s.entries;
      s.usedBytes += impl_->entries[i].bytes;
    }
  return s;
}

void ScalableFont::resetCacheStats() const {
  if (!impl_) return;
  const size_t cap = impl_->stats.capacityBytes;
  const int caps = impl_->stats.capacityEntries;
  const size_t over = impl_->stats.overheadBytes;
  impl_->stats = CacheStats{};
  impl_->stats.capacityBytes = cap;
  impl_->stats.capacityEntries = caps;
  impl_->stats.overheadBytes = over;
}

}  // namespace reader
