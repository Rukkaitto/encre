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
//   build: cmake --build build --target corpus_probe
//   run:   build/corpus_probe book1.epub book2.epub ... > run.jsonl

#include <cstdio>
#include <string>

#include "reader/book.h"
#include "reader/chapter.h"
#include "reader/host_fs.h"

namespace {

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
    }
    if (!cr.ok()) {
      ++truncated;
      if (firstError.empty()) firstError = cr.error();
    }
  }
  std::printf("{\"path\":\"%s\",\"opened\":true,\"title\":\"%s\",\"author\":\"%s\","
              "\"spine\":%d,\"unreadable\":%d,\"truncated\":%d,\"blocks\":%d,"
              "\"textBytes\":%ld,\"firstError\":\"%s\"}\n",
              esc(path).c_str(), esc(book.title).c_str(), esc(book.author).c_str(),
              book.chapterCount(), unreadable, truncated, blocks, bytes,
              esc(firstError).c_str());
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
