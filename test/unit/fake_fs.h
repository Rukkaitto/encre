#pragma once
// An in-memory FileSystem for unit tests: a map of paths to bodies and a set of
// directories, plus the two failures a real card produces and a test cannot ask
// a real card for -- no card at all, and a write that will not take.
//
// Test scaffolding, deliberately in test/unit/ rather than core/: nothing the
// firmware ships should be able to link against it. It is held to the same
// contract as HostFileSystem and SdFileSystem by test_filesystem.cpp, because a
// fake that is more forgiving than the real thing makes every test above it
// meaningless.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reader/dir_counts.h"
#include "reader/filesystem.h"

// A read handle over a COPY of the body.
//
// The copy is deliberate. A pointer into the map would dangle the moment a test
// wrote to the file or dropped the filesystem while a handle was open, and a fake
// whose failure mode is undefined behaviour is worse than no fake at all. It does
// mean the fake's handle is immune to a concurrent writeAll where a real one is
// not -- that is a divergence, and it is why the contract says nothing about
// writing to a file that is open for reading. Do not start relying on it.
class FakeFileHandle : public reader::FileHandle {
 public:
  explicit FakeFileHandle(std::string body) : body_(std::move(body)) {}

  uint32_t size() const override { return static_cast<uint32_t>(body_.size()); }
  uint32_t position() const override { return pos_; }

  size_t read(void* dst, size_t bytes) override {
    if (bytes == 0) return 0;  // a no-op, and `dst` is not touched
    const size_t left = body_.size() - pos_;
    const size_t n = bytes < left ? bytes : left;
    if (n != 0) std::memcpy(dst, body_.data() + pos_, n);
    pos_ += static_cast<uint32_t>(n);
    return n;
  }

  bool seek(uint32_t offset) override {
    // Refused, not clamped, and pos_ is left alone -- see FileHandle.
    if (offset > body_.size()) return false;
    pos_ = offset;
    return true;
  }

 private:
  std::string body_;
  uint32_t pos_ = 0;
};

class FakeFileSystem : public reader::FileSystem {
 public:
  // --- test controls -------------------------------------------------------

  // Simulates a card that is absent or would not mount. Contents survive, so a
  // test can pull the card and put it back.
  void setMounted(bool m) { mounted_ = m; }

  // Simulates a full or write-protected card: every writeAll fails and changes
  // nothing.
  void setFailWrites(bool f) { failWrites_ = f; }

  size_t fileCount() const { return files_.size(); }

  // HOW MANY TIMES THE CARD WAS WALKED. The device's listing costs ~2.9 ms an
  // ENTRY and the walk itself cannot be made cheaper (reader/dir_cache.h has the
  // SdFat reasoning), so the only thing a desktop test can assert about that cost
  // is the number of walks. Without this, a change that removes a listing and a
  // change that removes nothing look identical from up here.
  size_t listCalls() const { return listCalls_; }

  // The stored body, or nullptr. Lets a test assert on the bytes written without
  // going back through readAll.
  const std::string* peek(std::string_view path) const {
    auto it = files_.find(normalise(path));
    return it == files_.end() ? nullptr : &it->second;
  }

  // --- FileSystem ----------------------------------------------------------

  bool mounted() const override { return mounted_; }

  // THE FAKE MEMOISES TOO, AND IT HAS TO. The device's SdFileSystem answers this
  // and the desktop's HostFileSystem does not, so if the fake declined as well,
  // the only path the shipped firmware takes would be the one path nothing on the
  // desktop ever exercises -- and shell/ has no harness. Every countBooks test in
  // the suite therefore runs through the memo, which is what makes a staleness
  // bug a failing test rather than a device report.
  //
  // Null while the card is out, exactly as list() refuses then: a derived fact is
  // still an answer about a card, and answering one out of RAM for a card that is
  // in the user's hand is the defect the SD probe already shipped once.
  reader::DirCountCache* dirCounts() override { return mounted_ ? &dirCounts_ : nullptr; }

