// EVERY PAGE OF A REAL BOOK, TURNED FORWARD THE WAY A READER TURNS IT.
//
// Reported off the device: turning the page sometimes shows the SAME page again with
// the page number advanced. That is invisible to every existing test, because they
// drive short fixtures and assert page COUNTS and boundaries rather than comparing
// one page's text against the one before it -- a duplicate has the right count.
//
// So this walks a real EPUB through the real ReaderScreen, one Gesture::Next at a
// time, and reports two things a duplicate would show up as:
//
//   * two CONSECUTIVE pages whose laid-out text is identical, and
//   * two consecutive entries in the page index that record the SAME start cursor,
//     which is the mechanism that would produce the first.
//
//   build: cmake --build build --target paging_probe
//   run:   build/paging_probe book.epub [--chapters N] [--verbose]
// A STRAIGHT FORWARD WALK DOES NOT REPRODUCE IT, and that is the finding rather
// than a dead end: the device runs three IDLE JOBS in quiet windows between turns
// -- completeIndex, restreamAtCurrentPage and warmPageRing -- and every one of them
// rewinds the stream. A reader who pauses triggers them; a probe that turns pages
// back to back never does. So `--idle` interleaves them in the shell's own order.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "reader/book.h"
#include "reader/host_fs.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/screen_reader.h"
#include "reader/settings.h"
#include "reader/theme_quiet.h"
#include "reader/fontset.h"

