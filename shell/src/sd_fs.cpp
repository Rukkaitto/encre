#include "sd_fs.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <iterator>
#include <memory>
#include <new>
#include <utility>

namespace {

// One recursive mutex for the display/SD bus, created on first use. A
// function-local static is guarded by the runtime, and the first taker is the
// main task in setup() long before anything could contend -- but the guard is
// what makes that a fact rather than an assumption.
SemaphoreHandle_t busMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
  return m;
}

// Absolute, '/'-separated, no repeated and no trailing separator; "/" is the
// root.
//
// This is the THIRD copy of this function -- core/src/host_fs.cpp and
// test/unit/fake_fs.h have the other two -- and the duplication is deliberate for
// now: the three live in three build worlds (desktop-only TU, test scaffolding,
// Arduino) and what keeps them honest is behavioural, not textual. The
// "a redundant or trailing separator addresses the same thing" clause in
// reader/fs_contract.h runs against all three, so a copy that drifts fails a
// test rather than quietly addressing a different file. It matters here because
// SdFat, like POSIX stat(), will not resolve "file/" as the file.
std::string normalise(std::string_view path) {
  std::string out = "/";
  for (char c : path) {
    if (c == '/') {
      if (out.back() != '/') out.push_back('/');
    } else {
      out.push_back(c);
    }
  }
  if (out.size() > 1 && out.back() == '/') out.pop_back();
  return out;
}

// "" for the root, which has no parent. `normalised` is already normalised.
std::string parentOf(const std::string& normalised) {
  if (normalised == "/") return "";
  const size_t slash = normalised.rfind('/');
  return slash == 0 ? "/" : normalised.substr(0, slash);
}

}  // namespace

SpiBusGuard::SpiBusGuard() {
  SemaphoreHandle_t m = busMutex();
  if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY);
}

SpiBusGuard::~SpiBusGuard() {
  SemaphoreHandle_t m = busMutex();
  if (m) xSemaphoreGiveRecursive(m);
}

void SdFileSystem::noteCardGone(const char* where) {
  if (!live_) return;
  live_ = false;
  Serial.printf("[sd] card stopped answering during %s; storage is now unmounted\n", where);
  Serial.flush();
}

bool SdFileSystem::mount() {
  SpiBusGuard bus;  // begin() drives the display's CS line; see sd_fs.h
  live_ = SdMan.begin();
  Serial.printf("[sd] mount %s\n", live_ ? "ok" : "FAILED (no card, or it would not mount)");
  Serial.flush();
  return live_;
}

// What mounted() can and cannot promise.
//
// SDCardManager::ready() alone is not an answer: it latches whatever begin()
// concluded and keeps saying true after the card is pulled, and every method here
// promises to fail when there is no storage.
//
// What is NOT available to do better: SdFat's SdCard::status() -- one CMD13, the
// one call that would answer "is the card still there" cheaply -- sits behind
// SDCardManager's private `sd` member, and freeink-sdk/ is a submodule this
// project does not edit. Of the public surface, exists()/open() on an absent path
// fails identically whether the path is missing or the card is, and sdUsedBytes()
// rescans the FAT -- expensive, but unambiguously real, which is why deepProbe()
// uses it as the slow backstop.
//
// So mounted() is ready() AND a liveness flag, and the flag is maintained from
// three directions:
//
//   * FEEDBACK. Every operation below that gets a failure only a dead card
//     explains -- the root directory failing to open, a directory read setting
//     READ_ERROR, a file read stopping short of its own declared size, a write
//     that does not take -- calls noteCardGone() and clears it. So a card pulled
//     at runtime is reported the moment anything actually needs it, which is when
//     it matters, rather than never.
//   * probe(), the fast on-demand check -- a byte read from a real file, chosen
//     so the access cannot be served out of SdFat's single 512-byte sector cache
//     (see ProbeTarget in sd_fs.h). Deliberately not called from here: mounted()
//     is checked at the top of every method (writeAll checks it three times
//     through mkdirs), and a probe is real traffic on the bus the panel shares.
//   * deepProbe(), the slow backstop -- a whole-FAT scan, which no cache can
//     serve. It exists because the fast probe's guarantee is an argument about
//     cache geometry and this one is not an argument at all.
//
// The honest limit: mounted() is still not a card-detect -- there is no
// card-detect GPIO in the Xteink profiles at all (BoardConfig's SdPins has no
// detect field), and CMD13 is private to SDCardManager. "The card was there and
// nothing has since told us otherwise" is the strongest claim this SDK supports,
// and it is the claim this makes. What changed is how hard probe() tries: see
// ProbeTarget in sd_fs.h and deepProbe() below.
bool SdFileSystem::mounted() const { return live_ && SdMan.ready(); }

