#include "sd_fs.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
// SDCardManager's private `sd` member. Of the public surface, sdUsedBytes()
// rescans the FAT (seconds), and exists()/open() on an absent path fails
// identically whether the path is missing or the card is.
//
// So mounted() is ready() AND a liveness flag, and the flag is maintained from
// two directions:
//
//   * FEEDBACK. Every operation below that gets a failure only a dead card
//     explains -- the root directory failing to open, a directory read setting
//     READ_ERROR, a file read stopping short of its own declared size, a write
//     that does not take -- calls noteCardGone() and clears it. So a card pulled
//     at runtime is reported the moment anything actually needs it, which is when
//     it matters, rather than never.
//   * probe(), which reads the root directory on demand. Deliberately not called
//     from here: mounted() is checked at the top of every method (writeAll checks
//     it three times through mkdirs), and a probe is real traffic on the bus the
//     panel shares.
//
// The honest limit: probe() can be satisfied from SdFat's one-sector cache, so it
// is not a card-detect. There is no card-detect GPIO in the Xteink profiles
// either (BoardConfig's SdPins has no detect field at all). "The card was there
// and nothing has since told us otherwise" is the strongest claim this SDK
// supports, and it is the claim this makes.
bool SdFileSystem::mounted() const { return live_ && SdMan.ready(); }

bool SdFileSystem::probe() {
  SpiBusGuard bus;
  if (!SdMan.ready()) {
    live_ = false;
    return false;
  }
  FsFile root = SdMan.open("/", O_RDONLY);
  if (!root) {
    noteCardGone("probe");
    return false;
  }
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
  if (!ok) {
    noteCardGone("probe");
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

bool SdFileSystem::list(std::string_view path, std::vector<reader::DirEntry>& out) {
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
  // One level only, as the interface says. openNextFile() skips "." and ".."
  // itself (FatFile::openNext drops any entry whose name starts with '.'), so a
  // subdirectory lists exactly what was put in it.
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

  out.insert(out.end(), found.begin(), found.end());
  return true;
}

bool SdFileSystem::readAll(std::string_view path, std::string& out) {
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
  return true;
}

bool SdFileSystem::writeAll(std::string_view path, std::string_view data) {
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
