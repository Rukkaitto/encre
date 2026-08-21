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
  // A leftover may be a FILE, not a directory, and removeDir only removes
  // directories -- so it could not clear one and every clause skipped forever.
  // That is what the device showed: /.reader/fstest existed with
  // isDirectory()==0, mkdir refused because something was already there, and
  // mkdirs correctly refused to turn a file into a directory. Handle both kinds.
  if (fs.exists(kScratch)) {
    const bool gone = SdMan.removeDir(kScratch) || SdMan.remove(kScratch);
    if (!gone || fs.exists(kScratch)) return false;
  }
  return fs.mkdirs(kScratch);
}

// Why the wipe failed, printed once. On the device every clause skipped with
// CANNOT PREPARE, and mkdirs() collapses four separate outcomes into one false:
// the directory already existing as a file, SdMan.mkdir refusing, mkdir
// succeeding but isDirectory() misreading it, or the path itself being
// unresolvable. Reading the code cannot separate those, so print all four.
//
// The leading-dot component is the specific suspicion: '.' is special in a FAT
// directory, and while "/.reader/settings.json" opens for READ on this card,
// nothing has ever created a directory under it -- /.reader was already there and
// the settings file was loaded, not written. So the fallback probes a path with
// no dot component, which separates "dots break mkdir" from "mkdir is broken".
void explainWipeFailure(SdFileSystem& fs) {
  SpiBusGuard bus;
  Serial.printf("[fscontract] DIAGNOSING the failure to prepare %s\n", kScratch);

  Serial.printf("[fscontract]   parent /.reader: exists=%d\n", (int)fs.exists("/.reader"));
  {
    FsFile f = SdMan.open("/.reader", O_RDONLY);
    Serial.printf("[fscontract]   parent /.reader: open=%d isDirectory=%d\n", (int)(bool)f,
                  f ? (int)f.isDirectory() : -1);
    if (f) f.close();
  }
  Serial.printf("[fscontract]   target: exists=%d\n", (int)fs.exists(kScratch));

  const bool rawMkdir = SdMan.mkdir(kScratch, true);
  Serial.printf("[fscontract]   SdMan.mkdir(%s, pFlag=true) returned %d\n", kScratch,
                (int)rawMkdir);
  {
    FsFile f = SdMan.open(kScratch, O_RDONLY);
    Serial.printf("[fscontract]   after mkdir: open=%d isDirectory=%d\n", (int)(bool)f,
                  f ? (int)f.isDirectory() : -1);
    if (f) f.close();
  }
  Serial.printf("[fscontract]   after mkdir: fs.exists=%d fs.mkdirs=%d\n",
                (int)fs.exists(kScratch), (int)fs.mkdirs(kScratch));

  // Separate the TWO variables the first attempt confounded: whether the parent
  // is dot-prefixed, and whether the target is nested at all. /encretst was flat
  // AND undotted, so it could not tell them apart -- and the earlier conclusion
  // drawn from it ("a dot-prefixed parent breaks mkdir") was wrong, because the
  // real reason mkdir returned 0 was that a FILE already sat at the target.
  //
  // This matters beyond the harness: spec 5 puts /.reader/cache and
  // /.reader/state under a dot-prefixed parent, and Phase 3 has to CREATE those.
  struct Probe {
    const char* path;
    const char* what;
  };
  const Probe probes[] = {
      {"/encre_a", "flat, undotted"},
      {"/encre_b/sub", "nested under an undotted parent"},
      {"/.encre_c/sub", "nested under a DOT-prefixed parent"},
      {"/.reader/encre_d", "nested under /.reader itself"},
  };
  for (const Probe& pr : probes) {
    SdMan.removeDir(pr.path);
    SdMan.remove(pr.path);
    const bool made = SdMan.mkdir(pr.path, true);
    bool isDir = false, opened = false;
    {
      FsFile f = SdMan.open(pr.path, O_RDONLY);
      opened = (bool)f;
      isDir = f && f.isDirectory();
      if (f) f.close();
    }
    Serial.printf("[fscontract]   PROBE %-22s mkdir=%d open=%d isDirectory=%d  (%s)\n", pr.path,
                  (int)made, (int)opened, (int)isDir, pr.what);
    Serial.flush();
    SdMan.removeDir(pr.path);
    SdMan.remove(pr.path);
  }
  Serial.printf("[fscontract]   read the four PROBE lines: the first that fails names the "
                "variable that breaks mkdir. All four passing means the only fault was the "
                "stale file, now removable.\n");
  Serial.flush();
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
      static bool explained = false;
      if (!explained) {
        explained = true;
        explainWipeFailure(fs);
      }
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
