// EVERY BOOK IN THE CORPUS, THROUGH THE REAL OPEN PATH.
//
// Not in ctest, and deliberately, for the reason name_probe is not: it needs real
// books, and the repo's generated EPUBs are 1,400-word stubs. It runs `openBook` and
// walks every chapter, so what it measures is what the device does, minus the card
// and the heap.
//
// TWO METRICS, because one of them lies. "Did it open" scores a book that yields
// three of its ninety-two chapters as a success; `unreadable` and `truncated` are
// what stop that being reported as a win. `truncated` reads ChapterReader::ok(),
// which has always known and which nothing in the firmware asks -- next() returning
// false is also how a chapter ends normally, which is exactly how `Dark Plagueis`
// lost 177 of its 183 chapters in silence.
//
// AND A THIRD, ADDED FOR #90: HOW BIG THE BLOCKS ARE. `kMaxBlockBytes` bounds one
// block's `std::string`, and that bound is the largest contiguous allocation this
// layer makes -- so the number that decides it is the distribution of block sizes a
// real book produces, not a round figure. `maxBlock` and `big` are what say where a
// cap can be put without cutting anything a book actually wrote; run it with the cap
// raised out of the way and the sizes reported ARE the natural ones, because nothing
// was cut.
//
// `splits` is `ChapterReader::blocksSplit()` summed over the chapters -- the cuts the
// cap in force actually made, measured rather than derived from `big`.
//
//   build: cmake --build build --target corpus_probe
//   run:   build/corpus_probe book1.epub book2.epub ... > run.jsonl

#include <cstdio>
#include <string>
#include <vector>

#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/host_fs.h"

namespace {

// The floor for `big`. Not a cap candidate and deliberately below every one of them:
// a block under this cannot be cut by any cap worth considering, so listing it would
// be noise. Le Fléau's largest natural block is 4,406 bytes, so real prose does reach
// this and the list is not empty for a novel.
constexpr size_t kBigBlock = 2048;

// JSON needs its quotes and backslashes escaped, and a book title is arbitrary bytes
// off somebody's card -- including control characters.
std::string esc(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (const unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char b[8];
      std::snprintf(b, sizeof(b), "\\u%04x", c);
      out += b;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

void probe(reader::FileSystem& fs, const char* path) {
  reader::OpenedBook book;
  const char* reason = "";
  if (!reader::openBook(fs, path, book, &reason)) {
    std::printf("{\"path\":\"%s\",\"opened\":false,\"reason\":\"%s\"}\n", esc(path).c_str(),
                esc(reason).c_str());
    return;
  }
  int unreadable = 0, truncated = 0, blocks = 0;
  long bytes = 0;
  size_t maxBlock = 0, splits = 0;
  // EVERY BLOCK AT OR OVER kBigBlock, IN FULL, so a candidate cap can be priced in
  // Python without a rebuild -- the cuts a cap C makes over a natural block of N bytes
  // are arithmetic once N is known. A LIST rather than a histogram because a bucket
  // boundary is a decision, and the whole point of this run is not to bake one in.
  // 2 KB is well under any cap worth considering and well over a paragraph, so the
  // list stays short: real prose contributes almost nothing to it.
  std::vector<size_t> big;
  std::string firstError;
  reader::ChapterReader cr;
  for (int c = 0; c < book.chapterCount(); ++c) {
    const reader::ChapterLocation loc = book.locate(c);
    if (loc.compressedSize == 0) {
      ++unreadable;
      continue;
    }
    if (!cr.begin(fs, loc)) {
      ++unreadable;
      if (firstError.empty()) firstError = cr.error();
      continue;
    }
    reader::Block b;
    while (cr.next(b)) {
      ++blocks;
      bytes += static_cast<long>(b.text.size());
      if (b.text.size() > maxBlock) maxBlock = b.text.size();
      if (b.text.size() >= kBigBlock) big.push_back(b.text.size());
    }
    // BEFORE ok(), because a refused chapter still cut whatever it cut, and after the
    // walk rather than per block: this is a running total on the reader, not an event.
    // One forward walk per chapter and no rewind, so summing is exact -- see
    // ChapterReader::blocksSplit, which a rewind zeroes.
    splits += cr.blocksSplit();
    if (!cr.ok()) {
      ++truncated;
      if (firstError.empty()) firstError = cr.error();
    }
  }
  std::string bigList;
  for (const size_t n : big) {
    if (!bigList.empty()) bigList += ',';
    bigList += std::to_string(n);
  }
  std::printf("{\"path\":\"%s\",\"opened\":true,\"title\":\"%s\",\"author\":\"%s\","
              "\"spine\":%d,\"unreadable\":%d,\"truncated\":%d,\"blocks\":%d,"
              "\"textBytes\":%ld,\"maxBlock\":%zu,\"splits\":%zu,\"bigFrom\":%zu,"
              "\"big\":[%s],\"firstError\":\"%s\"}\n",
              esc(path).c_str(), esc(book.title).c_str(), esc(book.author).c_str(),
              book.chapterCount(), unreadable, truncated, blocks, bytes, maxBlock,
              splits, kBigBlock, bigList.c_str(), esc(firstError).c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: corpus_probe <book.epub> [more.epub ...] > run.jsonl\n");
    return 2;
  }
  reader::HostFileSystem fs("/");
  for (int i = 1; i < argc; ++i) probe(fs, argv[i]);
  return 0;
}