// The root-directory walk. This is the DEGRADED probe -- it is what the confirmed
// defect was, kept only for when no target file can be established, and every
// caller that ends up on it has to say so out loud.
//
// Why it is degraded: the root directory's sector is the single most likely
// sector to already be in SdFat's one 512-byte cache, so this can return true
// forever with the card out of the slot. Reading it repeatedly is the ONE access
// pattern guaranteed to be served from RAM.
bool SdFileSystem::readRootDirectory() {
  FsFile root = SdMan.open("/", O_RDONLY);
  if (!root) return false;
  root.rewind();
  FsFile first = root.openNextFile();
  bool ok;
  if (first) {
    first.close();
    ok = true;
  } else {
    // No entry can mean an empty root OR a card that stopped answering. The
    // directory's own error bits are what separate them: a clean end-of-directory
    // leaves them at zero.
    ok = (root.getError() == 0);
  }
  root.close();
  return ok;
}

// The real probe: open the target file and read a byte off it.
//
// THREE SECTORS, ONE CACHE. Opening "/.reader/settings.json" makes SdFat scan the
// root directory for ".reader" (a data-cache read of the root's sector), then scan
// /.reader for "settings.json" (a data-cache read of that directory's cluster,
// which EVICTS the root sector), then the 1-byte read at offset 0 takes
// FatFile::readPrivate's partial-read branch into dataCachePrepare() for the
// file's own first data sector (evicting the directory sector). One 512-byte
// cache cannot hold three sectors, and the one it is left holding is the last of
// the three -- so the next probe's FIRST access is already a miss.
//
// A byte is read rather than just opened deliberately. An open() that resolved
// entirely out of a directory sector still in cache would prove less; touching
// file data reaches a sector in the data area, which is a different region of the
// card entirely.
bool SdFileSystem::readProbeTargetFile() {
  FsFile f = SdMan.open(probeTargetPath_.c_str(), O_RDONLY);
  if (!f) return false;
  uint8_t byte = 0;
  const int n = f.read(&byte, 1);
  // Read the error bits BEFORE closing; there is no handle to ask afterwards.
  const bool err = f.getError() != 0;
  f.close();
  // n != 1 covers both a read error and a file that has become empty. An empty
  // target was refused at adoption time, so a zero here means the card.
  return n == 1 && !err;
}

bool SdFileSystem::probe() {
  SpiBusGuard bus;
  if (!SdMan.ready()) {
    live_ = false;
    return false;
  }
  if (probeTarget_ == ProbeTarget::File) {
    if (!readProbeTargetFile()) {
      noteCardGone("the fast probe (a byte read from its target file)");
      return false;
    }
  } else if (!readRootDirectory()) {
    noteCardGone("the fast probe (a root-directory read -- the DEGRADED fallback)");
    return false;
  }
  live_ = true;
  return true;
}

bool SdFileSystem::useFileProbeTarget(const char* path) {
  SpiBusGuard bus;
  // Fall back FIRST, so every early return below leaves a coherent state rather
  // than a File target pointing at a path that would not open.
  probeTarget_ = ProbeTarget::RootDir;
  probeTargetPath_.clear();
  if (!path || !*path) return false;
  if (!mounted()) return false;

  const std::string p = normalise(path);
  FsFile f = SdMan.open(p.c_str(), O_RDONLY);
  if (!f) return false;
  if (f.isDirectory()) {
    // A directory would be a probe barely better than "/": its entries can sit in
    // the same one cache slot, and no data-area sector is ever touched.
    f.close();
    return false;
  }
  uint8_t byte = 0;
  const int n = f.read(&byte, 1);
  f.close();
  if (n != 1) return false;  // empty, or unreadable right now

  probeTargetPath_ = p;
  probeTarget_ = ProbeTarget::File;
  return true;
}

bool SdFileSystem::armDeepProbe() {
  SpiBusGuard bus;
  deepBaseline_ = 0;
  if (!mounted()) return false;
  const uint64_t used = SdMan.sdUsedBytes();
  // 0 is how sdUsedBytes() reports a FAILED scan (freeClusterCount() < 0 sets its
  // cache to zero), so a volume whose honest answer were 0 could never be told
  // apart from a dead card. Refuse to arm rather than arm a mechanism that would
  // declare the card gone on its first run. In practice this does not happen: a
  // FAT32 volume's root directory occupies a cluster and an exFAT volume's
  // allocation bitmap and upcase table occupy several, so a formatted card
  // always reports some bytes used.
  if (used == 0) return false;
  deepBaseline_ = used;
  return true;
}

