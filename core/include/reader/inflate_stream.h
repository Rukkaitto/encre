#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace reader {

// Where compressed bytes come from. An interface rather than a FileHandle, so the
// decoder can be tested against a buffer, a fake, a real card, or a source that
// deliberately hands back one byte at a time -- which is the case that finds
// resumption bugs and which no real card would produce.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  // Up to `bytes` into `dst`. Returns the count; 0 means the input has ended.
  virtual size_t read(void* dst, size_t bytes) = 0;
};

// RAW DEFLATE (RFC 1951), DECODED IN BOUNDED CHUNKS.
//
// --- Why this exists at all ---------------------------------------------------
//
// stb_image's zlib decoder is one-shot: whole input buffer in, whole output buffer
// out. That is why 3B's reader held both, and why a real book could not be opened
// -- `Le Fléau`'s longest chapter is 315,852 bytes of XHTML from 64,677 compressed,
// so the inflate alone wanted 380 KB against a heap with ~142 KB free. There is no
// way to get chunks out of stb, and every route to bounded memory needs them --
// including inflating to a temp file, which needs incremental output to write
// incrementally.
//
// It also removes a 6,608-BYTE SINGLE STACK FRAME. stb inlines its three Huffman
// tables together, each carrying a 512-entry fast-lookup array; that frame is 41%
// of the loop task's whole stack and it panicked the device on the first book ever
// opened. The tables here are counts-and-symbols (RFC 1951's canonical form, as in
// zlib's own `puff.c`) at ~600 bytes each, and they live in this OBJECT, on the
// heap, not in a frame.
//
// --- The 32 KB window is the format, not a choice -----------------------------
//
// A DEFLATE match reaches up to 32,768 bytes back, so any decoder that does not
// hold its whole output must retain the last 32 KB of it. That sets the floor for
// this whole design: ~36 KB resident for a chapter of any length, against 546 KB
// for the largest chapter under 3B.
//
// --- The contract -------------------------------------------------------------
//
// `next()` returns a view INTO THE WINDOW, valid only until the following call --
// the same borrowed-until-next-call contract `Glyph::bitmap` and `Xml::text()`
// carry, and for the same reason: the alternative is a copy per chunk.
//
// Chunks are contiguous and never straddle the ring's wrap, so a caller can always
// treat one as a plain span. They are therefore of varying length, and a caller
// must not infer anything from a short one -- shortness means "the ring wrapped
// here", not "the input is ending".
//
// --- What it refuses ----------------------------------------------------------
//
// Every one of these is reachable from a file on somebody's card, and none of them
// may abort: a reserved block type, an over-subscribed or incomplete Huffman code,
// a length or distance symbol outside its alphabet, a distance reaching further
// back than the output produced so far, a stored block whose NLEN does not
// complement its LEN, and input that ends mid-stream. `error()` says which.
class Inflater {
 public:
  // DEFLATE's maximum match distance. Not tunable: a smaller window silently
  // corrupts any stream that uses a longer match, which is most of them.
  static constexpr size_t kWindowBytes = 32u * 1024u;

  // The compressed read-ahead. Four SdFat sectors: the source is a card and a
  // one-sector buffer would make every refill a real read.
  static constexpr size_t kInputBytes = 2048;

  // ONE ALLOCATION HOLDS EVERYTHING BIG, and this object holds a pointer to it.
  //
  // Measured, not assumed: with the window on the heap but the input buffer and
  // the Huffman tables as members, an Inflater declared as a local cost 6,336
  // bytes of stack -- barely better than the 7,348 of the stb chain it replaces,
  // and for the same reason (arrays in a frame). Every array now lives in the
  // block below, so a caller may put an Inflater wherever it likes.
  // Checked against sizeof(Scratch) by a static_assert in the .cpp, so this
  // number cannot quietly become smaller than the allocation it describes.
  static constexpr size_t kHeapBytes = kWindowBytes + kInputBytes + 3 * 640 + 320;

  // The most one `next()` will produce. Half the window, so a chunk can never
  // overwrite its own start, and the returned view stays intact while the caller
  // reads it.
  static constexpr size_t kChunkBytes = kWindowBytes / 2;

  Inflater() = default;
  ~Inflater();
  Inflater(const Inflater&) = delete;
  Inflater& operator=(const Inflater&) = delete;

