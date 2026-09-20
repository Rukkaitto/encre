#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "SdFat.h"
#include "harness_state.h"

// THE CARD, OVER A HOST DIRECTORY.
//
// NOTE WHAT IS NOT MODELLED, because sd_fs.cpp is NOT compiled here -- it is
// replaced by a desktop twin at the reader::FileSystem seam it already satisfies.
// So the listing cache, the probe targets, the deep probe's arithmetic and
// skippedNames are all outside this fake, and stay covered by test_dir_cache.cpp on
// the desktop and sd_selftest.cpp on a real card.
//
// AND THE SHARED SPI BUS IS UNMODELLABLE FROM HERE. The card is on the display's
// bus and SDCardManager does no locking; the harness can assert that SpiBusGuard is
// HELD, and it cannot produce the fault that guard exists to prevent.
class SDCardManagerClass {
 public:
  bool begin(int8_t cs = -1, uint32_t hz = 0, int8_t powerEnable = -1) {
    (void)cs;
    (void)hz;
    (void)powerEnable;
    harness::record("<card> begin present=%d", harness::cardPresent() ? 1 : 0);
    return harness::cardPresent();
  }
  bool ready() const { return harness::cardPresent(); }
  uint64_t sdUsedBytes() const { return 1234567; }

  bool exists(const char* path) {
    return harness::cardPresent() && std::filesystem::exists(host(path));
  }
  bool mkdir(const char* path, bool /*createParents*/ = true) {
    if (!harness::cardPresent()) return false;
    std::error_code ec;
    std::filesystem::create_directories(host(path), ec);
    return !ec;
  }
  bool remove(const char* path) {
    if (!harness::cardPresent()) return false;
    std::error_code ec;
    return std::filesystem::remove(host(path), ec);
  }
  bool rename(const char* from, const char* to) {
    if (!harness::cardPresent()) return false;
    std::error_code ec;
    std::filesystem::rename(host(from), host(to), ec);
    return !ec;
  }
  FsFile open(const char* path, uint8_t mode = O_RDONLY) {
    if (!harness::cardPresent()) return FsFile{};
    const char* m = (mode & O_APPEND) ? "ab" : (mode & (O_WRONLY | O_CREAT | O_TRUNC)) ? "wb" : "rb";
    if ((mode & O_RDWR) && !(mode & O_TRUNC)) m = "r+b";
    return FsFile{std::fopen(host(path).c_str(), m)};
  }

 private:
  static std::string host(const char* path) {
    const std::string p = path != nullptr ? path : "";
    return harness::cardRoot() + (p.empty() || p[0] == '/' ? p : "/" + p);
  }
};

inline SDCardManagerClass SdMan;
