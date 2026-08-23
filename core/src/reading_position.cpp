#include "reader/reading_position.h"

#include "reader/json.h"

namespace reader {

namespace {

// The keys, spelled once. A typo in one of a matched pair is a field that saves
// and never loads, which no test that round-trips through both would catch.
constexpr const char* kKeyVersion = "version";
constexpr const char* kKeyPath = "path";
constexpr const char* kKeySpine = "spine";
constexpr const char* kKeyBlock = "block";
constexpr const char* kKeyLine = "line";
constexpr const char* kKeyBytes = "bookBytes";
constexpr const char* kKeyPpem = "ppem";
constexpr const char* kKeyColumnW = "columnW";
constexpr const char* kKeyPercent = "percent";

// A negative index is not a position, and a file is free to claim one. Clamped at
// the boundary rather than refused: the rest of the record is still usable, which is
// the same call loadSettings makes for an out-of-range value.
int nonNegative(int64_t v) { return v < 0 ? 0 : static_cast<int>(v > 0x7fffffff ? 0 : v); }

}  // namespace

bool ReadingPosition::operator==(const ReadingPosition& o) const {
  return bookPath == o.bookPath && spine == o.spine && block == o.block && line == o.line &&
         bookBytes == o.bookBytes && ppem == o.ppem && columnW == o.columnW &&
         percent == o.percent;
}

PositionFit fitOf(const ReadingPosition& saved, std::string_view bookPath, uint32_t bookBytes,
                  int ppem, int columnW) {
  // THE PATH FIRST, and it is not one comparison among several: the filename is a
  // hash, so this is what tells a collision from a match. Everything else could
  // agree by coincidence.
  if (saved.bookPath != bookPath) return PositionFit::Unusable;
  // A record that never recorded its geometry cannot claim a line at this one.
  if (saved.ppem <= 0 || saved.columnW <= 0) return PositionFit::Rebound;
  if (saved.bookBytes != bookBytes) return PositionFit::Rebound;
  if (saved.ppem != ppem || saved.columnW != columnW) return PositionFit::Relaid;
  return PositionFit::Exact;
}

PositionRestore restoreFrom(const ReadingPosition& saved, PositionFit fit) {
  PositionRestore r;
  switch (fit) {
    case PositionFit::Unusable:
      return r;
    case PositionFit::Rebound:
      // The spine survives a re-export; a block index within a rewritten chapter
      // does not. The top of the chapter is where this lands.
      r.any = true;
      r.spine = saved.spine;
      return r;
    case PositionFit::Relaid:
      // Blocks are the document's, not the layout's, so the paragraph survives a
      // type-size change even though the line inside it does not.
      r.any = true;
      r.spine = saved.spine;
      r.cursor = Cursor{saved.block, 0};
      return r;
    case PositionFit::Exact:
      r.any = true;
      r.spine = saved.spine;
      r.cursor = Cursor{saved.block, saved.line};
      return r;
  }
  return r;
}

std::string serialise(const ReadingPosition& p) {
  JsonObject o;
  o.setInt(kKeyVersion, kPositionVersion);
  o.setString(kKeyPath, p.bookPath);
  o.setInt(kKeySpine, p.spine);
  o.setInt(kKeyBlock, p.block);
  o.setInt(kKeyLine, p.line);
  o.setInt(kKeyBytes, static_cast<int64_t>(p.bookBytes));
  o.setInt(kKeyPpem, p.ppem);
  o.setInt(kKeyColumnW, p.columnW);
  o.setInt(kKeyPercent, p.percent);
  return o.dump();
}

bool parsePosition(std::string_view text, ReadingPosition& out) {
  JsonObject o;
  if (!o.parse(text)) return false;

  // THE VERSION IS A GATE, not a field to read leniently. An older record whose
  // fields meant something else must read as "no position" rather than as a
  // position in the wrong place -- the same reason the session record gates on its
  // own version instead of guessing.
  int64_t version = 0;
  if (!o.getInt(kKeyVersion, version) || version != kPositionVersion) return false;

  ReadingPosition p;
  // The path is REQUIRED: without it fitOf cannot tell a hash collision from a
  // match, so a record that omits it is not usable as a record.
  if (!o.getString(kKeyPath, p.bookPath) || p.bookPath.empty()) return false;

  int64_t v = 0;
  if (!o.getInt(kKeySpine, v)) return false;
  p.spine = nonNegative(v);
  // block/line/geometry are OPTIONAL, defaulting to "not known". A record with a
  // spine and nothing else is a legitimate coarse position -- it is exactly what a
  // Rebound restore writes back -- so demanding them would refuse a file this code
  // itself produces.
  if (o.getInt(kKeyBlock, v)) p.block = nonNegative(v);
  if (o.getInt(kKeyLine, v)) p.line = nonNegative(v);
  if (o.getInt(kKeyBytes, v)) p.bookBytes = static_cast<uint32_t>(v < 0 ? 0 : v);
  if (o.getInt(kKeyPpem, v)) p.ppem = nonNegative(v);
  if (o.getInt(kKeyColumnW, v)) p.columnW = nonNegative(v);
  // Clamped, not trusted: this one is read straight onto a screen.
  if (o.getInt(kKeyPercent, v)) p.percent = v < 0 ? 0 : (v > 100 ? 100 : static_cast<int>(v));

  out = p;
  return true;
}

std::string statePathFor(std::string_view bookPath) {
  // FNV-1a, 32-bit. Chosen for being four lines and having no state to get wrong,
  // not for strength: nothing here is adversarial, and a collision is handled by
  // the stored path rather than made impossible.
  uint32_t h = 2166136261u;
  for (char c : bookPath) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  static const char kHex[] = "0123456789abcdef";
  std::string name(8, '0');
  for (int i = 7; i >= 0; --i) {
    name[static_cast<size_t>(i)] = kHex[h & 0xfu];
    h >>= 4;
  }
  return "/.reader/state/" + name + ".json";
}

}  // namespace reader