bool SdFileSystem::deepProbe() {
  SpiBusGuard bus;
  // Unarmed: this mechanism has no opinion, and saying "gone" would be a lie.
  // Callers gate on deepProbeArmed() and log when it is not armed.
  if (deepBaseline_ == 0) return true;
  if (!SdMan.ready()) {
    live_ = false;
    return false;
  }
  // The FAT scan. Only ZERO means failure -- a different non-zero number just
  // means files were written since the baseline, which is not our business.
  if (SdMan.sdUsedBytes() == 0) {
    noteCardGone("the FAT-scan backstop (sdUsedBytes/freeClusterCount)");
    return false;
  }
  live_ = true;
  return true;
}

bool SdFileSystem::isDirectory(const std::string& p) {
  if (p == "/") return true;  // a mounted volume always has a root
  FsFile f = SdMan.open(p.c_str(), O_RDONLY);
  if (!f) return false;
  const bool dir = f.isDirectory();
  f.close();
  return dir;
}

bool SdFileSystem::isFile(const std::string& p) {
  if (p == "/") return false;
  FsFile f = SdMan.open(p.c_str(), O_RDONLY);
  if (!f) return false;
  const bool dir = f.isDirectory();
  f.close();
  return !dir;
}

bool SdFileSystem::exists(std::string_view path) {
  SpiBusGuard bus;
  if (!mounted()) return false;
  const std::string p = normalise(path);
  if (p == "/") return true;
  return SdMan.exists(p.c_str());
}

// --- WHAT THE CARD COSTS -----------------------------------------------------
//
// THE BIGGEST NUMBER IN AN INTERACTION IS A DIRECTORY LISTING, AND IT HAD NO LOG
// LINE. `[i]` says a Confirm on Home took two seconds and that the time went to
// `disp=`; it cannot say that the two seconds were one call to list() over 406
// entries. That is the difference between knowing a screen is slow and knowing
// what to change, and this project's standing rule is that where the answer
// decides a design you measure the thing rather than argue about it -- the
// ~2.7 ms an entry this firmware quotes everywhere is a figure from one session
// that has never been re-checked against the card in the slot.
//
// Threshold rather than every call, because exists() and readAll() on a small
// file run in single-digit milliseconds and a line each would bury the one that
// matters. `perEntry` is printed because that, not the total, is the number that
// says whether a 203-book card is slow for a reason worth fixing.
static constexpr uint32_t kSlowFsMs = 15;

