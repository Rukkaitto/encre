#include "reader/name_extracts.h"

#include <algorithm>
#include <cstdio>

namespace reader {
namespace {

constexpr char kSep = '\t';

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Back up to a UTF-8 boundary. A window cut mid-sequence renders as a notdef box,
// which is worse than a shorter window.
size_t snapBack(std::string_view s, size_t i) {
  while (i > 0 && i < s.size() && isContinuation(s[i])) --i;
  return i;
}
size_t snapForward(std::string_view s, size_t i) {
  while (i < s.size() && isContinuation(s[i])) ++i;
  return i;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void appendInt(std::string& out, int v) {
  char buf[16];
  const int n = std::snprintf(buf, sizeof buf, "%d", v);
  out.append(buf, static_cast<size_t>(n < 0 ? 0 : n));
}

bool parseInt(std::string_view s, int& out) {
  if (s.empty()) return false;
  int v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

std::string partPath(const std::string& dir, int spine, int n) {
  std::string p = dir + "/";
  appendInt(p, spine);
  p += '-';
  appendInt(p, n);
  return p;
}

}  // namespace

std::string extractWindow(std::string_view sentence, size_t offset, size_t runBytes,
                          size_t budget) {
  if (sentence.empty()) return {};
  if (offset > sentence.size()) offset = sentence.size();
  if (offset + runBytes > sentence.size()) runBytes = sentence.size() - offset;
  if (sentence.size() <= budget) {
    // A SHORT SENTENCE YIELDS A SHORT EXTRACT, and that is the second row height on
    // the board rather than a defect: there is no 64th byte to show, and padding it
    // would be drawing blank space to make the list look regular.
    size_t b = 0, e = sentence.size();
    while (b < e && isSpace(sentence[b])) ++b;
    while (e > b && isSpace(sentence[e - 1])) --e;
    return std::string(sentence.substr(b, e - b));
  }
  // CENTRED ON THE NAME. If the run alone is over budget there is nothing to centre,
  // so the window starts at the run and takes what it can.
  size_t before = 0;
  if (runBytes < budget) before = (budget - runBytes) / 2;
  size_t start = offset > before ? offset - before : 0;
  size_t end = start + budget;
  if (end > sentence.size()) {
    end = sentence.size();
    start = end > budget ? end - budget : 0;
  }
  start = snapBack(sentence, start);
  end = snapForward(sentence, end);
  // TRIM TO WHOLE WORDS at both ends, but never into the run itself: a window that
  // cut the name in half would defeat the centring.
  if (start > 0) {
    size_t s = start;
    while (s < offset && !isSpace(sentence[s])) ++s;
    while (s < offset && isSpace(sentence[s])) ++s;
    if (s < offset) start = s;
  }
  const size_t runEnd = offset + runBytes;
  if (end < sentence.size()) {
    size_t e = end;
    while (e > runEnd && !isSpace(sentence[e - 1])) --e;
    while (e > runEnd && isSpace(sentence[e - 1])) --e;
    if (e > runEnd) end = e;
  }
  while (start < end && isSpace(sentence[start])) ++start;
  while (end > start && isSpace(sentence[end - 1])) --end;
  return std::string(sentence.substr(start, end - start));
}

// --- The part writer -----------------------------------------------------------

ExtractPartWriter::ExtractPartWriter(FileSystem& fs, std::string dir, int spine,
                                     size_t partBytes)
    : fs_(fs), dir_(std::move(dir)), spine_(spine), partBytes_(partBytes) {}

void ExtractPartWriter::add(std::string_view run, int block, std::string_view extract) {
  if (failed_) return;
  std::string line(run);
  line += kSep;
  appendInt(line, block);
  line += kSep;
  // A NEWLINE OR A TAB IN AN EXTRACT WOULD SPLIT THE RECORD. Block text can carry a
  // newline -- an EPUB's source is wrapped -- so they become spaces here rather than
  // being escaped: this is display text and a line break in it is not information.
  for (const char c : extract) line += (c == '\n' || c == '\r' || c == kSep) ? ' ' : c;
  line += '\n';
  if (!buf_.empty() && buf_.size() + line.size() > partBytes_) {
    if (!flush()) return;
  }
  buf_ += line;
}

bool ExtractPartWriter::flush() {
  if (buf_.empty()) return true;
  if (!fs_.mkdirs(dir_)) {
    failed_ = true;
    return false;
  }
  if (!fs_.writeAll(partPath(dir_, spine_, parts_), buf_)) {
    failed_ = true;
    return false;
  }
  ++parts_;
  buf_.clear();
  return true;
}

bool ExtractPartWriter::finish() {
  if (failed_) return false;
  return flush();
}

// --- Reading back --------------------------------------------------------------

bool readExtracts(FileSystem& fs, const std::string& dir, int spine, std::string_view run,
                  std::vector<StoredExtract>& out) {
  out.clear();
  // EVERY PART OF THE CHAPTER, IN ORDER, and it stops at the first one missing:
  // parts are written 0, 1, 2 without gaps, so an absent `n` means there is no more.
  for (int n = 0;; ++n) {
    std::string text;
    if (!fs.readAll(partPath(dir, spine, n), text)) break;
    size_t pos = 0;
    while (pos < text.size()) {
      const size_t nl = text.find('\n', pos);
      const size_t end = (nl == std::string::npos) ? text.size() : nl;
      const std::string_view line(text.data() + pos, end - pos);
      pos = (nl == std::string::npos) ? text.size() : nl + 1;
      const size_t t1 = line.find(kSep);
      if (t1 == std::string_view::npos) continue;
      if (line.substr(0, t1) != run) continue;
      const size_t t2 = line.find(kSep, t1 + 1);
      if (t2 == std::string_view::npos) continue;
      StoredExtract e;
      e.run.assign(run);
      if (!parseInt(line.substr(t1 + 1, t2 - t1 - 1), e.block)) continue;
      e.text.assign(line.substr(t2 + 1));
      out.push_back(std::move(e));
    }
  }
  return true;
}

// --- The capture ---------------------------------------------------------------

ExtractCapture::ExtractCapture(std::vector<std::string> wanted, std::vector<int> quota,
                               ExtractPartWriter& out)
    : wanted_(std::move(wanted)), quota_(std::move(quota)), out_(out) {
  quota_.resize(wanted_.size(), 0);
  kept_.assign(wanted_.size(), 0);
}

void ExtractCapture::onRun(std::string_view run, int blockInChapter, std::string_view sentence,
                           size_t offset, bool midSentence) {
  (void)midSentence;  // a first appearance may legitimately open a sentence
  // BINARY SEARCH, because `wanted` is the admitted set sorted by text and a chapter
  // is ~4,000 occurrences: a linear scan over ~400 wanted runs is the same arithmetic
  // the scanner's own table refused.
  const auto it = std::lower_bound(wanted_.begin(), wanted_.end(), run);
  if (it == wanted_.end() || *it != run) return;
  const size_t i = static_cast<size_t>(it - wanted_.begin());
  if (kept_[i] >= quota_[i]) return;
  ++kept_[i];
  out_.add(run, blockInChapter, extractWindow(sentence, offset, run.size()));
}

}  // namespace reader
