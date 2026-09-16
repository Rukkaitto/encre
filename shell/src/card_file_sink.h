#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "reader/http_transport.h"

// A `BodySink` THAT STREAMS ONTO THE CARD, so an article never exists in RAM.
//
// WHY IT IS NOT A `FileSystem` METHOD: `appendToCard`'s reason verbatim. The
// `reader::FileSystem` contract is 27 clauses driven by two harnesses
// (`test_filesystem.cpp` on the desktop, `sd_selftest.cpp` against a real card)
// and has no write HANDLE at all -- `writeAll` takes a whole buffer, which is
// the one thing a streaming sink must never have. Widening that contract means
// widening both harnesses for something `core/` will never call.
//
// IT WRITES TO `<path>.part` AND RENAMES ON `finish()`, so the article's real
// name appears only when the body is whole. Three things reach that rule:
//
//   - A SYNC IS INTERRUPTIBLE BY CONSTRUCTION. The transport is poll-shaped so
//     the panel and the buttons keep working, which means a cancel, a flat
//     battery or a pulled card can land in the middle of any article. A
//     half-written file under the real name is a row the list shows and the
//     reader cannot open.
//   - THE NEXT SYNC MUST RE-FETCH IT. `ArticleStore` decides what is already
//     held by whether `epubPath` exists, so a partial file under the real name
//     is indistinguishable from a complete one and would never be retried.
//   - AND A NON-2xx NEVER GETS HERE, which is the transport's half of the same
//     rule (see `BodySink::finish` -- the 401 clause).
//
// THE FILE IS HELD OPEN ACROSS THE WHOLE DOWNLOAD, and the bus guard is taken
// PER OPERATION rather than for the handle's lifetime -- `SdFileHandle`'s rule
// and for its reason. A download spans many `poll()` calls with paints between
// them, so a guard held across it would block `renderTop()` for the length of an
// article: not a deadlock, a panel that never repaints, which reads as a display
// fault.
//
// `SdFat` IS NOT IN THIS HEADER. The open file is behind a pimpl so that
// including this costs no SdFat, exactly as `sd_fs.h` forward-declares
// `SdFileHandle` and defines it in the .cpp.
class CardFileSink : public reader::BodySink {
 public:
  // `finalPath` is absolute and card-side -- `/.reader/articles/42.epub`. The
  // parent directory is created on the first write.
  explicit CardFileSink(std::string finalPath);
  ~CardFileSink() override;
  CardFileSink(const CardFileSink&) = delete;
  CardFileSink& operator=(const CardFileSink&) = delete;

  bool write(const uint8_t* data, size_t n) override;
  bool finish() override;

  // What reached the card. Not what the server said it would send: a short write
  // is a failure here, as it is in `writeAll`, and never a quiet success.
  size_t bytes() const { return bytes_; }
  const std::string& path() const { return final_; }
  const std::string& partPath() const { return part_; }
  // True once `finish()` has renamed. A sink that was never finished is one
  // whose `.part` the destructor removes.
  bool finished() const { return finished_; }

  // REMOVE THE `.part` AND FORGET IT. Idempotent, and the destructor's own
  // route, so a cancelled or refused request leaves nothing behind whether the
  // caller remembers to ask or simply drops the sink.
  void discard();

 private:
  struct Open;
  std::string final_;
  std::string part_;
  std::unique_ptr<Open> open_;
  size_t bytes_ = 0;
  bool failed_ = false;
  bool finished_ = false;
};
