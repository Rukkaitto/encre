#include "reader/host_fs.h"

// Desktop only. The srcFilter in core/library.json keeps this file out of the
// firmware build; this guard is the second line of defence, so a filter that
// stopped matching yields an empty object file instead of dragging <filesystem>
// into the ESP32 toolchain. Same arrangement as png.cpp.
#ifdef READER_DESKTOP

#include <cstdint>
#include <filesystem>
#include <fstream>
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
