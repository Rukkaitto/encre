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
// NON-OWNING: `bytes` must outlive the source -- and that requirement is now
// ENFORCED rather than stated, because stating it was not enough. One of the 71
// call sites built its document inline:
//
//     Grained src("<body><p>" + digits(...) + "</p></body>", 4096);
//
// The `operator+` temporary dies at the end of the constructor's full-expression,
// so every read() afterwards copied from freed heap. It went unnoticed because
// freeing a 13 KB block writes only 8 of its bytes: the document was still there
// unless an unrelated allocation reused the region, so the case passed alone and
// failed about one full-suite run in ten, in a DIFFERENT test each time it was
// looked at. ASan named it in one run; nothing else had, in the whole life of the
// file.
//
// So the rvalue-string constructor is DELETED. `Grained src(<temporary>, n)` is a
// compile error now, and a caller must name the buffer it is lending. This is the
// shape CLAUDE.md already argues for elsewhere -- a contract whose halves can be
// adopted separately is a mechanism not yet made structural, which is why
// `FocusScreen` makes its pair `final`. A comment saying "must outlive" is a rule
// somebody has to remember; a deleted overload is one they cannot get wrong.
//
// A string LITERAL is still fine and still compiles: it has static storage, so
// there is nothing to outlive.
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "reader/inflate_stream.h"

namespace grainsrc {

class Grained : public reader::ByteSource {
 public:
  Grained(std::string_view bytes, size_t grain) : b_(bytes), grain_(grain) {}

  // A temporary std::string cannot be lent: it is destroyed at the end of the
  // full-expression that builds this object, leaving b_ dangling. An exact match
  // beats the string_view conversion, so this claims the overload and the call
  // fails to COMPILE rather than reading freed heap at run time. Name the string.
  // (An lvalue string is unaffected -- it cannot bind to this.)
  Grained(std::string&&, size_t) = delete;

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
