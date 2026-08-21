#include "sd_selftest.h"

#if defined(ENCRE_FS_SELFTEST) && ENCRE_FS_SELFTEST

#include <Arduino.h>
#include <SDCardManager.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "reader/filesystem.h"
#include "reader/fs_contract.h"
#include "sd_fs.h"

namespace {

// Everything the clauses do happens under here. Wiped between clauses and
// removed at the end.
constexpr const char* kScratch = "/.reader/fstest";

// A view of another FileSystem rooted at a subdirectory, so the clauses' absolute
// paths ("/hello.txt", "/.reader/deep/settings.json") land inside the scratch
// directory and cannot touch the user's books. Same trick HostFileSystem uses to
// keep the simulator inside a temp directory.
class RootedFileSystem : public reader::FileSystem {
 public:
  RootedFileSystem(reader::FileSystem& inner, std::string root)
      : inner_(inner), root_(std::move(root)) {}

  bool mounted() const override { return inner_.mounted(); }
  bool exists(std::string_view p) override { return inner_.exists(at(p)); }
  bool list(std::string_view p, std::vector<reader::DirEntry>& out) override {
    return inner_.list(at(p), out);
  }
  bool readAll(std::string_view p, std::string& out) override { return inner_.readAll(at(p), out); }
  bool writeAll(std::string_view p, std::string_view d) override {
    return inner_.writeAll(at(p), d);
  }
  bool mkdirs(std::string_view p) override { return inner_.mkdirs(at(p)); }
  bool remove(std::string_view p) override { return inner_.remove(at(p)); }

 private:
  // No normalising here on purpose: the inner filesystem normalises, and the
  // clause that checks "//" and a trailing "/" has to reach it unmangled to mean
  // anything.
  std::string at(std::string_view p) const { return root_ + std::string(p); }

  reader::FileSystem& inner_;
  std::string root_;
};

// Prints every failure as it happens and counts everything, so the summary line
// is trustworthy even when the log scrolled.
class SerialReport : public reader::FsContractReport {
 public:
  void check(bool passed, const char* expr) override { note(passed, expr, ""); }
  bool require(bool passed, const char* expr) override {
    note(passed, expr, "(require) ");
    return passed;
  }

  int total = 0;
  int failures = 0;

 private:
  void note(bool passed, const char* expr, const char* prefix) {
    ++total;
    if (passed) return;
    ++failures;
    Serial.printf("[fscontract]     FAIL %s%s\n", prefix, expr);
    Serial.flush();
  }
};

// Fresh storage for the next clause. removeDir is SDCardManager's own recursive
// delete -- reader::FileSystem deliberately has no way to remove a directory, so
// the wipe cannot be written against the interface.
bool wipeScratch(SdFileSystem& fs) {
  SpiBusGuard bus;
  if (fs.exists(kScratch) && !SdMan.removeDir(kScratch)) return false;
  return fs.mkdirs(kScratch);
}

}  // namespace

int runSdFsContractSelfTest(SdFileSystem& fs) {
  Serial.printf("[fscontract] running the FileSystem contract against the card\n");
  Serial.flush();

  SerialReport report;

  if (!fs.mounted()) {
    // Not a skip: with no card the unmounted clause is the interesting one, and it
    // is the promise the SD-missing screen rests on.
    Serial.printf("[fscontract] no card -- running the unmounted clause only\n");
    reader::fsUnmountedClause().run(fs, report);
    Serial.printf("[fscontract] done: %d assertions, %d failed\n", report.total, report.failures);
    Serial.flush();
    return report.failures;
  }

  size_t count = 0;
  const reader::FsContractClause* clauses = reader::fsContractClauses(count);
  for (size_t i = 0; i < count; ++i) {
    const int before = report.failures;
    if (!wipeScratch(fs)) {
      Serial.printf("[fscontract]   CANNOT PREPARE %s -- skipping %s\n", kScratch,
                    clauses[i].name);
      Serial.flush();
      ++report.failures;
      continue;
    }
    RootedFileSystem rooted(fs, kScratch);
    Serial.printf("[fscontract]   %s\n", clauses[i].name);
    Serial.flush();
    reader::fsRunClause(clauses[i], rooted, report);
    if (report.failures != before) {
      Serial.printf("[fscontract]   ^ FAILED (%d)\n", report.failures - before);
      Serial.flush();
    }
  }

  {
    SpiBusGuard bus;
    SdMan.removeDir(kScratch);
  }

  Serial.printf("[fscontract] done: %d clauses, %d assertions, %d failed. skippedNames=%u\n",
                (int)count, report.total, report.failures, (unsigned)fs.skippedNames());
  Serial.flush();
  return report.failures;
}

#else

int runSdFsContractSelfTest(SdFileSystem&) { return -1; }

#endif  // ENCRE_FS_SELFTEST
