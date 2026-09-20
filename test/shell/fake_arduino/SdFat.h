#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#include "harness_state.h"

constexpr uint8_t O_RDONLY = 0x00;
constexpr uint8_t O_WRONLY = 0x01;
constexpr uint8_t O_RDWR = 0x02;
constexpr uint8_t O_CREAT = 0x10;
constexpr uint8_t O_TRUNC = 0x20;
constexpr uint8_t O_APPEND = 0x40;

// A HOST FILE, because the cover sink and the card log really do write bytes and a
// fake that dropped them would make every assertion above it vacuous. The tree
// lives under a scenario's own temp directory.
class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(std::FILE* f) : f_(f) {}
  FsFile(FsFile&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
  FsFile& operator=(FsFile&& o) noexcept {
    if (this != &o) { close(); f_ = o.f_; o.f_ = nullptr; }
    return *this;
  }
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  ~FsFile() { close(); }

  explicit operator bool() const { return f_ != nullptr; }
  size_t write(const uint8_t* data, size_t n) {
    return f_ != nullptr ? std::fwrite(data, 1, n, f_) : 0;
  }
  int read(void* buf, size_t n) {
    return f_ != nullptr ? static_cast<int>(std::fread(buf, 1, n, f_)) : -1;
  }
  bool seekSet(uint64_t pos) {
    return f_ != nullptr && std::fseek(f_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  // RETURNS bool, as the real SdFat does -- main.cpp chains it with &&, and a
  // void here is a compile error rather than a silent difference.
  bool sync() { return f_ != nullptr && std::fflush(f_) == 0; }
  int getWriteError() const { return f_ != nullptr && std::ferror(f_) ? 1 : 0; }
  void close() {
    if (f_ != nullptr) { std::fclose(f_); f_ = nullptr; }
  }

 private:
  std::FILE* f_ = nullptr;
};
