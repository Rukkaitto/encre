#include "reader/host_fs.h"

// Desktop only. The srcFilter in core/library.json keeps this file out of the
// firmware build; this guard is the second line of defence, so a filter that
// stopped matching yields an empty object file instead of dragging <filesystem>
// into the ESP32 toolchain. Same arrangement as png.cpp.
#ifdef READER_DESKTOP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace reader {
namespace {

namespace fsys = std::filesystem;

// Absolute, '/'-separated, no repeated and no trailing separator; "/" is the
// root. The fake normalises identically -- both are held to the same contract,
// and "the same" has to include which bytes name a file.
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

// "" for the root, which has no parent.
std::string parentOf(const std::string& normalised) {
  if (normalised == "/") return "";
  const size_t slash = normalised.rfind('/');
  return slash == 0 ? "/" : normalised.substr(0, slash);
}

// A read handle over an std::ifstream.
//
// The stream's own error flags are the thing to be careful with here: istream's
// read() sets BOTH eofbit and failbit when it delivers fewer bytes than asked
// for, and leaves them set, so a naive second read on the same handle returns
// nothing forever. That would make "a short read at the end of the file" a
// permanent failure rather than the ordinary event FileHandle says it is -- and
// on the desktop it would silently diverge from SdFat, which just returns the
// bytes it had. So every operation clears eof/fail afterwards and only
// badbit -- a real stream failure -- is treated as one.
class HostFileHandle : public FileHandle {
 public:
  HostFileHandle(std::ifstream in, uint32_t size) : in_(std::move(in)), size_(size) {}

  uint32_t size() const override { return size_; }
  uint32_t position() const override { return pos_; }

  size_t read(void* dst, size_t bytes) override {
    if (bytes == 0) return 0;  // a no-op, and `dst` is not touched
    // Clamp to the length recorded at open, so position() can never pass size().
    // The clamp is not paranoia: size_ was read once and the file on disk can
    // grow behind us (the simulator writes into the same tree it reads), and
    // without this a handle would quietly hand back bytes past the size it is
    // still reporting. SdFat clamps internally for the same reason, so clamping
    // here is also what keeps the two implementations answering alike.
    if (pos_ >= size_) return 0;
    const size_t left = size_ - pos_;
    if (bytes > left) bytes = left;

    in_.read(static_cast<char*>(dst), static_cast<std::streamsize>(bytes));
    const std::streamsize got = in_.gcount();
    if (in_.bad()) {
      // A real stream failure, as opposed to the end of the file. gcount() no
      // longer describes where the stream is, so report nothing delivered and
      // put it back where pos_ says it is: position() stays authoritative and
      // the handle stays usable.
      in_.clear();
      in_.seekg(static_cast<std::streamoff>(pos_), std::ios::beg);
      return 0;
    }
    in_.clear();  // a short read is the end of the file, not an error
    pos_ += static_cast<uint32_t>(got);
    return static_cast<size_t>(got);
  }

  bool seek(uint32_t offset) override {
    if (offset > size_) return false;  // refused, not clamped -- see FileHandle
    in_.clear();
    in_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in_) {
      in_.clear();
      in_.seekg(static_cast<std::streamoff>(pos_), std::ios::beg);
      return false;
    }
    pos_ = offset;
    return true;
  }

 private:
  std::ifstream in_;
  uint32_t size_ = 0;
  uint32_t pos_ = 0;
};

}  // namespace

HostFileSystem::HostFileSystem(std::string root) : root_(std::move(root)) {
  // A trailing separator on the root would double up when a path is appended.
  while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
}

std::string HostFileSystem::hostPath(std::string_view path) const {
  const std::string p = normalise(path);
  return p == "/" ? root_ : root_ + p;
}

bool HostFileSystem::mounted() const {
  std::error_code ec;
  return fsys::is_directory(root_, ec);
}

bool HostFileSystem::exists(std::string_view path) {
  if (!mounted()) return false;
  std::error_code ec;
  return fsys::exists(hostPath(path), ec);
}

bool HostFileSystem::list(std::string_view path, std::vector<DirEntry>& out) {
  if (!mounted()) return false;
  const std::string dir = hostPath(path);
  std::error_code ec;
  if (!fsys::is_directory(dir, ec)) return false;

  // Gathered locally and appended only on success: a failure part-way through
  // must leave `out` exactly as it was, not half-filled.
  std::vector<DirEntry> found;
  fsys::directory_iterator it(dir, ec);
  if (ec) return false;
  for (const auto& e : it) {
    DirEntry entry;
    entry.name = e.path().filename().string();
    entry.isDir = e.is_directory(ec);
    if (ec) return false;
    if (!entry.isDir) {
      const std::uintmax_t bytes = fsys::file_size(e.path(), ec);
      if (ec) return false;
      entry.size = bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytes);
    }
    found.push_back(std::move(entry));
  }
  out.insert(out.end(), found.begin(), found.end());
  return true;
}

bool HostFileSystem::readAll(std::string_view path, std::string& out) {
  if (!mounted()) return false;
  const std::string p = hostPath(path);
  std::error_code ec;
  if (!fsys::is_regular_file(p, ec)) return false;
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) return false;  // `out` is still untouched on every failure path
  out = std::move(body);
  return true;
}

std::unique_ptr<FileHandle> HostFileSystem::openRead(std::string_view path) {
  if (!mounted()) return nullptr;
  const std::string p = hostPath(path);
  std::error_code ec;
  // is_regular_file is what refuses a directory AND a missing path in one call,
  // and it is checked before the open so a directory never reaches ifstream --
  // which on some platforms opens one happily and then reads nothing.
  if (!fsys::is_regular_file(p, ec)) return nullptr;
  const std::uintmax_t bytes = fsys::file_size(p, ec);
  if (ec) return nullptr;
  // size() is uint32_t. A saturated length would make seek(size()) land in the
  // middle of the file, so refuse rather than lie about it.
  if (bytes > UINT32_MAX) return nullptr;
  std::ifstream in(p, std::ios::binary);
  if (!in) return nullptr;
  return std::unique_ptr<FileHandle>(
      new (std::nothrow) HostFileHandle(std::move(in), static_cast<uint32_t>(bytes)));
}

bool HostFileSystem::writeAll(std::string_view path, std::string_view data) {
  if (!mounted()) return false;
  const std::string normalised = normalise(path);
  if (normalised == "/") return false;
  const std::string p = hostPath(normalised);
  std::error_code ec;
  if (fsys::is_directory(p, ec)) return false;
  if (!mkdirs(parentOf(normalised))) return false;
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  f.close();
  return f.good();
}

bool HostFileSystem::mkdirs(std::string_view path) {
  if (!mounted()) return false;
  const std::string normalised = normalise(path);
  if (normalised == "/") return true;  // the root is the mount, and it exists
  const std::string p = hostPath(normalised);
  std::error_code ec;
  fsys::create_directories(p, ec);
  // create_directories reports false for a directory that already existed, so
  // the end state is what decides -- and it is also what fails when a component
  // of the chain is a regular file.
  return fsys::is_directory(p, ec);
}

bool HostFileSystem::remove(std::string_view path) {
  if (!mounted()) return false;
  const std::string p = hostPath(path);
  std::error_code ec;
  if (fsys::is_directory(p, ec)) return false;  // files only
  fsys::remove(p, ec);
  return !fsys::exists(p, ec);  // the end state, so an absent file is a success
}

}  // namespace reader

#endif  // READER_DESKTOP
