// WHAT THE PERCENTAGE SAYS AGAINST WHERE THE READER ACTUALLY IS.
//
// Reported off the device: an article whose one real chapter is surrounded by
// one-page wrappers read 83% at the chapter's halfway point. `progressPercent` is a
// fraction of the book's INFLATED BYTES, and two things make that not a reading
// position -- the inflater runs a whole 16 KB chunk ahead of the page on screen, and
// everything the layout drops (markup, styles, images) is counted as if it were read.
//
// So this walks a real EPUB page by page through the real ReaderScreen and prints
// the shipped percentage beside a reference one: pages laid out, over the book's own
// total pages, which is what the reader is looking at.
//
//   build: cmake --build build --target progress_probe
//   run:   build/progress_probe book.epub [--verbose]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "reader/book.h"
#include "reader/host_fs.h"
#include "reader/layout.h"
#include "reader/reading_store.h"
#include "reader/scalablefont.h"
#include "reader/screen_reader.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"
#include "reader/fontset.h"

namespace {

std::vector<uint8_t> slurp(const std::string& path) {
  std::vector<uint8_t> out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return out;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out.resize(static_cast<size_t>(n));
  if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  std::fclose(f);
  return out;
}

struct Stop {
  int at;
  int calls = 0;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: progress_probe book.epub [--verbose]\n");
    return 2;
  }
  bool verbose = false;
  for (int i = 2; i < argc; ++i)
    if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;

  std::vector<uint8_t> face = slurp(std::string(ASSETS_DIR) + "/built/literata_body.ttf");
  reader::ScalableFont body;
  if (!body.init(face.data(), face.size(), reader::kBodyPpem)) {
    std::fprintf(stderr, "no body face\n");
    return 2;
  }

  reader::HostFileSystem fs("/");
  reader::OpenedBook ob;
  const char* why = "";
  if (!reader::openBook(fs, argv[1], ob, &why)) {
    std::printf("cannot open: %s\n", why);
    return 1;
  }

  reader::QuietTheme theme;
  reader::FontSet fonts;
  reader::PageMetrics m;
  reader::Settings st;
  theme.readerMetrics(480, 800, fonts, body, st, m);

  // PASS 1: every chapter's page count, so the reference fraction has a denominator.
  std::vector<int> pages(static_cast<size_t>(ob.chapterCount()), 0);
  int totalPages = 0;
  for (int c = 0; c < ob.chapterCount(); ++c) {
    reader::ReaderScreen scr(fs, ob, c, &body);
    scr.setMetrics(m);
    // A CHAPTER WITH NO PAGES IS SKIPPED BY THE CONSTRUCTOR -- a cover is one `<img>`
    // and `document.h` drops images -- so a screen opened at a cover lands on the
    // chapter AFTER it, and taking its count for `c` credits the cover with the whole
    // of the first real chapter. That read as the percentage being 40pp low at the top
    // of a book; it was this line.
    if (scr.chapterIndex() != c) continue;
    scr.completeIndex(nullptr, nullptr);
    pages[static_cast<size_t>(c)] = scr.pageCount();
    totalPages += scr.pageCount();
  }
  if (totalPages == 0) {
    std::printf("no pages\n");
    return 1;
  }

  std::printf("%s\n  spine=%d pages=%d\n", ob.title.c_str(), ob.chapterCount(), totalPages);
  uint64_t bytes = 0;
  for (const reader::ChapterSpan& s : ob.chapters) bytes += s.uncompressedSize;
  for (int c = 0; c < ob.chapterCount(); ++c)
    std::printf("    spine %2d  %7u B  %4d pages\n", c,
                ob.chapters[static_cast<size_t>(c)].uncompressedSize, pages[static_cast<size_t>(c)]);
  std::printf("  total %llu B\n", static_cast<unsigned long long>(bytes));

  // PASS 2: turn every page, comparing the shipped percentage with pages-so-far.
  reader::ReaderScreen scr(fs, ob, 0, &body);
  scr.setMetrics(m);
  int before = 0;  // pages in chapters before the open one
  int worst = 0;
  int worstAt = 0, worstShown = 0, worstTrue = 0;
  int turns = 0;
  int prevC = -1;
  for (int guard = 0; guard < 40000; ++guard) {
    const int c = scr.chapterIndex();
    if (c != prevC) {
      before = 0;
      for (int i = 0; i < c; ++i) before += pages[static_cast<size_t>(i)];
      prevC = c;
    }
    const int shown = reader::progressPercent(ob, c, scr.pageIndex() + 1, scr.pageCount(),
                                              scr.chapterBytesRead());
    const int ref = ((before + scr.pageIndex() + 1) * 100 + totalPages / 2) / totalPages;
    if (verbose)
      std::printf("  spine=%-2d p%-4d  shown=%3d%%  pages=%3d%%  (bytes=%u/%u)\n", c,
                  scr.pageIndex() + 1, shown, ref, scr.chapterBytesRead(),
                  ob.chapters[static_cast<size_t>(c)].uncompressedSize);
    const int d = shown > ref ? shown - ref : ref - shown;
    if (d > worst) {
      worst = d;
      worstAt = turns;
      worstShown = shown;
      worstTrue = ref;
    }
    const int wasC = c, wasP = scr.pageIndex();
    scr.onGesture(reader::GestureEvent{reader::Gesture::Next, false, 1});
    if (scr.chapterIndex() == wasC && scr.pageIndex() == wasP) break;
    ++turns;
  }
  std::printf("\n  worst disagreement %dpp at turn %d: shown %d%%, pages say %d%%\n", worst,
              worstAt, worstShown, worstTrue);
  return 0;
}
