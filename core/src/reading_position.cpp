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
constexpr const char* kKeyChapter = "chapter";
constexpr const char* kKeyFinished = "finished";
// The anchor's three. Written only when there IS one, so a record for a reader with
// nowhere to go back to is byte-identical to one written before the anchor existed --
// which is what keeps the shell's "skip a write that changes nothing" true for the
// common case.
constexpr const char* kKeyAnchorSpine = "anchor_spine";
constexpr const char* kKeyAnchorBlock = "anchor_block";
constexpr const char* kKeyAnchorLine = "anchor_line";

// A negative index is not a position, and a file is free to claim one. Clamped at
// the boundary rather than refused: the rest of the record is still usable, which is
// the same call loadSettings makes for an out-of-range value.
int nonNegative(int64_t v) { return v < 0 ? 0 : static_cast<int>(v > 0x7fffffff ? 0 : v); }

}  // namespace

bool ReadingPosition::operator==(const ReadingPosition& o) const {
  return bookPath == o.bookPath && spine == o.spine && block == o.block && line == o.line &&
         bookBytes == o.bookBytes && ppem == o.ppem && columnW == o.columnW &&
         percent == o.percent && chapter == o.chapter && finished == o.finished &&
         anchorSpine == o.anchorSpine && anchorBlock == o.anchorBlock &&
         anchorLine == o.anchorLine;
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
  // A FINISHED BOOK OPENS AT THE FRONT, whatever its record still supports, and this
  // is checked BEFORE the fit because it is a different question. `fitOf` asks how
  // much of the record still APPLIES -- a re-export or a type-size change is about
  // validity. This asks whether it should be USED at all, and the answer for a book
  // the reader has declared finished is no: reopening one is re-reading it, not
  // resuming it. Reported from a device, where a DONE book reopened somewhere in its
  // last chapter.
  //
  // ANSWERED AS `any = false`, which is the same answer Unusable gets, because the
  // caller's response to both is identical -- start at the beginning -- and
  // loadPosition's own header already takes that line about its several false cases:
  // "Distinguishing them would be a distinction with no consequence."
  //
  // THE ANCHOR GOES WITH IT, by falling through to the same early return. A way back
  // to where the reader was is meaningless when they are no longer there.
  //
  // IT UN-MARKS ITSELF, so this is not a state the reader can get stuck in: the next
  // save from the Reader builds a fresh record (see saveReadingPosition), so reading
  // on from the front clears `finished` and the book resumes normally from then on.
  if (saved.finished) return r;
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
      // ONLY HERE. Relaid and Rebound both fall through above without touching the
      // anchor fields, which leaves `anchorAny` false -- so the drop is the default
      // rather than three more lines that have to remember to zero it.
      if (saved.hasAnchor()) {
        r.anchorAny = true;
        r.anchorSpine = saved.anchorSpine;
        r.anchorCursor = Cursor{saved.anchorBlock, saved.anchorLine};
      }
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
  o.setString(kKeyChapter, p.chapter);
  // ABSENT RATHER THAN false, exactly as the anchor's three keys are absent rather
  // than -1: an unfinished record is then byte-identical to one written before this
  // field existed, so writeIfChanged still answers Unchanged and no card is rewritten.
  if (p.finished) o.setBool(kKeyFinished, true);
  // ABSENT RATHER THAN -1 when there is no anchor. Three keys that appear only for a
  // reader who has somewhere to go back to, so the common record does not grow and
  // an unchanged save stays byte-identical.
  if (p.hasAnchor()) {
    o.setInt(kKeyAnchorSpine, p.anchorSpine);
    o.setInt(kKeyAnchorBlock, p.anchorBlock);
    o.setInt(kKeyAnchorLine, p.anchorLine);
  }
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
  // OPTIONAL, because every sidecar written before this field existed lacks it -- and a
  // position is still perfectly usable without a chapter name. The row draws blank.
  o.getString(kKeyChapter, p.chapter);
  // OPTIONAL, and its absence is "not finished" rather than a parse failure -- every
  // sidecar written before this field existed lacks it, and kPositionVersion
  // deliberately did not move so those records must still load.
  o.getBool(kKeyFinished, p.finished);
  // OPTIONAL AS A GROUP, and the SPINE is what decides. A record from a firmware that
  // did not know about anchors has none of the three; one written by a reader with no
  // way back has none either. Both must read as "no anchor" rather than as an anchor
  // at spine 0 -- which is a real page, so a defaulted zero would send `Up` to the
  // front of the book.
  if (o.getInt(kKeyAnchorSpine, v) && v >= 0) {
    p.anchorSpine = nonNegative(v);
    if (o.getInt(kKeyAnchorBlock, v)) p.anchorBlock = nonNegative(v);
    if (o.getInt(kKeyAnchorLine, v)) p.anchorLine = nonNegative(v);
  }

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