bool SdFileSystem::list(std::string_view path, std::vector<reader::DirEntry>& out) {
  const uint32_t fsT0 = millis();
  const size_t had = out.size();
  SpiBusGuard bus;
  if (!mounted()) return false;
  const std::string p = normalise(path);

  FsFile dir = SdMan.open(p.c_str(), O_RDONLY);
  if (!dir) {
    // A missing path and a dead card are indistinguishable here -- except for the
    // root, which a mounted volume always has.
    if (p == "/") noteCardGone("list");
    return false;
  }
  if (!dir.isDirectory()) {
    dir.close();
    return false;  // a file is not a listable directory
  }

  // Gathered locally and appended only on success: a read that fails part-way
  // must leave `out` exactly as it was, not half-filled. Same rule the desktop
  // implementations follow, and the contract's "list appends rather than
  // clearing" clause is what pins it.
  std::vector<reader::DirEntry> found;
  char name[kNameBufBytes];
  // One level only, as the interface says. No filtering needed for "." and "..":
  // FatFile::openNext skips any entry whose 8.3 SHORT-NAME field starts with '.',
  // and only the dot entries ever do -- short-name generation strips a leading dot
  // from a long name, so a dotfile like "/.reader" has an SFN of "READER~1" and IS
  // listed. (exFAT has no dot entries at all.) So a subdirectory lists exactly
  // what was put in it, hidden-by-convention names included, which is what the
  // contract's "/.reader" clauses need.
  for (FsFile f = dir.openNextFile(); f; f = dir.openNextFile()) {
    // Read everything off the handle BEFORE closing it, and close it on every
    // path out of the iteration -- including the skip below. The SDK's own loop
    // does the same, and leaking handles across a long directory is how a file
    // browser starts failing to open anything.
    const size_t named = f.getName(name, sizeof(name));
    const bool isDir = f.isDirectory();
    const uint64_t bytes = f.fileSize();
    f.close();

    // getName() returns 0 rather than truncating when the name does not fit, so
    // an over-long name is a name we cannot report -- NOT a shortened one. That
    // distinction is the whole point: a truncated LFN can collide with a real,
    // different file, and a browser that showed it would open or delete the wrong
    // book.
    //
    // The decision is to SKIP the entry and count it. The alternative -- listing
    // it marked unopenable -- needs a flag DirEntry does not have, and would
    // offer the user a row that no other method on this interface can act on,
    // because every one of them takes a path built from that same name. Skipping
    // loses the row; showing it would lose the row's meaning. skippedNames() is
    // how a diagnostic surfaces that it happened.
    if (named == 0) {
      ++skippedNames_;
      continue;
    }

    reader::DirEntry entry;
    entry.name = name;
    entry.isDir = isDir;
    // DirEntry::size is uint32_t and fileSize() is 64-bit, so a file over 4 GiB
    // has no honest value to report. It SATURATES at UINT32_MAX rather than
    // wrapping: wrapping would show a 5 GiB file as 1 GiB, which reads as a
    // plausible number and is wrong, while a pinned 4294967295 is visibly a
    // ceiling. HostFileSystem clamps identically. Nothing in V1 needs the true
    // size of such a file -- exFAT is the only format that can hold one, and the
    // whole-file readAll below refuses anything over 64 KB anyway -- so this is
    // about the display being wrong versus being capped.
    entry.size = isDir ? 0u : (bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytes));
    found.push_back(std::move(entry));
  }
  // Read the error before closing: getError() has no file to ask afterwards.
  // A clean end-of-directory leaves it at zero; anything else means the listing
  // is short, and a short listing is worse than no listing -- it is a library
  // that silently lost books.
  const bool readFailed = dir.getError() != 0;
  dir.close();
  if (readFailed) {
    noteCardGone("list");
    return false;
  }

  // MOVED, not copied. `found` is built to the side so a read that fails part
  // way appends nothing (the check above), and it dies at the closing brace --
  // so copying meant every entry's name string existed twice, transiently, for
  // no reason. On a folder with a few thousand files that second set is real
  // memory on a part with a ~155 KB floor, and `-fno-exceptions` makes the
  // allocation that fails an abort() with no diagnostic rather than a false.
  out.insert(out.end(), std::make_move_iterator(found.begin()),
             std::make_move_iterator(found.end()));
  const uint32_t fsMs = millis() - fsT0;
  const size_t got = out.size() - had;
  if (fsMs >= kSlowFsMs)
    Serial.printf("[fs] list %s -> %u entries in %lums (%lu.%02lu ms/entry)\n", p.c_str(),
                  (unsigned)got, (unsigned long)fsMs, (unsigned long)(got ? fsMs / got : 0),
                  (unsigned long)(got ? (fsMs * 100u / got) % 100u : 0));
  return true;
}

bool SdFileSystem::readAll(std::string_view path, std::string& out) {
  const uint32_t fsT0 = millis();
  SpiBusGuard bus;
  if (!mounted()) return false;
  const std::string p = normalise(path);

  FsFile f = SdMan.open(p.c_str(), O_RDONLY);
  if (!f) return false;  // absent, which is not a card failure
  if (f.isDirectory()) {
    f.close();
    return false;
  }
  const uint64_t size = f.fileSize();
  if (size > kMaxReadBytes) {
    f.close();
    Serial.printf("[sd] %s is %llu bytes; readAll refuses anything over %u\n", p.c_str(),
                  (unsigned long long)size, (unsigned)kMaxReadBytes);
    Serial.flush();
    return false;
  }

  // Built locally and moved on success, so `out` is untouched on every failure
  // path -- the contract is explicit about that, because a caller that keeps its
  // previous value on a failed read is the difference between falling back to
  // defaults and falling back to garbage.
  std::string body;
  body.resize(static_cast<size_t>(size));
  size_t got = 0;
  while (got < body.size()) {
    const int n = f.read(&body[got], body.size() - got);
    if (n <= 0) {
      f.close();
      // The file told us its own size and then would not produce it. That is the
      // card, not the caller.
      noteCardGone("readAll");
      return false;
    }
    got += static_cast<size_t>(n);
  }
  f.close();
  out = std::move(body);
  const uint32_t fsMs = millis() - fsT0;
  if (fsMs >= kSlowFsMs)
    Serial.printf("[fs] readAll %s %u bytes in %lums\n", p.c_str(), (unsigned)out.size(),
                  (unsigned long)fsMs);
  return true;
}

