#include "reader/inflate.h"

#include <climits>

// stb_image.h FOR ITS ZLIB DECODER ALONE, and the defines are the whole trick.
//
// Every image decoder is compiled out and `STBI_SUPPORT_ZLIB` keeps the inflate
// path: the header's own lines 582-583 read
//
//   #if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
//   #define STBI_NO_ZLIB
//
// so asking for no PNG would take the decoder with it unless zlib is asked for
// explicitly. The zlib block itself is `#ifndef STBI_NO_ZLIB`, separate from
// PNG's, which is what makes this combination legal rather than a happy accident.
//
// STB_IMAGE_STATIC because `core/src/png.cpp` ALSO instantiates this header, with
// the image decoders left in, for the desktop's golden PNGs. Two configurations of
// one header cannot share external symbols, so this copy's are internal. The cost
// is a second copy of the inflate code in the desktop build and none at all in the
// firmware, where png.cpp is filtered out (`core/library.json`).
//
// Vendored, not written: 3A's argument for stb_truetype applies unchanged -- this
// is a well-trodden decoder we would otherwise be writing ourselves against
// untrusted input. What we do NOT inherit is trust in its bounds checking, which
// is why inflate.h makes the caller own the output length.
#define STBI_NO_JPEG
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_STDIO
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace reader {

bool inflateRaw(std::string_view in, std::string& out) {
  // Both empty cases are refusals rather than vacuous successes. An entry with a
  // zero uncompressed length is not something a chapter can be, and stb would be
  // handed a zero-length buffer to write into.
  if (in.empty() || out.empty()) return false;

  // stb takes ints. A length that does not fit one is a claim from the file that
  // this build cannot represent, so it is refused before the cast rather than
  // wrapped into a small positive number -- which is how a 3 GB claim becomes a
  // tiny buffer and a heap overrun.
  if (in.size() > static_cast<size_t>(INT_MAX) || out.size() > static_cast<size_t>(INT_MAX))
    return false;

  const int wrote = stbi_zlib_decode_noheader_buffer(
      out.data(), static_cast<int>(out.size()), in.data(), static_cast<int>(in.size()));

  // EXACTLY the expected length, and the equality is the check. stb returns -1 on
  // a malformed stream and the byte count otherwise -- so a stream that decoded
  // SHORT returns a positive number, which a `wrote >= 0` test would accept and
  // hand back a buffer with a tail of zeros. That is a chapter missing its end,
  // reported as success.
  return wrote >= 0 && static_cast<size_t>(wrote) == out.size();
}

}  // namespace reader
