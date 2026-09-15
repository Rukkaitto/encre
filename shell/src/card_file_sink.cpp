#include "card_file_sink.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include <new>

#include "sd_fs.h"

// The open `.part`, kept out of the header so nothing that includes it pays for
// SdFat. `SdFileHandle`'s shape one file over.
struct CardFileSink::Open {
  FsFile f;
};

namespace {

// The parent of an absolute path, or "" for a path with no parent. `mkdirs`
// takes a directory, and the sink is handed a FILE.
std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return std::string();
  return path.substr(0, slash);
}

}  // namespace

CardFileSink::CardFileSink(std::string finalPath)
    : final_(std::move(finalPath)), part_(final_ + ".part") {}

CardFileSink::~CardFileSink() {
  // A SINK THAT WAS NEVER FINISHED LEAVES NOTHING BEHIND, and the destructor is
  // what makes that true without a caller remembering. A cancelled request, a
  // 401, a refused write and a sink simply dropped all land here.
  if (!finished_) discard();
  open_.reset();
}

bool CardFileSink::write(const uint8_t* data, size_t n) {
  if (failed_ || finished_) return false;
  if (data == nullptr) return false;
  if (n == 0) return true;

  if (!open_) {
    // OPENED LAZILY, so a request that is refused before a byte arrives creates
    // no file at all -- there is nothing for the destructor to clean up and
    // nothing for a later sync to see.
    SpiBusGuard bus;
    const std::string parent = parentOf(part_);
    if (!parent.empty()) SdMan.mkdir(parent.c_str());
    // O_TRUNC, never O_APPEND: a `.part` left by a previous attempt is a
    // fragment of a DIFFERENT response, and appending to it would build a file
    // that is corrupt in a way no size check can see.
    auto open = std::unique_ptr<Open>(new (std::nothrow) Open);
    if (!open) {
      failed_ = true;
      return false;
    }
    open->f = SdMan.open(part_.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!open->f) {
      failed_ = true;
      return false;
    }
    open_ = std::move(open);
  }

  SpiBusGuard bus;
  const size_t wrote = open_->f.write(data, n);
  // A SHORT WRITE IS A FAILURE, NOT A SUCCESS, which is `writeAll`'s rule and
  // the one that matters most on a card that is filling up: a sink reporting
  // true for 2 KB of a 4 KB chunk produces a file that is the right NAME and the
  // wrong bytes.
  if (wrote != n || open_->f.getWriteError()) {
    failed_ = true;
    return false;
  }
  bytes_ += n;
  return true;
}

bool CardFileSink::finish() {
  if (failed_ || finished_) return false;

  {
    SpiBusGuard bus;
    if (open_) {
      // SYNC BEFORE THE RENAME, because the rename is the promise. A directory
      // entry pointing at data still sitting in a buffer is exactly the file
      // this class exists not to produce.
      const bool synced = open_->f.sync();
      open_->f.close();
      open_.reset();
      if (!synced) {
        failed_ = true;
        return false;
      }
    } else {
      // A 200 WITH AN EMPTY BODY REACHES HERE WITH NO FILE OPEN, and it is a
      // failure rather than an empty article: wallabag's export endpoint always
      // answers with an EPUB, so zero bytes is a truncated response that the
      // status did not admit to. Refusing leaves no row to open.
      failed_ = true;
      return false;
    }

    // `rename` REFUSES OVER AN EXISTING NAME on FAT, so the destination goes
    // first. That is not a race worth guarding: one sync runs at a time and the
    // article it is replacing is the one it just re-fetched.
    if (SdMan.exists(final_.c_str())) SdMan.remove(final_.c_str());
    if (!SdMan.rename(part_.c_str(), final_.c_str())) {
      failed_ = true;
      return false;
    }
  }

  finished_ = true;
  return true;
}

void CardFileSink::discard() {
  SpiBusGuard bus;
  if (open_) {
    open_->f.close();
    open_.reset();
  }
  if (!part_.empty() && SdMan.exists(part_.c_str())) SdMan.remove(part_.c_str());
  bytes_ = 0;
}
