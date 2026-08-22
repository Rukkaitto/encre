#include "reader/inflate_stream.h"

#include <cstring>
#include <new>

namespace reader {
namespace {

// RFC 1951 section 3.2.5: the length codes 257..285 and their extra bits.
constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19, 23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

// The distance codes 0..29.
constexpr uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                                    17,   25,   33,   49,   65,   97,    129,   193,
                                    257,  385,  513,  769,  1025, 1537,  2049,  3073,
                                    4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13};

// The order the dynamic-block header states its code-length code lengths in
// (RFC 1951 section 3.2.7). Not sorted, and not derivable -- it puts the most
// commonly used lengths first so the header itself compresses.
constexpr uint8_t kClOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                  11, 4,  12, 3, 13, 2, 14, 1, 15};

}  // namespace

Inflater::~Inflater() { delete s_; }

bool Inflater::fail(const char* why) {
  error_ = why;
  state_ = State::Failed;
  return false;
}

bool Inflater::begin(ByteSource& src) {
  // REUSED, not reallocated. begin() is called again on every backward page turn
  // -- the stream cannot be seeked, so reaching an earlier page means decoding from
  // the chapter's start again -- and churning 32 KB each time is how a heap with
  // ~142 KB free gets fragmented into one that cannot serve the next chapter.
  if (s_ == nullptr) {
    s_ = new (std::nothrow) Scratch;
    if (s_ == nullptr) {
      error_ = "not enough memory for the inflate window";
      return false;
    }
  }
  src_ = &src;
  wpos_ = 0;
  totalOut_ = 0;
  inLen_ = inAt_ = 0;
  inputEnded_ = false;
  hold_ = 0;
  bitCount_ = 0;
  state_ = State::BlockHeader;
  lastBlock_ = false;
  storedLeft_ = 0;
  matchLeft_ = matchDist_ = 0;
  error_ = "";
  return true;
}

bool Inflater::refill() {
  if (inAt_ < inLen_) return true;
  if (inputEnded_) return false;
  inLen_ = src_->read(s_->in, kInputBytes);
  inAt_ = 0;
  if (inLen_ == 0) {
    inputEnded_ = true;
    return false;
  }
  return true;
}

bool Inflater::needBits(int n) {
  while (bitCount_ < n) {
    if (!refill()) return false;
    hold_ |= static_cast<uint32_t>(s_->in[inAt_++]) << bitCount_;
    bitCount_ += 8;
  }
  return true;
}

uint32_t Inflater::takeBits(int n) {
  const uint32_t v = hold_ & ((1u << n) - 1u);
  hold_ >>= n;
  bitCount_ -= n;
  return v;
}

// Canonical decode, bit by bit. `code` accumulates the bits read so far, `first`
// is the first code of the current length and `index` the first symbol of it --
// zlib's `puff.c` algorithm, chosen because its whole state is four ints. A
// 512-entry fast table would be faster and is what put 6,608 bytes on stb's stack.
int Inflater::decodeSymbol(const Huff& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; ++len) {
    if (!needBits(1)) return -1;  // input ended mid-code
    code |= static_cast<int>(takeBits(1));
    const int count = h.count[len];
    if (code - first < count) return h.symbol[index + (code - first)];
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  return -1;  // a code longer than 15 bits cannot exist in DEFLATE
}

bool Inflater::buildHuff(Huff& h, const uint8_t* lengths, int n) {
  std::memset(h.count, 0, sizeof(h.count));
  for (int i = 0; i < n; ++i) {
    if (lengths[i] > 15) return fail("a Huffman code length above 15");
    ++h.count[lengths[i]];
  }
  // Length 0 means "this symbol is not used"; it is not a code.
  h.count[0] = 0;

  // OVER-SUBSCRIPTION CHECK, and it is not optional: a table built from a
  // malformed set decodes garbage indefinitely rather than failing, which on
  // untrusted input is the difference between a refused book and a hang.
  int left = 1;
  for (int len = 1; len <= 15; ++len) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return fail("an over-subscribed Huffman code");
  }

  int offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
  for (int i = 0; i < n; ++i)
    if (lengths[i] != 0) h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);

  // `left > 0` is an INCOMPLETE code, which is legal in exactly one case: a
  // distance table with a single used symbol, in a block that turns out to
  // contain no matches at all. Refusing it outright rejects real files.
  return true;
}

