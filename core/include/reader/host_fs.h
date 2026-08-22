#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// A FileSystem over the host OS, for the simulator and for desktop tests that
// want real files rather than the in-memory fake.
//
// DESKTOP ONLY. <filesystem> is a host-OS dependency, so this translation unit
// is excluded from the firmware build twice over: core/library.json's srcFilter
// drops host_fs.cpp exactly as it drops png.cpp, and the body is additionally
// guarded by READER_DESKTOP so a filter that silently stopped matching produces
// an empty object file rather than a <filesystem> include reaching the ESP32
// toolchain.
//
// Every path is resolved under `root`, so an absolute reader path like
// "/.reader/settings.json" lands inside a temp directory and cannot escape to
// the real filesystem root. mounted() is whether `root` is a directory -- the
// desktop stand-in for a card in the slot -- and an unmounted instance refuses
// every operation rather than creating `root` on the way past.
class HostFileSystem : public FileSystem {
 public:
  explicit HostFileSystem(std::string root);

  const std::string& root() const { return root_; }

  // The real on-disk path a reader path maps to. Exposed for tests and for the
  // simulator's logging.
  std::string hostPath(std::string_view path) const;

  bool mounted() const override;
  bool exists(std::string_view path) override;
  bool list(std::string_view path, std::vector<DirEntry>& out) override;
  bool readAll(std::string_view path, std::string& out) override;
  // The handle class itself stays inside host_fs.cpp: it holds an std::ifstream,
  // and putting <fstream> in a core/include header would put a host-OS
  // dependency somewhere the firmware can reach it by including this file.
  std::unique_ptr<FileHandle> openRead(std::string_view path) override;
  bool writeAll(std::string_view path, std::string_view data) override;
  bool mkdirs(std::string_view path) override;
  bool remove(std::string_view path) override;

 private:
  std::string root_;
};

}  // namespace reader