// The read handle. Deliberately NOT in an anonymous namespace: sd_fs.h has to be
// able to name it to befriend it, so it lives at file scope with an external
// name and is defined only here.
//
// THE FsFile's LIFETIME IS THE HANDLE'S, AND NOTHING ELSE'S.
//
// SdFat is compiled with DESTRUCTOR_CLOSES_FILE == 0 in this build (checked in
// SdFatConfig.h -- it is the default and nothing overrides it), so ~FsFile does
// NOT close the file. An FsFile that goes out of scope unclosed keeps its
// directory entry pinned, and a firmware that leaks them stops being able to open
// anything at all -- the failure arrives long after the leak, on an unrelated
// screen, which is why the SDK's own loops close on every path and so does
// list() above.
//
// Three things make it unambiguous here:
//   * the handle owns exactly ONE FsFile, opened in openAt() and closed in the
//     destructor. There is no other close() and no other open().
//   * openRead() hands back a std::unique_ptr<reader::FileHandle> with a virtual
//     destructor, so the close happens at scope exit on every path out --
//     including an early return the caller did not think about. reader::FileHandle
//     deletes copy and move for this reason: two owners would close it twice or
//     not at all.
//   * a failed openAt() closes before returning false, and the destructor's
//     `if (file_)` makes the second close a no-op rather than a double close.
class SdFileHandle : public reader::FileHandle {
 public:
  explicit SdFileHandle(SdFileSystem& fs) : fs_(fs) {}

  ~SdFileHandle() override {
    SpiBusGuard bus;
    if (file_) file_.close();
  }

  // The guard is the caller's (openRead holds it), so this must not take it --
  // recursively it would be harmless, but it would also be a lie about who owns
  // the bus here.
  bool openAt(const char* path) {
    file_ = SdMan.open(path, O_RDONLY);
    if (!file_) return false;
    if (file_.isDirectory()) {
      file_.close();
      return false;  // a directory opens perfectly well; only this refuses it
    }
    const uint64_t bytes = file_.fileSize();
    // size() is uint32_t. Saturating would make seek(size()) land in the middle
    // of the file and every offset past 4 GiB unreachable but not reported, so
    // refuse the open instead of handing back a handle that lies about its shape.
    // Only exFAT can hold such a file; nothing in V1 should meet one.
    if (bytes > UINT32_MAX) {
      file_.close();
      Serial.printf("[sd] %s is %llu bytes; openRead cannot address past %u\n", path,
                    (unsigned long long)bytes, (unsigned)UINT32_MAX);
      Serial.flush();
      return false;
    }
    size_ = static_cast<uint32_t>(bytes);
    pos_ = 0;
    return true;
  }

  // No guard: answered from RAM, so this never reaches the bus. See the note in
  // sd_fs.h about why the guard is per-operation.
  uint32_t size() const override { return size_; }
  uint32_t position() const override { return pos_; }

  size_t read(void* dst, size_t bytes) override {
    if (bytes == 0) return 0;  // a no-op, and `dst` is never dereferenced
    SpiBusGuard bus;
    if (!file_ || !fs_.mounted()) return 0;
    const int n = file_.read(dst, bytes);
    if (n <= 0) {
      // n == 0 at the end of the file is ordinary and NOT a card failure --
      // FatFile::read clamps the count to what is left, so a request at EOF
      // returns 0 without touching the card. Below size() it is the same evidence
      // readAll acts on: the file told us its length and would not produce it.
      if (pos_ < size_) fs_.noteCardGone("a handle read");
      return 0;
    }
    pos_ += static_cast<uint32_t>(n);
    return static_cast<size_t>(n);
  }

  bool seek(uint32_t offset) override {
    // Refused past the end, position unchanged -- which is also exactly what
    // FatFile::seekSet does (pos > m_fileSize fails and restores m_curCluster),
    // so this is the primitive rather than something emulated on top of it. See
    // reader::FileHandle for why refused and not clamped.
    if (offset > size_) return false;
    SpiBusGuard bus;
    if (!file_ || !fs_.mounted()) return false;
    if (!file_.seekSet(offset)) return false;
    pos_ = offset;
    return true;
  }

 private:
  SdFileSystem& fs_;
  FsFile file_;
  uint32_t size_ = 0;
  uint32_t pos_ = 0;
};