bool Inflater::readBlockHeader() {
  if (!needBits(3)) {
    // A clean end at a block boundary without a final block ever being declared.
    // The file is truncated, which is a refusal rather than a silent short read.
    return fail("the compressed stream ended before its final block");
  }
  lastBlock_ = takeBits(1) != 0;
  const uint32_t type = takeBits(2);
  switch (type) {
    case 0: {
      // Stored: the rest of the current byte is discarded, then LEN and its
      // complement.
      hold_ = 0;
      bitCount_ = 0;
      uint8_t hdr[4];
      for (int i = 0; i < 4; ++i) {
        if (!refill()) return fail("a stored block's header is truncated");
        hdr[i] = s_->in[inAt_++];
      }
      const uint32_t len = static_cast<uint32_t>(hdr[0]) | (static_cast<uint32_t>(hdr[1]) << 8);
      const uint32_t nlen = static_cast<uint32_t>(hdr[2]) | (static_cast<uint32_t>(hdr[3]) << 8);
      if ((len ^ 0xFFFFu) != nlen) return fail("a stored block's length is not complemented");
      storedLeft_ = len;
      state_ = State::Stored;
      return true;
    }
    case 1: {
      // The fixed tables of RFC 1951 section 3.2.6, built rather than tabulated:
      // the lengths are the specification's own four ranges, and building them
      // exercises the same buildHuff a dynamic block uses.
      uint8_t lengths[288];
      for (int i = 0; i < 144; ++i) lengths[i] = 8;
      for (int i = 144; i < 256; ++i) lengths[i] = 9;
      for (int i = 256; i < 280; ++i) lengths[i] = 7;
      for (int i = 280; i < 288; ++i) lengths[i] = 8;
      if (!buildHuff(s_->lit, lengths, 288)) return false;
      uint8_t dists[30];
      for (int i = 0; i < 30; ++i) dists[i] = 5;
      if (!buildHuff(s_->dist, dists, 30)) return false;
      state_ = State::Compressed;
      return true;
    }
    case 2:
      if (!readDynamicTables()) return false;
      state_ = State::Compressed;
      return true;
    default:
      return fail("a reserved DEFLATE block type");
  }
}

bool Inflater::readDynamicTables() {
  if (!needBits(14)) return fail("a dynamic block's header is truncated");
  const int nlit = static_cast<int>(takeBits(5)) + 257;
  const int ndist = static_cast<int>(takeBits(5)) + 1;
  const int ncl = static_cast<int>(takeBits(4)) + 4;
  // 286 and 30 are the alphabets' real sizes; the 5-bit fields can claim more.
  if (nlit > 286 || ndist > 30) return fail("a dynamic block claims too many symbols");

  uint8_t clLengths[19] = {0};
  for (int i = 0; i < ncl; ++i) {
    if (!needBits(3)) return fail("a dynamic block's code lengths are truncated");
    clLengths[kClOrder[i]] = static_cast<uint8_t>(takeBits(3));
  }
  if (!buildHuff(s_->codeLen, clLengths, 19)) return false;

  // The two alphabets' lengths are coded together, with a run-length escape.
  // `s_->codeLengths` rather than a local: 316 bytes of frame, and this function
  // sits under the whole decode.
  uint8_t* lengths = s_->codeLengths;
  std::memset(lengths, 0, 286 + 30);
  const int total = nlit + ndist;
  int at = 0;
  while (at < total) {
    const int sym = decodeSymbol(s_->codeLen);
    if (sym < 0) return fail("a malformed code-length code");
    if (sym < 16) {
      lengths[at++] = static_cast<uint8_t>(sym);
      continue;
    }
    int repeat = 0;
    uint8_t value = 0;
    if (sym == 16) {
      // Repeat the PREVIOUS length. With nothing before it there is nothing to
      // repeat, and a file that says so is malformed rather than zero-filled.
      if (at == 0) return fail("a repeat code with no previous length");
      value = lengths[at - 1];
      if (!needBits(2)) return fail("a repeat count is truncated");
      repeat = 3 + static_cast<int>(takeBits(2));
    } else if (sym == 17) {
      if (!needBits(3)) return fail("a zero-run count is truncated");
      repeat = 3 + static_cast<int>(takeBits(3));
    } else {
      if (!needBits(7)) return fail("a zero-run count is truncated");
      repeat = 11 + static_cast<int>(takeBits(7));
    }
    // BOUNDED, because the count is a number the file states: a run claiming to
    // pass the end of the table would write past `lengths`.
    if (at + repeat > total) return fail("a length run overruns the alphabet");
    for (int i = 0; i < repeat; ++i) lengths[at++] = value;
  }

  // Symbol 256 ends a block, so a literal/length table without it can never
  // terminate -- an infinite decode on a file that simply omits it.
  if (lengths[256] == 0) return fail("a block with no end-of-block code");

  if (!buildHuff(s_->lit, lengths, nlit)) return false;
  return buildHuff(s_->dist, lengths + nlit, ndist);
}