  bool exists(std::string_view path) override {
    if (!mounted_) return false;
    const std::string p = normalise(path);
    return files_.count(p) != 0 || dirs_.count(p) != 0;
  }

  bool list(std::string_view path, std::vector<reader::DirEntry>& out) override {
    ++listCalls_;
    if (!mounted_) return false;
    const std::string dir = normalise(path);
    if (dirs_.count(dir) == 0) return false;
    for (const auto& d : dirs_) {
      if (d == "/" || parentOf(d) != dir) continue;
      out.push_back(reader::DirEntry{leafOf(d), true, 0});
    }
    for (const auto& [p, body] : files_) {
      if (parentOf(p) != dir) continue;
      out.push_back(reader::DirEntry{leafOf(p), false, static_cast<uint32_t>(body.size())});
    }
    return true;
  }

  bool readAll(std::string_view path, std::string& out) override {
    if (!mounted_) return false;
    auto it = files_.find(normalise(path));
    if (it == files_.end()) return false;
    out = it->second;
    return true;
  }

  std::unique_ptr<reader::FileHandle> openRead(std::string_view path) override {
    if (!mounted_) return nullptr;
    const std::string p = normalise(path);
    if (dirs_.count(p) != 0) return nullptr;  // a directory is not readable
    auto it = files_.find(p);
    if (it == files_.end()) return nullptr;
    // size() is uint32_t, so a longer file has no honest length to report and a
    // saturated one would break seek arithmetic. Refuse, as the real ones do.
    if (it->second.size() > UINT32_MAX) return nullptr;
    // nothrow for the same reason the device does it: a failed open is a null
    // handle, never an abort. See FileHandle.
    return std::unique_ptr<reader::FileHandle>(new (std::nothrow) FakeFileHandle(it->second));
  }

  bool writeAll(std::string_view path, std::string_view data) override {
    // Every mutator drops the derived counts, above its own refusals and before
    // it touches anything -- the rule SdFileSystem's listing cache follows, for
    // the reason given there: deciding which refusals are "safe" is how an
    // invalidation ends up with a hole in it.
    dirCounts_.clear();
    if (!mounted_ || failWrites_) return false;
    const std::string p = normalise(path);
    if (p == "/" || dirs_.count(p) != 0) return false;
    if (!makeDirs(parentOf(p))) return false;
    files_[p] = std::string(data);
    return true;
  }

  bool mkdirs(std::string_view path) override {
    dirCounts_.clear();
    if (!mounted_) return false;
    return makeDirs(normalise(path));
  }

  bool remove(std::string_view path) override {
    dirCounts_.clear();
    if (!mounted_) return false;
    const std::string p = normalise(path);
    if (dirs_.count(p) != 0) return false;  // files only
    files_.erase(p);
    return true;
  }

  // --- path helpers, shared with the tests ---------------------------------

  // Absolute, '/'-separated, no repeated or trailing separator. "/" is the root.
  static std::string normalise(std::string_view path) {
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

 private:
  // "" for the root, which has no parent.
  static std::string parentOf(const std::string& p) {
    if (p == "/") return "";
    const size_t slash = p.rfind('/');
    return slash == 0 ? "/" : p.substr(0, slash);
  }

  static std::string leafOf(const std::string& p) { return p.substr(p.rfind('/') + 1); }

  // Creates every component of `dir`, failing if any of them is a file. `dir`
  // is already normalised.
  bool makeDirs(const std::string& dir) {
    if (dir == "/" || dir.empty()) return true;
    for (size_t i = 1; i <= dir.size(); ++i) {
      if (i != dir.size() && dir[i] != '/') continue;
      const std::string prefix = dir.substr(0, i);
      if (files_.count(prefix) != 0) return false;
      dirs_.insert(prefix);
    }
    return true;
  }

  std::map<std::string, std::string> files_;
  std::set<std::string> dirs_{"/"};
  bool mounted_ = true;
  bool failWrites_ = false;
  size_t listCalls_ = 0;
  reader::DirCountCache dirCounts_;
};