  // Allocates the window -- nothrow, so a heap that cannot serve 32 KB is a false
  // and not an abort(). `src` must outlive this object.
  bool begin(ByteSource& src);
  bool ready() const { return s_ != nullptr; }

  // The next run of decompressed bytes, or an empty view when there are none: ask
  // done() and error() to tell the two apart. Empty with done() false and no
  // error means the input ended cleanly at a block boundary but the stream never
  // declared itself final, which is a truncated file.
  std::string_view next();

  bool done() const { return state_ == State::Done; }
  const char* error() const { return error_; }

  // Bytes produced so far. Also what bounds a match's distance: a stream cannot
  // reference further back than it has written.
  uint32_t produced() const { return totalOut_; }

 private:
  enum class State : uint8_t { BlockHeader, Stored, Compressed, Done, Failed };

  // RFC 1951's canonical Huffman table: how many codes of each length, and the
  // symbols in canonical order. ~600 bytes, against stb's ~2 KB per table -- and
  // the whole reason a frame here is a few hundred bytes rather than 6,608.
  struct Huff {
    uint16_t count[16];
    uint16_t symbol[288];
  };

  // Everything that is not a scalar, in one heap block. `codeLengths` is the
  // scratch the dynamic-block header needs -- transient, but held here rather than
  // in a frame for the reason above.
  struct Scratch {
    uint8_t window[kWindowBytes];
    uint8_t in[kInputBytes];
    Huff lit;
    Huff dist;
    Huff codeLen;                 // the header's own code-length alphabet
    uint8_t codeLengths[286 + 30];
  };

  // The documented figure cannot drift below the real one. kHeapBytes is what this
  // header tells a caller the decoder costs, and a field added above without
  // updating it would make that a lie -- here rather than in the .cpp so no access
  // has to be widened for it.
  static_assert(sizeof(Scratch) <= kHeapBytes,
                "Scratch outgrew the kHeapBytes figure this header documents");

  bool fail(const char* why);
  bool refill();                 // more compressed bytes from the source
  bool needBits(int n);          // ensure `n` bits are in the hold
  uint32_t takeBits(int n);      // consume them, LSB first
  int decodeSymbol(const Huff& h);   // -1 on a malformed code
  bool buildHuff(Huff& h, const uint8_t* lengths, int n);
  bool readBlockHeader();
  bool readDynamicTables();
  void put(uint8_t byte);

  ByteSource* src_ = nullptr;
  Scratch* s_ = nullptr;        // the one allocation
  size_t wpos_ = 0;             // where the next byte lands in the ring
  uint32_t totalOut_ = 0;

  size_t inLen_ = 0, inAt_ = 0;
  bool inputEnded_ = false;

  uint32_t hold_ = 0;           // bit accumulator, LSB first
  int bitCount_ = 0;

  State state_ = State::BlockHeader;
  bool lastBlock_ = false;
  uint32_t storedLeft_ = 0;     // bytes remaining in a stored block

  // A match that did not fit in the chunk being returned, resumed on the next
  // call. This is what makes the decoder re-entrant without the caller ever
  // seeing a partial symbol.
  uint32_t matchLeft_ = 0;
  uint32_t matchDist_ = 0;

  const char* error_ = "";
};


// An Inflater's output AS a ByteSource, which is what puts the decoder and the XML
// tokenizer together: one produces bounded chunks, the other wants bytes and does
// its own small-buffer bookkeeping.
//
// The chunk view is only borrowed until the next `next()`, and this never calls
// `next()` while bytes of the current chunk are unread -- which is the whole of the
// bookkeeping. It copies, and the copy is the price of the two layers not having to
// know each other's buffer strategy: one pass over the stream, no allocation.
class InflateSource : public ByteSource {
 public:
  explicit InflateSource(Inflater& inf) : inf_(&inf) {}
  size_t read(void* dst, size_t bytes) override;

  // Why the stream stopped, for a caller that got a short read and needs to tell
  // "the chapter ended" from "the chapter is corrupt".
  bool done() const { return inf_->done(); }
  const char* error() const { return inf_->error(); }

 private:
  Inflater* inf_;
  std::string_view chunk_;
  size_t at_ = 0;
};

}  // namespace reader