void Inflater::put(uint8_t byte) {
  s_->window[wpos_] = byte;
  wpos_ = (wpos_ + 1) % kWindowBytes;
  ++totalOut_;
}

std::string_view Inflater::next() {
  if (state_ == State::Failed || state_ == State::Done) return {};

  const size_t start = wpos_;
  // Contiguous by construction: never past the ring's end, and never more than
  // half the window, so the run cannot overwrite its own beginning.
  const size_t room = kWindowBytes - start;
  const size_t cap = room < kChunkBytes ? room : kChunkBytes;
  size_t produced = 0;

  while (produced < cap) {
    // A match left over from the previous chunk, resumed. Byte at a time on
    // purpose: an overlapping match (distance 1 is a run of one byte) is defined
    // to read what it has just written, so a memcpy would be wrong.
    if (matchLeft_ > 0) {
      const size_t from = (wpos_ + kWindowBytes - matchDist_) % kWindowBytes;
      put(s_->window[from]);
      --matchLeft_;
      ++produced;
      continue;
    }

    if (state_ == State::BlockHeader) {
      if (!readBlockHeader()) break;
      continue;
    }

    if (state_ == State::Stored) {
      if (storedLeft_ == 0) {
        state_ = lastBlock_ ? State::Done : State::BlockHeader;
        if (state_ == State::Done) break;
        continue;
      }
      if (!refill()) {
        fail("a stored block ended early");
        break;
      }
      put(s_->in[inAt_++]);
      --storedLeft_;
      ++produced;
      continue;
    }

    // Compressed.
    const int sym = decodeSymbol(s_->lit);
    if (sym < 0) {
      fail("a malformed literal/length code");
      break;
    }
    if (sym < 256) {
      put(static_cast<uint8_t>(sym));
      ++produced;
      continue;
    }
    if (sym == 256) {
      state_ = lastBlock_ ? State::Done : State::BlockHeader;
      if (state_ == State::Done) break;
      continue;
    }

    const int lenIdx = sym - 257;
    if (lenIdx >= 29) {
      fail("a length symbol outside the alphabet");
      break;
    }
    if (!needBits(kLenExtra[lenIdx])) {
      fail("a length's extra bits are truncated");
      break;
    }
    const uint32_t length = kLenBase[lenIdx] + takeBits(kLenExtra[lenIdx]);

    const int dsym = decodeSymbol(s_->dist);
    if (dsym < 0 || dsym >= 30) {
      fail("a distance symbol outside the alphabet");
      break;
    }
    if (!needBits(kDistExtra[dsym])) {
      fail("a distance's extra bits are truncated");
      break;
    }
    const uint32_t distance = kDistBase[dsym] + takeBits(kDistExtra[dsym]);

    // TWO BOUNDS, both reachable from a crafted file. Past the window is a read
    // of bytes the stream never wrote; past what has been produced is a read from
    // before the start of the output, which on the first block is uninitialised
    // memory.
    if (distance > kWindowBytes || distance > totalOut_) {
      fail("a match reaches back further than the stream has written");
      break;
    }
    matchLeft_ = length;
    matchDist_ = distance;
  }

  if (state_ == State::Failed) return {};
  return std::string_view(reinterpret_cast<const char*>(s_->window + start), produced);
}

size_t InflateSource::read(void* dst, size_t bytes) {
  size_t wrote = 0;
  while (wrote < bytes) {
    if (at_ >= chunk_.size()) {
      chunk_ = inf_->next();
      at_ = 0;
      if (chunk_.empty()) break;  // done, or failed -- ask the Inflater which
    }
    const size_t take = chunk_.size() - at_ < bytes - wrote ? chunk_.size() - at_ : bytes - wrote;
    std::memcpy(static_cast<char*>(dst) + wrote, chunk_.data() + at_, take);
    at_ += take;
    wrote += take;
  }
  return wrote;
}

}  // namespace reader
