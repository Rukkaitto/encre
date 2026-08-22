#include "reader/zip.h"

#include <cstring>

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
  const uint64_t window = size < kMaxEocdSearch ? size : kMaxEocdSearch;
  std::string tail(static_cast<size_t>(window), '\0');
  if (!readAt(file, size - window, tail.data(), tail.size()))
    return fail("could not read the end of the file");

  const unsigned char* t = reinterpret_cast<const unsigned char*>(tail.data());
  int64_t eocd = -1;
  // From the end: the LAST record wins, which matters because an archive can
  // contain another archive's bytes and the outer one's EOCD is the later.
  for (int64_t i = static_cast<int64_t>(tail.size()) - 22; i >= 0; --i) {
    if (le32(t + i) == kEocdSig) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) return fail("no end-of-central-directory record found");

  const unsigned char* e = t + eocd;
  const uint16_t claimed = le16(e + 10);   // entries on this disk
  const uint32_t cdSize = le32(e + 12);
  const uint32_t cdOffset = le32(e + 16);

  if (claimed > kMaxEntries) return fail("the archive claims more entries than we will read");
  if (cdOffset > size || cdSize > size || cdOffset + static_cast<uint64_t>(cdSize) > size)
    return fail("the central directory is not inside the file");

  std::string cd(cdSize, '\0');
  if (cdSize > 0 && !readAt(file, cdOffset, cd.data(), cd.size()))
    return fail("could not read the central directory");

  entries_.reserve(claimed);
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

  std::string raw(entry.compressedSize, '\0');
  if (!readAt(file, dataAt, raw.data(), raw.size())) return false;

  if (!entry.deflated) {
    // Stored: the two sizes must agree, and a file that says otherwise is
    // describing something this cannot represent.
    if (entry.compressedSize != entry.uncompressedSize) return false;
    out = std::move(raw);
    return true;
  }

  out.assign(entry.uncompressedSize, '\0');
  if (!inflateRaw(raw, out)) {
    out.clear();
    return false;
  }
  return true;
}

}  // namespace reader
