#pragma once
// A ByteSource that hands out at most `grain` bytes per read, over a caller-owned
// buffer.
//
// GRAIN 1 IS THE LOAD-BEARING CASE: a source that always satisfies a full read
// hides every resumption bug a streaming parser has to get right -- the bit
// reader's refill mid-code, a tag name or an entity split across a read
// boundary, a BOM arriving one byte at a time. One byte at a time forces every
// one of those on every stream, which is exactly what test_inflate_stream.cpp
// was built to find.
//
// THIS IS THE FOURTH TIME THIS CLASS WAS ABOUT TO BE WRITTEN. It already existed,
// byte-for-byte identical apart from the copy loop, in test_inflate_stream.cpp,
// test_document.cpp and test_xml.cpp -- one streaming layer at a time needing the
// same thing to exercise its own resumption path. CLAUDE.md's rule is explicit
// about exactly this shape: the second copy is the extraction point, not the
// fifth.
//
// Non-owning: `bytes` must outlive the source, as it did at all three call sites
// this replaces.
#include <cstddef>
#include <cstring>
#include <string_view>

#include "reader/inflate_stream.h"

namespace grainsrc {

class Grained : public reader::ByteSource {
 public:
  Grained(std::string_view bytes, size_t grain) : b_(bytes), grain_(grain) {}

  size_t read(void* dst, size_t want) override {
    const size_t n = want < grain_ ? want : grain_;
    const size_t got = b_.size() - at_ < n ? b_.size() - at_ : n;
    std::memcpy(dst, b_.data() + at_, got);
    at_ += got;
    return got;
  }

 private:
  std::string_view b_;
  size_t grain_;
  size_t at_ = 0;
};

}  // namespace grainsrc