std::unique_ptr<reader::FileHandle> SdFileSystem::openRead(std::string_view path) {
  SpiBusGuard bus;
  if (!mounted()) return nullptr;
  const std::string p = normalise(path);
  if (p == "/") return nullptr;  // the root is a directory

  // nothrow, and this is not decoration: the firmware is -fno-exceptions, so the
  // ordinary operator new calls std::terminate on failure, which is an abort()
  // with no diagnostic. A failed open must be a null handle -- including when
  // what failed was the allocation.
  std::unique_ptr<SdFileHandle> h(new (std::nothrow) SdFileHandle(*this));
  if (!h) return nullptr;
  if (!h->openAt(p.c_str())) return nullptr;
  return h;
}

bool SdFileSystem::writeAll(std::string_view path, std::string_view data) {
  const uint32_t fsT0 = millis();
  SpiBusGuard bus;
  if (!mounted()) return false;
  const std::string p = normalise(path);
  if (p == "/") return false;
  if (isDirectory(p)) return false;  // files only; do not truncate a directory
  if (!mkdirs(parentOf(p))) return false;

  FsFile f = SdMan.open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) return false;
  // write(ptr, 0) with a null ptr is not worth finding out about: an empty
  // string_view need not have a valid data().
  const size_t wrote = data.empty() ? 0 : f.write(data.data(), data.size());
  const bool writeError = f.getWriteError();
  const bool synced = f.sync();
  f.close();

  // VERIFY THE BYTE COUNT. A card that is full, worn out or half-removed
  // short-writes and SdFat reports it only here; a writeAll that returned true on
  // a short write is exactly how a settings file ends up truncated, and a
  // truncated JSON file is unparseable -- so the next boot silently reverts every
  // setting the user changed.
  if (wrote != data.size() || writeError || !synced) {
    Serial.printf("[sd] short write on %s: %u of %u bytes (writeError=%d sync=%d)\n", p.c_str(),
                  (unsigned)wrote, (unsigned)data.size(), (int)writeError, (int)synced);
    Serial.flush();
    // O_TRUNC already destroyed whatever was there, so the remnant is not a
    // fallback -- it is a shorter, plausible-looking version of the file that a
    // later read would trust. Better gone.
    SdMan.remove(p.c_str());
    noteCardGone("writeAll");
    return false;
  }
  // A CARD WRITE IS ONE OF THE THREE THINGS THAT CAN STALL A PRESS, and it is the
  // one with no ceiling: a small file plus its FAT metadata is usually tens of
  // milliseconds, and a card doing internal housekeeping can take far longer with
  // nothing on our side to see it. Two of these fire on the way out of a book (the
  // position and the pointer), which is why `post=` on that interaction is the
  // field to read.
  const uint32_t fsMs = millis() - fsT0;
  if (fsMs >= kSlowFsMs)
    Serial.printf("[fs] writeAll %s %u bytes in %lums\n", p.c_str(), (unsigned)data.size(),
                  (unsigned long)fsMs);
  return true;
}

bool SdFileSystem::mkdirs(std::string_view path) {
  SpiBusGuard bus;
  if (!mounted()) return false;
  const std::string p = normalise(path);
  if (p.empty() || p == "/") return true;  // the root is the mount, and it exists
  if (isDirectory(p)) return true;         // idempotent
  if (isFile(p)) return false;             // a file is not, and will not become, a directory
  // pFlag = true creates the missing parents. SdFat opens each component before
  // creating it and fails when one is a regular file, which is what makes
  // "/blocker/under" fail with /blocker a file and create nothing.
  SdMan.mkdir(p.c_str(), true);
  return isDirectory(p);  // the end state decides, not the return value
}

bool SdFileSystem::remove(std::string_view path) {
  SpiBusGuard bus;
  // Counted before any of the refusals below, and that is the point: a caller
  // that asked to delete something is reason enough to distrust a cached
  // listing, whether or not this call is the one that changes the card. See
  // removals() in the header.
  ++removals_;
  if (!mounted()) return false;
  const std::string p = normalise(path);
  if (p == "/") return false;
  if (isDirectory(p)) return false;  // files only
  // The end state is the contract: a file that was already gone counts as
  // removed, because a caller deleting a book cares that it is gone, not about
  // racing whatever deleted it first.
  if (!SdMan.exists(p.c_str())) return true;
  SdMan.remove(p.c_str());
  return !SdMan.exists(p.c_str());
}
