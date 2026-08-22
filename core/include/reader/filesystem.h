#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

struct DirEntry {
  std::string name;  // leaf name, not a path
  bool isDir = false;
  uint32_t size = 0;  // 0 for directories
};

// An open, SEEKABLE read handle on one file. This is the EPUB path; readAll is
// not, and never was (see FileSystem::readAll below).
//
// RANDOM ACCESS IS THE REQUIREMENT, not merely streaming. A zip's central
// directory sits at the END of the archive, so a forward-only stream cannot read
// an EPUB at all: the reader has to seek to the tail, parse the directory, then
// seek back to each entry's local header. seek() is therefore not a convenience
// on top of read() -- it is the reason this class exists.
//
// The whole point is that the CALLER owns the buffer. readAll's ceiling exists
// because -fno-exceptions turns a std::string::resize that cannot allocate into
// an abort() with no diagnostic; a handle never sizes an allocation to the file,
// so a caller can walk a 40 MB archive through a 512-byte buffer on the stack.
// The handle object itself is one small fixed allocation, and every
// implementation makes it with `new (std::nothrow)` so that even THAT cannot
// abort -- a failed open is a null pointer, never a crash.
//
// The contract, pinned in fs_contract.h and run against all three
// implementations including the real card:
//
//  - size() is the file's length in bytes, fixed for the handle's lifetime: a
//    read handle cannot grow the file, so it is read once at open
//  - position() is the offset of the next byte, always 0 <= position() <= size()
//  - read() returns how many bytes it got and advances position() by that much.
//    A SHORT READ IS NORMAL -- at the end of the file there is nothing to be
//    short of. Reading at or past the end returns 0 and is NOT an error.
//  - read(dst, 0) is a no-op: it returns 0, does not move position(), and does
//    not touch `dst` (so a null `dst` with a zero count is fine)
//  - seek(offset) BEYOND size() IS REFUSED: it returns false and leaves
//    position() exactly where it was. seek(size()) is legal and succeeds.
//    See the note below on why refused rather than clamped.
//  - seeking backwards is as ordinary as seeking forwards
//
// WHY SEEK PAST THE END IS REFUSED RATHER THAN CLAMPED. Three reasons, and the
// third is the one that decided it:
//  - SdFat's FatFile::seekSet already fails for pos > fileSize and leaves
//    m_curPosition alone, so refusing IS the device's primitive. Clamping would
//    mean emulating a behaviour on top of it in the one implementation nothing
//    but the self-test checks, which is where this project's bugs live.
//  - A bad offset is reported at the call that made it, rather than being turned
//    into a legitimate-looking zero-byte read a few lines later.
//  - Clamping would make an out-of-range offset INDISTINGUISHABLE from a
//    deliberate seek to EOF, and a zip reader deliberately seeks near the end --
//    the end-of-central-directory scan starts at size() minus a window. It needs
//    "that offset does not exist" and "I am at the end" to be different answers.
//
// TELLING A TRUNCATED FILE FROM A DEAD CARD. read() reports only a byte count,
// so a short read while position() < size() means the read failed rather than
// that the file ended. Ask the FileSystem it came from which: an implementation
// that noticed the card stop answering has already cleared mounted().
class FileHandle {
 public:
  virtual ~FileHandle() = default;

  // Non-copyable and non-movable: an implementation owns an OS or SdFat file
  // object whose close() must happen exactly once, and duplicating the owner is
  // how a handle gets closed twice or not at all.
  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;

  virtual uint32_t size() const = 0;
  virtual uint32_t position() const = 0;

  // Up to `bytes` into `dst`. Returns the count, which may be less than asked
  // for; 0 means end of file (or `bytes` was 0).
  virtual size_t read(void* dst, size_t bytes) = 0;

  // Absolute. False -- with position() unchanged -- for an offset past size().
  virtual bool seek(uint32_t offset) = 0;

 protected:
  FileHandle() = default;
};

// Everything core/ knows about storage.
//
// Deliberately small: exists / list / readAll / writeAll / mkdirs / remove, plus
// openRead for the one case a whole-file read cannot serve. Every V1 chrome need
// is "read this small file" or "list this directory", and an interface that stops
// there can be faked in a test with a std::map.
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
//  - openRead is null for a missing path, for a directory, and for anything at
//    all while mounted() is false; two handles may be open at once and neither
//    sees the other's position (a zip reader holds the archive open while it
//    reads an entry, so that is not hypothetical)
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

  // Whole-file read. Intended for the small JSON files V1 stores; an
  // implementation may cap it (SdFileSystem does, at 64 KB) because sizing an
  // allocation to a file the user chose is how -fno-exceptions turns a big file
  // into an abort. Use openRead for anything that is not a settings-sized file.
  virtual bool readAll(std::string_view path, std::string& out) = 0;

  // An open, seekable read handle, or null. Null for a missing path, for a
  // directory, while mounted() is false, and if the handle itself cannot be
  // allocated -- never an abort, and never a handle that is not usable.
  //
  // The handle keeps the file open for as long as it lives and closes it when
  // destroyed, so ownership is the unique_ptr and nothing else: there is no
  // close() for a caller to forget, and no path out of a scope that leaks the
  // underlying file object.
  virtual std::unique_ptr<FileHandle> openRead(std::string_view path) = 0;

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
