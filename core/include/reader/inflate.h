#pragma once
#include <string>
#include <string_view>

namespace reader {

// RAW DEFLATE, which is what a zip entry holds -- no zlib header, no adler32.
// `stbi_zlib_decode_noheader_buffer`'s job, and stb_image.h is already vendored
// for its zlib decoder alone (see inflate.cpp for how the image decoders are
// compiled out).
//
// `out` MUST ALREADY BE SIZED to the uncompressed length, and that is the whole
// safety property rather than an inconvenience:
//
//   * A zip entry's header states its uncompressed size, so the length is known
//     before a byte is read. The caller sizes the buffer from that -- after
//     bounding it, because the number is a CLAIM made by a file off a user's
//     card, not a fact.
//   * Under `-fno-exceptions` a `resize` that cannot allocate is an `abort()`
//     with no diagnostic. A function that grew its own output would turn a
//     corrupt length field into a dead device; one that fills a buffer it was
//     handed cannot.
//   * stb range-checks nothing on hostile input -- its own header says so, and
//     3A recorded the same for fonts. `olen` is the only thing standing between
//     a malformed stream and the heap, so the caller owning it is deliberate.
//
// True only when the stream decoded to EXACTLY `out.size()` bytes. Short is a
// refusal, not a success with a partial buffer: a truncated entry and a correct
// one must not be indistinguishable, because the difference is a chapter with its
// end missing.
bool inflateRaw(std::string_view in, std::string& out);

}  // namespace reader
