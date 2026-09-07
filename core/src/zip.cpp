#include "reader/zip.h"

#include <new>

#include <cstring>

#include "reader/heapguard.h"
#include "reader/inflate.h"

namespace reader {
namespace {

constexpr uint32_t kCentralSig = 0x02014b50u;
constexpr uint32_t kEocdSig = 0x06054b50u;
constexpr uint32_t kLocalSig = 0x04034b50u;

// A zip is little-endian throughout, whatever the host is. Read byte by byte
// rather than memcpy-ing a struct: this project targets a RISC-V part where an
// unaligned load is not free, and a packed struct would be a second place the
// field order lives.
uint16_t le16(const unsigned char* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Read exactly n bytes at an absolute offset, or fail. Short reads are a failure
// rather than a partial success for the reason the FileHandle contract gives: a
// truncated read and a correct one must stay distinguishable.
bool readAt(FileHandle& f, uint64_t at, void* dst, size_t n) {
  if (n == 0) return true;
  if (at > f.size() || f.size() - at < n) return false;
  if (!f.seek(at)) return false;
  return f.read(dst, n) == n;
}

// General purpose bit 0 is encryption; bit 3 is a data descriptor, which means
// the sizes in the LOCAL header are zero and only the central directory has them.
// We read sizes from the central directory anyway, so bit 3 is survivable -- but
// bit 0 is not, because the bytes are ciphertext.
constexpr uint16_t kFlagEncrypted = 0x0001;

constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflated = 8;

// A HEAP BUFFER THAT CAN FAIL WITHOUT ABORTING.
//
// `new` and every std::string/vector growth abort() under -fno-exceptions, with no
// message and no stack -- and this file allocates sizes a FILE states. The device
// found that the honest way: opening a full-length novel called abort() from
// `std::string tail(...)` below, rebooted, and came back on Home, which reads as a
// navigation bug rather than as out of memory.
//
// std::nothrow returns nullptr instead. "This book needs more memory than this
// device has" is then a refusal with a reason, which is a thing a screen can say.
class Buf {
 public:
  explicit Buf(size_t n) : p_(new (std::nothrow) char[n]), n_(p_ != nullptr ? n : 0) {}
  ~Buf() { delete[] p_; }
  Buf(const Buf&) = delete;
  Buf& operator=(const Buf&) = delete;
  bool ok() const { return p_ != nullptr; }
  char* data() { return p_; }
  const char* data() const { return p_; }
  size_t size() const { return n_; }

 private:
  char* p_;
  size_t n_;
};

// Whether the heap could serve `n` bytes right now, without keeping them.
//
// For the one buffer this file does not own: Zip::read fills a caller's
// std::string, whose assign() would abort.
//
// THIS FILE WROTE THAT PROBE FIRST AND IS NO LONGER THE ONLY PLACE THAT NEEDS IT,
// so the body moved to reader/heapguard.h and this is the name it was called by.
// `Epub::open`, `loadToc`, `readItalicClasses` and `openBook` all grow containers
// from numbers a file states, and a fourth hand-rolled copy of a five-line probe is
// the second-copy rule this project keeps paying to retrofit.
bool canAllocate(size_t n) { return Heap::hasBlock(n); }

// The backward EOCD scan's window, on the STACK. Sized at four SdFat sectors: the
// scan walks backwards, so every read is a real card read, and a 64 KB window at
// 512 bytes a step would be 128 of them.
constexpr size_t kScanChunk = 2048;

}  // namespace

bool Zip::fail(const char* why) {
  reason_ = why;
  entries_.clear();
  return false;
}

bool Zip::open(FileHandle& file) {
  entries_.clear();
  reason_ = "";

  const uint64_t size = file.size();
  // 22 bytes is an EOCD with no comment, which is the smallest a zip can be.
  if (size < 22) return fail("too small to be a zip: no end-of-central-directory record");

  // SCAN BACKWARDS FOR THE EOCD, bounded by the comment field's maximum rather
  // than by the file. A legal comment pushes the record up to 64 KB from the end,
  // so a reader that only checks the last 22 bytes misses it -- and scanning the
  // whole file would make a large non-zip cost a full read before being refused.
  //
  // IN CHUNKS, ON THE STACK, and that is the whole point of the shape below. This
  // used to allocate the window in one std::string -- up to 65,558 bytes -- which
  // is the largest single allocation in the reader and the first thing to fail on a
  // real book: the device had 142 KB free and no contiguous block that size, because
  // 203 library entries had fragmented it. A backward scan needs a sliding view, not
  // the whole window at once.
  const uint64_t window = size < kMaxEocdSearch ? size : kMaxEocdSearch;
  const uint64_t windowStart = size - window;
  unsigned char buf[kScanChunk + 3];  // +3 so a signature at a chunk's top edge reads whole

  int64_t eocd = -1;
  // `hi` is exclusive: a signature must START below it. Each pass drops it by a
  // chunk, so this terminates at windowStart.
  uint64_t hi = size;
  while (eocd < 0 && hi > windowStart) {
    const uint64_t lo = (hi - windowStart > kScanChunk) ? (hi - kScanChunk) : windowStart;
    const uint64_t readEnd = (lo + kScanChunk + 3 < size) ? (lo + kScanChunk + 3) : size;
    const size_t n = static_cast<size_t>(readEnd - lo);
    if (!readAt(file, lo, buf, n)) return fail("could not read the end of the file");
    // Backwards within the chunk, and the chunks run backwards, so the FIRST hit is
    // the LAST record in the file -- which matters because an archive can contain
    // another archive's bytes and the outer one's EOCD is the later.
    //
    // The full 22 bytes must fit inside the file, exactly as the one-buffer version
    // required: accepting a 4-byte signature in the last 21 bytes would let a stray
    // signature there shadow the real record and turn a readable zip into a refusal.
    for (int64_t i = static_cast<int64_t>(hi - lo) - 1; i >= 0; --i) {
      if (static_cast<size_t>(i) + 4 > n) continue;
      if (lo + static_cast<uint64_t>(i) + 22 > size) continue;
      if (le32(buf + i) == kEocdSig) {
        eocd = static_cast<int64_t>(lo + static_cast<uint64_t>(i));
        break;
      }
    }
    hi = lo;
  }
  if (eocd < 0) return fail("no end-of-central-directory record found");

  // The record itself, read on its own now that the window is not resident.
  unsigned char record[22];
  if (!readAt(file, static_cast<uint64_t>(eocd), record, sizeof(record)))
    return fail("could not read the end-of-central-directory record");
  const unsigned char* e = record;
  const uint16_t claimed = le16(e + 10);   // entries on this disk
  const uint32_t cdSize = le32(e + 12);
  const uint32_t cdOffset = le32(e + 16);

  if (claimed > kMaxEntries) return fail("the archive claims more entries than we will read");
  if (cdOffset > size || cdSize > size || cdOffset + static_cast<uint64_t>(cdSize) > size)
    return fail("the central directory is not inside the file");

  Buf cd(cdSize);
  if (!cd.ok()) return fail("not enough memory to read the central directory");
  if (cdSize > 0 && !readAt(file, cdOffset, cd.data(), cd.size()))
    return fail("could not read the central directory");

  // SIZED BY A NUMBER THE FILE STATES, exactly as `Buf cd` above is -- and until
  // this line it was the one allocation in this file that could not refuse. 512
  // entries (kMaxEntries) is ~20 KB of contiguous vector on the device, taken while
  // the central directory buffer above is still held, and a `reserve` that cannot
  // allocate is `abort()` with no diagnostic.
  //
  // AFTER THE CAP AND AFTER `cd`, both deliberately: the cap is what bounds this at
  // all, and probing before `cd` was taken would answer a question nobody asked --
  // `Zip::read`'s own note, one allocation up.
  //
  // The loop below runs exactly `claimed` times, so this reserve is the ONLY
  // allocation the vector makes and guarding it closes the site rather than
  // narrowing it.
  if (!ensureRoom(entries_, claimed))
    return fail("not enough memory to hold the archive's entry list");
  const unsigned char* p = reinterpret_cast<const unsigned char*>(cd.data());
  size_t at = 0;
  for (uint16_t i = 0; i < claimed; ++i) {
    // A fixed-size header, then three variable fields. Every step is bounds
    // checked against the directory we actually read, so an entry count that
    // lies runs out of directory rather than off the end of the buffer.
    if (at + 46 > cd.size()) return fail("the central directory ends mid-entry");
    if (le32(p + at) != kCentralSig) return fail("a central directory entry has no signature");

    const uint16_t flags = le16(p + at + 8);
    const uint16_t method = le16(p + at + 10);
    const uint32_t csize = le32(p + at + 20);
    const uint32_t usize = le32(p + at + 24);
    const uint16_t nameLen = le16(p + at + 28);
    const uint16_t extraLen = le16(p + at + 30);
    const uint16_t commentLen = le16(p + at + 32);
    const uint32_t local = le32(p + at + 42);

    if (at + 46 + nameLen + extraLen + commentLen > cd.size())
      return fail("a central directory entry's name runs past the directory");
    // REFUSED AT OPEN, not at read: both are knowable from the directory, and an
    // entry that can never be read is not an entry. Refusing the whole archive
    // rather than skipping the entry is deliberate -- an EPUB whose chapter is
    // encrypted is not a book with one missing page, it is a book we cannot read.
    if ((flags & kFlagEncrypted) != 0) return fail("the archive is encrypted");
    if (method != kMethodStored && method != kMethodDeflated)
      return fail("an entry uses a compression method we do not implement");

    Entry entry;
    // A NAME IS A FILE-STATED LENGTH TOO, up to 65,535 bytes by the format and
    // bounded here only by the directory actually read. Every real EPUB's longest
    // name is ~100 bytes, so this refuses nothing a book does -- it is the same
    // one-line guard as the entry list above, on the same kind of number, and the
    // alternative is an `abort()` for a name.
    if (!ensureRoom(entry.name, nameLen))
      return fail("not enough memory to hold an entry's name");
    entry.name.assign(reinterpret_cast<const char*>(p + at + 46), nameLen);
    entry.compressedSize = csize;
    entry.uncompressedSize = usize;
    entry.localHeaderOffset = local;
    entry.deflated = (method == kMethodDeflated);
    entries_.push_back(std::move(entry));

    at += 46u + nameLen + extraLen + commentLen;
  }

  return true;
}

const Zip::Entry* Zip::find(std::string_view name) const {
  if (name.empty()) return nullptr;
  for (const Entry& e : entries_)
    if (e.name == name) return &e;
  return nullptr;
}

bool Zip::read(FileHandle& file, const Entry& entry, std::string& out) const {
  out.clear();
  // CAPPED BEFORE ALLOCATED. uncompressedSize is a number the file states, and
  // under -fno-exceptions a resize that cannot allocate is an abort() with no
  // diagnostic -- so a 200 MB claim must be refused here rather than attempted.
  if (entry.uncompressedSize > kMaxEntryBytes) return false;
  if (entry.compressedSize > kMaxEntryBytes) return false;

  // The local header restates the sizes and may disagree with the directory; only
  // its LENGTHS are read, to find where the data begins. The directory is the
  // authority on everything else.
  unsigned char header[30];
  if (!readAt(file, entry.localHeaderOffset, header, sizeof(header))) return false;
  if (le32(header) != kLocalSig) return false;
  const uint16_t nameLen = le16(header + 26);
  const uint16_t extraLen = le16(header + 28);
  const uint64_t dataAt =
      static_cast<uint64_t>(entry.localHeaderOffset) + 30u + nameLen + extraLen;

  Buf raw(entry.compressedSize);
  if (!raw.ok()) {
    reason_ = "not enough memory to read an archive entry";
    return false;
  }
  if (!readAt(file, dataAt, raw.data(), raw.size())) return false;

  if (!entry.deflated) {
    // Stored: the two sizes must agree, and a file that says otherwise is
    // describing something this cannot represent.
    if (entry.compressedSize != entry.uncompressedSize) return false;
    if (!canAllocate(entry.uncompressedSize + 1)) {
      reason_ = "not enough memory to read an archive entry";
      return false;
    }
    out.assign(raw.data(), raw.size());
    return true;
  }

  // PROBED WHILE `raw` IS STILL HELD, deliberately: both buffers are live during
  // the inflate, so the question is whether the heap can serve the second one on
  // top of the first. Probing before allocating raw would answer a question nobody
  // asked.
  if (!canAllocate(static_cast<size_t>(entry.uncompressedSize) + 1)) {
    reason_ = "not enough memory to inflate an archive entry";
    return false;
  }
  out.assign(entry.uncompressedSize, '\0');
  if (!inflateRaw(std::string_view(raw.data(), raw.size()), out)) {
    out.clear();
    reason_ = "an archive entry's compressed data is malformed";
    return false;
  }
  return true;
}

bool Zip::locateData(FileHandle& file, uint32_t localHeaderOffset, uint32_t compressedSize,
                     uint32_t& dataOffset) {
  unsigned char header[30];
  if (!readAt(file, localHeaderOffset, header, sizeof(header))) return false;
  if (le32(header) != kLocalSig) return false;
  const uint16_t nameLen = le16(header + 26);
  const uint16_t extraLen = le16(header + 28);
  const uint64_t at = static_cast<uint64_t>(localHeaderOffset) + 30u + nameLen + extraLen;
  if (at + compressedSize > file.size()) return false;
  dataOffset = static_cast<uint32_t>(at);
  return true;
}

bool Zip::locate(FileHandle& file, const Entry& entry, uint32_t& dataOffset) const {
  // The local header restates the sizes and may disagree with the directory; only
  // its LENGTHS are read, to find where the data begins. The directory is the
  // authority on everything else.
  unsigned char header[30];
  if (!readAt(file, entry.localHeaderOffset, header, sizeof(header))) {
    reason_ = "could not read a local header";
    return false;
  }
  if (le32(header) != kLocalSig) {
    reason_ = "an entry has no local header signature";
    return false;
  }
  const uint16_t nameLen = le16(header + 26);
  const uint16_t extraLen = le16(header + 28);
  const uint64_t at = static_cast<uint64_t>(entry.localHeaderOffset) + 30u + nameLen + extraLen;
  if (at + entry.compressedSize > file.size()) {
    reason_ = "an entry's data runs past the end of the file";
    return false;
  }
  dataOffset = static_cast<uint32_t>(at);
  return true;
}

void EntrySource::reset(FileHandle& file, uint32_t dataOffset, uint32_t compressedSize) {
  file_ = &file;
  at_ = dataOffset;
  left_ = compressedSize;
}

size_t EntrySource::read(void* dst, size_t bytes) {
  if (file_ == nullptr || left_ == 0) return 0;
  const size_t want = bytes < left_ ? bytes : left_;
  if (!file_->seek(at_)) return 0;
  const size_t got = file_->read(dst, want);
  at_ += static_cast<uint32_t>(got);
  left_ -= static_cast<uint32_t>(got);
  return got;
}

}  // namespace reader
