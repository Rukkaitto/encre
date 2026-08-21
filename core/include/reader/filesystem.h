#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

struct DirEntry {
  std::string name;  // leaf name, not a path
  bool isDir = false;
  uint32_t size = 0;  // 0 for directories
};

// Everything core/ knows about storage.
//
// Deliberately small: exists / list / readAll / writeAll / mkdirs / remove. Every
// V1 need is "read this small file" or "list this directory", and an interface
// that stops there can be faked in a test with a std::map.
//
// NOT here, on purpose: open handles, seeking, streaming reads. Phase 3's EPUBs
// are megabytes against ~230 KB of heap, so they cannot use readAll and will need
// a handle with read(buf, n). That is a different shape and designing it now,
// with no consumer to check it against, would mean guessing. Add it when the
// EPUB reader exists -- the omission is a decision, not an oversight.
//
// Paths are absolute, '/'-separated, and never end in '/'. "/" itself is the
// root directory. An implementation normalises a redundant separator or a
// trailing one rather than failing, so a caller that joins two paths naively
// still addresses the file it meant.
//
// The contract every implementation owes, pinned once in
// test/unit/test_filesystem.cpp and run against each of them:
//
//  - a written file reads back byte-identical, embedded newlines, NULs and an
//    empty body included; writeAll TRUNCATES rather than appending
//  - writeAll creates missing parents; it fails on a path that is a directory
//  - list APPENDS to `out` and never clears it, reports isDir, and returns false
//    for a file or a missing path
//  - readAll leaves `out` untouched when it returns false
//  - remove is about the END STATE: true for a file that is already absent,
//    false for a directory
//  - with mounted() false EVERY operation fails, including the ones that create
//    -- an unmounted filesystem must not quietly bring itself into existence
class FileSystem {
 public:
  virtual ~FileSystem() = default;

  // False when there is no usable storage -- no card, or a card that would not
  // mount. Every other method fails in that state.
  virtual bool mounted() const = 0;

  virtual bool exists(std::string_view path) = 0;

  // Appends `path`'s entries to `out` (does not clear it). False if `path` is
  // not a readable directory. Order is unspecified: callers that care sort.
  virtual bool list(std::string_view path, std::vector<DirEntry>& out) = 0;

  // Whole-file read. Intended for the small JSON files V1 stores; see the note
  // above about why this is not the EPUB path.
  virtual bool readAll(std::string_view path, std::string& out) = 0;

  // Creates or truncates. Creates parent directories as needed, so a caller
  // saving settings does not have to mkdirs first.
  virtual bool writeAll(std::string_view path, std::string_view data) = 0;

  virtual bool mkdirs(std::string_view path) = 0;

  // Files only. Returns true if the file is gone afterwards, including when it
  // was already absent -- callers deleting a book care about the end state, not
  // about racing something else that deleted it first.
  virtual bool remove(std::string_view path) = 0;
};

}  // namespace reader