namespace {

// A PRESS LANDING IN THE MIDDLE OF AN IDLE WALK, which is what `--idle` alone does
// not model: the shell hands every one of the three walks a stop predicate answered
// from the input queue, so on a device that is being READ they are interrupted far
// more often than they complete. `kInterruptAfter` calls, then stop.
struct Interrupter {
  int budget = 0;
  int calls = 0;
  int fired = 0;
};
bool stopAfter(void* ctx) {
  auto* it = static_cast<Interrupter*>(ctx);
  if (it->budget < 0) return false;
  if (++it->calls > it->budget) {
    ++it->fired;
    return true;
  }
  return false;
}


std::string pageText(const reader::Page& p) {
  std::string s;
  for (const reader::LaidLine& l : p.lines) {
    s += l.text;
    s += '\n';
  }
  return s;
}

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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: paging_probe book.epub [--chapters N] [--verbose]\n");
    return 2;
  }
  int chapterLimit = 1 << 30;
  bool verbose = false;
  bool idle = false;
  bool whole = false;
  int interrupt = -1;  // -1 = never interrupt
  bool jobs[3] = {true, true, true};  // count, restream, warm
  bool trace = false;
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--chapters") == 0 && i + 1 < argc) chapterLimit = std::atoi(argv[++i]);
    if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
    if (std::strcmp(argv[i], "--trace") == 0) trace = true;
    if (std::strcmp(argv[i], "--idle") == 0) idle = true;
    if (std::strcmp(argv[i], "--whole") == 0) whole = true;
    if (std::strcmp(argv[i], "--interrupt") == 0 && i + 1 < argc) interrupt = std::atoi(argv[++i]);
    // Which of the three quiet-window walks to run, so a reproduction can name one.
    if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
      const char* w = argv[++i];
      jobs[0] = std::strcmp(w, "count") == 0;
      jobs[1] = std::strcmp(w, "restream") == 0;
      jobs[2] = std::strcmp(w, "warm") == 0;
    }
  }

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

  // --- THE WHOLE BOOK, CONTINUOUSLY, WHICH IS WHAT A READER DOES ------------
  //
  // The per-chapter walk below builds a fresh ReaderScreen each time and so never
  // crosses a chapter. A reader crosses, and openChapterAt is a different landing
  // from the constructor's -- so a duplicate that lives at a crossing is invisible
  // to it.
  if (whole) {
    reader::ReaderScreen scr(fs, ob, 0, &body);
    reader::QuietTheme theme;
    reader::FontSet fonts;
    reader::PageMetrics m;
    reader::Settings st;
    theme.readerMetrics(480, 800, fonts, body, st, m);
    scr.setMetrics(m);
    std::string prev = pageText(scr.page());
    int prevC = scr.chapterIndex(), prevP = scr.pageIndex();
    int turns = 0, dups = 0, interrupted = 0;
    for (int guard = 0; guard < 40000; ++guard) {
      if (idle) {
        Interrupter it{interrupt, 0, 0};
        if (jobs[0] && scr.indexPending()) scr.completeIndex(stopAfter, &it);
        it.calls = 0;
        if (jobs[1] && !scr.hasLiveStream()) scr.restreamAtCurrentPage(stopAfter, &it);
        it.calls = 0;
        if (jobs[2]) scr.warmPageRing(stopAfter, &it);
        interrupted += it.fired;
      }
      scr.onGesture(reader::GestureEvent{reader::Gesture::Next, false, 1});
      if (scr.chapterIndex() == prevC && scr.pageIndex() == prevP) break;  // the book ended
      ++turns;
      const std::string now = pageText(scr.page());
      // WITHIN ONE CHAPTER ONLY. Across a crossing this compared two different
      // chapters' pages, and a book with a run of one-page navigation chapters each
      // reading "back" is not a book with duplicated pages -- 15 false positives on
      // one Gutenberg title, which is exactly the kind of noise that gets a real
      // finding dismissed.
      if (now == prev && !now.empty() && scr.chapterIndex() == prevC) {
        ++dups;
        std::printf("DUP spine %d p%d -> p%d, identical (%zu bytes)\n", prevC, prevP,
                    scr.pageIndex(), now.size());
        if (verbose) std::printf("    |%.90s|\n", now.c_str());
      }
      if (trace) {
        const reader::Cursor c2 = scr.currentCursor();
        std::printf("  turn spine=%d at=%-4d count=%-5d cursor=(%d,%d) live=%d pending=%d%s\n",
                    scr.chapterIndex(), scr.pageIndex(), scr.pageCount(), c2.block, c2.line,
                    scr.hasLiveStream() ? 1 : 0, scr.indexPending() ? 1 : 0,
                    now == prev ? "   <== SAME TEXT AS PREVIOUS" : "");
      }
      prev = now;
      prevC = scr.chapterIndex();
      prevP = scr.pageIndex();
    }
    std::printf("\nwhole book%s: %d turns, %d walk(s) interrupted, %d duplicate page(s)\n",
                idle ? " (+idle)" : "", turns, interrupted, dups);
    return dups == 0 ? 0 : 1;
  }

  const int chapters = ob.chapterCount() < chapterLimit ? ob.chapterCount() : chapterLimit;
  int dupPages = 0, dupCursors = 0, totalPages = 0, chaptersWalked = 0;

  for (int c = 0; c < chapters; ++c) {
    reader::ReaderScreen scr(fs, ob, c, &body);
    reader::QuietTheme theme;
    reader::FontSet fonts;
    reader::PageMetrics m;
    reader::Settings s;
    theme.readerMetrics(480, 800, fonts, body, s, m);
    scr.setMetrics(m);
    if (scr.pageCount() == 0) continue;
    ++chaptersWalked;

    // Every page of THIS chapter, forward, stopping at the chapter's end rather than
    // crossing -- a crossing is a different mechanism and would hide which chapter a
    // duplicate belongs to.
    std::string prev = pageText(scr.page());
    int prevIndex = scr.pageIndex();
    // THE INDEX'S OWN KEY, through the public accessor: currentCursor() IS
    // starts_[at_], and two consecutive pages recording the same start is the
    // mechanism a duplicate page would be a symptom of.
    reader::Cursor prevCursor = scr.currentCursor();
    const int startChapter = scr.chapterIndex();
    for (int guard = 0; guard < 5000; ++guard) {
      // THE QUIET WINDOW, in shell/src/main.cpp's own order: the count first, then
      // the restream (only where no stream stands), then the warm. Uninterrupted,
      // which is the reader who put the device down rather than the one skimming.
      if (idle) {
        if (scr.indexPending()) scr.completeIndex();
        if (!scr.hasLiveStream()) scr.restreamAtCurrentPage();
        scr.warmPageRing();
      }
      const reader::GestureEvent g{reader::Gesture::Next, false, 1};
      scr.onGesture(g);
      if (scr.chapterIndex() != startChapter) break;  // crossed out of this chapter
      if (scr.pageIndex() == prevIndex) break;        // nothing moved: the end
      ++totalPages;
      const std::string now = pageText(scr.page());
      if (now == prev && !now.empty()) {
        ++dupPages;
        std::printf("DUP PAGE  spine=%d page %d -> %d identical (%zu bytes)\n", c, prevIndex,
                    scr.pageIndex(), now.size());
        if (verbose) std::printf("    |%.90s|\n", now.c_str());
      }
      const reader::Cursor cur = scr.currentCursor();
      if (cur.block == prevCursor.block && cur.line == prevCursor.line) {
        ++dupCursors;
        std::printf("DUP CURSOR spine=%d page %d and %d both start at (block %d, line %d)\n", c,
                    prevIndex, scr.pageIndex(), cur.block, cur.line);
      }
      prev = now;
      prevCursor = cur;
      prevIndex = scr.pageIndex();
    }

  }

  std::printf("\n%s\n  chapters walked %d, pages turned %d\n"
              "  duplicate consecutive PAGES  : %d\n"
              "  duplicate consecutive CURSORS: %d\n",
              argv[1], chaptersWalked, totalPages, dupPages, dupCursors);
  return dupPages == 0 && dupCursors == 0 ? 0 : 1;
}
